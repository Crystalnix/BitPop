// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_VIDEO_CAPTURE_CONTROLLER_EVENT_HANDLER_H_
#define CONTENT_BROWSER_RENDERER_HOST_VIDEO_CAPTURE_CONTROLLER_EVENT_HANDLER_H_

#include <map>

#include "base/time.h"
#include "ui/gfx/surface/transport_dib.h"

// ID used for identifying an object of VideoCaptureController.
struct VideoCaptureControllerID {
 public:
  VideoCaptureControllerID();
  VideoCaptureControllerID(int32 rid, int did);
  ~VideoCaptureControllerID();
  bool operator<(const VideoCaptureControllerID& vc) const;

  int32 routing_id;
  int device_id;
};

// VideoCaptureControllerEventHandler is the interface for
// VideoCaptureController to notify clients about the events such as
// BufferReady, FrameInfo, Error, etc.
class VideoCaptureControllerEventHandler {
 public:
  // An Error have occurred in the VideoCaptureDevice.
  virtual void OnError(const VideoCaptureControllerID& id) = 0;

  // An TransportDIB have been filled with I420 video.
  virtual void OnBufferReady(const VideoCaptureControllerID& id,
                             TransportDIB::Handle handle,
                             base::Time timestamp) = 0;

  // The frame resolution the VideoCaptureDevice capture video in.
  virtual void OnFrameInfo(const VideoCaptureControllerID& id,
                           int width,
                           int height,
                           int frame_rate) = 0;

  // Report that this object can be deleted.
  virtual void OnReadyToDelete(const VideoCaptureControllerID& id) = 0;

 protected:
  virtual ~VideoCaptureControllerEventHandler() {}
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_VIDEO_CAPTURE_CONTROLLER_EVENT_HANDLER_H_
