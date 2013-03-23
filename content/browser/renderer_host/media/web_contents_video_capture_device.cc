// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Implementation notes: This needs to work on a variety of hardware
// configurations where the speed of the CPU and GPU greatly affect overall
// performance.  Therefore, the process of capturing has been split up into a
// pipeline of three stages.  Each stage executes on its own thread:
//
//   1. Capture: A bitmap is snapshotted/copied from the RenderView's backing
//      store.  This executes on the UI BrowserThread.
//   2. Render: The captured bitmap usually needs to be scaled to a size which
//      will fit within a video frame (if the GPU could not do this already).
//      Also, the video frame itself will be drawn such that the scaled capture
//      is centered with black bars on the sides (to preserve the aspect ratio
//      of the capture).
//   3. Deliver: The rendered video frame is presented to the consumer (which
//      (implements the VideoCaptureDevice::EventHandler interface).  As of this
//      writing, the consumer callback code seems to block the thread for a
//      significant amount of time to do further processing of its own.
//
// Depending on the capabilities of the hardware, each pipeline stage can take
// up to one full time period to execute without any resulting loss of frame
// rate.  This is because each thread can process a subsequent frame in
// parallel.  A timing diagram helps illustrate this point (@30 FPS):
//
//    Time: 0ms                 33ms                 66ms                 99ms
// thread1: |-Capture-f1------v |-Capture-f2------v  |-Capture-f3----v    |-Capt
// thread2:                   |-Render-f1-----v   |-Render-f2-----v  |-Render-f3
// thread3:                                   |-Deliver-f1-v      |-Deliver-f2-v
//
// In the above example, both capturing and rendering *each* take almost the
// full 33 ms available between frames, yet we see that delivery of each frame
// is made without dropping frames.
//
// Finally, the implementation detects when the pipeline simply becomes too
// backlogged, and begins dropping frames to compensate.  Turning on verbose
// logging will cause the effective frame rate to be logged at 5-second
// intervals.

#include "content/browser/renderer_host/media/web_contents_video_capture_device.h"

#include <algorithm>
#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback_forward.h"
#include "base/debug/trace_event.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread.h"
#include "base/time.h"
#include "content/browser/renderer_host/media/web_contents_capture_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "media/base/bind_to_loop.h"
#include "media/video/capture/video_capture_types.h"
#include "skia/ext/image_operations.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/rect.h"

// Used to self-trampoline invocation of methods to the approprate thread.  This
// should be used sparingly, only when it's not clear which thread is invoking a
// method.
#define ENSURE_INVOKED_ON_THREAD(thread, ...) {  \
  DCHECK(thread.IsRunning());  \
  if (MessageLoop::current() != thread.message_loop()) {  \
    thread.message_loop()->PostTask(FROM_HERE, base::Bind(__VA_ARGS__));  \
    return;  \
  }  \
}

namespace content {

namespace {

const int kMinFrameWidth = 2;
const int kMinFrameHeight = 2;

// Returns the nearest even integer closer to zero.
template<typename IntType>
IntType MakeEven(IntType x) {
  return x & static_cast<IntType>(-2);
}

// Determine a |fitted_size| that would fit within a video frame with the same
// aspect ratio as the given source_width/height.
void CalculateFittedSize(int source_width, int source_height,
                         int frame_width, int frame_height,
                         gfx::Size* fitted_size) {
  DCHECK_LT(0, source_width);
  DCHECK_LT(0, source_height);
  DCHECK_LT(0, frame_width);
  DCHECK_LT(0, frame_height);
  DCHECK(fitted_size);

  // If the source size is "fatter" than the frame size, scale it such that the
  // fitted width equals the frame width.  Likewise, if it's thinner, then scale
  // it such that the fitted height equals the frame height.
  //
  // Details: The following calculations have been denormalized to allow simpler
  // integer math.  We seek to test the following:
  //   capture_aspect_ratio >= frame_aspect_ratio
  // Let a/b (width divided by height) be the capture_aspect_ratio, and c/d be
  // the frame_aspect_ratio.  Then, we have:
  //   a/b >= c/d
  // Because b and d are both positive, we can denormalize by multiplying both
  // sides by b*d, and we get the following equivalent expression:
  //   a*d >= b*c
  const int capture_aspect_ratio_denormalized = source_width * frame_height;
  const int frame_aspect_ratio_denormalized = frame_width * source_height;
  int fitted_width, fitted_height;
  if (capture_aspect_ratio_denormalized >= frame_aspect_ratio_denormalized) {
    fitted_width = frame_width;
    fitted_height = frame_aspect_ratio_denormalized / source_width;
  } else {
    fitted_height = frame_height;
    fitted_width = capture_aspect_ratio_denormalized / source_height;
  }
  // Make each dimension a positive, even number; if not already.
  fitted_width = std::max(kMinFrameWidth, MakeEven(fitted_width));
  fitted_height = std::max(kMinFrameHeight, MakeEven(fitted_height));

  *fitted_size = gfx::Size(fitted_width, fitted_height);
}

// Keeps track of the RenderView to be sourced, and executes copying of the
// backing store on the UI BrowserThread.
class BackingStoreCopier : public WebContentsObserver {
 public:
  // Result status and done callback used with StartCopy().
  enum Result {
    OK,
    TRANSIENT_ERROR,
    NO_SOURCE,
  };
  typedef base::Callback<void(Result result,
                              scoped_ptr<skia::PlatformBitmap> capture,
                              const base::Time& capture_time)> DoneCB;

