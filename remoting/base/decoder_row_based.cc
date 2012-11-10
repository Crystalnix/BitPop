// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/base/decoder_row_based.h"

#include "base/logging.h"
#include "remoting/base/decompressor.h"
#include "remoting/base/decompressor_zlib.h"
#include "remoting/base/decompressor_verbatim.h"
#include "remoting/base/util.h"

namespace remoting {

namespace {
// Both input and output data are assumed to be RGBA32.
const int kBytesPerPixel = 4;
}

DecoderRowBased* DecoderRowBased::CreateZlibDecoder() {
  return new DecoderRowBased(new DecompressorZlib(),
                             VideoPacketFormat::ENCODING_ZLIB);
}

DecoderRowBased* DecoderRowBased::CreateVerbatimDecoder() {
  return new DecoderRowBased(new DecompressorVerbatim(),
                             VideoPacketFormat::ENCODING_VERBATIM);
}

DecoderRowBased::DecoderRowBased(Decompressor* decompressor,
                                 VideoPacketFormat::Encoding encoding)
    : state_(kUninitialized),
      clip_(SkIRect::MakeEmpty()),
      decompressor_(decompressor),
      encoding_(encoding),
      row_pos_(0),
      row_y_(0),
      screen_size_(SkISize::Make(0, 0)) {
}

DecoderRowBased::~DecoderRowBased() {
}

bool DecoderRowBased::IsReadyForData() {
  switch (state_) {
    case kUninitialized:
    case kError:
      return false;
    case kReady:
    case kProcessing:
    case kPartitionDone:
    case kDone:
      return true;
  }
  NOTREACHED();
  return false;
}

void DecoderRowBased::Initialize(const SkISize& screen_size) {
  decompressor_->Reset();
  updated_region_.setEmpty();
  screen_buffer_.reset(NULL);

  screen_size_ = screen_size;
  // Allocate the screen buffer, if necessary.
  if (!screen_size_.isEmpty()) {
    screen_buffer_.reset(new uint8[
        screen_size_.width() * screen_size_.height() * kBytesPerPixel]);
  }

  state_ = kReady;
}

Decoder::DecodeResult DecoderRowBased::DecodePacket(const VideoPacket* packet) {
  UpdateStateForPacket(packet);

  if (state_ == kError) {
    return DECODE_ERROR;
  }

  const uint8* in = reinterpret_cast<const uint8*>(packet->data().data());
  const int in_size = packet->data().size();
  const int row_size = clip_.width() * kBytesPerPixel;

  int out_stride = screen_size_.width() * kBytesPerPixel;
  uint8* out = screen_buffer_.get() + out_stride * (clip_.top() + row_y_) +
      kBytesPerPixel * clip_.left();

  // Consume all the data in the message.
  bool decompress_again = true;
  int used = 0;
  while (decompress_again && used < in_size) {
    if (row_y_ >= clip_.height()) {
      state_ = kError;
      LOG(WARNING) << "Too much data is received for the given rectangle.";
      return DECODE_ERROR;
    }

    int written = 0;
    int consumed = 0;
    decompress_again = decompressor_->Process(
        in + used, in_size - used, out + row_pos_, row_size - row_pos_,
        &consumed, &written);
    used += consumed;
    row_pos_ += written;

    // If this row is completely filled then move onto the next row.
    if (row_pos_ == row_size) {
      ++row_y_;
      row_pos_ = 0;
      out += out_stride;
    }
  }

  if (state_ == kPartitionDone || state_ == kDone) {
    if (row_y_ < clip_.height()) {
      state_ = kError;
      LOG(WARNING) << "Received LAST_PACKET, but didn't get enough data.";
      return DECODE_ERROR;
    }

    updated_region_.op(clip_, SkRegion::kUnion_Op);
    decompressor_->Reset();
  }

  if (state_ == kDone) {
    return DECODE_DONE;
  } else {
    return DECODE_IN_PROGRESS;
  }
}

void DecoderRowBased::UpdateStateForPacket(const VideoPacket* packet) {
  if (state_ == kError) {
    return;
  }

  if (packet->flags() & VideoPacket::FIRST_PACKET) {
    if (state_ != kReady && state_ != kDone && state_ != kPartitionDone) {
      state_ = kError;
      LOG(WARNING) << "Received unexpected FIRST_PACKET.";
      return;
    }

    // Reset the buffer location status variables on the first packet.
    clip_.setXYWH(packet->format().x(), packet->format().y(),
                  packet->format().width(), packet->format().height());
    if (!SkIRect::MakeSize(screen_size_).contains(clip_)) {
      state_ = kError;
      LOG(WARNING) << "Invalid clipping area received.";
      return;
    }

    state_ = kProcessing;
    row_pos_ = 0;
    row_y_ = 0;
  }

  if (state_ != kProcessing) {
    state_ = kError;
    LOG(WARNING) << "Received unexpected packet.";
    return;
  }

  if (packet->flags() & VideoPacket::LAST_PACKET) {
    if (state_ != kProcessing) {
      state_ = kError;
      LOG(WARNING) << "Received unexpected LAST_PACKET.";
      return;
    }
    state_ = kPartitionDone;
  }

  if (packet->flags() & VideoPacket::LAST_PARTITION) {
    if (state_ != kPartitionDone) {
      state_ = kError;
      LOG(WARNING) << "Received unexpected LAST_PARTITION.";
      return;
    }
    state_ = kDone;
  }

  return;
}

VideoPacketFormat::Encoding DecoderRowBased::Encoding() {
  return encoding_;
}

void DecoderRowBased::Invalidate(const SkISize& view_size,
                                 const SkRegion& region) {
  updated_region_.op(region, SkRegion::kUnion_Op);
}

void DecoderRowBased::RenderFrame(const SkISize& view_size,
                                  const SkIRect& clip_area,
                                  uint8* image_buffer,
                                  int image_stride,
                                  SkRegion* output_region) {
  output_region->setEmpty();

  // TODO(alexeypa): scaling is not implemented.
  SkIRect clip_rect = SkIRect::MakeSize(screen_size_);
  if (!clip_rect.intersect(clip_area))
    return;

  int screen_stride = screen_size_.width() * kBytesPerPixel;

  for (SkRegion::Iterator i(updated_region_); !i.done(); i.next()) {
    SkIRect rect(i.rect());
    if (!rect.intersect(clip_rect))
      continue;

    CopyRGB32Rect(screen_buffer_.get(), screen_stride,
                  clip_rect,
                  image_buffer, image_stride,
                  clip_area,
                  rect);
    output_region->op(rect, SkRegion::kUnion_Op);
  }

  updated_region_.setEmpty();
}

}  // namespace remoting
