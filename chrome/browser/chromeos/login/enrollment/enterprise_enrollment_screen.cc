// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/enrollment/enterprise_enrollment_screen.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/cryptohome_library.h"
#include "chrome/browser/chromeos/login/screen_observer.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/enterprise_metrics.h"

namespace chromeos {

namespace {

// Retry for InstallAttrs initialization every 500ms.
const int kLockRetryIntervalMs = 500;

}  // namespace

EnterpriseEnrollmentScreen::EnterpriseEnrollmentScreen(
    ScreenObserver* observer,
    EnterpriseEnrollmentScreenActor* actor)
    : WizardScreen(observer),
      actor_(actor),
      is_auto_enrollment_(false),
      is_showing_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)) {
  actor_->SetController(this);
  // Init the TPM if it has not been done until now (in debug build we might
  // have not done that yet).
  chromeos::CryptohomeLibrary* cryptohome =
      chromeos::CrosLibrary::Get()->GetCryptohomeLibrary();
  if (cryptohome &&
      cryptohome->TpmIsEnabled() &&
      !cryptohome->TpmIsBeingOwned() &&
      !cryptohome->TpmIsOwned()) {
    cryptohome->TpmCanAttemptOwnership();
  }
}

EnterpriseEnrollmentScreen::~EnterpriseEnrollmentScreen() {}

void EnterpriseEnrollmentScreen::SetParameters(bool is_auto_enrollment,
                                               const std::string& user) {
  is_auto_enrollment_ = is_auto_enrollment;
  user_ = user;
}

void EnterpriseEnrollmentScreen::PrepareToShow() {
  actor_->PrepareToShow();
}

void EnterpriseEnrollmentScreen::Show() {
  is_showing_ = true;
  actor_->Show();
}

void EnterpriseEnrollmentScreen::Hide() {
  is_showing_ = false;
  actor_->Hide();
}

void EnterpriseEnrollmentScreen::OnOAuthTokenAvailable(
    const std::string& user,
    const std::string& token) {
  user_ = user;
  RegisterForDevicePolicy(token);
}

void EnterpriseEnrollmentScreen::OnConfirmationClosed(bool go_back_to_signin) {
  get_screen_observer()->OnExit(go_back_to_signin ?
      ScreenObserver::ENTERPRISE_ENROLLMENT_COMPLETED :
      ScreenObserver::ENTERPRISE_AUTO_MAGIC_ENROLLMENT_COMPLETED);
}

bool EnterpriseEnrollmentScreen::IsAutoEnrollment(std::string* user) {
  if (is_auto_enrollment_)
    *user = user_;
  return is_auto_enrollment_;
}

void EnterpriseEnrollmentScreen::OnPolicyStateChanged(
    policy::CloudPolicySubsystem::PolicySubsystemState state,
    policy::CloudPolicySubsystem::ErrorDetails error_details) {

  if (is_showing_) {
    switch (state) {
      case policy::CloudPolicySubsystem::UNENROLLED:
        if (error_details == policy::CloudPolicySubsystem::BAD_SERIAL_NUMBER) {
          actor_->ShowSerialNumberError();
          break;
        }
        // Still working...
        return;
      case policy::CloudPolicySubsystem::BAD_GAIA_TOKEN:
      case policy::CloudPolicySubsystem::LOCAL_ERROR:
        actor_->ShowFatalEnrollmentError();
        break;
      case policy::CloudPolicySubsystem::UNMANAGED:
        actor_->ShowAccountError();
        break;
      case policy::CloudPolicySubsystem::NETWORK_ERROR:
        actor_->ShowNetworkEnrollmentError();
        break;
      case policy::CloudPolicySubsystem::TOKEN_FETCHED:
        WriteInstallAttributesData();
        return;
      case policy::CloudPolicySubsystem::SUCCESS:
        // Success!
        registrar_.reset();
        actor_->ShowConfirmationScreen();
        return;
    }
    // We have an error.
    if (!is_auto_enrollment_) {
      UMA_HISTOGRAM_ENUMERATION(policy::kMetricEnrollment,
                                policy::kMetricEnrollmentPolicyFailed,
                                policy::kMetricEnrollmentSize);
    }
    LOG(WARNING) << "Policy subsystem error during enrollment: " << state
                 << " details: " << error_details;
  }

  // Stop the policy infrastructure.
  registrar_.reset();
  g_browser_process->browser_policy_connector()->ResetDevicePolicy();
}

void EnterpriseEnrollmentScreen::WriteInstallAttributesData() {
  // Since this method is also called directly.
  weak_ptr_factory_.InvalidateWeakPtrs();

  switch (g_browser_process->browser_policy_connector()->LockDevice(user_)) {
    case policy::EnterpriseInstallAttributes::LOCK_SUCCESS: {
      // Proceed with policy fetch.
      policy::BrowserPolicyConnector* connector =
          g_browser_process->browser_policy_connector();
      connector->FetchCloudPolicy();
      return;
    }
    case policy::EnterpriseInstallAttributes::LOCK_NOT_READY: {
      // InstallAttributes not ready yet, retry later.
      LOG(WARNING) << "Install Attributes not ready yet will retry in "
                   << kLockRetryIntervalMs << "ms.";
      MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&EnterpriseEnrollmentScreen::WriteInstallAttributesData,
                     weak_ptr_factory_.GetWeakPtr()),
          kLockRetryIntervalMs);
      return;
    }
    case policy::EnterpriseInstallAttributes::LOCK_BACKEND_ERROR: {
      actor_->ShowFatalEnrollmentError();
      return;
    }
    case policy::EnterpriseInstallAttributes::LOCK_WRONG_USER: {
      LOG(ERROR) << "Enrollment can not proceed because the InstallAttrs "
                 << "has been locked already!";
      actor_->ShowFatalEnrollmentError();
      return;
    }
  }

  NOTREACHED();
}

void EnterpriseEnrollmentScreen::RegisterForDevicePolicy(
    const std::string& token) {
  policy::BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  if (!connector->device_cloud_policy_subsystem()) {
    LOG(ERROR) << "Cloud policy subsystem not initialized.";
  } else if (connector->IsEnterpriseManaged()) {
    LOG(ERROR) << "Device is already enterprise managed!";
  } else if (connector->device_cloud_policy_subsystem()->state() ==
      policy::CloudPolicySubsystem::SUCCESS) {
    LOG(ERROR) << "A previous enrollment already succeeded!";
  } else {
    // Make sure the device policy subsystem is in a clean slate.
    connector->ResetDevicePolicy();
    connector->ScheduleServiceInitialization(0);
    registrar_.reset(new policy::CloudPolicySubsystem::ObserverRegistrar(
        connector->device_cloud_policy_subsystem(), this));
    // Push the credentials to the policy infrastructure. It'll start enrollment
    // and notify us of progress through CloudPolicySubsystem::Observer.
    connector->RegisterForDevicePolicy(user_, token, is_auto_enrollment_);
    return;
  }
  NOTREACHED();
  if (is_showing_)
    actor_->ShowFatalEnrollmentError();
}

}  // namespace chromeos