  BackingStoreCopier(int render_process_id, int render_view_id);

  // If non-NULL, use the given |override| to access the backing store.
  // This is used for unit testing.
  void SetRenderWidgetHostForTesting(RenderWidgetHost* override);

  // Starts the copy from the backing store.  Must be run on the UI
  // BrowserThread.  |done_cb| is invoked with result status.  When successful
  // (OK), the bitmap of the capture is transferred to the callback along with
  // the timestamp at which the capture was completed.
  void StartCopy(int frame_number, int desired_width, int desired_height,
                 const DoneCB& done_cb);

 private:
  void LookUpAndObserveWebContents();

  void CopyFromBackingStoreComplete(int frame_number,
                                    scoped_ptr<skia::PlatformBitmap> capture,
                                    const DoneCB& done_cb, bool success);

  // The "starting point" to find the capture source.
  const int render_process_id_;
  const int render_view_id_;

  // If the following is NULL (normal behavior), the implementation should
  // access RenderWidgetHost via web_contents().
  RenderWidgetHost* rwh_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(BackingStoreCopier);
};

// Renders captures (from the backing store) into video frame buffers on a
// separate thread.  Manages use of internally-owned video frame buffers.
class VideoFrameRenderer {
 public:
  typedef base::Callback<void(const SkBitmap*)> DoneCB;

  VideoFrameRenderer();

  // Render the |capture| into a video frame buffer of the given size, then
  // invoke |done_cb| with a pointer to the result.  The caller must guarantee
  // Release() will be called after the result is no longer needed.
  void Render(int frame_number,
              scoped_ptr<skia::PlatformBitmap> capture,
              int frame_width, int frame_height,
              const DoneCB& done_cb);

  // Return |frame_buffer| to the internal pool for re-use.
  void Release(const SkBitmap* frame_buffer);

 private:
  void RenderOnRenderThread(int frame_number,
                            scoped_ptr<skia::PlatformBitmap> capture,
                            int frame_width, int frame_height,
                            const DoneCB& done_cb);

  struct RenderOutput {
    SkBitmap frame_buffer;
    gfx::Rect region_used;
    bool in_use;
  };

  base::Thread render_thread_;
  base::Lock lock_;  // Guards changes to output_[i].in_use.
  RenderOutput output_[2];

  DISALLOW_COPY_AND_ASSIGN(VideoFrameRenderer);
};

// Wrapper around media::VideoCaptureDevice::EventHandler to provide synchronous
// access to the underlying instance.
class SynchronizedConsumer {
 public:
  SynchronizedConsumer();

  void SetConsumer(media::VideoCaptureDevice::EventHandler* consumer);

  void OnFrameInfo(const media::VideoCaptureCapability& info);
  void OnError();
  void OnIncomingCapturedFrame(const uint8* pixels, int size,
                               const base::Time& timestamp);

 private:
  base::Lock consumer_lock_;
  media::VideoCaptureDevice::EventHandler* wrapped_consumer_;

  DISALLOW_COPY_AND_ASSIGN(SynchronizedConsumer);
};

// Delivers rendered video frames to a consumer on a separate thread.  Also
// responsible for logging the effective frame rate.
class VideoFrameDeliverer {
 public:
  explicit VideoFrameDeliverer(SynchronizedConsumer* consumer);

  void Deliver(int frame_number,
               const SkBitmap& frame_buffer, const base::Time& frame_timestamp,
               const base::Closure& done_cb);

 private:
  void DeliverOnDeliverThread(int frame_number,
                              const SkBitmap& frame_buffer,
                              const base::Time& frame_timestamp,
                              const base::Closure& done_cb);

  base::Thread deliver_thread_;
  SynchronizedConsumer* const consumer_;

  // The following keep track of and log the effective frame rate (from the
  // deliver stage) whenever verbose logging is turned on.
  base::Time last_frame_rate_log_time_;
  int count_frames_rendered_;

