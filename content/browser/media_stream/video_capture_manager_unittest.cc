// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit test for VideoCaptureManager

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/process_util.h"
#include "content/browser/browser_thread.h"
#include "content/browser/media_stream/media_stream_provider.h"
#include "content/browser/media_stream/video_capture_manager.h"
#include "media/video/capture/video_capture_device.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::Return;

namespace media_stream {

// Listener class used to track progress of VideoCaptureManager test
class MockMediaStreamProviderListener : public MediaStreamProviderListener {
 public:
  MockMediaStreamProviderListener()
      : devices_() {
  }
  ~MockMediaStreamProviderListener() {}

  MOCK_METHOD2(Opened, void(MediaStreamType, MediaCaptureSessionId));
  MOCK_METHOD2(Closed, void(MediaStreamType, MediaCaptureSessionId));
  MOCK_METHOD1(DevicesEnumerated, void(const MediaCaptureDevices&));
  MOCK_METHOD3(Error, void(MediaStreamType, MediaCaptureSessionId,
                           MediaStreamProviderError));

  virtual void DevicesEnumerated(MediaStreamType stream_type,
                                 const MediaCaptureDevices& devices) {
    devices_.clear();
    for (MediaCaptureDevices::const_iterator it = devices.begin();
        it != devices.end();
        ++it) {
      devices_.push_back(*it);
    }
    DevicesEnumerated(devices);
  }

  media_stream::MediaCaptureDevices devices_;
};  // class MockMediaStreamProviderListener

}  // namespace media_stream

namespace {

// Needed as an input argument to Start()
class MockFrameObserver: public media::VideoCaptureDevice::EventHandler {
 public:
  virtual void OnError() {}
  void OnFrameInfo(const media::VideoCaptureDevice::Capability& info) {}
  virtual void OnIncomingCapturedFrame(const uint8* data, int length,
                                       base::Time timestamp) {}
};

// Test class
class VideoCaptureManagerTest : public testing::Test {
 public:
  VideoCaptureManagerTest()
      : listener_(),
        message_loop_(),
        io_thread_(),
        frame_observer_() {
  }
  virtual ~VideoCaptureManagerTest() {}

 protected:
  virtual void SetUp() {
    listener_.reset(new media_stream::MockMediaStreamProviderListener());
    message_loop_.reset(new MessageLoop(MessageLoop::TYPE_IO));
    io_thread_.reset(new BrowserThread(BrowserThread::IO, message_loop_.get()));
    frame_observer_.reset(new MockFrameObserver());
  }

  virtual void TearDown() {
    io_thread_.reset();
  }

  // Called on the VideoCaptureManager thread.
  static void PostQuitMessageLoop(MessageLoop* message_loop) {
    message_loop->PostTask(FROM_HERE, new MessageLoop::QuitTask());
  }

  // Called on the main thread.
  static void PostQuitOnVideoCaptureManagerThread(MessageLoop* message_loop) {
    media_stream::VideoCaptureManager::Get()->GetMessageLoop()->PostTask(
        FROM_HERE, NewRunnableFunction(&PostQuitMessageLoop, message_loop));
  }

