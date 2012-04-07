// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/dbus/cros_disks_client.h"

#include "base/bind.h"
#include "base/stl_util.h"
#include "chrome/browser/chromeos/system/runtime_environment.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

const char* kDefaultMountOptions[] = {
  "rw",
  "nodev",
  "noexec",
  "nosuid",
};

const char* kDefaultUnmountOptions[] = {
  "force",
};

// Returns the device type from the given arguments.
DeviceType GetDeviceType(bool is_optical, bool is_rotational) {
  if (is_optical)
    return OPTICAL;
  if (is_rotational)
    return HDD;
  return FLASH;
}

// Pops a bool value when |reader| is not NULL.
// Returns true when a value is popped, false otherwise.
bool MaybePopBool(dbus::MessageReader* reader, bool* value) {
  if (!reader)
    return false;
  return reader->PopBool(value);
}

// Pops a string value when |reader| is not NULL.
// Returns true when a value is popped, false otherwise.
bool MaybePopString(dbus::MessageReader* reader, std::string* value) {
  if (!reader)
    return false;
  return reader->PopString(value);
}

// Pops a uint64 value when |reader| is not NULL.
// Returns true when a value is popped, false otherwise.
bool MaybePopUint64(dbus::MessageReader* reader, uint64* value) {
  if (!reader)
    return false;
  return reader->PopUint64(value);
}

// Pops an array of strings when |reader| is not NULL.
// Returns true when an array is popped, false otherwise.
bool MaybePopArrayOfStrings(dbus::MessageReader* reader,
                            std::vector<std::string>* value) {
  if (!reader)
    return false;
  return reader->PopArrayOfStrings(value);
}

// The CrosDisksClient implementation.
class CrosDisksClientImpl : public CrosDisksClient {
 public:
  explicit CrosDisksClientImpl(dbus::Bus* bus)
      : proxy_(bus->GetObjectProxy(cros_disks::kCrosDisksServiceName,
                                   cros_disks::kCrosDisksServicePath)),
        weak_ptr_factory_(this) {
  }