  DISALLOW_COPY_AND_ASSIGN(VideoFrameDeliverer);
};

BackingStoreCopier::BackingStoreCopier(int render_process_id,
                                       int render_view_id)
    : render_process_id_(render_process_id), render_view_id_(render_view_id),
      rwh_for_testing_(NULL) {}

void BackingStoreCopier::LookUpAndObserveWebContents() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Look-up the RenderViewHost and, from that, the WebContents that wraps it.
  // If successful, begin observing the WebContents instance.  If unsuccessful,
  // stop observing and post an error.
  //
  // Why this can be unsuccessful: The request for mirroring originates in a
  // render process, and this request is based on the current RenderView
  // associated with a tab.  However, by the time we get up-and-running here,
  // there have been multiple back-and-forth IPCs between processes, as well as
  // a bit of indirection across threads.  It's easily possible that, in the
  // meantime, the original RenderView may have gone away.
  RenderViewHost* const rvh =
      RenderViewHost::FromID(render_process_id_, render_view_id_);
  DVLOG_IF(1, !rvh) << "RenderViewHost::FromID("
                    << render_process_id_ << ", " << render_view_id_
                    << ") returned NULL.";
  Observe(rvh ? WebContents::FromRenderViewHost(rvh) : NULL);
  DVLOG_IF(1, !web_contents())
      << "WebContents::FromRenderViewHost(" << rvh << ") returned NULL.";
}

void BackingStoreCopier::SetRenderWidgetHostForTesting(
    RenderWidgetHost* override) {
  rwh_for_testing_ = override;
}

void BackingStoreCopier::StartCopy(int frame_number,
                                   int desired_width, int desired_height,
                                   const DoneCB& done_cb) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  TRACE_EVENT_ASYNC_BEGIN1("mirroring", "Capture", this,
                           "frame_number", frame_number);

  RenderWidgetHost* rwh;
  if (rwh_for_testing_) {
    rwh = rwh_for_testing_;
  } else {
    if (!web_contents()) {  // No source yet.
      LookUpAndObserveWebContents();
      if (!web_contents()) {  // No source ever.
        done_cb.Run(NO_SOURCE,
                    scoped_ptr<skia::PlatformBitmap>(NULL), base::Time());
        return;
      }
    }
    rwh = web_contents()->GetRenderViewHost();
    if (!rwh) {
      // Transient failure state (e.g., a RenderView is being replaced).
      done_cb.Run(TRANSIENT_ERROR,
                  scoped_ptr<skia::PlatformBitmap>(NULL), base::Time());
      return;
    }
  }

  gfx::Size fitted_size;
  if (RenderWidgetHostView* const view = rwh->GetView()) {
    const gfx::Size& view_size = view->GetViewBounds().size();
    if (!view_size.IsEmpty()) {
      CalculateFittedSize(view_size.width(), view_size.height(),
                          desired_width, desired_height,
                          &fitted_size);
    }
  }

  // TODO(miu): Look into tweaking the interface to CopyFromBackingStore, since
  // it seems poor to have to allocate a new skia::PlatformBitmap as an output
  // buffer for each successive frame (rather than reuse buffers).  Perhaps
  // PlatformBitmap itself should only re-Allocate when necessary?
  skia::PlatformBitmap* const bitmap = new skia::PlatformBitmap();
  scoped_ptr<skia::PlatformBitmap> capture(bitmap);
  rwh->CopyFromBackingStore(
      gfx::Rect(),
      fitted_size,
      base::Bind(&BackingStoreCopier::CopyFromBackingStoreComplete,
                 base::Unretained(this),
                 frame_number, base::Passed(&capture), done_cb),
      bitmap);

  // TODO(miu): When a tab is not visible to the user, rendering stops.  For
  // mirroring, however, it's important that rendering continues to happen.
}

void BackingStoreCopier::CopyFromBackingStoreComplete(
    int frame_number, scoped_ptr<skia::PlatformBitmap> capture,
    const DoneCB& done_cb, bool success) {
  // Note: No restriction on which thread invokes this method but, currently,
  // it's always the UI BrowserThread.

  TRACE_EVENT_ASYNC_END1("mirroring", "Capture", this,
                         "frame_number", frame_number);

  if (success) {
    done_cb.Run(OK, capture.Pass(), base::Time::Now());
  } else {
    // Capture can fail due to transient issues, so just skip this frame.
    DVLOG(1) << "CopyFromBackingStore was not successful; skipping frame.";
    done_cb.Run(TRANSIENT_ERROR,
                scoped_ptr<skia::PlatformBitmap>(NULL), base::Time());
  }
}

VideoFrameRenderer::VideoFrameRenderer()
    : render_thread_("WebContentsVideo_RenderThread") {
  output_[0].in_use = false;
  output_[1].in_use = false;
  render_thread_.Start();
}

