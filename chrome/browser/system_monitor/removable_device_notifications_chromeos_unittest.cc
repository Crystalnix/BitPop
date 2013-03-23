// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromeos::RemovableDeviceNotificationsCros unit tests.

#include "chrome/browser/system_monitor/removable_device_notifications_chromeos.h"

#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/system_monitor/system_monitor.h"
#include "base/test/mock_devices_changed_observer.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/system_monitor/media_storage_util.h"
#include "chrome/browser/system_monitor/removable_device_constants.h"
#include "chromeos/disks/mock_disk_mount_manager.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace {

using content::BrowserThread;
using disks::DiskMountManager;
using testing::_;

const char kDeviceNameWithManufacturerDetails[] = "110 KB (CompanyA, Z101)";
const char kDevice1[] = "/dev/d1";
const char kDevice1Name[] = "d1";
const char kDevice1NameWithSizeInfo[] = "110 KB d1";
const char kDevice2[] = "/dev/disk/d2";
const char kDevice2Name[] = "d2";
const char kDevice2NameWithSizeInfo[] = "207 KB d2";
const char kEmptyDeviceLabel[] = "";
const char kMountPointA[] = "mnt_a";
const char kMountPointB[] = "mnt_b";
const char kSDCardDeviceName1[] = "8.6 MB Amy_SD";
const char kSDCardDeviceName2[] = "8.6 MB SD Card";
const char kSDCardMountPoint1[] = "media/removable/Amy_SD";
const char kSDCardMountPoint2[] = "media/removable/SD Card";
const char kProductName[] = "Z101";
const char kUniqueId1[] = "FFFF-FFFF";
const char kUniqueId2[] = "FFFF-FF0F";
const char kVendorName[] = "CompanyA";

uint64 kDevice1SizeInBytes = 113048;
uint64 kDevice2SizeInBytes = 212312;
uint64 kSDCardSizeInBytes = 9000000;

std::string GetDCIMDeviceId(const std::string& unique_id) {
  return chrome::MediaStorageUtil::MakeDeviceId(
      chrome::MediaStorageUtil::REMOVABLE_MASS_STORAGE_WITH_DCIM,
      chrome::kFSUniqueIdPrefix + unique_id);
}

// Wrapper class to test RemovableDeviceNotificationsCros.
class RemovableDeviceNotificationsCrosTest : public testing::Test {
 public:
  RemovableDeviceNotificationsCrosTest();
  virtual ~RemovableDeviceNotificationsCrosTest();

 protected:
  // testing::Test:
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  void MountDevice(MountError error_code,
                   const DiskMountManager::MountPointInfo& mount_info,
                   const std::string& unique_id,
                   const std::string& device_label,
                   const std::string& vendor_name,
                   const std::string& product_name,
                   DeviceType device_type,
                   uint64 device_size_in_bytes);

  void UnmountDevice(MountError error_code,
                     const DiskMountManager::MountPointInfo& mount_info);

  uint64 GetDeviceStorageSize(const std::string& device_location);

  // Create a directory named |dir| relative to the test directory.
  // Set |with_dcim_dir| to true if the created directory will have a "DCIM"
  // subdirectory.
  // Returns the full path to the created directory on success, or an empty
  // path on failure.
  FilePath CreateMountPoint(const std::string& dir, bool with_dcim_dir);

  static void PostQuitToUIThread();
  static void WaitForFileThread();

  base::MockDevicesChangedObserver& observer() {
    return *mock_devices_changed_observer_;
  }

 private:
  // The message loops and threads to run tests on.
  MessageLoop ui_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;

  // Temporary directory for created test data.
  base::ScopedTempDir scoped_temp_dir_;

  // Objects that talks with RemovableDeviceNotificationsCros.
  base::SystemMonitor system_monitor_;
  scoped_ptr<base::MockDevicesChangedObserver> mock_devices_changed_observer_;
  // Owned by DiskMountManager.
  disks::MockDiskMountManager* disk_mount_manager_mock_;

  scoped_refptr<RemovableDeviceNotificationsCros> notifications_;

  DISALLOW_COPY_AND_ASSIGN(RemovableDeviceNotificationsCrosTest);
};

RemovableDeviceNotificationsCrosTest::RemovableDeviceNotificationsCrosTest()
    : ui_thread_(BrowserThread::UI, &ui_loop_),
      file_thread_(BrowserThread::FILE) {
}

RemovableDeviceNotificationsCrosTest::~RemovableDeviceNotificationsCrosTest() {
}