  // CrosDisksClient override.
  virtual void Mount(const std::string& source_path,
                     MountType type,
                     MountCallback callback,
                     ErrorCallback error_callback) OVERRIDE {
    dbus::MethodCall method_call(cros_disks::kCrosDisksInterface,
                                 cros_disks::kMount);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(source_path);
    writer.AppendString("");  // auto detect filesystem.
    std::vector<std::string> mount_options(kDefaultMountOptions,
                                           kDefaultMountOptions +
                                           arraysize(kDefaultMountOptions));
    writer.AppendArrayOfStrings(mount_options);
    proxy_->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                       base::Bind(&CrosDisksClientImpl::OnMount,
                                  weak_ptr_factory_.GetWeakPtr(),
                                  callback,
                                  error_callback));
  }

  // CrosDisksClient override.
  virtual void Unmount(const std::string& device_path,
                       UnmountCallback callback,
                       ErrorCallback error_callback) OVERRIDE {
    dbus::MethodCall method_call(cros_disks::kCrosDisksInterface,
                                 cros_disks::kUnmount);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(device_path);
    std::vector<std::string> unmount_options(kDefaultUnmountOptions,
                                             kDefaultUnmountOptions +
                                             arraysize(kDefaultUnmountOptions));
    writer.AppendArrayOfStrings(unmount_options);
    proxy_->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                       base::Bind(&CrosDisksClientImpl::OnUnmount,
                                  weak_ptr_factory_.GetWeakPtr(),
                                  device_path,
                                  callback,
                                  error_callback));
  }

  // CrosDisksClient override.
  virtual void EnumerateAutoMountableDevices(
      EnumerateAutoMountableDevicesCallback callback,
      ErrorCallback error_callback) OVERRIDE {
    dbus::MethodCall method_call(cros_disks::kCrosDisksInterface,
                                 cros_disks::kEnumerateAutoMountableDevices);
    proxy_->CallMethod(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&CrosDisksClientImpl::OnEnumerateAutoMountableDevices,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback,
                   error_callback));
  }

  // CrosDisksClient override.
  virtual void FormatDevice(const std::string& device_path,
                            const std::string& filesystem,
                            FormatDeviceCallback callback,
                            ErrorCallback error_callback) OVERRIDE {
    dbus::MethodCall method_call(cros_disks::kCrosDisksInterface,
                                 cros_disks::kFormatDevice);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(device_path);
    writer.AppendString(filesystem);
    proxy_->CallMethod(&method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                       base::Bind(&CrosDisksClientImpl::OnFormatDevice,
                                  weak_ptr_factory_.GetWeakPtr(),
                                  device_path,
                                  callback,
                                  error_callback));
  }

  // CrosDisksClient override.
  virtual void GetDeviceProperties(const std::string& device_path,
                                   GetDevicePropertiesCallback callback,
                                   ErrorCallback error_callback) OVERRIDE {
    dbus::MethodCall method_call(cros_disks::kCrosDisksInterface,
                                 cros_disks::kGetDeviceProperties);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(device_path);
    proxy_->CallMethod(&method_call,
                       dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                       base::Bind(&CrosDisksClientImpl::OnGetDeviceProperties,
                                  weak_ptr_factory_.GetWeakPtr(),
                                  device_path,
                                  callback,
                                  error_callback));
  }

  // CrosDisksClient override.
  virtual void SetUpConnections(
      MountEventHandler mount_event_handler,
      MountCompletedHandler mount_completed_handler) OVERRIDE {
    static const SignalEventTuple kSignalEventTuples[] = {
      { cros_disks::kDeviceAdded, DEVICE_ADDED },
      { cros_disks::kDeviceScanned, DEVICE_SCANNED },
      { cros_disks::kDeviceRemoved, DEVICE_REMOVED },
      { cros_disks::kDiskAdded, DISK_ADDED },
      { cros_disks::kDiskChanged, DISK_CHANGED },
      { cros_disks::kDiskRemoved, DISK_REMOVED },
      { cros_disks::kFormattingFinished, FORMATTING_FINISHED },
    };
    const size_t kNumSignalEventTuples = arraysize(kSignalEventTuples);

    for (size_t i = 0; i < kNumSignalEventTuples; ++i) {
      proxy_->ConnectToSignal(
          cros_disks::kCrosDisksInterface,
          kSignalEventTuples[i].signal_name,
          base::Bind(&CrosDisksClientImpl::OnMountEvent,
                     weak_ptr_factory_.GetWeakPtr(),
                     kSignalEventTuples[i].event_type,
                     mount_event_handler),
          base::Bind(&CrosDisksClientImpl::OnSignalConnected,
                     weak_ptr_factory_.GetWeakPtr()));
    }
    proxy_->ConnectToSignal(
        cros_disks::kCrosDisksInterface,
        cros_disks::kMountCompleted,
        base::Bind(&CrosDisksClientImpl::OnMountCompleted,
                   weak_ptr_factory_.GetWeakPtr(),
                   mount_completed_handler ),
        base::Bind(&CrosDisksClientImpl::OnSignalConnected,
                   weak_ptr_factory_.GetWeakPtr()));
  }

 private:
  // A struct to contain a pair of signal name and mount event type.
  // Used by SetUpConnections.
  struct SignalEventTuple {
    const char *signal_name;
    MountEventType event_type;
  };

  // Handles the result of Mount and calls |callback| or |error_callback|.
  void OnMount(MountCallback callback,
               ErrorCallback error_callback,
               dbus::Response* response) {
    if (!response) {
      error_callback.Run();
      return;
    }
    callback.Run();
  }

  // Handles the result of Unount and calls |callback| or |error_callback|.
  void OnUnmount(const std::string& device_path,
                 UnmountCallback callback,
                 ErrorCallback error_callback,
                 dbus::Response* response) {
    if (!response) {
      error_callback.Run();
      return;
    }
    callback.Run(device_path);
  }

  // Handles the result of EnumerateAutoMountableDevices and calls |callback| or
  // |error_callback|.
  void OnEnumerateAutoMountableDevices(
      EnumerateAutoMountableDevicesCallback callback,
      ErrorCallback error_callback,
      dbus::Response* response) {
    if (!response) {
      error_callback.Run();
      return;
    }
    dbus::MessageReader reader(response);
    std::vector<std::string> device_paths;
    if (!reader.PopArrayOfStrings(&device_paths)) {
      LOG(ERROR) << "Invalid response: " << response->ToString();
      error_callback.Run();
      return;
    }
    callback.Run(device_paths);
  }

  // Handles the result of FormatDevice and calls |callback| or
  // |error_callback|.
  void OnFormatDevice(const std::string& device_path,
                      FormatDeviceCallback callback,
                      ErrorCallback error_callback,
                      dbus::Response* response) {
    if (!response) {
      error_callback.Run();
      return;
    }
    dbus::MessageReader reader(response);
    bool success = false;
    if (!reader.PopBool(&success)) {
      LOG(ERROR) << "Invalid response: " << response->ToString();
      error_callback.Run();
      return;
    }
    callback.Run(device_path, success);
  }

  // Handles the result of GetDeviceProperties and calls |callback| or
  // |error_callback|.
  void OnGetDeviceProperties(const std::string& device_path,
                             GetDevicePropertiesCallback callback,
                             ErrorCallback error_callback,
                             dbus::Response* response) {
    if (!response) {
      error_callback.Run();
      return;
    }
    DiskInfo disk(device_path, response);
    callback.Run(disk);
  }

  // Handles mount event signals and calls |handler|.
  void OnMountEvent(MountEventType event_type,
                    MountEventHandler handler,
                    dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    std::string device;
    if (!reader.PopString(&device)) {
      LOG(ERROR) << "Invalid signal: " << signal->ToString();
      return;
    }
    handler.Run(event_type, device);
  }

  // Handles MountCompleted signal and calls |handler|.
  void OnMountCompleted(MountCompletedHandler handler, dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    unsigned int error_code = 0;
    std::string source_path;
    unsigned int mount_type = 0;
    std::string mount_path;
    if (!reader.PopUint32(&error_code) ||
        !reader.PopString(&source_path) ||
        !reader.PopUint32(&mount_type) ||
        !reader.PopString(&mount_path)) {
      LOG(ERROR) << "Invalid signal: " << signal->ToString();
      return;
    }
    handler.Run(static_cast<MountError>(error_code), source_path,
                static_cast<MountType>(mount_type), mount_path);
  }

  // Handles the result of signal connection setup.
  void OnSignalConnected(const std::string& interface,
                         const std::string& signal,
                         bool successed) {
    LOG_IF(ERROR, !successed) << "Connect to " << interface << " " <<
        signal << " failed.";
  }

  dbus::ObjectProxy* proxy_;
  base::WeakPtrFactory<CrosDisksClientImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(CrosDisksClientImpl);
};