void VideoFrameRenderer::Render(int frame_number,
                                scoped_ptr<skia::PlatformBitmap> capture,
                                int frame_width, int frame_height,
                                const DoneCB& done_cb) {
  render_thread_.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&VideoFrameRenderer::RenderOnRenderThread,
                 base::Unretained(this),
                 frame_number, base::Passed(&capture),
                 frame_width, frame_height, done_cb));
}

void VideoFrameRenderer::RenderOnRenderThread(
    int frame_number,
    scoped_ptr<skia::PlatformBitmap> capture,
    int frame_width, int frame_height,
    const DoneCB& done_cb) {
  DCHECK_EQ(render_thread_.message_loop(), MessageLoop::current());

  TRACE_EVENT1("mirroring", "RenderFrame", "frame_number", frame_number);

  const SkBitmap& captured_bitmap = capture->GetBitmap();
  gfx::Size fitted_size;
  {
    SkAutoLockPixels locker(captured_bitmap);

    // Sanity-check the captured bitmap.
    if (captured_bitmap.empty() ||
        !captured_bitmap.readyToDraw() ||
        captured_bitmap.config() != SkBitmap::kARGB_8888_Config ||
        captured_bitmap.width() < 2 || captured_bitmap.height() < 2) {
      DVLOG(1) << "captured_bitmap unacceptable (size="
               << captured_bitmap.getSize()
               << ", ready=" << captured_bitmap.readyToDraw()
               << ", config=" << captured_bitmap.config() << ')';
      return;
    }

    // Calculate the fitted_size based on the size of the captured_bitmap.
    CalculateFittedSize(captured_bitmap.width(), captured_bitmap.height(),
                        frame_width, frame_height,
                        &fitted_size);
  }

  // Select an available output buffer.
  RenderOutput* out;
  {
    base::AutoLock guard(lock_);
    if (!output_[0].in_use) {
      out = &output_[0];
    } else if (!output_[1].in_use) {
      out = &output_[1];
      DVLOG_IF(1, out->frame_buffer.empty()) << "Needing to use second buffer.";
    } else {
      DVLOG(1) << "All buffers are in-use.";
      return;
    }
  }

  // TODO(miu): The rest of this method is not optimal, both in CPU and memory
  // usage.  We need to revisit this code and replace most of it with a
  // single-shot, optimized Scale+YUVConvert function.

  // Scale the bitmap to the required size, if necessary.
  const SkBitmap* scaled_bitmap = &captured_bitmap;
  SkBitmap skia_resized_bitmap;
  if (captured_bitmap.width() != fitted_size.width() ||
      captured_bitmap.height() != fitted_size.height()) {
    skia_resized_bitmap = skia::ImageOperations::Resize(
        captured_bitmap, skia::ImageOperations::RESIZE_BOX,
        fitted_size.width(), fitted_size.height());
    scaled_bitmap = &skia_resized_bitmap;
  }

  // Realloc the frame buffer, if necessary.
  if (out->frame_buffer.width() != frame_width ||
      out->frame_buffer.height() != frame_height) {
    out->frame_buffer.setConfig(
        SkBitmap::kARGB_8888_Config, frame_width, frame_height);
    if (!out->frame_buffer.allocPixels()) {
      DVLOG(1) << "Failed to allocate memory for frame buffer.";
      return;
    }
    out->region_used = gfx::Rect(-1, -1, 0, 0);
  }

  {
    SkAutoLockPixels locker(out->frame_buffer);

    // Calculate the region to place the scaled_bitmap within the video frame
    // buffer.  If the region has moved/contracted since the last use of the
    // frame buffer, clear the frame buffer (i.e., paint it all black).
    const gfx::Rect region_in_frame = gfx::Rect(
        MakeEven((frame_width - fitted_size.width()) / 2),
        MakeEven((frame_height - fitted_size.height()) / 2),
        fitted_size.width(),
        fitted_size.height());
    if (!region_in_frame.Contains(out->region_used)) {
      out->frame_buffer.eraseColor(SK_ColorBLACK);
    }
    out->region_used = region_in_frame;

    scaled_bitmap->copyPixelsTo(
        out->frame_buffer.getAddr32(region_in_frame.x(), region_in_frame.y()),
        out->frame_buffer.getSize(),
        out->frame_buffer.rowBytes(),
        true);
  }

  // The result is now ready.
  {
    base::AutoLock guard(lock_);
    out->in_use = true;
  }
  done_cb.Run(&out->frame_buffer);
}

