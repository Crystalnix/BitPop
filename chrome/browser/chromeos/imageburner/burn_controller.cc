// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/imageburner/burn_controller.h"

#include "base/bind.h"
#include "base/file_path.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/cros/burn_library.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/imageburner/burn_manager.h"
#include "grit/generated_resources.h"
#include "googleurl/src/gurl.h"

namespace chromeos {
namespace imageburner {

namespace {

const char kImageZipFileName[] = "chromeos_image.bin.zip";

// 3.9GB. It is less than 4GB because true device size ussually varies a little.
const uint64 kMinDeviceSize = static_cast<uint64>(3.9 * 1000 * 1000 * 1000);

// Returns true when |disk| is a device on which we can burn recovery image.
bool IsBurnableDevice(const disks::DiskMountManager::Disk& disk) {
  return disk.is_parent() && !disk.on_boot_device() && disk.has_media() &&
         (disk.device_type() == DEVICE_TYPE_USB ||
          disk.device_type() == DEVICE_TYPE_SD);
}

class BurnControllerImpl
    : public BurnController,
      public disks::DiskMountManager::Observer,
      public BurnLibrary::Observer,
      public NetworkLibrary::NetworkManagerObserver,
      public StateMachine::Observer,
      public BurnManager::Delegate,
      public BurnManager::Observer {
 public:
  explicit BurnControllerImpl(BurnController::Delegate* delegate)
      : burn_manager_(NULL),
        state_machine_(NULL),
        observing_burn_lib_(false),
        working_(false),
        delegate_(delegate) {
    disks::DiskMountManager::GetInstance()->AddObserver(this);
    CrosLibrary::Get()->GetNetworkLibrary()->AddNetworkManagerObserver(this);
    burn_manager_ = BurnManager::GetInstance();
    burn_manager_->AddObserver(this);
    state_machine_ = burn_manager_->state_machine();
    state_machine_->AddObserver(this);
  }

  virtual ~BurnControllerImpl() {
    CrosLibrary::Get()->GetBurnLibrary()->RemoveObserver(this);
    if (state_machine_)
      state_machine_->RemoveObserver(this);
    burn_manager_->RemoveObserver(this);
    CrosLibrary::Get()->GetNetworkLibrary()->RemoveNetworkManagerObserver(this);
    disks::DiskMountManager::GetInstance()->RemoveObserver(this);
  }

  // disks::DiskMountManager::Observer interface.
  virtual void DiskChanged(disks::DiskMountManagerEventType event,
                           const disks::DiskMountManager::Disk* disk)
      OVERRIDE {
    if (!IsBurnableDevice(*disk))
      return;
    if (event == disks::MOUNT_DISK_ADDED) {
      delegate_->OnDeviceAdded(*disk);
    } else if (event == disks::MOUNT_DISK_REMOVED) {
      delegate_->OnDeviceRemoved(*disk);
      if (burn_manager_->target_device_path().value() == disk->device_path())
        ProcessError(IDS_IMAGEBURN_DEVICE_NOT_FOUND_ERROR);
    }
  }

  virtual void DeviceChanged(disks::DiskMountManagerEventType event,
                             const std::string& device_path) OVERRIDE {
  }

  virtual void MountCompleted(
      disks::DiskMountManager::MountEvent event_type,
      MountError error_code,
      const disks::DiskMountManager::MountPointInfo& mount_info)
      OVERRIDE {
  }

  // BurnLibrary::Observer interface.
  virtual void BurnProgressUpdated(BurnLibrary* object,
                                   BurnEvent evt,
                                   const ImageBurnStatus& status) OVERRIDE {
    switch (evt) {
      case(BURN_SUCCESS):
        FinalizeBurn();
        break;
      case(BURN_FAIL):
        ProcessError(IDS_IMAGEBURN_BURN_ERROR);
        break;
      case(BURN_UPDATE):
        delegate_->OnProgress(BURNING, status.amount_burnt, status.total_size);
        break;
      case(UNZIP_STARTED):
        delegate_->OnProgress(UNZIPPING, 0, 0);
        break;
      case(UNZIP_FAIL):
        ProcessError(IDS_IMAGEBURN_EXTRACTING_ERROR);
        break;
      case(UNZIP_COMPLETE):
        // We ignore this.
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  // NetworkLibrary::NetworkManagerObserver interface.
  virtual void OnNetworkManagerChanged(NetworkLibrary* obj) OVERRIDE {
    if (state_machine_->state() == StateMachine::INITIAL && CheckNetwork())
      delegate_->OnNetworkDetected();

    if (state_machine_->state() == StateMachine::DOWNLOADING &&
        !CheckNetwork())
      ProcessError(IDS_IMAGEBURN_NETWORK_ERROR);
  }

  // BurnManager::Observer override.
  virtual void OnDownloadUpdated(
      int64 received_bytes,
      int64 total_bytes,
      const base::TimeDelta& time_remaining) OVERRIDE {
    if (state_machine_->state() == StateMachine::DOWNLOADING) {
      delegate_->OnProgressWithRemainingTime(DOWNLOADING,
                                             received_bytes,
                                             total_bytes,
                                             time_remaining);
    }
  }

  // BurnManager::Observer override.
  virtual void OnDownloadCancelled() OVERRIDE {
    DownloadCompleted(false);
  }

  // BurnManager::Observer override.
  virtual void OnDownloadCompleted() OVERRIDE {
    DownloadCompleted(true);
  }

  // StateMachine::Observer interface.
  virtual void OnBurnStateChanged(StateMachine::State state) OVERRIDE {
    if (state == StateMachine::CANCELLED) {
      ProcessError(IDS_IMAGEBURN_USER_ERROR);
    } else if (state != StateMachine::INITIAL && !working_) {
      // User has started burn process, so let's start observing.
      StartBurnImage(FilePath(), FilePath());
    }
  }

  virtual void OnError(int error_message_id) OVERRIDE {
    delegate_->OnFail(error_message_id);
    working_ = false;
  }

  // Part of BurnManager::Delegate interface.
  virtual void OnImageDirCreated(bool success) OVERRIDE {
    if (success) {
      zip_image_file_path_ =
          burn_manager_->GetImageDir().Append(kImageZipFileName);
      burn_manager_->FetchConfigFile(this);
    } else {
      DownloadCompleted(success);
    }
  }

  // Part of BurnManager::Delegate interface.
  virtual void OnConfigFileFetched(bool success,
                                   const std::string& image_file_name,
                                   const GURL& image_download_url) OVERRIDE {
    if (!success) {
      DownloadCompleted(false);
      return;
    }
    image_file_name_ = image_file_name;

    if (state_machine_->download_finished()) {
      BurnImage();
      return;
    }

    if (!state_machine_->download_started()) {
      burn_manager_->FetchImage(image_download_url, zip_image_file_path_);
      state_machine_->OnDownloadStarted();
    }
  }

  // BurnController override.
  virtual void Init() OVERRIDE {
    if (state_machine_->state() == StateMachine::BURNING) {
      // There is nothing else left to do but observe burn progress.
      BurnImage();
    } else if (state_machine_->state() != StateMachine::INITIAL) {
      // User has started burn process, so let's start observing.
      StartBurnImage(FilePath(), FilePath());
    }
  }

  // BurnController override.
  virtual std::vector<disks::DiskMountManager::Disk> GetBurnableDevices()
      OVERRIDE {
    const disks::DiskMountManager::DiskMap& disks =
        disks::DiskMountManager::GetInstance()->disks();
    std::vector<disks::DiskMountManager::Disk> result;
    for (disks::DiskMountManager::DiskMap::const_iterator iter = disks.begin();
         iter != disks.end();
         ++iter) {
      const disks::DiskMountManager::Disk& disk = *iter->second;
      if (IsBurnableDevice(disk))
        result.push_back(disk);
    }
    return result;
  }

  // BurnController override.
  virtual void CancelBurnImage() OVERRIDE {
    state_machine_->OnCancelation();
  }

  // BurnController override.
  // May be called with empty values if there is a handler that has started
  // burning, and thus set the target paths.
  virtual void StartBurnImage(const FilePath& target_device_path,
                              const FilePath& target_file_path) OVERRIDE {
    if (!target_device_path.empty() && !target_file_path.empty() &&
        state_machine_->new_burn_posible()) {
      if (!CheckNetwork()) {
        delegate_->OnNoNetwork();
        return;
      }
      burn_manager_->set_target_device_path(target_device_path);
      burn_manager_->set_target_file_path(target_file_path);
      uint64 device_size = GetDeviceSize(
          burn_manager_->target_device_path().value());
      if (device_size < kMinDeviceSize) {
        delegate_->OnDeviceTooSmall(device_size);
        return;
      }
    }
    if (working_)
      return;
    working_ = true;
    // Send progress signal now so ui doesn't hang in intial state until we get
    // config file
    delegate_->OnProgress(DOWNLOADING, 0, 0);
    if (burn_manager_->GetImageDir().empty()) {
      burn_manager_->CreateImageDir(this);
    } else {
      OnImageDirCreated(true);
    }
  }

 private:
  void DownloadCompleted(bool success) {
    if (success) {
      state_machine_->OnDownloadFinished();
      BurnImage();
    } else {
      ProcessError(IDS_IMAGEBURN_DOWNLOAD_ERROR);
    }
  }

  void BurnImage() {
    if (!observing_burn_lib_) {
      CrosLibrary::Get()->GetBurnLibrary()->AddObserver(this);
      observing_burn_lib_ = true;
    }
    if (state_machine_->state() == StateMachine::BURNING)
      return;
    state_machine_->OnBurnStarted();

    CrosLibrary::Get()->GetBurnLibrary()->DoBurn(
        zip_image_file_path_,
        image_file_name_, burn_manager_->target_file_path(),
        burn_manager_->target_device_path());
  }

  void FinalizeBurn() {
    state_machine_->OnSuccess();
    burn_manager_->ResetTargetPaths();
    CrosLibrary::Get()->GetBurnLibrary()->RemoveObserver(this);
    observing_burn_lib_ = false;
    delegate_->OnSuccess();
    working_ = false;
  }

  // Error is ussually detected by all existing Burn handlers, but only first
  // one that calls ProcessError should actually process it.
  void ProcessError(int message_id) {
    // If we are in intial state, error has already been dispached.
    if (state_machine_->state() == StateMachine::INITIAL) {
      // We don't need burn library since we are not the ones doing the cleanup.
      if (observing_burn_lib_) {
        CrosLibrary::Get()->GetBurnLibrary()->RemoveObserver(this);
        observing_burn_lib_ = false;
      }
      return;
    }

    // Remember burner state, since it will be reset after OnError call.
    StateMachine::State state = state_machine_->state();

    // Dispach error. All hadlers' OnError event will be called before returning
    // from this. This includes us, too.
    state_machine_->OnError(message_id);

    // Do cleanup.
    if (state  == StateMachine::DOWNLOADING) {
      burn_manager_->CancelImageFetch();
    } else if (state == StateMachine::BURNING) {
      DCHECK(observing_burn_lib_);
      // Burn library doesn't send cancelled signal upon CancelBurnImage
      // invokation.
      CrosLibrary::Get()->GetBurnLibrary()->CancelBurnImage();
      CrosLibrary::Get()->GetBurnLibrary()->RemoveObserver(this);
      observing_burn_lib_ = false;
    }
    burn_manager_->ResetTargetPaths();
  }

  int64 GetDeviceSize(const std::string& device_path) {
    disks::DiskMountManager* disk_mount_manager =
        disks::DiskMountManager::GetInstance();
    const disks::DiskMountManager::Disk* disk =
        disk_mount_manager->FindDiskBySourcePath(device_path);
    return disk ? disk->total_size_in_bytes() : 0;
  }

  bool CheckNetwork() {
    return CrosLibrary::Get()->GetNetworkLibrary()->Connected();
  }

  FilePath zip_image_file_path_;
  std::string image_file_name_;
  BurnManager* burn_manager_;
  StateMachine* state_machine_;
  bool observing_burn_lib_;
  bool working_;
  BurnController::Delegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(BurnControllerImpl);
};

}  // namespace

// static
BurnController* BurnController::CreateBurnController(
    content::WebContents* web_contents,
    Delegate* delegate) {
  return new BurnControllerImpl(delegate);
}

}  // namespace imageburner
}  // namespace chromeos
