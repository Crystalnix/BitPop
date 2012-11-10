// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_VIDEO_FRAME_CAPTURER_FAKE_H_
#define REMOTING_HOST_VIDEO_FRAME_CAPTURER_FAKE_H_

#include "base/memory/scoped_ptr.h"
#include "remoting/host/video_frame_capturer.h"
#include "remoting/host/video_frame_capturer_helper.h"

namespace remoting {

// A VideoFrameCapturerFake generates artificial image for testing purpose.
//
// VideoFrameCapturerFake is double-buffered as required by VideoFrameCapturer.
// See remoting/host/video_frame_capturer.h.
class VideoFrameCapturerFake : public VideoFrameCapturer {
 public:
  VideoFrameCapturerFake();
  virtual ~VideoFrameCapturerFake();

  // Overridden from VideoFrameCapturer:
  virtual void Start(
      const CursorShapeChangedCallback& callback) OVERRIDE;
  virtual void Stop() OVERRIDE;
  virtual media::VideoFrame::Format pixel_format() const OVERRIDE;
  virtual void InvalidateRegion(const SkRegion& invalid_region) OVERRIDE;
  virtual void CaptureInvalidRegion(
      const CaptureCompletedCallback& callback) OVERRIDE;
  virtual const SkISize& size_most_recent() const OVERRIDE;

 private:
  // Generates an image in the front buffer.
  void GenerateImage();

  // Called when the screen configuration is changed.
  void ScreenConfigurationChanged();

  SkISize size_;
  int bytes_per_row_;
  int box_pos_x_;
  int box_pos_y_;
  int box_speed_x_;
  int box_speed_y_;

  VideoFrameCapturerHelper helper_;

  // We have two buffers for the screen images as required by Capturer.
  static const int kNumBuffers = 2;
  scoped_array<uint8> buffers_[kNumBuffers];

  // The current buffer with valid data for reading.
  int current_buffer_;

  // Format of pixels returned in buffer.
  media::VideoFrame::Format pixel_format_;

  DISALLOW_COPY_AND_ASSIGN(VideoFrameCapturerFake);
};

}  // namespace remoting

#endif  // REMOTING_HOST_VIDEO_FRAME_CAPTURER_FAKE_H_
