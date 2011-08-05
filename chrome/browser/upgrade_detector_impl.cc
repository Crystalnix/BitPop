// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/upgrade_detector_impl.h"

#include <string>

#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/task.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/platform_util.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/installer/util/browser_distribution.h"
#include "content/browser/browser_thread.h"
#include "ui/base/resource/resource_bundle.h"

#if defined(OS_WIN)
#include "chrome/installer/util/install_util.h"
#elif defined(OS_MACOSX)
#include "chrome/browser/cocoa/keystone_glue.h"
#elif defined(OS_POSIX)
#include "base/process_util.h"
#include "base/version.h"
#endif

namespace {

// How long (in milliseconds) to wait (each cycle) before checking whether
// Chrome's been upgraded behind our back.
const int kCheckForUpgradeMs = 2 * 60 * 60 * 1000;  // 2 hours.

// How long to wait (each cycle) before checking which severity level we should
// be at. Once we reach the highest severity, the timer will stop.
const int kNotifyCycleTimeMs = 20 * 60 * 1000;  // 20 minutes.

// Same as kNotifyCycleTimeMs but only used during testing.
const int kNotifyCycleTimeForTestingMs = 500;  // Half a second.

std::string CmdLineInterval() {
  const CommandLine& cmd_line = *CommandLine::ForCurrentProcess();
  return cmd_line.GetSwitchValueASCII(switches::kCheckForUpdateIntervalSec);
}

// How often to check for an upgrade.
int GetCheckForUpgradeEveryMs() {
  // Check for a value passed via the command line.
  int interval_ms;
  std::string interval = CmdLineInterval();
  if (!interval.empty() && base::StringToInt(interval, &interval_ms))
    return interval_ms * 1000;  // Command line value is in seconds.

  return kCheckForUpgradeMs;
}

// This task checks the currently running version of Chrome against the
// installed version. If the installed version is newer, it runs the passed
// callback task. Otherwise it just deletes the task.
class DetectUpgradeTask : public Task {
 public:
  explicit DetectUpgradeTask(Task* upgrade_detected_task,
                             bool* is_unstable_channel)
      : upgrade_detected_task_(upgrade_detected_task),
        is_unstable_channel_(is_unstable_channel) {
  }

  virtual ~DetectUpgradeTask() {
    if (upgrade_detected_task_) {
      // This has to get deleted on the same thread it was created.
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                              new DeleteTask<Task>(upgrade_detected_task_));
    }
  }

  virtual void Run() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

    scoped_ptr<Version> installed_version;

#if defined(OS_WIN)
    // Get the version of the currently *installed* instance of Chrome,
    // which might be newer than the *running* instance if we have been
    // upgraded in the background.
    // TODO(tommi): Check if using the default distribution is always the right
    // thing to do.
    BrowserDistribution* dist = BrowserDistribution::GetDistribution();
    installed_version.reset(InstallUtil::GetChromeVersion(dist, false));
    if (!installed_version.get()) {
      // User level Chrome is not installed, check system level.
      installed_version.reset(InstallUtil::GetChromeVersion(dist, true));
    }
#elif defined(OS_MACOSX)
    installed_version.reset(
        Version::GetVersionFromString(UTF16ToASCII(
            keystone_glue::CurrentlyInstalledVersion())));
#elif defined(OS_POSIX)
    // POSIX but not Mac OS X: Linux, etc.
    CommandLine command_line(*CommandLine::ForCurrentProcess());
    command_line.AppendSwitch(switches::kProductVersion);
    std::string reply;
    if (!base::GetAppOutput(command_line, &reply)) {
      DLOG(ERROR) << "Failed to get current file version";
      return;
    }

    installed_version.reset(Version::GetVersionFromString(reply));
#endif

    platform_util::Channel channel = platform_util::GetChannel();
    *is_unstable_channel_ = channel == platform_util::CHANNEL_DEV ||
                            channel == platform_util::CHANNEL_CANARY;

    // Get the version of the currently *running* instance of Chrome.
    chrome::VersionInfo version_info;
    if (!version_info.is_valid()) {
      NOTREACHED() << "Failed to get current file version";
      return;
    }
    scoped_ptr<Version> running_version(
        Version::GetVersionFromString(version_info.Version()));
    if (running_version.get() == NULL) {
      NOTREACHED() << "Failed to parse version info";
      return;
    }

    // |installed_version| may be NULL when the user downgrades on Linux (by
    // switching from dev to beta channel, for example). The user needs a
    // restart in this case as well. See http://crbug.com/46547
    if (!installed_version.get() ||
        (installed_version->CompareTo(*running_version) > 0)) {
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                              upgrade_detected_task_);
      upgrade_detected_task_ = NULL;
    }
  }

 private:
  Task* upgrade_detected_task_;
  bool* is_unstable_channel_;
};

}  // namespace

