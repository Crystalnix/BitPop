// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SYNC_SETUP_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SYNC_SETUP_HANDLER_H_

#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/timer.h"
#include "chrome/browser/signin/signin_tracker.h"
#include "chrome/browser/ui/webui/options2/options_ui.h"
#include "chrome/browser/ui/webui/signin/login_ui_service.h"

class LoginUIService;
class ProfileManager;
class ProfileSyncService;
class SigninManager;

class SyncSetupHandler : public options2::OptionsPageUIHandler,
                         public SigninTracker::Observer,
                         public LoginUIService::LoginUI {
 public:
  // Constructs a new SyncSetupHandler. |profile_manager| may be NULL.
  explicit SyncSetupHandler(ProfileManager* profile_manager);
  virtual ~SyncSetupHandler();

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(base::DictionaryValue* localized_strings)
      OVERRIDE;
  virtual void RegisterMessages() OVERRIDE;

  // SigninTracker::Observer implementation.
  virtual void GaiaCredentialsValid() OVERRIDE;
  virtual void SigninFailed(const GoogleServiceAuthError& error) OVERRIDE;
  virtual void SigninSuccess() OVERRIDE;

  // LoginUIService::LoginUI implementation.
  virtual void FocusUI() OVERRIDE;
  virtual void CloseUI() OVERRIDE;

  static void GetStaticLocalizedValues(
      base::DictionaryValue* localized_strings,
      content::WebUI* web_ui);

  // Initializes the sync setup flow and shows the setup UI. If |force_login| is
  // true, then the user is forced through the login flow even if they are
  // already signed in (useful for when it is necessary to force the user to
  // re-enter credentials so new tokens can be fetched).
  void OpenSyncSetup(bool force_login);

  // Shows advanced configuration dialog without going through sign in dialog.
  // Kicks the sync backend if necessary with showing spinner dialog until it
  // gets ready.
  void OpenConfigureSync();

  // Terminates the sync setup flow.
  void CloseSyncSetup();

 protected:
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, GaiaErrorInitializingSync);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, HandleCaptcha);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, HandleGaiaAuthFailure);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, SelectCustomEncryption);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, SuccessfullySetPassphrase);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, TestSyncEverything);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, TestSyncAllManually);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, TestPassphraseStillRequired);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, TestSyncIndividualTypes);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, TurnOnEncryptAll);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest,
                           UnrecoverableErrorInitializingSync);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, UnsuccessfullySetPassphrase);
  FRIEND_TEST_ALL_PREFIXES(SyncSetupHandlerTest, SubmitAuthWithInvalidUsername);


  // Subclasses must implement this to show the setup UI that's appropriate
  // for the page this is contained in.
  virtual void ShowSetupUI() = 0;

  // Overridden by subclasses (like SyncPromoHandler) to log stats about the
  // user's signin activity.
  virtual void RecordSignin();

  // Display the configure sync UI. If |show_advanced| is true, skip directly
  // to the "advanced settings" dialog, otherwise give the user the simpler
  // "Sync Everything" dialog. Overridden by subclasses to allow them to skip
  // the sync setup dialog if desired.
  // If |passphrase_failed| is true, then the user previously tried to enter an
  // invalid passphrase.
  virtual void DisplayConfigureSync(bool show_advanced, bool passphrase_failed);

  // Called when configuring sync is done to close the dialog and start syncing.
  void ConfigureSyncDone();

  // Helper routine that gets the ProfileSyncService associated with the parent
  // profile.
  ProfileSyncService* GetSyncService() const;

  // Returns the LoginUIService for the parent profile.
  LoginUIService* GetLoginUIService() const;

 private:
  // Callbacks from the page.
  void OnDidClosePage(const base::ListValue* args);
  void HandleSubmitAuth(const base::ListValue* args);
  void HandleConfigure(const base::ListValue* args);
  void HandlePassphraseEntry(const base::ListValue* args);
  void HandlePassphraseCancel(const base::ListValue* args);
  void HandleAttachHandler(const base::ListValue* args);
  void HandleShowErrorUI(const base::ListValue* args);
  void HandleShowSetupUI(const base::ListValue* args);
  void HandleShowSetupUIWithoutLogin(const base::ListValue* args);
  void HandleDoSignOutOnAuthError(const base::ListValue* args);
  void HandleStopSyncing(const base::ListValue* args);
  void HandleCloseTimeout(const base::ListValue* args);

  // Helper routine that gets the Profile associated with this object (virtual
  // so tests can override).
  virtual Profile* GetProfile() const;

  // Shows the GAIA login success page then exits.
  void DisplayGaiaSuccessAndClose();

  // Displays the GAIA login success page then transitions to sync setup.
  void DisplayGaiaSuccessAndSettingUp();

  // Displays the GAIA login form. If |fatal_error| is true, displays the fatal
  // error UI.
  void DisplayGaiaLogin(bool fatal_error);

  // Displays the GAIA login form with a custom error message (used for errors
  // like "email address already in use by another profile"). No message
  // displayed if |error_message| is empty. Displays fatal error UI if
  // |fatal_error| = true.
  void DisplayGaiaLoginWithErrorMessage(const string16& error_message,
                                        bool fatal_error);

  // A utility function to call before actually showing setup dialog. Makes sure
  // that a new dialog can be shown and sets flag that setup is in progress.
  bool PrepareSyncSetup();

  // Displays spinner-only UI indicating that something is going on in the
  // background.
  // TODO(kochi): better to show some message that the user can understand what
  // is running in the background.
  void DisplaySpinner();

  // Displays an error dialog which shows timeout of starting the sync backend.
  void DisplayTimeout();

  // Returns true if this object is the active login object.
  bool IsActiveLogin() const;

  // Initiates a login via the signin manager.
  void TryLogin(const std::string& username,
                const std::string& password,
                const std::string& captcha,
                const std::string& access_code);

  // If a wizard already exists, focus it and return true.
  bool FocusExistingWizardIfPresent();

  // Invokes the javascript call to close the setup overlay.
  void CloseOverlay();

  // Returns true if the given login data is valid, false otherwise. If the
  // login data is not valid then on return |error_message| will be set to  a
  // localized error message. Note, |error_message| must not be NULL.
  bool IsLoginAuthDataValid(const std::string& username,
                            string16* error_message);

  // Returns the SigninManager for the parent profile.
  SigninManager* GetSignin() const;

  // The SigninTracker object used to determine when the user has fully signed
  // in (this requires waiting for various services to initialize and tracking
  // errors from multiple sources). Should only be non-null while the login UI
  // is visible.
  scoped_ptr<SigninTracker> signin_tracker_;

  // Set to true whenever the sync configure UI is visible. This is used to tell
  // what stage of the setup wizard the user was in and to update the UMA
  // histograms in the case that the user cancels out.
  bool configuring_sync_;

  // Weak reference to the profile manager.
  ProfileManager* const profile_manager_;

  // Cache of the last name the client attempted to authenticate.
  std::string last_attempted_user_email_;

  // The error from the last signin attempt.
  GoogleServiceAuthError last_signin_error_;

  // When setup starts with login UI, retry login if signing in failed.
  // When setup starts without login UI, do not retry login and fail.
  bool retry_on_signin_failure_;

  // The OneShotTimer object used to timeout of starting the sync backend
  // service.
  scoped_ptr<base::OneShotTimer<SyncSetupHandler> > backend_start_timer_;

  DISALLOW_COPY_AND_ASSIGN(SyncSetupHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_SYNC_SETUP_HANDLER_H_
