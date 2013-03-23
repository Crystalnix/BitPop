// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/message_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/system_monitor/system_monitor.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/system_monitor/media_storage_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chrome {

namespace {

// Sample mtp device id and unique id.
const char kMtpDeviceId[] = "mtp:VendorModelSerial:ABC:1233:1237912873";
const char kUniqueId[] = "VendorModelSerial:ABC:1233:1237912873";
const char kImageCaptureDeviceId[] = "ic:xyz";

}  // namespace

typedef testing::Test MediaStorageUtilTest;

// Test to verify |MediaStorageUtil::MakeDeviceId| functionality using a sample
// mtp device unique id.
TEST_F(MediaStorageUtilTest, MakeMtpDeviceId) {
  std::string device_id =
      MediaStorageUtil::MakeDeviceId(MediaStorageUtil::MTP_OR_PTP, kUniqueId);
  ASSERT_EQ(kMtpDeviceId, device_id);
}

// Test to verify |MediaStorageUtil::CrackDeviceId| functionality using a sample
// mtp device id.
TEST_F(MediaStorageUtilTest, CrackMtpDeviceId) {
  MediaStorageUtil::Type type;
  std::string id;
  ASSERT_TRUE(MediaStorageUtil::CrackDeviceId(kMtpDeviceId, &type, &id));
  ASSERT_EQ(kUniqueId, id);
  ASSERT_EQ(MediaStorageUtil::MTP_OR_PTP, type);
}

TEST_F(MediaStorageUtilTest, TestImageCaptureDeviceId) {
  MediaStorageUtil::Type type;
  std::string id;
  EXPECT_TRUE(MediaStorageUtil::CrackDeviceId(kImageCaptureDeviceId,
                                              &type, &id));
  EXPECT_EQ(MediaStorageUtil::MAC_IMAGE_CAPTURE, type);
  EXPECT_EQ("xyz", id);
}

TEST_F(MediaStorageUtilTest, CanCreateFileSystemForImageCapture) {
  EXPECT_TRUE(MediaStorageUtil::CanCreateFileSystem(kImageCaptureDeviceId,
                                                    FilePath()));
  EXPECT_FALSE(MediaStorageUtil::CanCreateFileSystem(
      "dcim:xyz", FilePath(FILE_PATH_LITERAL("relative"))));
  EXPECT_FALSE(MediaStorageUtil::CanCreateFileSystem(
      "dcim:xyz", FilePath(FILE_PATH_LITERAL("../refparent"))));
}

TEST_F(MediaStorageUtilTest, DetectDeviceFiltered) {
  MessageLoop loop;
#if defined(OS_MACOSX)
  // This needs to happen before SystemMonitor's ctor.
  base::SystemMonitor::AllocateSystemIOPorts();
#endif
  // Installs global. Required MessageLoop.
  // On Mac, requires AllocateSystemIOPorts.
  base::SystemMonitor monitor;

  content::TestBrowserThread file_thread(content::BrowserThread::FILE, &loop);

  MediaStorageUtil::DeviceIdSet devices;
  devices.insert(kImageCaptureDeviceId);

  base::WaitableEvent event(true, false);
  MediaStorageUtil::FilterAttachedDevices(&devices,
      base::Bind(&base::WaitableEvent::Signal, base::Unretained(&event)));
  loop.RunUntilIdle();
  event.Wait();
  EXPECT_FALSE(devices.find(kImageCaptureDeviceId) != devices.end());

  base::SystemMonitor::Get()->ProcessRemovableStorageAttached(
      kImageCaptureDeviceId, ASCIIToUTF16("name"),
      FILE_PATH_LITERAL("/location"));
  devices.insert(kImageCaptureDeviceId);
  event.Reset();
  MediaStorageUtil::FilterAttachedDevices(&devices,
      base::Bind(&base::WaitableEvent::Signal, base::Unretained(&event)));
  loop.RunUntilIdle();
  event.Wait();

  EXPECT_TRUE(devices.find(kImageCaptureDeviceId) != devices.end());
}

}  // namespace chrome