void RemovableDeviceNotificationsCrosTest::SetUp() {
  ASSERT_TRUE(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
  file_thread_.Start();
  mock_devices_changed_observer_.reset(new base::MockDevicesChangedObserver);
  system_monitor_.AddDevicesChangedObserver(
      mock_devices_changed_observer_.get());

  disk_mount_manager_mock_ = new disks::MockDiskMountManager();
  DiskMountManager::InitializeForTesting(disk_mount_manager_mock_);
  disk_mount_manager_mock_->SetupDefaultReplies();

  // Initialize the test subject.
  notifications_ = new RemovableDeviceNotificationsCros();
}

void RemovableDeviceNotificationsCrosTest::TearDown() {
  notifications_ = NULL;
  disk_mount_manager_mock_ = NULL;
  DiskMountManager::Shutdown();
  system_monitor_.RemoveDevicesChangedObserver(
      mock_devices_changed_observer_.get());
  WaitForFileThread();
}

void RemovableDeviceNotificationsCrosTest::MountDevice(
    MountError error_code,
    const DiskMountManager::MountPointInfo& mount_info,
    const std::string& unique_id,
    const std::string& device_label,
    const std::string& vendor_name,
    const std::string& product_name,
    DeviceType device_type,
    uint64 device_size_in_bytes) {
  if (error_code == MOUNT_ERROR_NONE) {
    disk_mount_manager_mock_->CreateDiskEntryForMountDevice(
        mount_info, unique_id, device_label, vendor_name, product_name,
        device_type, device_size_in_bytes);
  }
  notifications_->OnMountEvent(disks::DiskMountManager::MOUNTING,
                               error_code,
                               mount_info);
  WaitForFileThread();
}

void RemovableDeviceNotificationsCrosTest::UnmountDevice(
    MountError error_code,
    const DiskMountManager::MountPointInfo& mount_info) {
  notifications_->OnMountEvent(disks::DiskMountManager::UNMOUNTING,
                               error_code,
                               mount_info);
  if (error_code == MOUNT_ERROR_NONE)
    disk_mount_manager_mock_->RemoveDiskEntryForMountDevice(mount_info);
  WaitForFileThread();
}

uint64 RemovableDeviceNotificationsCrosTest::GetDeviceStorageSize(
    const std::string& device_location) {
  return notifications_->GetStorageSize(device_location);
}

FilePath RemovableDeviceNotificationsCrosTest::CreateMountPoint(
    const std::string& dir, bool with_dcim_dir) {
  FilePath return_path(scoped_temp_dir_.path());
  return_path = return_path.AppendASCII(dir);
  FilePath path(return_path);
  if (with_dcim_dir)
    path = path.Append(chrome::kDCIMDirectoryName);
  if (!file_util::CreateDirectory(path))
    return FilePath();
  return return_path;
}

// static
void RemovableDeviceNotificationsCrosTest::PostQuitToUIThread() {
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          MessageLoop::QuitClosure());
}

// static
void RemovableDeviceNotificationsCrosTest::WaitForFileThread() {
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          base::Bind(&PostQuitToUIThread));
  MessageLoop::current()->Run();
}

// Simple test case where we attach and detach a media device.
TEST_F(RemovableDeviceNotificationsCrosTest, BasicAttachDetach) {
  testing::Sequence mock_sequence;
  FilePath mount_path1 = CreateMountPoint(kMountPointA, true);
  ASSERT_FALSE(mount_path1.empty());
  DiskMountManager::MountPointInfo mount_info(kDevice1,
                                              mount_path1.value(),
                                              MOUNT_TYPE_DEVICE,
                                              disks::MOUNT_CONDITION_NONE);
  EXPECT_CALL(observer(),
              OnRemovableStorageAttached(GetDCIMDeviceId(kUniqueId1),
                                         ASCIIToUTF16(kDevice1NameWithSizeInfo),
                                         mount_path1.value()))
      .InSequence(mock_sequence);
  MountDevice(MOUNT_ERROR_NONE, mount_info, kUniqueId1, kDevice1Name,
              kVendorName, kProductName, DEVICE_TYPE_USB, kDevice1SizeInBytes);

  EXPECT_CALL(observer(),
              OnRemovableStorageDetached(GetDCIMDeviceId(kUniqueId1)))
      .InSequence(mock_sequence);
  UnmountDevice(MOUNT_ERROR_NONE, mount_info);

  FilePath mount_path2 = CreateMountPoint(kMountPointB, true);
  ASSERT_FALSE(mount_path2.empty());
  DiskMountManager::MountPointInfo mount_info2(kDevice2,
                                               mount_path2.value(),
                                               MOUNT_TYPE_DEVICE,
                                               disks::MOUNT_CONDITION_NONE);
  EXPECT_CALL(observer(),
              OnRemovableStorageAttached(GetDCIMDeviceId(kUniqueId2),
                                         ASCIIToUTF16(kDevice2NameWithSizeInfo),
                                         mount_path2.value()))
      .InSequence(mock_sequence);
  MountDevice(MOUNT_ERROR_NONE, mount_info2, kUniqueId2, kDevice2Name,
              kVendorName, kProductName, DEVICE_TYPE_USB, kDevice2SizeInBytes);

  EXPECT_CALL(observer(),
              OnRemovableStorageDetached(GetDCIMDeviceId(kUniqueId2)))
      .InSequence(mock_sequence);
  UnmountDevice(MOUNT_ERROR_NONE, mount_info2);
}

