// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromeos::MediaDeviceNotifications implementation.

#include "chrome/browser/media_gallery/media_device_notifications_chromeos.h"

#include "base/file_path.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/stl_util.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/media_gallery/media_device_notifications_utils.h"
#include "content/public/browser/browser_thread.h"

namespace chromeos {

namespace {

bool GetDeviceInfo(const std::string& source_path, std::string* device_id,
                   string16* device_label) {
  // Get the media device uuid and label if exists.
  const disks::DiskMountManager::Disk* disk =
      disks::DiskMountManager::GetInstance()->FindDiskBySourcePath(source_path);
  if (!disk)
    return false;

  *device_id = disk->fs_uuid();

  // TODO(kmadhusu): If device label is empty, extract vendor and model details
  // and use them as device_label.
  *device_label = UTF8ToUTF16(disk->device_label().empty() ?
                              FilePath(source_path).BaseName().value() :
                              disk->device_label());
  return true;
}

}  // namespace

using content::BrowserThread;

MediaDeviceNotifications::MediaDeviceNotifications() {
  DCHECK(disks::DiskMountManager::GetInstance());
  disks::DiskMountManager::GetInstance()->AddObserver(this);
  CheckExistingMountPointsOnUIThread();
}

MediaDeviceNotifications::~MediaDeviceNotifications() {
  disks::DiskMountManager* manager = disks::DiskMountManager::GetInstance();
  if (manager) {
    manager->RemoveObserver(this);
  }
}

void MediaDeviceNotifications::CheckExistingMountPointsOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  const disks::DiskMountManager::MountPointMap& mount_point_map =
      disks::DiskMountManager::GetInstance()->mount_points();
  for (disks::DiskMountManager::MountPointMap::const_iterator it =
           mount_point_map.begin(); it != mount_point_map.end(); ++it) {
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        base::Bind(&MediaDeviceNotifications::CheckMountedPathOnFileThread,
                   this, it->second));
  }
}

void MediaDeviceNotifications::DiskChanged(
    disks::DiskMountManagerEventType event,
    const disks::DiskMountManager::Disk* disk) {
}

void MediaDeviceNotifications::DeviceChanged(
    disks::DiskMountManagerEventType event,
    const std::string& device_path) {
}

void MediaDeviceNotifications::MountCompleted(
    disks::DiskMountManager::MountEvent event_type,
    MountError error_code,
    const disks::DiskMountManager::MountPointInfo& mount_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Ignore mount points that are not devices.
  if (mount_info.mount_type != MOUNT_TYPE_DEVICE)
    return;
  // Ignore errors.
  if (error_code != MOUNT_ERROR_NONE)
    return;
  if (mount_info.mount_condition != disks::MOUNT_CONDITION_NONE)
    return;

  switch (event_type) {
    case disks::DiskMountManager::MOUNTING: {
      if (ContainsKey(mount_map_, mount_info.mount_path)) {
        NOTREACHED();
        return;
      }

      BrowserThread::PostTask(
          BrowserThread::FILE, FROM_HERE,
          base::Bind(&MediaDeviceNotifications::CheckMountedPathOnFileThread,
                     this, mount_info));
      break;
    }
    case disks::DiskMountManager::UNMOUNTING: {
      MountMap::iterator it = mount_map_.find(mount_info.mount_path);
      if (it == mount_map_.end())
        return;
      base::SystemMonitor::Get()->ProcessMediaDeviceDetached(it->second);
      mount_map_.erase(it);
      break;
    }
  }
}

void MediaDeviceNotifications::CheckMountedPathOnFileThread(
    const disks::DiskMountManager::MountPointInfo& mount_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  if (!chrome::IsMediaDevice(mount_info.mount_path))
    return;

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&MediaDeviceNotifications::AddMountedPathOnUIThread,
                 this, mount_info));
}

void MediaDeviceNotifications::AddMountedPathOnUIThread(
    const disks::DiskMountManager::MountPointInfo& mount_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (ContainsKey(mount_map_, mount_info.mount_path)) {
    NOTREACHED();
    return;
  }

  // Get the media device uuid and label if exists.
  std::string device_id;
  string16 device_label;
  if (!GetDeviceInfo(mount_info.source_path, &device_id, &device_label))
    return;

  // Keep track of device uuid, to see how often we receive empty uuid values.
  UMA_HISTOGRAM_BOOLEAN("MediaDeviceNotification.device_uuid_available",
                        !device_id.empty());
  if (device_id.empty())
    return;

  mount_map_.insert(std::make_pair(mount_info.mount_path, device_id));
  base::SystemMonitor::Get()->ProcessMediaDeviceAttached(
      device_id,
      device_label,
      base::SystemMonitor::TYPE_PATH,
      mount_info.mount_path);
}

}  // namespace chrome
