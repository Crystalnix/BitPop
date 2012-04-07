// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_FRAME_CONSUMER_H_
#define REMOTING_CLIENT_FRAME_CONSUMER_H_

#include "remoting/base/decoder.h"  // For UpdatedRects

namespace remoting {

class FrameConsumer {
 public:
  FrameConsumer() {}
  virtual ~FrameConsumer() {}

  // Request a frame be allocated from the FrameConsumer.
  //
  // If a frame cannot be allocated to fit the format, and |size|
  // requirements, |frame_out| will be set to NULL.
  //
  // An allocated frame will have at least the |size| requested, but
  // may be bigger. Query the retrun frame for the actual frame size,
  // stride, etc.
  //
  // The AllocateFrame call is asynchronous.  From invocation, until when the
  // |done| callback is invoked, |frame_out| should be considered to be locked
  // by the FrameConsumer, must remain a valid pointer, and should not be
  // examined or modified.  After |done| is called, the |frame_out| will
  // contain a result of the allocation. If a frame could not be allocated,
  // |frame_out| will be NULL.
  //
  // All frames retrieved via the AllocateFrame call must be released by a
  // corresponding call ReleaseFrame(scoped_refptr<VideoFrame>* frame_out.
  virtual void AllocateFrame(media::VideoFrame::Format format,
                             const SkISize& size,
                             scoped_refptr<media::VideoFrame>* frame_out,
                             const base::Closure& done) = 0;

  virtual void ReleaseFrame(media::VideoFrame* frame) = 0;

  // OnPartialFrameOutput() is called every time at least one rectangle of
  // output is produced.  The |frame| is guaranteed to have valid data for all
  // of |region|.
  //
  // Both |frame| and |region| are guaranteed to be valid until the |done|
  // callback is invoked.
  virtual void OnPartialFrameOutput(media::VideoFrame* frame,
                                    SkRegion* region,
                                    const base::Closure& done) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(FrameConsumer);
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_FRAME_CONSUMER_H_