// Removable mass storage devices with no dcim folder are also recognized.
TEST_F(RemovableDeviceNotificationsCrosTest, NoDCIM) {
  testing::Sequence mock_sequence;
  FilePath mount_path = CreateMountPoint(kMountPointA, false);
  const std::string kUniqueId = "FFFF-FFFF";
  ASSERT_FALSE(mount_path.empty());
  DiskMountManager::MountPointInfo mount_info(kDevice1,
                                              mount_path.value(),
                                              MOUNT_TYPE_DEVICE,
                                              disks::MOUNT_CONDITION_NONE);
  const std::string device_id = chrome::MediaStorageUtil::MakeDeviceId(
      chrome::MediaStorageUtil::REMOVABLE_MASS_STORAGE_NO_DCIM,
      chrome::kFSUniqueIdPrefix + kUniqueId);
  EXPECT_CALL(observer(),
              OnRemovableStorageAttached(device_id,
                                         ASCIIToUTF16(kDevice1NameWithSizeInfo),
                                         mount_path.value())).Times(1);
  MountDevice(MOUNT_ERROR_NONE, mount_info, kUniqueId, kDevice1Name,
              kVendorName, kProductName, DEVICE_TYPE_USB, kDevice1SizeInBytes);
}

// Non device mounts and mount errors are ignored.
TEST_F(RemovableDeviceNotificationsCrosTest, Ignore) {
  testing::Sequence mock_sequence;
  FilePath mount_path = CreateMountPoint(kMountPointA, true);
  const std::string kUniqueId = "FFFF-FFFF";
  ASSERT_FALSE(mount_path.empty());

  // Mount error.
  DiskMountManager::MountPointInfo mount_info(kDevice1,
                                              mount_path.value(),
                                              MOUNT_TYPE_DEVICE,
                                              disks::MOUNT_CONDITION_NONE);
  EXPECT_CALL(observer(), OnRemovableStorageAttached(_, _, _)).Times(0);
  MountDevice(MOUNT_ERROR_UNKNOWN, mount_info, kUniqueId, kDevice1Name,
              kVendorName, kProductName, DEVICE_TYPE_USB, kDevice1SizeInBytes);

  // Not a device
  mount_info.mount_type = MOUNT_TYPE_ARCHIVE;
  EXPECT_CALL(observer(), OnRemovableStorageAttached(_, _, _)).Times(0);
  MountDevice(MOUNT_ERROR_NONE, mount_info, kUniqueId, kDevice1Name,
              kVendorName, kProductName, DEVICE_TYPE_USB, kDevice1SizeInBytes);

  // Unsupported file system.
  mount_info.mount_type = MOUNT_TYPE_DEVICE;
  mount_info.mount_condition = disks::MOUNT_CONDITION_UNSUPPORTED_FILESYSTEM;
  EXPECT_CALL(observer(), OnRemovableStorageAttached(_, _, _)).Times(0);
  MountDevice(MOUNT_ERROR_NONE, mount_info, kUniqueId, kDevice1Name,
              kVendorName, kProductName, DEVICE_TYPE_USB, kDevice1SizeInBytes);
}

