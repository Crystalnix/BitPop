// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/client/plugin/pepper_view.h"

#include "base/message_loop.h"
#include "ppapi/cpp/graphics_2d.h"
#include "ppapi/cpp/image_data.h"
#include "ppapi/cpp/point.h"
#include "ppapi/cpp/rect.h"
#include "ppapi/cpp/size.h"
#include "remoting/base/tracer.h"
#include "remoting/base/util.h"
#include "remoting/client/chromoting_stats.h"
#include "remoting/client/client_context.h"
#include "remoting/client/plugin/chromoting_instance.h"
#include "remoting/client/plugin/pepper_util.h"

namespace remoting {

PepperView::PepperView(ChromotingInstance* instance, ClientContext* context)
  : instance_(instance),
    context_(context),
    viewport_width_(0),
    viewport_height_(0),
    is_static_fill_(false),
    static_fill_color_(0),
    ALLOW_THIS_IN_INITIALIZER_LIST(task_factory_(this)) {
}

PepperView::~PepperView() {
}

bool PepperView::Initialize() {
  return true;
}

void PepperView::TearDown() {
  DCHECK(CurrentlyOnPluginThread());

  task_factory_.RevokeAll();
}

void PepperView::Paint() {
  DCHECK(CurrentlyOnPluginThread());

  TraceContext::tracer()->PrintString("Start Paint.");
  // TODO(ajwong): We're assuming the native format is BGRA_PREMUL below. This
  // is wrong.
  if (is_static_fill_) {
    LOG(ERROR) << "Static filling " << static_fill_color_;
    pp::ImageData image(instance_, pp::ImageData::GetNativeImageDataFormat(),
                        pp::Size(viewport_width_, viewport_height_),
                        false);
    if (image.is_null()) {
      LOG(ERROR) << "Unable to allocate image of size: "
                 << viewport_width_ << "x" << viewport_height_;
      return;
    }

    for (int y = 0; y < image.size().height(); y++) {
      for (int x = 0; x < image.size().width(); x++) {
        *image.GetAddr32(pp::Point(x, y)) = static_fill_color_;
      }
    }

    // For ReplaceContents, make sure the image size matches the device context
    // size!  Otherwise, this will just silently do nothing.
    graphics2d_.ReplaceContents(&image);
    graphics2d_.Flush(TaskToCompletionCallback(
        task_factory_.NewRunnableMethod(&PepperView::OnPaintDone,
                                        base::Time::Now())));
  } else {
    // TODO(ajwong): We need to keep a backing store image of the viewport that
    // has the data here which can be redrawn.
    return;
  }
  TraceContext::tracer()->PrintString("End Paint.");
}

void PepperView::PaintFrame(media::VideoFrame* frame, UpdatedRects* rects) {
  DCHECK(CurrentlyOnPluginThread());

  TraceContext::tracer()->PrintString("Start Paint Frame.");

  SetViewport(0, 0, frame->width(), frame->height());

  uint8* frame_data = frame->data(media::VideoFrame::kRGBPlane);
  const int kFrameStride = frame->stride(media::VideoFrame::kRGBPlane);
  const int kBytesPerPixel = GetBytesPerPixel(media::VideoFrame::RGB32);

  if (!backing_store_.get() || backing_store_->is_null()) {
    LOG(ERROR) << "Backing store is not available.";
    return;
  }

  // Copy updated regions to the backing store and then paint the regions.
  for (size_t i = 0; i < rects->size(); ++i) {
    // TODO(ajwong): We're assuming the native format is BGRA_PREMUL below. This
    // is wrong.
    const gfx::Rect& r = (*rects)[i];

    // TODO(hclam): Make sure rectangles are valid.
    if (r.width() <= 0 || r.height() <= 0)
      continue;

    uint8* in = frame_data + kFrameStride * r.y() + kBytesPerPixel * r.x();
    uint8* out = reinterpret_cast<uint8*>(backing_store_->data()) +
        backing_store_->stride() * r.y() + kBytesPerPixel * r.x();

    // TODO(hclam): We really should eliminate this memory copy.
    for (int j = 0; j < r.height(); ++j) {
      memcpy(out, in, r.width() * kBytesPerPixel);
      in += kFrameStride;
      out += backing_store_->stride();
    }

    // Pepper Graphics 2D has a strange and badly documented API that the
    // point here is the offset from the source rect. Why?
    graphics2d_.PaintImageData(*backing_store_.get(), pp::Point(0, 0),
                               pp::Rect(r.x(), r.y(), r.width(), r.height()));
  }

  graphics2d_.Flush(TaskToCompletionCallback(
      task_factory_.NewRunnableMethod(&PepperView::OnPaintDone,
                                      base::Time::Now())));

  TraceContext::tracer()->PrintString("End Paint Frame.");
}

void PepperView::SetSolidFill(uint32 color) {
  DCHECK(CurrentlyOnPluginThread());

  is_static_fill_ = true;
  static_fill_color_ = color;
}

void PepperView::UnsetSolidFill() {
  DCHECK(CurrentlyOnPluginThread());

  is_static_fill_ = false;
}

void PepperView::SetConnectionState(ConnectionState state) {
  DCHECK(CurrentlyOnPluginThread());

  // TODO(hclam): Re-consider the way we communicate with Javascript.
  ChromotingScriptableObject* scriptable_obj = instance_->GetScriptableObject();
  switch (state) {
    case CREATED:
      SetSolidFill(kCreatedColor);
      scriptable_obj->SetConnectionInfo(STATUS_CONNECTING, QUALITY_UNKNOWN);
      break;

    case CONNECTED:
      UnsetSolidFill();
      scriptable_obj->SignalLoginChallenge();
      break;

    case DISCONNECTED:
      SetSolidFill(kDisconnectedColor);
      scriptable_obj->SetConnectionInfo(STATUS_CLOSED, QUALITY_UNKNOWN);
      break;

    case FAILED:
      SetSolidFill(kFailedColor);
      scriptable_obj->SetConnectionInfo(STATUS_FAILED, QUALITY_UNKNOWN);
      break;
  }
}

void PepperView::UpdateLoginStatus(bool success, const std::string& info) {
  DCHECK(CurrentlyOnPluginThread());

  // TODO(hclam): Re-consider the way we communicate with Javascript.
  ChromotingScriptableObject* scriptable_obj = instance_->GetScriptableObject();
  if (success)
    scriptable_obj->SetConnectionInfo(STATUS_CONNECTED, QUALITY_UNKNOWN);
  else
    scriptable_obj->SignalLoginChallenge();
}

void PepperView::SetViewport(int x, int y, int width, int height) {
  DCHECK(CurrentlyOnPluginThread());

  if ((width == viewport_width_) && (height == viewport_height_))
    return;

  viewport_width_ = width;
  viewport_height_ = height;

  graphics2d_ = pp::Graphics2D(instance_,
                               pp::Size(viewport_width_, viewport_height_),
                               false);
  if (!instance_->BindGraphics(graphics2d_)) {
    LOG(ERROR) << "Couldn't bind the device context.";
    return;
  }

  // Allocate the backing store to save the desktop image.
  backing_store_.reset(
      new pp::ImageData(instance_, pp::ImageData::GetNativeImageDataFormat(),
                        pp::Size(viewport_width_, viewport_height_), false));
  DCHECK(backing_store_.get() && !backing_store_->is_null())
      << "Not enough memory for backing store.";

  instance_->GetScriptableObject()->SetDesktopSize(width, height);
}

void PepperView::AllocateFrame(media::VideoFrame::Format format,
                               size_t width,
                               size_t height,
                               base::TimeDelta timestamp,
                               base::TimeDelta duration,
                               scoped_refptr<media::VideoFrame>* frame_out,
                               Task* done) {
  DCHECK(CurrentlyOnPluginThread());

  media::VideoFrame::CreateFrame(media::VideoFrame::RGB32,
                                 width, height,
                                 base::TimeDelta(), base::TimeDelta(),
                                 frame_out);
  if (*frame_out) {
    (*frame_out)->AddRef();
  }
  done->Run();
  delete done;
}

void PepperView::ReleaseFrame(media::VideoFrame* frame) {
  DCHECK(CurrentlyOnPluginThread());

  if (frame) {
    LOG(WARNING) << "Frame released.";
    frame->Release();
  }
}

void PepperView::OnPartialFrameOutput(media::VideoFrame* frame,
                                      UpdatedRects* rects,
                                      Task* done) {
  DCHECK(CurrentlyOnPluginThread());

  TraceContext::tracer()->PrintString("Calling PaintFrame");
  // TODO(ajwong): Clean up this API to be async so we don't need to use a
  // member variable as a hack.
  PaintFrame(frame, rects);
  done->Run();
  delete done;
}

void PepperView::OnPaintDone(base::Time paint_start) {
  DCHECK(CurrentlyOnPluginThread());
  TraceContext::tracer()->PrintString("Paint flushed");
  instance_->GetStats()->video_paint_ms()->Record(
      (base::Time::Now() - paint_start).InMilliseconds());
  return;
}

}  // namespace remoting
