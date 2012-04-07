// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/video/capture/mac/video_capture_device_mac.h"

#import <QTKit/QTKit.h>

#include "base/logging.h"
#include "base/time.h"
#include "media/video/capture/mac/video_capture_device_qtkit_mac.h"

namespace media {

void VideoCaptureDevice::GetDeviceNames(Names* device_names) {
  // Loop through all available devices and add to |device_names|.
  device_names->clear();

  // TODO(mflodman) Return name and id as NSArray* instead of QTCaptureDevice*.
  for (QTCaptureDevice* device in [VideoCaptureDeviceQTKit deviceNames]) {
    Name name;
    NSString* qt_device_name = [device localizedDisplayName];
    name.device_name = [qt_device_name UTF8String];
    NSString* qt_unique_id = [device uniqueID];
    name.unique_id = [qt_unique_id UTF8String];
    device_names->push_back(name);
  }
}

VideoCaptureDevice* VideoCaptureDevice::Create(const Name& device_name) {
  VideoCaptureDeviceMac* capture_device =
      new VideoCaptureDeviceMac(device_name);
  if (!capture_device->Init()) {
    LOG(ERROR) << "Could not initialize VideoCaptureDevice.";
    delete capture_device;
    capture_device = NULL;
  }
  return capture_device;
}

VideoCaptureDeviceMac::VideoCaptureDeviceMac(const Name& device_name)
    : device_name_(device_name),
      observer_(NULL),
      state_(kNotInitialized),
      capture_device_(nil) {
}

VideoCaptureDeviceMac::~VideoCaptureDeviceMac() {
  [capture_device_ release];
}

void VideoCaptureDeviceMac::Allocate(int width, int height, int frame_rate,
                                     EventHandler* observer) {
  if (state_ != kIdle) {
    return;
  }
  observer_ = observer;
  NSString* deviceId =
      [NSString stringWithUTF8String:device_name_.unique_id.c_str()];

  if (![capture_device_ setCaptureDevice:deviceId]) {
    SetErrorState("Could not open capture device.");
    return;
  }
  if (![capture_device_ setCaptureHeight:height
                                   width:width
                               frameRate:frame_rate]) {
    SetErrorState("Could not configure capture device.");
    return;
  }

  state_ = kAllocated;
  Capability current_settings;
  current_settings.color = kARGB;
  current_settings.width = width;
  current_settings.height = height;
  current_settings.frame_rate = frame_rate;

  observer_->OnFrameInfo(current_settings);
}

void VideoCaptureDeviceMac::Start() {
  DCHECK_EQ(state_, kAllocated);
  if (![capture_device_ startCapture]) {
    SetErrorState("Could not start capture device.");
    return;
  }
  state_ = kCapturing;
}

void VideoCaptureDeviceMac::Stop() {
  DCHECK_EQ(state_, kCapturing);
  [capture_device_ stopCapture];
  state_ = kAllocated;
}

void VideoCaptureDeviceMac::DeAllocate() {
  if (state_ != kAllocated && state_ != kCapturing) {
    return;
  }
  if (state_ == kCapturing) {
    [capture_device_ stopCapture];
  }
  [capture_device_ setCaptureDevice:nil];
  state_ = kIdle;
}

const VideoCaptureDevice::Name& VideoCaptureDeviceMac::device_name() {
  return device_name_;
}

bool VideoCaptureDeviceMac::Init() {
  DCHECK_EQ(state_, kNotInitialized);

  Names device_names;
  GetDeviceNames(&device_names);
  for (Names::iterator it = device_names.begin();
       it != device_names.end();
       ++it) {
    if (device_name_.unique_id == it->unique_id) {
      capture_device_ =
          [[VideoCaptureDeviceQTKit alloc] initWithFrameReceiver:this];
      if (!capture_device_) {
        return false;
      }
      state_ = kIdle;
      return true;
    }
  }
  return false;
}

void VideoCaptureDeviceMac::ReceiveFrame(const uint8* video_frame,
                                         int video_frame_length,
                                         const Capability& frame_info) {
  observer_->OnIncomingCapturedFrame(video_frame, video_frame_length,
                                     base::Time::Now());
}

void VideoCaptureDeviceMac::SetErrorState(const std::string& reason) {
  DLOG(ERROR) << reason;
  state_ = kError;
  observer_->OnError();
}

} // namespace media
