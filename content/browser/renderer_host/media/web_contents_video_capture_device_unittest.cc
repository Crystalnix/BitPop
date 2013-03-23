// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/media/web_contents_video_capture_device.h"

#include "base/bind_helpers.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/waitable_event.h"
#include "content/browser/browser_thread_impl.h"
#include "content/browser/renderer_host/render_widget_host_delegate.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_browser_context.h"
#include "media/video/capture/video_capture_types.h"
#include "skia/ext/platform_canvas.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"

namespace content {
namespace {
const int kTestWidth = 1280;
const int kTestHeight = 720;
const int kBytesPerPixel = 4;
const int kTestFramesPerSecond = 8;
const SkColor kNothingYet = 0xdeadbeef;
const SkColor kNotInterested = ~kNothingYet;
}

// A stub implementation which returns solid-color bitmaps in calls to
// CopyFromBackingStore().  The unit tests can change the color for successive
// captures.
class StubRenderWidgetHost : public RenderWidgetHostImpl {
 public:
  StubRenderWidgetHost(RenderProcessHost* process, int routing_id)
      : RenderWidgetHostImpl(&delegate_, process, routing_id),
        color_(kNothingYet) {}

  void SetSolidColor(SkColor color) {
    base::AutoLock guard(lock_);
    color_ = color;
  }

  virtual void CopyFromBackingStore(
      const gfx::Rect& src_rect,
      const gfx::Size& accelerated_dst_size,
      const base::Callback<void(bool)>& callback,
      skia::PlatformBitmap* output) OVERRIDE {
    DCHECK(output);
    EXPECT_TRUE(output->Allocate(kTestWidth, kTestHeight, true));
    SkBitmap bitmap = output->GetBitmap();
    {
      SkAutoLockPixels locker(bitmap);
      base::AutoLock guard(lock_);
      bitmap.eraseColor(color_);
    }

    callback.Run(true);
  }

 private:
  class StubRenderWidgetHostDelegate : public RenderWidgetHostDelegate {
   public:
    StubRenderWidgetHostDelegate() {}
    virtual ~StubRenderWidgetHostDelegate() {}

   private:
    DISALLOW_COPY_AND_ASSIGN(StubRenderWidgetHostDelegate);
  };

  StubRenderWidgetHostDelegate delegate_;
  base::Lock lock_;  // Guards changes to color_.
  SkColor color_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(StubRenderWidgetHost);
};

// A stub consumer of captured video frames, which checks the output of
// WebContentsVideoCaptureDevice.
class StubConsumer : public media::VideoCaptureDevice::EventHandler {
 public:
  StubConsumer() : output_changed_(&lock_),
                   picture_color_(kNothingYet),
                   error_encountered_(false) {}
  virtual ~StubConsumer() {}

  // Returns false if an error was encountered.
  bool WaitForNextColorOrError(SkColor expected_color) {
    base::AutoLock guard(lock_);
    while (picture_color_ != expected_color && !error_encountered_) {
      output_changed_.Wait();
    }
    if (!error_encountered_) {
      EXPECT_EQ(expected_color, picture_color_);
      return true;
    } else {
      return false;
    }
  }

  virtual void OnIncomingCapturedFrame(const uint8* data, int length,
                                       base::Time timestamp) OVERRIDE {
    DCHECK(data);
    static const int kNumPixels = kTestWidth * kTestHeight;
    EXPECT_EQ(kNumPixels * kBytesPerPixel, length);
    const uint32* p = reinterpret_cast<const uint32*>(data);
    const uint32* const p_end = p + kNumPixels;
    const SkColor color = *p;
    bool all_pixels_are_the_same_color = true;
    for (++p; p < p_end; ++p) {
      if (*p != color) {
        all_pixels_are_the_same_color = false;
        break;
      }
    }
    EXPECT_TRUE(all_pixels_are_the_same_color);

    {
      base::AutoLock guard(lock_);
      if (color != picture_color_) {
        picture_color_ = color;
        output_changed_.Signal();
      }
    }
  }

  virtual void OnError() OVERRIDE {
    base::AutoLock guard(lock_);
    error_encountered_ = true;
    output_changed_.Signal();
  }