// A stub implementaion of CrosDisksClient.
class CrosDisksClientStubImpl : public CrosDisksClient {
 public:
  CrosDisksClientStubImpl() {}
  virtual ~CrosDisksClientStubImpl() {}

  virtual void Mount(const std::string& source_path,
                     MountType type,
                     MountCallback callback,
                     ErrorCallback error_callback) OVERRIDE {}
  virtual void Unmount(const std::string& device_path,
                       UnmountCallback callback,
                       ErrorCallback error_callback) OVERRIDE {}
  virtual void EnumerateAutoMountableDevices(
      EnumerateAutoMountableDevicesCallback callback,
      ErrorCallback error_callback) OVERRIDE {}
  virtual void FormatDevice(const std::string& device_path,
                            const std::string& filesystem,
                            FormatDeviceCallback callback,
                            ErrorCallback error_callback) OVERRIDE {}
  virtual void GetDeviceProperties(const std::string& device_path,
                                   GetDevicePropertiesCallback callback,
                                   ErrorCallback error_callback) OVERRIDE {}
  virtual void SetUpConnections(
      MountEventHandler mount_event_handler,
      MountCompletedHandler mount_completed_handler) OVERRIDE {}

 private:
  DISALLOW_COPY_AND_ASSIGN(CrosDisksClientStubImpl);
};

} // namespace

////////////////////////////////////////////////////////////////////////////////
// DiskInfo

DiskInfo::DiskInfo(const std::string& device_path, dbus::Response* response)
    : device_path_(device_path),
      is_drive_(false),
      has_media_(false),
      on_boot_device_(false),
      device_type_(UNDEFINED),
      total_size_in_bytes_(0),
      is_read_only_(false),
      is_hidden_(true) {
  InitializeFromResponse(response);
}

DiskInfo::~DiskInfo() {
}