void VideoFrameRenderer::Release(const SkBitmap* frame_buffer) {
  if (frame_buffer == &(output_[0].frame_buffer)) {
    base::AutoLock guard(lock_);
    output_[0].in_use = false;
  }
  if (frame_buffer == &(output_[1].frame_buffer)) {
    base::AutoLock guard(lock_);
    output_[1].in_use = false;
  }
}

SynchronizedConsumer::SynchronizedConsumer() : wrapped_consumer_(NULL) {}

void SynchronizedConsumer::SetConsumer(
    media::VideoCaptureDevice::EventHandler* consumer) {
  base::AutoLock guard(consumer_lock_);
  wrapped_consumer_ = consumer;
}

void SynchronizedConsumer::OnFrameInfo(
    const media::VideoCaptureCapability& info) {
  base::AutoLock guard(consumer_lock_);
  if (wrapped_consumer_) {
    wrapped_consumer_->OnFrameInfo(info);
  }
}

void SynchronizedConsumer::OnError() {
  base::AutoLock guard(consumer_lock_);
  if (wrapped_consumer_) {
    wrapped_consumer_->OnError();
  }
}

void SynchronizedConsumer::OnIncomingCapturedFrame(
    const uint8* pixels, int size, const base::Time& timestamp) {
  base::AutoLock guard(consumer_lock_);
  if (wrapped_consumer_) {
    wrapped_consumer_->OnIncomingCapturedFrame(pixels, size, timestamp);
  }
}

VideoFrameDeliverer::VideoFrameDeliverer(SynchronizedConsumer* consumer)
    : deliver_thread_("WebContentsVideo_DeliverThread"), consumer_(consumer) {
  DCHECK(consumer_);
  deliver_thread_.Start();
}

void VideoFrameDeliverer::Deliver(
    int frame_number,
    const SkBitmap& frame_buffer, const base::Time& frame_timestamp,
    const base::Closure& done_cb) {
  deliver_thread_.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&VideoFrameDeliverer::DeliverOnDeliverThread,
                 base::Unretained(this),
                 frame_number, base::ConstRef(frame_buffer), frame_timestamp,
                 done_cb));
}

void VideoFrameDeliverer::DeliverOnDeliverThread(
    int frame_number,
    const SkBitmap& frame_buffer, const base::Time& frame_timestamp,
    const base::Closure& done_cb) {
  DCHECK_EQ(deliver_thread_.message_loop(), MessageLoop::current());

  TRACE_EVENT1("mirroring", "DeliverFrame", "frame_number", frame_number);

  // Send the frame to the consumer.
  // Note: The consumer will do an ARGB-->YUV conversion in this callback,
  // blocking the current thread for a bit.
  SkAutoLockPixels frame_buffer_locker(frame_buffer);
  consumer_->OnIncomingCapturedFrame(
      static_cast<const uint8*>(frame_buffer.getPixels()),
      frame_buffer.getSize(),
      frame_timestamp);

  // Log frame rate, if verbose logging is turned on.
  if (VLOG_IS_ON(1)) {
    static const base::TimeDelta kFrameRateLogInterval =
        base::TimeDelta::FromSeconds(5);
    const base::Time& now = base::Time::Now();
    if (last_frame_rate_log_time_.is_null()) {
      last_frame_rate_log_time_ = now;
      count_frames_rendered_ = 0;
    } else {
      ++count_frames_rendered_;
      const base::TimeDelta elapsed = now - last_frame_rate_log_time_;
      if (elapsed >= kFrameRateLogInterval) {
        const double measured_fps =
            count_frames_rendered_ / elapsed.InSecondsF();
        VLOG(1) << "Current measured frame rate for CaptureMachine@" << this
                << " is " << measured_fps << " FPS.";
        last_frame_rate_log_time_ = now;
        count_frames_rendered_ = 0;
      }
    }
  }

  // All done.
  done_cb.Run();
}

}  // namespace

