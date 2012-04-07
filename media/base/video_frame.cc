// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/video_frame.h"

#include "base/logging.h"
#include "media/base/video_util.h"

namespace media {

// static
scoped_refptr<VideoFrame> VideoFrame::CreateFrame(
    VideoFrame::Format format,
    size_t width,
    size_t height,
    base::TimeDelta timestamp,
    base::TimeDelta duration) {
  DCHECK(width > 0 && height > 0);
  DCHECK(width * height < 100000000);
  scoped_refptr<VideoFrame> frame(new VideoFrame(
      format, width, height, timestamp, duration));
  switch (format) {
    case VideoFrame::RGB555:
    case VideoFrame::RGB565:
      frame->AllocateRGB(2u);
      break;
    case VideoFrame::RGB24:
      frame->AllocateRGB(3u);
      break;
    case VideoFrame::RGB32:
    case VideoFrame::RGBA:
      frame->AllocateRGB(4u);
      break;
    case VideoFrame::YV12:
    case VideoFrame::YV16:
      frame->AllocateYUV();
      break;
    case VideoFrame::ASCII:
      frame->AllocateRGB(1u);
      break;
    default:
      NOTREACHED();
      return NULL;
  }
  return frame;
}

// static
scoped_refptr<VideoFrame> VideoFrame::WrapNativeTexture(
    uint32 texture_id,
    size_t width,
    size_t height,
    base::TimeDelta timestamp,
    base::TimeDelta duration,
    const base::Closure& no_longer_needed) {
  scoped_refptr<VideoFrame> frame(
      new VideoFrame(NATIVE_TEXTURE, width, height, timestamp, duration));
  frame->texture_id_ = texture_id;
  frame->texture_no_longer_needed_ = no_longer_needed;
  return frame;
}

// static
scoped_refptr<VideoFrame> VideoFrame::CreateEmptyFrame() {
  return new VideoFrame(
      VideoFrame::EMPTY, 0, 0, base::TimeDelta(), base::TimeDelta());
}

// static
scoped_refptr<VideoFrame> VideoFrame::CreateBlackFrame(int width, int height) {
  DCHECK_GT(width, 0);
  DCHECK_GT(height, 0);

  // Create our frame.
  const base::TimeDelta kZero;
  scoped_refptr<VideoFrame> frame =
      VideoFrame::CreateFrame(VideoFrame::YV12, width, height, kZero, kZero);

  // Now set the data to YUV(0,128,128).
  const uint8 kBlackY = 0x00;
  const uint8 kBlackUV = 0x80;
  FillYUV(frame, kBlackY, kBlackUV, kBlackUV);
  return frame;
}

static inline size_t RoundUp(size_t value, size_t alignment) {
  // Check that |alignment| is a power of 2.
  DCHECK((alignment + (alignment - 1)) == (alignment | (alignment - 1)));
  return ((value + (alignment - 1)) & ~(alignment-1));
}

void VideoFrame::AllocateRGB(size_t bytes_per_pixel) {
  // Round up to align at a 64-bit (8 byte) boundary for each row.  This
  // is sufficient for MMX reads (movq).
  size_t bytes_per_row = RoundUp(width_ * bytes_per_pixel, 8);
  strides_[VideoFrame::kRGBPlane] = bytes_per_row;
  data_[VideoFrame::kRGBPlane] = new uint8[bytes_per_row * height_];
  DCHECK(!(reinterpret_cast<intptr_t>(data_[VideoFrame::kRGBPlane]) & 7));
  COMPILE_ASSERT(0 == VideoFrame::kRGBPlane, RGB_data_must_be_index_0);
}

static const int kFramePadBytes = 15;  // Allows faster SIMD YUV convert.

void VideoFrame::AllocateYUV() {
  DCHECK(format_ == VideoFrame::YV12 || format_ == VideoFrame::YV16);
  // Align Y rows at 32-bit (4 byte) boundaries.  The stride for both YV12 and
  // YV16 is 1/2 of the stride of Y.  For YV12, every row of bytes for U and V
  // applies to two rows of Y (one byte of UV for 4 bytes of Y), so in the
  // case of YV12 the strides are identical for the same width surface, but the
  // number of bytes allocated for YV12 is 1/2 the amount for U & V as YV16.
  // We also round the height of the surface allocated to be an even number
  // to avoid any potential of faulting by code that attempts to access the Y
  // values of the final row, but assumes that the last row of U & V applies to
  // a full two rows of Y.
  size_t y_height = rows(VideoFrame::kYPlane);
  size_t y_stride = RoundUp(row_bytes(VideoFrame::kYPlane), 4);
  size_t uv_stride = RoundUp(row_bytes(VideoFrame::kUPlane), 4);
  size_t uv_height = rows(VideoFrame::kUPlane);
  size_t y_bytes = y_height * y_stride;
  size_t uv_bytes = uv_height * uv_stride;

  uint8* data = new uint8[y_bytes + (uv_bytes * 2) + kFramePadBytes];
  COMPILE_ASSERT(0 == VideoFrame::kYPlane, y_plane_data_must_be_index_0);
  data_[VideoFrame::kYPlane] = data;
  data_[VideoFrame::kUPlane] = data + y_bytes;
  data_[VideoFrame::kVPlane] = data + y_bytes + uv_bytes;
  strides_[VideoFrame::kYPlane] = y_stride;
  strides_[VideoFrame::kUPlane] = uv_stride;
  strides_[VideoFrame::kVPlane] = uv_stride;
}

VideoFrame::VideoFrame(VideoFrame::Format format,
                       size_t width,
                       size_t height,
                       base::TimeDelta timestamp,
                       base::TimeDelta duration)
    : format_(format),
      width_(width),
      height_(height),
      texture_id_(0) {
  SetTimestamp(timestamp);
  SetDuration(duration);
  memset(&strides_, 0, sizeof(strides_));
  memset(&data_, 0, sizeof(data_));
}

VideoFrame::~VideoFrame() {
  if (format_ == NATIVE_TEXTURE && !texture_no_longer_needed_.is_null()) {
    texture_no_longer_needed_.Run();
    texture_no_longer_needed_.Reset();
  }

  // In multi-plane allocations, only a single block of memory is allocated
  // on the heap, and other |data| pointers point inside the same, single block
  // so just delete index 0.
  delete[] data_[0];
}

bool VideoFrame::IsValidPlane(size_t plane) const {
  switch (format_) {
    case RGB555:
    case RGB565:
    case RGB24:
    case RGB32:
    case RGBA:
      return plane == kRGBPlane;

    case YV12:
    case YV16:
      return plane == kYPlane || plane == kUPlane || plane == kVPlane;

    case NATIVE_TEXTURE:
      NOTREACHED() << "NATIVE_TEXTUREs don't use plane-related methods!";
      return false;

    default:
      break;
  }

  // Intentionally leave out non-production formats.
  NOTREACHED() << "Unsupported video frame format: " << format_;
  return false;
}

int VideoFrame::stride(size_t plane) const {
  DCHECK(IsValidPlane(plane));
  return strides_[plane];
}

int VideoFrame::row_bytes(size_t plane) const {
  DCHECK(IsValidPlane(plane));
  switch (format_) {
    case RGB555:
    case RGB565:
    case RGB24:
    case RGB32:
    case RGBA:
      return width_;

    case YV12:
    case YV16:
      if (plane == kYPlane)
        return width_;
      return RoundUp(width_, 2) / 2;

    default:
      break;
  }

  // Intentionally leave out non-production formats.
  NOTREACHED() << "Unsupported video frame format: " << format_;
  return 0;
}

int VideoFrame::rows(size_t plane) const {
  DCHECK(IsValidPlane(plane));
  switch (format_) {
    case RGB555:
    case RGB565:
    case RGB24:
    case RGB32:
    case RGBA:
    case YV16:
      return height_;

    case YV12:
      if (plane == kYPlane)
        return height_;
      return RoundUp(height_, 2) / 2;

    default:
      break;
  }

  // Intentionally leave out non-production formats.
  NOTREACHED() << "Unsupported video frame format: " << format_;
  return 0;
}

uint8* VideoFrame::data(size_t plane) const {
  DCHECK(IsValidPlane(plane));
  return data_[plane];
}

uint32 VideoFrame::texture_id() const {
  DCHECK_EQ(format_, NATIVE_TEXTURE);
  return texture_id_;
}

bool VideoFrame::IsEndOfStream() const {
  return format_ == VideoFrame::EMPTY;
}

}  // namespace media
