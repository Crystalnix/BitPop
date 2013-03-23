// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_MEDIA_WEB_CONTENTS_VIDEO_CAPTURE_DEVICE_H_
#define CONTENT_BROWSER_RENDERER_HOST_MEDIA_WEB_CONTENTS_VIDEO_CAPTURE_DEVICE_H_

#include <string>

#include "base/callback_forward.h"
#include "base/memory/ref_counted.h"
#include "content/common/content_export.h"
#include "media/video/capture/video_capture_device.h"

namespace content {

class CaptureMachine;  // Defined in web_contents_video_capture_device.cc.
class RenderWidgetHost;

// A virtualized VideoCaptureDevice that mirrors the displayed contents of a
// tab (accessed via its associated WebContents instance), producing a stream of
// video frames.
//
// An instance is created by providing a device_id.  The device_id contains the
// routing ID for a RenderViewHost, and from the RenderViewHost instance, a
// reference to its associated WebContents instance is acquired.  From then on,
// WebContentsVideoCaptureDevice will capture from whatever render view is
// currently associated with that WebContents instance.  This allows the
// underlying render view to be swapped out (e.g., due to navigation or
// crashes/reloads), without any interrpution in capturing.
class CONTENT_EXPORT WebContentsVideoCaptureDevice
    : public media::VideoCaptureDevice {
 public:
  // Construct from the a |device_id| string of the form:
  //   "render_process_id:render_view_id"
  static media::VideoCaptureDevice* Create(const std::string& device_id);

  // Construct an instance with the following |test_source| injected for testing
  // purposes.  |destroy_cb| is invoked once all outstanding objects are
  // completely destroyed.
  // TODO(miu): Passing a destroy callback suggests needing to revisit the
  // design philosophy of an asynchronous DeAllocate().  http://crbug.com/158641
  static media::VideoCaptureDevice* CreateForTesting(
      RenderWidgetHost* test_source, const base::Closure& destroy_cb);

  virtual ~WebContentsVideoCaptureDevice();

  // VideoCaptureDevice implementation.
  virtual void Allocate(int width,
                        int height,
                        int frame_rate,
                        VideoCaptureDevice::EventHandler* consumer) OVERRIDE;
  virtual void Start() OVERRIDE;
  virtual void Stop() OVERRIDE;
  virtual void DeAllocate() OVERRIDE;

  // Note: The following is just a pass-through of the device_id provided to the
  // constructor.  It does not change when the content of the page changes
  // (e.g., due to navigation), or when the underlying RenderView is
  // swapped-out.
  virtual const Name& device_name() OVERRIDE;

 private:
  // Constructors.  The latter is used for testing.
  WebContentsVideoCaptureDevice(
      const Name& name, int render_process_id, int render_view_id);
  WebContentsVideoCaptureDevice(RenderWidgetHost* test_source,
                                const base::Closure& destroy_cb);

  Name device_name_;
  scoped_refptr<CaptureMachine> capturer_;

  DISALLOW_COPY_AND_ASSIGN(WebContentsVideoCaptureDevice);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_MEDIA_WEB_CONTENTS_VIDEO_CAPTURE_DEVICE_H_
