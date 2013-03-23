// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/system_monitor/removable_device_notifications_mac.h"

#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/mac/foundation_util.h"
#include "base/message_loop.h"
#include "base/sys_string_conversions.h"
#include "base/system_monitor/system_monitor.h"
#include "base/test/mock_devices_changed_observer.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/system_monitor/media_storage_util.h"
#include "chrome/browser/system_monitor/removable_device_constants.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chrome {

uint64 kTestSize = 1000000ULL;

namespace {

DiskInfoMac CreateDiskInfoMac(const std::string& unique_id,
                              const std::string& model_name,
                              const string16& display_name,
                              const FilePath& mount_point,
                              uint64 size_bytes) {
  NSMutableDictionary *dict = [NSMutableDictionary dictionary];
  [dict setObject:@"dummy_bsd_name"
           forKey:base::mac::CFToNSCast(kDADiskDescriptionMediaBSDNameKey)];
  [dict setObject:base::SysUTF8ToNSString(unique_id)
           forKey:base::mac::CFToNSCast(kDADiskDescriptionDeviceRevisionKey)];
  if (!model_name.empty()) {
    [dict setObject:base::SysUTF8ToNSString(model_name)
             forKey:base::mac::CFToNSCast(kDADiskDescriptionDeviceModelKey)];
  }
  NSString* path = base::mac::FilePathToNSString(mount_point);
  [dict setObject:[NSURL fileURLWithPath:path]
           forKey:base::mac::CFToNSCast(kDADiskDescriptionVolumePathKey)];
  [dict setObject:base::SysUTF16ToNSString(display_name)
           forKey:base::mac::CFToNSCast(kDADiskDescriptionVolumeNameKey)];
  [dict setObject:[NSNumber numberWithBool:YES]
           forKey:base::mac::CFToNSCast(kDADiskDescriptionMediaRemovableKey)];
  [dict setObject:[NSNumber numberWithInt:size_bytes]
           forKey:base::mac::CFToNSCast(kDADiskDescriptionMediaSizeKey)];
  return DiskInfoMac::BuildDiskInfoOnFileThread(base::mac::NSToCFCast(dict));
}

}  // namespace

class RemovableDeviceNotificationsMacTest : public testing::Test {
 public:
  RemovableDeviceNotificationsMacTest()
      : message_loop_(MessageLoop::TYPE_IO),
        file_thread_(content::BrowserThread::FILE, &message_loop_) {
  }

  virtual void SetUp() OVERRIDE {
    base::SystemMonitor::AllocateSystemIOPorts();
    system_monitor_.reset(new base::SystemMonitor());

    mock_devices_changed_observer_.reset(new base::MockDevicesChangedObserver);
    system_monitor_->AddDevicesChangedObserver(
        mock_devices_changed_observer_.get());

    notifications_ = new RemovableDeviceNotificationsMac;

    unique_id_ = "test_id";
    display_name_ = ASCIIToUTF16("977 KB Test Display Name");
    mount_point_ = FilePath("/unused_test_directory");
    device_id_ = MediaStorageUtil::MakeDeviceId(
        MediaStorageUtil::REMOVABLE_MASS_STORAGE_NO_DCIM, unique_id_);
    disk_info_ = CreateDiskInfoMac(unique_id_, "",
                                   ASCIIToUTF16("Test Display Name"),
                                   mount_point_, kTestSize);
  }

 protected:
  // The message loop and file thread to run tests on.
  MessageLoop message_loop_;
  content::TestBrowserThread file_thread_;

  // SystemMonitor and DevicesChangedObserver to hook together to test.
  scoped_ptr<base::SystemMonitor> system_monitor_;
  scoped_ptr<base::MockDevicesChangedObserver> mock_devices_changed_observer_;

  // Information about the disk.
  std::string unique_id_;
  string16 display_name_;
  FilePath mount_point_;
  std::string device_id_;
  DiskInfoMac disk_info_;

  scoped_refptr<RemovableDeviceNotificationsMac> notifications_;
};

TEST_F(RemovableDeviceNotificationsMacTest, AddRemove) {
  {
    EXPECT_CALL(*mock_devices_changed_observer_,
                OnRemovableStorageAttached(device_id_,
                                           display_name_,
                                           mount_point_.value()));
    notifications_->UpdateDisk(
        disk_info_, RemovableDeviceNotificationsMac::UPDATE_DEVICE_ADDED);
    message_loop_.RunUntilIdle();
  }

  {
    EXPECT_CALL(*mock_devices_changed_observer_,
                OnRemovableStorageDetached(device_id_));
    notifications_->UpdateDisk(
        disk_info_, RemovableDeviceNotificationsMac::UPDATE_DEVICE_REMOVED);
    message_loop_.RunUntilIdle();
  }
}