UpgradeDetectorImpl::UpgradeDetectorImpl()
    : ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)),
      is_unstable_channel_(false) {
  CommandLine command_line(*CommandLine::ForCurrentProcess());
  if (command_line.HasSwitch(switches::kDisableBackgroundNetworking))
    return;
  // Windows: only enable upgrade notifications for official builds.
  // Mac: only enable them if the updater (Keystone) is present.
  // Linux (and other POSIX): always enable regardless of branding.
#if (defined(OS_WIN) && defined(GOOGLE_CHROME_BUILD)) || defined(OS_POSIX)
#if defined(OS_MACOSX)
  if (keystone_glue::KeystoneEnabled())
#endif
  {
    detect_upgrade_timer_.Start(
        base::TimeDelta::FromMilliseconds(GetCheckForUpgradeEveryMs()),
        this, &UpgradeDetectorImpl::CheckForUpgrade);
  }
#endif
}

UpgradeDetectorImpl::~UpgradeDetectorImpl() {
}

void UpgradeDetectorImpl::CheckForUpgrade() {
  method_factory_.RevokeAll();
  Task* callback_task =
      method_factory_.NewRunnableMethod(&UpgradeDetectorImpl::UpgradeDetected);
  // We use FILE as the thread to run the upgrade detection code on all
  // platforms. For Linux, this is because we don't want to block the UI thread
  // while launching a background process and reading its output; on the Mac and
  // on Windows checking for an upgrade requires reading a file.
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          new DetectUpgradeTask(callback_task,
                                                &is_unstable_channel_));
}

void UpgradeDetectorImpl::UpgradeDetected() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Stop the recurring timer (that is checking for changes).
  detect_upgrade_timer_.Stop();

  NotifyUpgradeDetected();

  // Start the repeating timer for notifying the user after a certain period.
  // The called function will eventually figure out that enough time has passed
  // and stop the timer.
  int cycle_time = CmdLineInterval().empty() ? kNotifyCycleTimeMs :
                                               kNotifyCycleTimeForTestingMs;
  upgrade_notification_timer_.Start(
      base::TimeDelta::FromMilliseconds(cycle_time),
      this, &UpgradeDetectorImpl::NotifyOnUpgrade);
}

void UpgradeDetectorImpl::NotifyOnUpgrade() {
  base::TimeDelta delta = base::Time::Now() - upgrade_detected_time();
  std::string interval = CmdLineInterval();

  // A command line interval implies testing, which we'll make more convenient
  // by switching to seconds of waiting instead of days between flipping
  // severity. This works in conjunction with the similar interval.empty()
  // check below.
  int64 time_passed = interval.empty() ? delta.InHours() : delta.InSeconds();

  if (is_unstable_channel_) {
    // There's only one threat level for unstable channels like dev and
    // canary, and it hits after one hour. During testing, it hits after one
    // minute.
    const int kUnstableThreshold = 1;

    if (time_passed >= kUnstableThreshold) {
      set_upgrade_notification_stage(UPGRADE_ANNOYANCE_LOW);

      // That's as high as it goes.
      upgrade_notification_timer_.Stop();
    } else {
      return;  // Not ready to recommend upgrade.
    }
  } else {
    const int kMultiplier = interval.empty() ? 24 : 1;
    // 14 days when not testing, otherwise 14 seconds.
    const int kSevereThreshold = 14 * kMultiplier;
    const int kHighThreshold = 7 * kMultiplier;
    const int kElevatedThreshold = 4 * kMultiplier;
    const int kLowThreshold = 2 * kMultiplier;

    // These if statements must be sorted (highest interval first).
    if (time_passed >= kSevereThreshold) {
      set_upgrade_notification_stage(UPGRADE_ANNOYANCE_SEVERE);

      // We can't get any higher, baby.
      upgrade_notification_timer_.Stop();
    } else if (time_passed >= kHighThreshold) {
      set_upgrade_notification_stage(UPGRADE_ANNOYANCE_HIGH);
    } else if (time_passed >= kElevatedThreshold) {
      set_upgrade_notification_stage(UPGRADE_ANNOYANCE_ELEVATED);
    } else if (time_passed >= kLowThreshold) {
      set_upgrade_notification_stage(UPGRADE_ANNOYANCE_LOW);
    } else {
      return;  // Not ready to recommend upgrade.
    }
  }

  NotifyUpgradeRecommended();
}

// static
UpgradeDetectorImpl* UpgradeDetectorImpl::GetInstance() {
  return Singleton<UpgradeDetectorImpl>::get();
}

// static
UpgradeDetector* UpgradeDetector::GetInstance() {
  return UpgradeDetectorImpl::GetInstance();
}