TEST_F(RemovableDeviceNotificationsCrosTest, SDCardAttachDetach) {
  testing::Sequence mock_sequence;
  FilePath mount_path1 = CreateMountPoint(kSDCardMountPoint1, true);
  ASSERT_FALSE(mount_path1.empty());
  DiskMountManager::MountPointInfo mount_info1(kSDCardDeviceName1,
                                               mount_path1.value(),
                                               MOUNT_TYPE_DEVICE,
                                               disks::MOUNT_CONDITION_NONE);
  EXPECT_CALL(observer(),
              OnRemovableStorageAttached(GetDCIMDeviceId(kUniqueId2),
                                         ASCIIToUTF16(kSDCardDeviceName1),
                                         mount_path1.value()))
      .InSequence(mock_sequence);
  MountDevice(MOUNT_ERROR_NONE, mount_info1, kUniqueId2, kSDCardDeviceName1,
              kVendorName, kProductName, DEVICE_TYPE_SD, kSDCardSizeInBytes);

  EXPECT_CALL(observer(),
              OnRemovableStorageDetached(GetDCIMDeviceId(kUniqueId2)))
      .InSequence(mock_sequence);
  UnmountDevice(MOUNT_ERROR_NONE, mount_info1);

  FilePath mount_path2 = CreateMountPoint(kSDCardMountPoint2, true);
  ASSERT_FALSE(mount_path2.empty());
  DiskMountManager::MountPointInfo mount_info2(kSDCardDeviceName2,
                                               mount_path2.value(),
                                               MOUNT_TYPE_DEVICE,
                                               disks::MOUNT_CONDITION_NONE);
  EXPECT_CALL(observer(),
              OnRemovableStorageAttached(GetDCIMDeviceId(kUniqueId2),
                                         ASCIIToUTF16(kSDCardDeviceName2),
                                         mount_path2.value()))
      .InSequence(mock_sequence);
  MountDevice(MOUNT_ERROR_NONE, mount_info2, kUniqueId2, kSDCardDeviceName2,
              kVendorName, kProductName, DEVICE_TYPE_SD, kSDCardSizeInBytes);

  EXPECT_CALL(observer(),
              OnRemovableStorageDetached(GetDCIMDeviceId(kUniqueId2)))
      .InSequence(mock_sequence);
  UnmountDevice(MOUNT_ERROR_NONE, mount_info2);
}

TEST_F(RemovableDeviceNotificationsCrosTest, AttachDeviceWithEmptyLabel) {
  testing::Sequence mock_sequence;
  FilePath mount_path1 = CreateMountPoint(kMountPointA, true);
  ASSERT_FALSE(mount_path1.empty());
  DiskMountManager::MountPointInfo mount_info(kEmptyDeviceLabel,
                                              mount_path1.value(),
                                              MOUNT_TYPE_DEVICE,
                                              disks::MOUNT_CONDITION_NONE);
  EXPECT_CALL(observer(), OnRemovableStorageAttached(
      GetDCIMDeviceId(kUniqueId1),
      ASCIIToUTF16(kDeviceNameWithManufacturerDetails),
      mount_path1.value()))
      .InSequence(mock_sequence);
  MountDevice(MOUNT_ERROR_NONE, mount_info, kUniqueId1, kEmptyDeviceLabel,
              kVendorName, kProductName, DEVICE_TYPE_USB, kDevice1SizeInBytes);

  EXPECT_CALL(observer(),
              OnRemovableStorageDetached(GetDCIMDeviceId(kUniqueId1)))
      .InSequence(mock_sequence);
  UnmountDevice(MOUNT_ERROR_NONE, mount_info);
}

TEST_F(RemovableDeviceNotificationsCrosTest, GetStorageSize) {
  testing::Sequence mock_sequence;
  FilePath mount_path1 = CreateMountPoint(kMountPointA, true);
  ASSERT_FALSE(mount_path1.empty());
  DiskMountManager::MountPointInfo mount_info(kEmptyDeviceLabel,
                                              mount_path1.value(),
                                              MOUNT_TYPE_DEVICE,
                                              disks::MOUNT_CONDITION_NONE);
  EXPECT_CALL(observer(), OnRemovableStorageAttached(
      GetDCIMDeviceId(kUniqueId1),
      ASCIIToUTF16(kDeviceNameWithManufacturerDetails),
      mount_path1.value()))
      .InSequence(mock_sequence);
  MountDevice(MOUNT_ERROR_NONE, mount_info, kUniqueId1, kEmptyDeviceLabel,
              kVendorName, kProductName, DEVICE_TYPE_USB, kDevice1SizeInBytes);

  EXPECT_EQ(kDevice1SizeInBytes, GetDeviceStorageSize(mount_path1.value()));
  EXPECT_CALL(observer(),
              OnRemovableStorageDetached(GetDCIMDeviceId(kUniqueId1)))
      .InSequence(mock_sequence);
  UnmountDevice(MOUNT_ERROR_NONE, mount_info);
}

}  // namespace

}  // namespace chrome