// Initialize |this| from |response| given by the cros-disks service.
// Below is an example of |response|'s raw message (long string is ellipsized).
//
//
// message_type: MESSAGE_METHOD_RETURN
// destination: :1.8
// sender: :1.16
// signature: a{sv}
// serial: 96
// reply_serial: 267
//
// array [
//   dict entry {
//     string "DeviceFile"
//     variant       string "/dev/sdb"
//   }
//   dict entry {
//     string "DeviceIsDrive"
//     variant       bool true
//   }
//   dict entry {
//     string "DeviceIsMediaAvailable"
//     variant       bool true
//   }
//   dict entry {
//     string "DeviceIsMounted"
//     variant       bool false
//   }
//   dict entry {
//     string "DeviceIsOnBootDevice"
//     variant       bool false
//   }
//   dict entry {
//     string "DeviceIsOpticalDisc"
//     variant       bool false
//   }
//   dict entry {
//     string "DeviceIsReadOnly"
//     variant       bool false
//   }
//   dict entry {
//     string "DeviceIsVirtual"
//     variant       bool false
//   }
//   dict entry {
//     string "DeviceMediaType"
//     variant       uint32 1
//   }
//   dict entry {
//     string "DeviceMountPaths"
//     variant       array [
//       ]
//   }
//   dict entry {
//     string "DevicePresentationHide"
//     variant       bool true
//   }
//   dict entry {
//     string "DeviceSize"
//     variant       uint64 7998537728
//   }
//   dict entry {
//     string "DriveIsRotational"
//     variant       bool false
//   }
//   dict entry {
//     string "DriveModel"
//     variant       string "TransMemory"
//   }
//   dict entry {
//     string "IdLabel"
//     variant       string ""
//   }
//   dict entry {
//     string "IdUuid"
//     variant       string ""
//   }
//   dict entry {
//     string "NativePath"
//     variant       string "/sys/devices/pci0000:00/0000:00:1d.7/usb1/1-4/...
//   }
// ]
void DiskInfo::InitializeFromResponse(dbus::Response* response) {
  dbus::MessageReader response_reader(response);
  dbus::MessageReader array_reader(response);
  if (!response_reader.PopArray(&array_reader)) {
    LOG(ERROR) << "Invalid response: " << response->ToString();
    return;
  }
  // TODO(satorux): Rework this code using Protocol Buffers. crosbug.com/22626
  typedef std::map<std::string, dbus::MessageReader*> PropertiesMap;
  PropertiesMap properties;
  STLValueDeleter<PropertiesMap> properties_value_deleter(&properties);
  while (array_reader.HasMoreData()) {
    dbus::MessageReader* value_reader = new dbus::MessageReader(response);
    dbus::MessageReader dict_entry_reader(response);
    std::string key;
    if (!array_reader.PopDictEntry(&dict_entry_reader) ||
        !dict_entry_reader.PopString(&key) ||
        !dict_entry_reader.PopVariant(value_reader)) {
      LOG(ERROR) << "Invalid response: " << response->ToString();
      return;
    }
    properties[key] = value_reader;
  }
  MaybePopBool(properties[cros_disks::kDeviceIsDrive], &is_drive_);
  MaybePopBool(properties[cros_disks::kDeviceIsReadOnly], &is_read_only_);
  MaybePopBool(properties[cros_disks::kDevicePresentationHide], &is_hidden_);
  MaybePopBool(properties[cros_disks::kDeviceIsMediaAvailable], &has_media_);
  MaybePopBool(properties[cros_disks::kDeviceIsOnBootDevice],
               &on_boot_device_);
  MaybePopString(properties[cros_disks::kNativePath], &system_path_);
  MaybePopString(properties[cros_disks::kDeviceFile], &file_path_);
  MaybePopString(properties[cros_disks::kDriveModel], &drive_model_);
  MaybePopString(properties[cros_disks::kIdLabel], &label_);
  MaybePopUint64(properties[cros_disks::kDeviceSize], &total_size_in_bytes_);

  std::vector<std::string> mount_paths;
  if (MaybePopArrayOfStrings(properties[cros_disks::kDeviceMountPaths],
                             &mount_paths) && !mount_paths.empty())
    mount_path_ = mount_paths[0];

  bool is_rotational = false;
  bool is_optical = false;
  if (MaybePopBool(properties[cros_disks::kDriveIsRotational],
                   &is_rotational) &&
      MaybePopBool(properties[cros_disks::kDeviceIsOpticalDisc],
                   &is_optical))
    device_type_ = GetDeviceType(is_optical, is_rotational);
}

////////////////////////////////////////////////////////////////////////////////
// CrosDisksClient

CrosDisksClient::CrosDisksClient() {}

CrosDisksClient::~CrosDisksClient() {}

// static
CrosDisksClient* CrosDisksClient::Create(dbus::Bus* bus) {
  if (system::runtime_environment::IsRunningOnChromeOS())
    return new CrosDisksClientImpl(bus);
  else
    return new CrosDisksClientStubImpl();
}

}  // namespace chromeos