// The "meat" of the video capture implementation, which is a ref-counted class.
// Separating this from the "shell class" WebContentsVideoCaptureDevice allows
// safe destruction without needing to block any threads (e.g., the IO
// BrowserThread).
//
// CaptureMachine manages a simple state machine and the pipeline (see notes at
// top of this file).  It times the start of successive captures and
// facilitates the processing of each through the stages of the pipeline.
class CaptureMachine
    : public base::RefCountedThreadSafe<CaptureMachine, CaptureMachine> {
 public:
  CaptureMachine(int render_process_id, int render_view_id);

  // Sets the capture source to the given |override| for unit testing.
  // Also, |destroy_cb| will be invoked after CaptureMachine is fully destroyed
  // (to synchronize tear-down).
  void InitializeForTesting(RenderWidgetHost* override,
                            const base::Closure& destroy_cb);

  // Synchronously sets/unsets the consumer.  Pass |consumer| as NULL to remove
  // the reference to the consumer; then, once this method returns,
  // CaptureMachine will no longer invoke callbacks on the old consumer from any
  // thread.
  void SetConsumer(media::VideoCaptureDevice::EventHandler* consumer);

  // Asynchronous requests to change CaptureMachine state.
  void Allocate(int width, int height, int frame_rate);
  void Start();
  void Stop();
  void DeAllocate();

 private:
  friend class base::RefCountedThreadSafe<CaptureMachine, CaptureMachine>;

  // Flag indicating current state.
  enum State {
    kIdle,
    kAllocated,
    kCapturing,
    kError,
    kDestroyed
  };

  virtual ~CaptureMachine();

  void TransitionStateTo(State next_state);

  // Stops capturing and notifies consumer_ of an error state.
  void Error();

  // Schedules the next frame capture off of the system clock, skipping frames
  // to catch-up if necessary.
  void ScheduleNextFrameCapture();

  // The glue between the pipeline stages.
  void StartSnapshot();
  void SnapshotComplete(int frame_number,
                        BackingStoreCopier::Result result,
                        scoped_ptr<skia::PlatformBitmap> capture,
                        const base::Time& capture_time);
  void RenderComplete(int frame_number,
                      const base::Time& capture_time,
                      const SkBitmap* frame_buffer);
  void DeliverComplete(const SkBitmap* frame_buffer);

  // Specialized RefCounted traits for CaptureMachine, so that operator delete
  // is called from an "outside" thread.  See comments for "traits" in
  // base/memory/ref_counted.h.
  static void Destruct(const CaptureMachine* x);
  static void DeleteFromOutsideThread(const CaptureMachine* x);

  SynchronizedConsumer consumer_;  // Recipient of frames.

  // Used to ensure state machine transitions occur synchronously, and that
  // capturing executes at regular intervals.
  base::Thread manager_thread_;

  State state_;  // Current lifecycle state.
  media::VideoCaptureCapability settings_;  // Capture settings.
  base::Time next_start_capture_time_;  // When to start capturing next frame.
  int frame_number_;  // Counter of frames, including skipped frames.
  base::TimeDelta capture_period_;  // Time between frames.

  bool is_snapshotting_;  // True while taking a snapshot with copier_.
  int num_renders_pending_;  // The number of renders enqueued.

  // The three pipeline stages.
  BackingStoreCopier copier_;
  VideoFrameRenderer renderer_;
  VideoFrameDeliverer deliverer_;

  base::Closure destroy_cb_;  // Invoked once CaptureMachine is destroyed.

  DISALLOW_COPY_AND_ASSIGN(CaptureMachine);
};

CaptureMachine::CaptureMachine(int render_process_id, int render_view_id)
    : manager_thread_("WebContentsVideo_ManagerThread"),
      state_(kIdle),
      is_snapshotting_(false),
      num_renders_pending_(0),
      copier_(render_process_id, render_view_id),
      deliverer_(&consumer_) {
  manager_thread_.Start();
}

void CaptureMachine::InitializeForTesting(RenderWidgetHost* override,
                                          const base::Closure& destroy_cb) {
  copier_.SetRenderWidgetHostForTesting(override);
  destroy_cb_ = destroy_cb;
}

void CaptureMachine::SetConsumer(
    media::VideoCaptureDevice::EventHandler* consumer) {
  consumer_.SetConsumer(consumer);
}

void CaptureMachine::Allocate(int width, int height, int frame_rate) {
  ENSURE_INVOKED_ON_THREAD(manager_thread_,
                           &CaptureMachine::Allocate, this,
                           width, height, frame_rate);

  if (state_ != kIdle) {
    DVLOG(1) << "Allocate() invoked when not in state Idle.";
    return;
  }

  if (frame_rate <= 0) {
    DVLOG(1) << "invalid frame_rate: " << frame_rate;
    Error();
    return;
  }

  // Frame dimensions must each be a positive, even integer, since the consumer
  // wants (or will convert to) YUV420.
  width = MakeEven(width);
  height = MakeEven(height);
  if (width < kMinFrameWidth || height < kMinFrameHeight) {
    DVLOG(1) << "invalid width (" << width << ") and/or height ("
             << height << ")";
    Error();
    return;
  }

  settings_.width = width;
  settings_.height = height;
  settings_.frame_rate = frame_rate;
  settings_.color = media::VideoCaptureCapability::kARGB;
  settings_.expected_capture_delay = 0;
  settings_.interlaced = false;

  capture_period_ = base::TimeDelta::FromMicroseconds(
      1000000.0 / settings_.frame_rate + 0.5);

  consumer_.OnFrameInfo(settings_);

  TransitionStateTo(kAllocated);
}