  virtual void OnFrameInfo(const media::VideoCaptureCapability& info) OVERRIDE {
    EXPECT_EQ(kTestWidth, info.width);
    EXPECT_EQ(kTestHeight, info.height);
    EXPECT_EQ(kTestFramesPerSecond, info.frame_rate);
    EXPECT_EQ(media::VideoCaptureCapability::kARGB, info.color);
  }

 private:
  base::Lock lock_;
  base::ConditionVariable output_changed_;
  SkColor picture_color_;
  bool error_encountered_;

  DISALLOW_COPY_AND_ASSIGN(StubConsumer);
};

// Test harness that sets up a minimal environment with necessary stubs.
class WebContentsVideoCaptureDeviceTest : public testing::Test {
 public:
  WebContentsVideoCaptureDeviceTest() {}

 protected:
  virtual void SetUp() {
    // This is a MessageLoop for the current thread.  The MockRenderProcessHost
    // will schedule its destruction in this MessageLoop during TearDown().
    message_loop_.reset(new MessageLoop(MessageLoop::TYPE_IO));

    // The CopyFromBackingStore and WebContents tracking occur on the UI thread.
    ui_thread_.reset(new BrowserThreadImpl(BrowserThread::UI));
    ui_thread_->Start();

    // And the rest...
    browser_context_.reset(new TestBrowserContext());
    source_.reset(new StubRenderWidgetHost(
        new MockRenderProcessHost(browser_context_.get()), MSG_ROUTING_NONE));
    destroyed_.reset(new base::WaitableEvent(true, false));
    device_.reset(WebContentsVideoCaptureDevice::CreateForTesting(
        source_.get(),
        base::Bind(&base::WaitableEvent::Signal,
                   base::Unretained(destroyed_.get()))));
    consumer_.reset(new StubConsumer);
  }

  virtual void TearDown() {
    // Tear down in opposite order of set-up.
    device_->DeAllocate();  // Guarantees no more use of consumer_.
    consumer_.reset();
    device_.reset();  // Release reference to internal CaptureMachine.
    message_loop_->RunUntilIdle();  // Just in case.
    destroyed_->Wait();  // Wait until CaptureMachine is fully destroyed.
    destroyed_.reset();
    source_.reset();
    browser_context_.reset();
    ui_thread_->Stop();
    ui_thread_.reset();
    message_loop_->RunUntilIdle();  // Deletes MockRenderProcessHost.
    message_loop_.reset();
  }

  // Accessors.
  StubRenderWidgetHost* source() const { return source_.get(); }
  media::VideoCaptureDevice* device() const { return device_.get(); }
  StubConsumer* consumer() const { return consumer_.get(); }

 private:
  scoped_ptr<MessageLoop> message_loop_;
  scoped_ptr<BrowserThreadImpl> ui_thread_;
  scoped_ptr<TestBrowserContext> browser_context_;
  scoped_ptr<StubRenderWidgetHost> source_;
  scoped_ptr<base::WaitableEvent> destroyed_;
  scoped_ptr<media::VideoCaptureDevice> device_;
  scoped_ptr<StubConsumer> consumer_;

  DISALLOW_COPY_AND_ASSIGN(WebContentsVideoCaptureDeviceTest);
};

// The "happy case" test.  No scaling is needed, so we should be able to change
// the picture emitted from the source and expect to see each delivered to the
// consumer.
TEST_F(WebContentsVideoCaptureDeviceTest, GoesThroughAllTheMotions) {
  device()->Allocate(kTestWidth, kTestHeight, kTestFramesPerSecond,
                     consumer());

  device()->Start();
  source()->SetSolidColor(SK_ColorRED);
  EXPECT_TRUE(consumer()->WaitForNextColorOrError(SK_ColorRED));
  source()->SetSolidColor(SK_ColorGREEN);
  EXPECT_TRUE(consumer()->WaitForNextColorOrError(SK_ColorGREEN));
  source()->SetSolidColor(SK_ColorBLUE);
  EXPECT_TRUE(consumer()->WaitForNextColorOrError(SK_ColorBLUE));
  source()->SetSolidColor(SK_ColorBLACK);
  EXPECT_TRUE(consumer()->WaitForNextColorOrError(SK_ColorBLACK));

  device()->DeAllocate();
}

TEST_F(WebContentsVideoCaptureDeviceTest, RejectsInvalidAllocateParams) {
  device()->Allocate(1280, 720, -2, consumer());
  EXPECT_FALSE(consumer()->WaitForNextColorOrError(kNotInterested));
}

}  // namespace content
