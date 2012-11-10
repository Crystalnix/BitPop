// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/eula_screen.h"

#include "base/logging.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/cryptohome_library.h"
#include "chrome/browser/chromeos/customization_document.h"
#include "chrome/browser/chromeos/login/screen_observer.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"

namespace chromeos {

EulaScreen::EulaScreen(ScreenObserver* observer, EulaScreenActor* actor)
    : WizardScreen(observer), actor_(actor), password_fetcher_(this) {
  DCHECK(actor_);
  if (actor_)
    actor_->SetDelegate(this);
}

EulaScreen::~EulaScreen() {
  if (actor_)
    actor_->SetDelegate(NULL);
}

void EulaScreen::PrepareToShow() {
  if (actor_)
    actor_->PrepareToShow();
}

void EulaScreen::Show() {
  // Command to own the TPM.
  chromeos::CrosLibrary::Get()->
      GetCryptohomeLibrary()->TpmCanAttemptOwnership();
  if (actor_)
    actor_->Show();
}

void EulaScreen::Hide() {
  if (actor_)
    actor_->Hide();
}

std::string EulaScreen::GetName() const {
  return WizardController::kEulaScreenName;
}

GURL EulaScreen::GetOemEulaUrl() const {
  const StartupCustomizationDocument* customization =
      StartupCustomizationDocument::GetInstance();
  if (customization->IsReady()) {
    std::string locale = customization->initial_locale();
    std::string eula_page = customization->GetEULAPage(locale);
    if (!eula_page.empty())
      return GURL(eula_page);

    VLOG(1) << "No eula found for locale: " << locale;
  } else {
    LOG(ERROR) << "No manifest found.";
  }
  return GURL();
}

void EulaScreen::OnExit(bool accepted, bool is_usage_stats_checked) {
  get_screen_observer()->SetUsageStatisticsReporting(is_usage_stats_checked);
  get_screen_observer()->OnExit(accepted
                   ? ScreenObserver::EULA_ACCEPTED
                   : ScreenObserver::EULA_BACK);
}

void EulaScreen::InitiatePasswordFetch() {
  if (tpm_password_.empty()) {
    password_fetcher_.Fetch();
    // Will call actor after password has been fetched.
  } else if (actor_) {
    actor_->OnPasswordFetched(tpm_password_);
  }
}

void EulaScreen::OnPasswordFetched(const std::string& tpm_password) {
  tpm_password_ = tpm_password;
  if (actor_)
    actor_->OnPasswordFetched(tpm_password_);
}

bool EulaScreen::IsUsageStatsEnabled() const {
  return get_screen_observer()->GetUsageStatisticsReporting();
}

void EulaScreen::OnActorDestroyed(EulaScreenActor* actor) {
  if (actor_ == actor)
    actor_ = NULL;
}

}  // namespace chromeos
