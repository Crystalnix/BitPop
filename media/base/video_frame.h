// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_VIDEO_FRAME_H_
#define MEDIA_BASE_VIDEO_FRAME_H_

#include "base/callback.h"
#include "media/base/buffers.h"

namespace media {

class MEDIA_EXPORT VideoFrame : public StreamSample {
 public:
  enum {
    kMaxPlanes = 3,

    kRGBPlane = 0,

    kYPlane = 0,
    kUPlane = 1,
    kVPlane = 2,
  };

  // Surface formats roughly based on FOURCC labels, see:
  // http://www.fourcc.org/rgb.php
  // http://www.fourcc.org/yuv.php
  // Keep in sync with WebKit::WebVideoFrame!
  enum Format {
    INVALID,     // Invalid format value.  Used for error reporting.
    RGB555,      // 16bpp RGB packed 5:5:5
    RGB565,      // 16bpp RGB packed 5:6:5
    RGB24,       // 24bpp RGB packed 8:8:8
    RGB32,       // 32bpp RGB packed with extra byte 8:8:8
    RGBA,        // 32bpp RGBA packed 8:8:8:8
    YV12,        // 12bpp YVU planar 1x1 Y, 2x2 VU samples
    YV16,        // 16bpp YVU planar 1x1 Y, 2x1 VU samples
    NV12,        // 12bpp YVU planar 1x1 Y, 2x2 UV interleaving samples
    EMPTY,       // An empty frame.
    ASCII,       // A frame with ASCII content. For testing only.
    I420,        // 12bpp YVU planar 1x1 Y, 2x2 UV samples.
    NATIVE_TEXTURE,  // Native texture.  Pixel-format agnostic.
  };

  // Creates a new frame in system memory with given parameters. Buffers for
  // the frame are allocated but not initialized.
  static scoped_refptr<VideoFrame> CreateFrame(
      Format format,
      size_t width,
      size_t height,
      base::TimeDelta timestamp,
      base::TimeDelta duration);

  // Wraps a native texture of the given parameters with a VideoFrame.  When the
  // frame is destroyed |no_longer_needed.Run()| will be called.
  static scoped_refptr<VideoFrame> WrapNativeTexture(
      uint32 texture_id,
      size_t width,
      size_t height,
      base::TimeDelta timestamp,
      base::TimeDelta duration,
      const base::Closure& no_longer_needed);

  // Creates a frame with format equals to VideoFrame::EMPTY, width, height
  // timestamp and duration are all 0.
  static scoped_refptr<VideoFrame> CreateEmptyFrame();

  // Allocates YV12 frame based on |width| and |height|, and sets its data to
  // the YUV equivalent of RGB(0,0,0).
  static scoped_refptr<VideoFrame> CreateBlackFrame(int width, int height);

  Format format() const { return format_; }

  size_t width() const { return width_; }

  size_t height() const { return height_; }

  int stride(size_t plane) const;

  // Returns the number of bytes per row and number of rows for a given plane.
  //
  // As opposed to stride(), row_bytes() refers to the bytes representing
  // visible pixels.
  int row_bytes(size_t plane) const;
  int rows(size_t plane) const;

  // Returns pointer to the buffer for a given plane. The memory is owned by
  // VideoFrame object and must not be freed by the caller.
  uint8* data(size_t plane) const;

  // Returns the ID of the native texture wrapped by this frame.  Only valid to
  // call if this is a NATIVE_TEXTURE frame.
  uint32 texture_id() const;

  // StreamSample interface.
  virtual bool IsEndOfStream() const OVERRIDE;

 private:
  // Clients must use the static CreateFrame() method to create a new frame.
  VideoFrame(Format format,
             size_t video_width,
             size_t video_height,
             base::TimeDelta timestamp,
             base::TimeDelta duration);

  virtual ~VideoFrame();

  // Used internally by CreateFrame().
  void AllocateRGB(size_t bytes_per_pixel);
  void AllocateYUV();

  // Used to DCHECK() plane parameters.
  bool IsValidPlane(size_t plane) const;

  // Frame format.
  Format format_;

  // Width and height of surface.
  size_t width_;
  size_t height_;

  // Array of strides for each plane, typically greater or equal to the width
  // of the surface divided by the horizontal sampling period.  Note that
  // strides can be negative.
  int32 strides_[kMaxPlanes];

  // Array of data pointers to each plane.
  uint8* data_[kMaxPlanes];

  // Native texture ID, if this is a NATIVE_TEXTURE frame.
  uint32 texture_id_;
  base::Closure texture_no_longer_needed_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(VideoFrame);
};

}  // namespace media

#endif  // MEDIA_BASE_VIDEO_FRAME_H_
