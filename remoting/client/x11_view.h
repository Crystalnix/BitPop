// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_X11_VIEW_H_
#define REMOTING_CLIENT_X11_VIEW_H_

#include "base/memory/scoped_ptr.h"
#include "base/task.h"
#include "media/base/video_frame.h"
#include "remoting/base/decoder.h"  // For UpdatedRects
#include "remoting/client/chromoting_view.h"
#include "remoting/client/frame_consumer.h"

typedef unsigned long XID;
typedef struct _XDisplay Display;

namespace remoting {

// A ChromotingView implemented using X11 and XRender.
class X11View : public ChromotingView, public FrameConsumer {
 public:
  X11View();
  virtual ~X11View();

  // ChromotingView implementations.
  virtual bool Initialize() OVERRIDE;
  virtual void TearDown() OVERRIDE;
  virtual void Paint() OVERRIDE;
  virtual void SetSolidFill(uint32 color) OVERRIDE;
  virtual void UnsetSolidFill() OVERRIDE;
  virtual void SetConnectionState(ConnectionState s) OVERRIDE;
  virtual void UpdateLoginStatus(bool success, const std::string& info)
      OVERRIDE;
  virtual void SetViewport(int x, int y, int width, int height) OVERRIDE;
  virtual gfx::Point ConvertScreenToHost(const gfx::Point& p) const OVERRIDE;

  // FrameConsumer implementation.
  virtual void AllocateFrame(media::VideoFrame::Format format,
                             size_t width,
                             size_t height,
                             base::TimeDelta timestamp,
                             base::TimeDelta duration,
                             scoped_refptr<media::VideoFrame>* frame_out,
                             Task* done);
  virtual void ReleaseFrame(media::VideoFrame* frame);
  virtual void OnPartialFrameOutput(media::VideoFrame* frame,
                                    UpdatedRects* rects,
                                    Task* done);

  Display* display() { return display_; }

 private:
  void InitPaintTarget();
  void PaintRect(media::VideoFrame* frame, const gfx::Rect& clip);

  Display* display_;
  XID window_;

  // A picture created in the X server that represents drawing area of the
  // window.
  XID picture_;

  DISALLOW_COPY_AND_ASSIGN(X11View);
};

}  // namespace remoting

DISABLE_RUNNABLE_METHOD_REFCOUNT(remoting::X11View);

#endif  // REMOTING_CLIENT_X11_VIEW_H_