  // SyncWithVideoCaptureManagerThread() waits until all pending tasks on the
  // video_capture_manager internal thread are executed while also processing
  // pending task in message_loop_ on the current thread. It is used to
  // synchronize with the video capture manager thread when we are stopping a
  // video capture device.
  void SyncWithVideoCaptureManagerThread() {
    message_loop_->PostTask(
        FROM_HERE, NewRunnableFunction(&PostQuitOnVideoCaptureManagerThread,
                                       message_loop_.get()));
    message_loop_->Run();
  }
  scoped_ptr<media_stream::MockMediaStreamProviderListener> listener_;
  scoped_ptr<MessageLoop> message_loop_;
  scoped_ptr<BrowserThread> io_thread_;
  scoped_ptr<MockFrameObserver> frame_observer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(VideoCaptureManagerTest);
};

// Test cases

// Try to open, start, stop and close a device.
TEST_F(VideoCaptureManagerTest, CreateAndClose) {
  InSequence s;
  EXPECT_CALL(*listener_, DevicesEnumerated(_))
    .Times(1);
  EXPECT_CALL(*listener_, Opened(media_stream::kVideoCapture, _))
    .Times(1);
  EXPECT_CALL(*listener_, Closed(media_stream::kVideoCapture, _))
    .Times(1);

  media_stream::VideoCaptureManager* vcm =
      media_stream::VideoCaptureManager::Get();
  // Make sure fake devices are used
  vcm->UseFakeDevice();
  vcm->Register(listener_.get());
  vcm->EnumerateDevices();

  // Wait to get device callback...
  SyncWithVideoCaptureManagerThread();

  int video_session_id = vcm->Open(listener_->devices_.front());

  media::VideoCaptureParams capture_params;
  capture_params.session_id = video_session_id;
  capture_params.width = 320;
  capture_params.height = 240;
  capture_params.frame_per_second = 30;
  vcm->Start(capture_params, frame_observer_.get());

  vcm->Stop(video_session_id, NULL);
  vcm->Close(video_session_id);

  // Wait to check callbacks before removing the listener
  SyncWithVideoCaptureManagerThread();
  vcm->Unregister();
}

// Open the same device twice, should fail.
TEST_F(VideoCaptureManagerTest, OpenTwice) {
  InSequence s;
  EXPECT_CALL(*listener_, DevicesEnumerated(_))
    .Times(1);
  EXPECT_CALL(*listener_, Opened(media_stream::kVideoCapture, _))
    .Times(1);
  EXPECT_CALL(*listener_, Error(media_stream::kVideoCapture, _,
                                media_stream::kDeviceAlreadyInUse))
    .Times(1);
  EXPECT_CALL(*listener_, Closed(media_stream::kVideoCapture, _))
    .Times(1);

  media_stream::VideoCaptureManager* vcm =
      media_stream::VideoCaptureManager::Get();
  // Make sure fake devices are used
  vcm->UseFakeDevice();
  vcm->Register(listener_.get());
  vcm->EnumerateDevices();

  // Wait to get device callback...
  SyncWithVideoCaptureManagerThread();

  int video_session_id = vcm->Open(listener_->devices_.front());

  // This should trigger an error callback with error code 'kDeviceAlreadyInUse'
  vcm->Open(listener_->devices_.front());

  vcm->Close(video_session_id);

  // Wait to check callbacks before removing the listener
  SyncWithVideoCaptureManagerThread();
  vcm->Unregister();
}

// Open two different devices.
TEST_F(VideoCaptureManagerTest, OpenTwo) {
  InSequence s;
  EXPECT_CALL(*listener_, DevicesEnumerated(_))
    .Times(1);
  EXPECT_CALL(*listener_, Opened(media_stream::kVideoCapture, _))
    .Times(2);
  EXPECT_CALL(*listener_, Closed(media_stream::kVideoCapture, _))
    .Times(2);

  media_stream::VideoCaptureManager* vcm =
      media_stream::VideoCaptureManager::Get();
  // Make sure fake devices are used
  vcm->UseFakeDevice();
  vcm->Register(listener_.get());
  vcm->EnumerateDevices();

  // Wait to get device callback...
  SyncWithVideoCaptureManagerThread();

  media_stream::MediaCaptureDevices::iterator it =
      listener_->devices_.begin();

  int video_session_id_first = vcm->Open(*it);

  // This should trigger an error callback with error code 'kDeviceAlreadyInUse'
  ++it;
  int video_session_id_second = vcm->Open(*it);

  vcm->Close(video_session_id_first);
  vcm->Close(video_session_id_second);

  // Wait to check callbacks before removing the listener
  SyncWithVideoCaptureManagerThread();
  vcm->Unregister();
}

// Try open a non-existing device.
TEST_F(VideoCaptureManagerTest, OpenNotExisting) {
  InSequence s;
  EXPECT_CALL(*listener_, DevicesEnumerated(_))
    .Times(1);
  EXPECT_CALL(*listener_, Error(media_stream::kVideoCapture, _,
                                media_stream::kDeviceNotAvailable))
    .Times(1);

  media_stream::VideoCaptureManager* vcm =
      media_stream::VideoCaptureManager::Get();
  // Make sure fake devices are used
  vcm->UseFakeDevice();
  vcm->Register(listener_.get());
  vcm->EnumerateDevices();

  // Wait to get device callback...
  SyncWithVideoCaptureManagerThread();

  media_stream::MediaStreamType stream_type = media_stream::kVideoCapture;
  std::string device_name("device_doesnt_exist");
  std::string device_id("id_doesnt_exist");
  media_stream::MediaCaptureDeviceInfo dummy_device(stream_type, device_name,
                                                    device_id, false);

  // This should fail with error code 'kDeviceNotAvailable'
  vcm->Open(dummy_device);

  // Wait to check callbacks before removing the listener
  SyncWithVideoCaptureManagerThread();
  vcm->Unregister();
}

// Start a device using "magic" id, i.e. call Start without calling Open.
TEST_F(VideoCaptureManagerTest, StartUsingId) {
  InSequence s;
  EXPECT_CALL(*listener_, Opened(media_stream::kVideoCapture, _))
    .Times(1);
  EXPECT_CALL(*listener_, Closed(media_stream::kVideoCapture, _))
    .Times(1);

  media_stream::VideoCaptureManager* vcm =
      media_stream::VideoCaptureManager::Get();
  // Make sure fake devices are used
  vcm->UseFakeDevice();
  vcm->Register(listener_.get());

  media::VideoCaptureParams capture_params;
  capture_params.session_id =
      media_stream::VideoCaptureManager::kStartOpenSessionId;
  capture_params.width = 320;
  capture_params.height = 240;
  capture_params.frame_per_second = 30;
  // Start shall trigger the Open callback
  vcm->Start(capture_params, frame_observer_.get());

  // Stop shall trigger the Close callback
  vcm->Stop(media_stream::VideoCaptureManager::kStartOpenSessionId, NULL);

  // Wait to check callbacks before removing the listener
  SyncWithVideoCaptureManagerThread();
  vcm->Unregister();
}

}  // namespace