void CaptureMachine::Start() {
  ENSURE_INVOKED_ON_THREAD(manager_thread_, &CaptureMachine::Start, this);

  if (state_ != kAllocated) {
    return;
  }

  TransitionStateTo(kCapturing);

  next_start_capture_time_ = base::Time::Now();
  frame_number_ = 0;
  ScheduleNextFrameCapture();
}

void CaptureMachine::Stop() {
  ENSURE_INVOKED_ON_THREAD(manager_thread_, &CaptureMachine::Stop, this);

  if (state_ != kCapturing) {
    return;
  }

  TransitionStateTo(kAllocated);
}

void CaptureMachine::DeAllocate() {
  ENSURE_INVOKED_ON_THREAD(manager_thread_,
                           &CaptureMachine::DeAllocate, this);

  if (state_ == kCapturing) {
    Stop();
  }
  if (state_ == kAllocated) {
    TransitionStateTo(kIdle);
  }
}

CaptureMachine::~CaptureMachine() {
  DVLOG(1) << "CaptureMachine@" << this << " destroying.";
  state_ = kDestroyed;
  // Note: Implicit destructors will be called after this, which will block the
  // current thread while joining on the other threads.  However, this should be
  // instantaneous since the other threads' task queues *must* be empty at this
  // point (because CaptureMachine's ref-count is zero).
}

// static
void CaptureMachine::Destruct(const CaptureMachine* x) {
  // The current thread is very likely to be one owned by CaptureMachine.  When
  // ~CaptureMachine() is called, it will attempt to join with the
  // CaptureMachine-owned threads, including itself.  Since it's illegal for a
  // thread to join with itself, we need to trampoline the destructor call to
  // another thread.
  BrowserThread::PostBlockingPoolTask(
      FROM_HERE, base::Bind(&DeleteFromOutsideThread, x));
}

// static
void CaptureMachine::DeleteFromOutsideThread(const CaptureMachine* x) {
  const base::Closure run_after_delete = x->destroy_cb_;
  // Note: Thread joins are about to happen here (in ~CaptureThread()).
  delete x;
  if (!run_after_delete.is_null()) {
    run_after_delete.Run();
  }
}

void CaptureMachine::TransitionStateTo(State next_state) {
  DCHECK_EQ(manager_thread_.message_loop(), MessageLoop::current());

#ifndef NDEBUG
  static const char* kStateNames[] = {
    "Idle", "Allocated", "Capturing", "Error", "Destroyed"
  };
  DVLOG(1) << "State change: " << kStateNames[state_]
           << " --> " << kStateNames[next_state];
#endif

  state_ = next_state;
}

void CaptureMachine::Error() {
  DCHECK_EQ(manager_thread_.message_loop(), MessageLoop::current());

  if (state_ == kCapturing) {
    Stop();
  }
  TransitionStateTo(kError);

  consumer_.OnError();
}

void CaptureMachine::ScheduleNextFrameCapture() {
  DCHECK_EQ(manager_thread_.message_loop(), MessageLoop::current());

  if (state_ != kCapturing) {
    return;
  }

  DCHECK_LT(0, settings_.frame_rate);
  next_start_capture_time_ += capture_period_;
  ++frame_number_;
  const base::Time& now = base::Time::Now();
  if (next_start_capture_time_ < now) {
    // One or more frame captures were missed.  Skip ahead.
    const base::TimeDelta& behind_by = now - next_start_capture_time_;
    const int64 num_frames_missed = (behind_by / capture_period_) + 1;
    VLOG(1) << "Ran behind by " << num_frames_missed << " frames.";
    next_start_capture_time_ += capture_period_ * num_frames_missed;
    frame_number_ += num_frames_missed;
  } else if (now + capture_period_ < next_start_capture_time_) {
    // Note: This should only happen if the system clock has been reset
    // backwards in time.
    VLOG(1) << "Resetting next capture start time due to clock skew.";
    next_start_capture_time_ = now + capture_period_;
  }

  manager_thread_.message_loop()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&CaptureMachine::StartSnapshot, this),
      next_start_capture_time_ - now);
}

void CaptureMachine::StartSnapshot() {
  DCHECK_EQ(manager_thread_.message_loop(), MessageLoop::current());

  if (state_ != kCapturing) {
    return;
  }

  if (!is_snapshotting_) {
    is_snapshotting_ = true;

    const BackingStoreCopier::DoneCB& done_cb =
        media::BindToLoop(manager_thread_.message_loop_proxy(),
                          base::Bind(&CaptureMachine::SnapshotComplete, this,
                                     frame_number_));
    const base::Closure& start_cb =
        base::Bind(&BackingStoreCopier::StartCopy,
                   base::Unretained(&copier_),
                   frame_number_, settings_.width, settings_.height, done_cb);
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, start_cb);
  }

  ScheduleNextFrameCapture();
}

