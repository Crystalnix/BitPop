// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYSTEM_MONITOR_VOLUME_MOUNT_WATCHER_WIN_H_
#define CHROME_BROWSER_SYSTEM_MONITOR_VOLUME_MOUNT_WATCHER_WIN_H_

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/string16.h"
#include "base/system_monitor/system_monitor.h"

namespace chrome {

// This class watches the volume mount points and sends notifications to
// base::SystemMonitor about the device attach/detach events. This is a
// singleton class instantiated by RemovableDeviceNotificationsWindowWin.
class VolumeMountWatcherWin
    : public base::RefCountedThreadSafe<VolumeMountWatcherWin> {
 public:
  VolumeMountWatcherWin();

  // Must be called after the file thread is created.
  void Init();

  // Gets the information about the device mounted at |device_path|. On success,
  // returns true and fills in |location|, |unique_id|, |name| and |removable|.
  virtual bool GetDeviceInfo(const FilePath& device_path,
                             string16* location,
                             std::string* unique_id,
                             string16* name,
                             bool* removable);

  // Processes DEV_BROADCAST_VOLUME messages and triggers a SystemMonitor
  // notification if appropriate.
  void OnWindowMessage(UINT event_type, LPARAM data);

 protected:
  // VolumeMountWatcherWin is ref-counted.
  virtual ~VolumeMountWatcherWin();

  // Returns a vector of all the removable mass storage devices that are
  // connected.
  virtual std::vector<FilePath> GetAttachedDevices();

 private:
  friend class base::RefCountedThreadSafe<VolumeMountWatcherWin>;

  // Key: Mass storage device mount point.
  // Value: Mass storage device ID string.
  typedef std::map<string16, std::string> MountPointDeviceIdMap;

  // Adds a new mass storage device specified by |device_path|.
  void AddNewDevice(const FilePath& device_path);

  // Enumerate and add existing mass storage devices on file thread.
  void AddExistingDevicesOnFileThread();

  // Identifies the device type and handles the device attach event.
  void CheckDeviceTypeOnFileThread(const std::string& unique_id,
                                   const string16& device_name,
                                   const FilePath& device);

  // Handles mass storage device attach event on UI thread.
  void HandleDeviceAttachEventOnUIThread(const std::string& device_id,
                                         const string16& device_name,
                                         const string16& device_location);

  // Handles mass storage device detach event on UI thread.
  void HandleDeviceDetachEventOnUIThread(const string16& device_location);

  // A map from device mount point to device id. Only accessed on the UI
  // thread.
  MountPointDeviceIdMap device_ids_;

  DISALLOW_COPY_AND_ASSIGN(VolumeMountWatcherWin);
};

}  // namespace chrome

#endif  // CHROME_BROWSER_SYSTEM_MONITOR_VOLUME_MOUNT_WATCHER_WIN_H_
