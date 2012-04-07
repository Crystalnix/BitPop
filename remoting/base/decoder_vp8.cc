// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/base/decoder_vp8.h"

#include <math.h>

#include "base/logging.h"
#include "media/base/media.h"
#include "media/base/yuv_convert.h"
#include "remoting/base/util.h"

extern "C" {
#define VPX_CODEC_DISABLE_COMPAT 1
#include "third_party/libvpx/libvpx.h"
}

namespace remoting {

DecoderVp8::DecoderVp8()
    : state_(kUninitialized),
      codec_(NULL),
      last_image_(NULL),
      clip_rect_(SkIRect::MakeEmpty()),
      output_size_(SkISize::Make(0, 0)) {
}

DecoderVp8::~DecoderVp8() {
  if (codec_) {
    vpx_codec_err_t ret = vpx_codec_destroy(codec_);
    CHECK(ret == VPX_CODEC_OK) << "Failed to destroy codec";
  }
  delete codec_;
}

void DecoderVp8::Initialize(scoped_refptr<media::VideoFrame> frame) {
  DCHECK_EQ(kUninitialized, state_);

  if (frame->format() != media::VideoFrame::RGB32) {
    LOG(INFO) << "DecoderVp8 only supports RGB32 as output";
    state_ = kError;
    return;
  }
  frame_ = frame;

  state_ = kReady;
}

Decoder::DecodeResult DecoderVp8::DecodePacket(const VideoPacket* packet) {
  DCHECK_EQ(kReady, state_);

  // Initialize the codec as needed.
  if (!codec_) {
    codec_ = new vpx_codec_ctx_t();

    // TODO(hclam): Scale the number of threads with number of cores of the
    // machine.
    vpx_codec_dec_cfg config;
    config.w = 0;
    config.h = 0;
    config.threads = 2;
    vpx_codec_err_t ret =
        vpx_codec_dec_init(
            codec_, vpx_codec_vp8_dx(), &config, 0);
    if (ret != VPX_CODEC_OK) {
      LOG(INFO) << "Cannot initialize codec.";
      delete codec_;
      codec_ = NULL;
      state_ = kError;
      return DECODE_ERROR;
    }
  }

  // Do the actual decoding.
  vpx_codec_err_t ret = vpx_codec_decode(
      codec_, reinterpret_cast<const uint8*>(packet->data().data()),
      packet->data().size(), NULL, 0);
  if (ret != VPX_CODEC_OK) {
    LOG(INFO) << "Decoding failed:" << vpx_codec_err_to_string(ret) << "\n"
              << "Details: " << vpx_codec_error(codec_) << "\n"
              << vpx_codec_error_detail(codec_);
    return DECODE_ERROR;
  }

  // Gets the decoded data.
  vpx_codec_iter_t iter = NULL;
  vpx_image_t* image = vpx_codec_get_frame(codec_, &iter);
  if (!image) {
    LOG(INFO) << "No video frame decoded";
    return DECODE_ERROR;
  }
  last_image_ = image;

  SkRegion region;
  for (int i = 0; i < packet->dirty_rects_size(); ++i) {
    Rect remoting_rect = packet->dirty_rects(i);
    SkIRect rect = SkIRect::MakeXYWH(remoting_rect.x(),
                                     remoting_rect.y(),
                                     remoting_rect.width(),
                                     remoting_rect.height());
    region.op(rect, SkRegion::kUnion_Op);
  }

  RefreshRegion(region);
  return DECODE_DONE;
}

void DecoderVp8::GetUpdatedRegion(SkRegion* region) {
  region->swap(updated_region_);
}

void DecoderVp8::Reset() {
  frame_ = NULL;
  state_ = kUninitialized;
}

bool DecoderVp8::IsReadyForData() {
  return state_ == kReady;
}

VideoPacketFormat::Encoding DecoderVp8::Encoding() {
  return VideoPacketFormat::ENCODING_VP8;
}

void DecoderVp8::SetOutputSize(const SkISize& size) {
  output_size_ = size;
}

void DecoderVp8::SetClipRect(const SkIRect& clip_rect) {
  clip_rect_ = clip_rect;
}

void DecoderVp8::RefreshRegion(const SkRegion& region) {
  // TODO(wez): Fix the rest of the decode pipeline not to assume the frame
  // size is the host dimensions, since it's not when scaling.  If the host
  // gets smaller, then the output size will be too big and we'll overrun the
  // frame, so currently we render 1:1 in that case; the app will see the
  // host size change and resize us if need be.
  if (output_size_.width() > static_cast<int>(frame_->width()))
    output_size_.set(frame_->width(), output_size_.height());
  if (output_size_.height() > static_cast<int>(frame_->height()))
    output_size_.set(output_size_.width(), frame_->height());

  if (!DoScaling()) {
    ConvertRegion(region, &updated_region_);
  } else {
    ScaleAndConvertRegion(region, &updated_region_);
  }
}

bool DecoderVp8::DoScaling() const {
  DCHECK(last_image_);
  return !output_size_.equals(last_image_->d_w, last_image_->d_h);
}

void DecoderVp8::ConvertRegion(const SkRegion& input_region,
                               SkRegion* output_region) {
  if (!last_image_)
    return;

  output_region->setEmpty();

  // Clip based on both the output dimensions and Pepper clip rect.
  // ConvertYUVToRGB32WithRect() requires even X and Y coordinates, so we align
  // |clip_rect| to prevent clipping from breaking alignment.  We then clamp it
  // to the image dimensions, which may lead to odd width & height, which we
  // can cope with.
  SkIRect clip_rect = AlignRect(clip_rect_);
  if (!clip_rect.intersect(SkIRect::MakeWH(last_image_->d_w, last_image_->d_h)))
    return;

  uint8* output_rgb_buf = frame_->data(media::VideoFrame::kRGBPlane);
  const int output_stride = frame_->stride(media::VideoFrame::kRGBPlane);

  for (SkRegion::Iterator i(input_region); !i.done(); i.next()) {
    // Align the rectangle so the top-left coordinates are even, for
    // ConvertYUVToRGB32WithRect().
    SkIRect dest_rect(AlignRect(i.rect()));

    // Clip the rectangle, preserving alignment since |clip_rect| is aligned.
    if (!dest_rect.intersect(clip_rect))
      continue;

    ConvertYUVToRGB32WithRect(last_image_->planes[0],
                              last_image_->planes[1],
                              last_image_->planes[2],
                              output_rgb_buf,
                              dest_rect,
                              last_image_->stride[0],
                              last_image_->stride[1],
                              output_stride);

    output_region->op(dest_rect, SkRegion::kUnion_Op);
  }
}

void DecoderVp8::ScaleAndConvertRegion(const SkRegion& input_region,
                                       SkRegion* output_region) {
  if (!last_image_)
    return;

  DCHECK(output_size_.width() <= static_cast<int>(frame_->width()));
  DCHECK(output_size_.height() <= static_cast<int>(frame_->height()));

  output_region->setEmpty();

  // Clip based on both the output dimensions and Pepper clip rect.
  SkIRect clip_rect = clip_rect_;
  if (!clip_rect.intersect(SkIRect::MakeSize(output_size_)))
    return;

  SkISize image_size = SkISize::Make(last_image_->d_w, last_image_->d_h);
  uint8* output_rgb_buf = frame_->data(media::VideoFrame::kRGBPlane);
  const int output_stride = frame_->stride(media::VideoFrame::kRGBPlane);

  for (SkRegion::Iterator i(input_region); !i.done(); i.next()) {
    // Determine the scaled area affected by this rectangle changing.
    SkIRect output_rect = ScaleRect(i.rect(), image_size, output_size_);
    if (!output_rect.intersect(clip_rect))
      continue;

    // The scaler will not to read outside the input dimensions.
    media::ScaleYUVToRGB32WithRect(last_image_->planes[0],
                                   last_image_->planes[1],
                                   last_image_->planes[2],
                                   output_rgb_buf,
                                   image_size.width(),
                                   image_size.height(),
                                   output_size_.width(),
                                   output_size_.height(),
                                   output_rect.x(),
                                   output_rect.y(),
                                   output_rect.right(),
                                   output_rect.bottom(),
                                   last_image_->stride[0],
                                   last_image_->stride[1],
                                   output_stride);

    output_region->op(output_rect, SkRegion::kUnion_Op);
  }
}

}  // namespace remoting