void CaptureMachine::SnapshotComplete(int frame_number,
                                      BackingStoreCopier::Result result,
                                      scoped_ptr<skia::PlatformBitmap> capture,
                                      const base::Time& capture_time) {
  DCHECK_EQ(manager_thread_.message_loop(), MessageLoop::current());

  DCHECK(is_snapshotting_);
  is_snapshotting_ = false;

  if (state_ != kCapturing) {
    return;
  }

  switch (result) {
    case BackingStoreCopier::OK:
      if (num_renders_pending_ <= 1) {
        ++num_renders_pending_;
        DCHECK(capture);
        DCHECK(!capture_time.is_null());
        renderer_.Render(
            frame_number,
            capture.Pass(),
            settings_.width, settings_.height,
            media::BindToLoop(manager_thread_.message_loop_proxy(),
                              base::Bind(&CaptureMachine::RenderComplete, this,
                                         frame_number, capture_time)));
      }
      break;

    case BackingStoreCopier::TRANSIENT_ERROR:
      // Skip this frame.
      break;

    case BackingStoreCopier::NO_SOURCE:
      DVLOG(1) << "no capture source";
      Error();
      break;
  }
}

void CaptureMachine::RenderComplete(int frame_number,
                                    const base::Time& capture_time,
                                    const SkBitmap* frame_buffer) {
  DCHECK_EQ(manager_thread_.message_loop(), MessageLoop::current());

  --num_renders_pending_;
  DCHECK_LE(0, num_renders_pending_);

  if (state_ != kCapturing) {
    return;
  }

  DCHECK(!capture_time.is_null());
  DCHECK(frame_buffer);
  deliverer_.Deliver(
      frame_number, *frame_buffer, capture_time,
      base::Bind(&CaptureMachine::DeliverComplete, this, frame_buffer));
}

void CaptureMachine::DeliverComplete(const SkBitmap* frame_buffer) {
  renderer_.Release(frame_buffer);
}

WebContentsVideoCaptureDevice::WebContentsVideoCaptureDevice(
    const media::VideoCaptureDevice::Name& name,
    int render_process_id, int render_view_id)
    : device_name_(name),
      capturer_(new CaptureMachine(render_process_id, render_view_id)) {}

WebContentsVideoCaptureDevice::WebContentsVideoCaptureDevice(
    RenderWidgetHost* test_source, const base::Closure& destroy_cb)
    : capturer_(new CaptureMachine(-1, -1)) {
  device_name_.device_name = "WebContentsForTesting";
  device_name_.unique_id = "-1:-1";
  capturer_->InitializeForTesting(test_source, destroy_cb);
}

WebContentsVideoCaptureDevice::~WebContentsVideoCaptureDevice() {
  DVLOG(2) << "WebContentsVideoCaptureDevice@" << this << " destroying.";
}

// static
media::VideoCaptureDevice* WebContentsVideoCaptureDevice::Create(
    const std::string& device_id) {
  // Parse device_id into render_process_id and render_view_id.
  int render_process_id = -1;
  int render_view_id = -1;
  if (!WebContentsCaptureUtil::ExtractTabCaptureTarget(device_id,
                                                       &render_process_id,
                                                       &render_view_id))
    return NULL;

  media::VideoCaptureDevice::Name name;
  base::SStringPrintf(&name.device_name,
                      "WebContents[%.*s]",
                      static_cast<int>(device_id.size()), device_id.data());
  name.unique_id = device_id;

  return new WebContentsVideoCaptureDevice(
      name, render_process_id, render_view_id);
}

// static
media::VideoCaptureDevice* WebContentsVideoCaptureDevice::CreateForTesting(
    RenderWidgetHost* test_source, const base::Closure& destroy_cb) {
  return new WebContentsVideoCaptureDevice(test_source, destroy_cb);
}

void WebContentsVideoCaptureDevice::Allocate(
    int width, int height, int frame_rate,
    VideoCaptureDevice::EventHandler* consumer) {
  DCHECK(capturer_);
  capturer_->SetConsumer(consumer);
  capturer_->Allocate(width, height, frame_rate);
}

void WebContentsVideoCaptureDevice::Start() {
  DCHECK(capturer_);
  capturer_->Start();
}

void WebContentsVideoCaptureDevice::Stop() {
  DCHECK(capturer_);
  capturer_->Stop();
}

void WebContentsVideoCaptureDevice::DeAllocate() {
  DCHECK(capturer_);
  capturer_->SetConsumer(NULL);
  capturer_->DeAllocate();
}

const media::VideoCaptureDevice::Name&
WebContentsVideoCaptureDevice::device_name() {
  return device_name_;
}

}  // namespace content