TEST_F(RemovableDeviceNotificationsMacTest, UpdateVolumeName) {
  {
    EXPECT_CALL(*mock_devices_changed_observer_,
                OnRemovableStorageAttached(device_id_,
                                           display_name_,
                                           mount_point_.value()));
    notifications_->UpdateDisk(
        disk_info_, RemovableDeviceNotificationsMac::UPDATE_DEVICE_ADDED);
    message_loop_.RunUntilIdle();
  }

  {
    string16 new_display_name(ASCIIToUTF16("977 KB Test Display Name"));
    DiskInfoMac info2 = CreateDiskInfoMac(
        unique_id_, "", ASCIIToUTF16("Test Display Name"), mount_point_,
        kTestSize);
    EXPECT_CALL(*mock_devices_changed_observer_,
                OnRemovableStorageDetached(device_id_));
    EXPECT_CALL(*mock_devices_changed_observer_,
                OnRemovableStorageAttached(device_id_,
                                           new_display_name,
                                           mount_point_.value()));
    notifications_->UpdateDisk(
        info2, RemovableDeviceNotificationsMac::UPDATE_DEVICE_CHANGED);
    message_loop_.RunUntilIdle();
  }
}

TEST_F(RemovableDeviceNotificationsMacTest, DCIM) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  file_util::CreateDirectory(temp_dir.path().Append(kDCIMDirectoryName));

  FilePath mount_point = temp_dir.path();
  DiskInfoMac info = CreateDiskInfoMac(
      unique_id_, "", ASCIIToUTF16("Test Display Name"), mount_point,
      kTestSize);
  std::string device_id = MediaStorageUtil::MakeDeviceId(
      MediaStorageUtil::REMOVABLE_MASS_STORAGE_WITH_DCIM, unique_id_);

  {
    EXPECT_CALL(*mock_devices_changed_observer_,
                OnRemovableStorageAttached(device_id,
                                           display_name_,
                                           mount_point.value()));
    notifications_->UpdateDisk(
        info, RemovableDeviceNotificationsMac::UPDATE_DEVICE_ADDED);
    message_loop_.RunUntilIdle();
  }
}

TEST_F(RemovableDeviceNotificationsMacTest, GetDeviceInfo) {
  {
    EXPECT_CALL(*mock_devices_changed_observer_,
                OnRemovableStorageAttached(device_id_,
                                           display_name_,
                                           mount_point_.value()));
    notifications_->UpdateDisk(
        disk_info_, RemovableDeviceNotificationsMac::UPDATE_DEVICE_ADDED);
    message_loop_.RunUntilIdle();
  }

  base::SystemMonitor::RemovableStorageInfo info;
  EXPECT_TRUE(notifications_->GetDeviceInfoForPath(
      mount_point_.AppendASCII("foo"), &info));
  EXPECT_EQ(info.device_id, device_id_);
  EXPECT_EQ(info.name, ASCIIToUTF16("Test Display Name"));
  EXPECT_EQ(info.location, mount_point_.value());

  EXPECT_FALSE(notifications_->GetDeviceInfoForPath(
      FilePath("/non/matching/path"), &info));
}

TEST_F(RemovableDeviceNotificationsMacTest, GetStorageSize) {
  EXPECT_CALL(*mock_devices_changed_observer_,
              OnRemovableStorageAttached(testing::_,
                                         testing::_,
                                         testing::_));
  notifications_->UpdateDisk(
      disk_info_, RemovableDeviceNotificationsMac::UPDATE_DEVICE_ADDED);
  message_loop_.RunUntilIdle();

  EXPECT_EQ(kTestSize,
            notifications_->GetStorageSize("/unused_test_directory"));
}

// Test that mounting a DMG doesn't send a notification.
TEST_F(RemovableDeviceNotificationsMacTest, DMG) {
  EXPECT_CALL(*mock_devices_changed_observer_,
              OnRemovableStorageAttached(testing::_,
                                         testing::_,
                                         testing::_)).Times(0);
  DiskInfoMac info = CreateDiskInfoMac(
      unique_id_, "Disk Image", display_name_, mount_point_, kTestSize);
  notifications_->UpdateDisk(
      info, RemovableDeviceNotificationsMac::UPDATE_DEVICE_ADDED);
  message_loop_.RunUntilIdle();
}

}  // namespace chrome
