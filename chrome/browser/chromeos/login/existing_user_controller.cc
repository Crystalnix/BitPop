// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/existing_user_controller.h"

#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/boot_times_loader.h"
#include "chrome/browser/chromeos/cros_settings.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/cryptohome_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/customization_document.h"
#include "chrome/browser/chromeos/dbus/dbus_thread_manager.h"
#include "chrome/browser/chromeos/dbus/session_manager_client.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/login_display_host.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/wizard_accessibility_helper.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/system/statistics_provider.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/dialog_style.h"
#include "chrome/browser/ui/views/window.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/net/gaia/google_service_auth_error.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "grit/generated_resources.h"
#include "net/http/http_auth_cache.h"
#include "net/http/http_network_session.h"
#include "net/http/http_transaction_factory.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/widget/widget.h"

namespace chromeos {

namespace {

// Url for setting up sync authentication.
const char kSettingsSyncLoginURL[] = "chrome://settings/personal";

// URL that will be opened when user logs in first time on the device.
const char kGetStartedURLPattern[] =
    "http://www.gstatic.com/chromebook/gettingstarted/index-%s.html";

// Divider that starts parameters in URL.
const char kGetStartedParamsStartMark[] = "#";

// Parameter to be added to GetStarted URL that contains board.
const char kGetStartedBoardParam[] = "board=%s";

// Parameter to be added to GetStarted URL
// when first user signs in for the first time.
// TODO(nkostylev): Uncomment once server side supports new param format.
// const char kGetStartedOwnerParam[] = "/first";
const char kGetStartedOwnerParam[] = "first";

// URL for account creation.
const char kCreateAccountURL[] =
    "https://www.google.com/accounts/NewAccount?service=mail";

// ChromeVox tutorial URL.
const char kChromeVoxTutorialURL[] =
    "http://google-axs-chrome.googlecode.com/"
    "svn/trunk/chromevox_tutorial/interactive_tutorial_start.html";

// Landing URL when launching Guest mode to fix captive portal.
const char kCaptivePortalLaunchURL[] = "http://www.google.com/";

// Delay for transferring the auth cache to the system profile.
const long int kAuthCacheTransferDelayMs = 2000;

// Makes a call to the policy subsystem to reload the policy when we detect
// authentication change.
void RefreshPoliciesOnUIThread() {
  if (g_browser_process->browser_policy_connector())
    g_browser_process->browser_policy_connector()->RefreshPolicies();
}

// Copies any authentication details that were entered in the login profile in
// the mail profile to make sure all subsystems of Chrome can access the network
// with the provided authentication which are possibly for a proxy server.
void TransferContextAuthenticationsOnIOThread(
    net::URLRequestContextGetter* default_profile_context_getter,
    net::URLRequestContextGetter* browser_process_context_getter) {
  net::HttpAuthCache* new_cache =
      browser_process_context_getter->GetURLRequestContext()->
      http_transaction_factory()->GetSession()->http_auth_cache();
  net::HttpAuthCache* old_cache =
      default_profile_context_getter->GetURLRequestContext()->
      http_transaction_factory()->GetSession()->http_auth_cache();
  new_cache->UpdateAllFrom(*old_cache);
  VLOG(1) << "Main request context populated with authentication data.";
  // Last but not least tell the policy subsystem to refresh now as it might
  // have been stuck until now too.
  content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
                                   base::Bind(&RefreshPoliciesOnUIThread));
}

}  // namespace

// static
ExistingUserController* ExistingUserController::current_controller_ = NULL;

////////////////////////////////////////////////////////////////////////////////
// ExistingUserController, public:

ExistingUserController::ExistingUserController(LoginDisplayHost* host)
    : login_status_consumer_(NULL),
      host_(host),
      login_display_(host_->CreateLoginDisplay(this)),
      num_login_attempts_(0),
      cros_settings_(CrosSettings::Get()),
      weak_factory_(this),
      is_owner_login_(false),
      offline_failed_(false),
      is_login_in_progress_(false),
      do_auto_enrollment_(false) {
  DCHECK(current_controller_ == NULL);
  current_controller_ = this;

  registrar_.Add(this,
                 chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_AUTH_SUPPLIED,
                 content::NotificationService::AllSources());
  cros_settings_->AddSettingsObserver(kAccountsPrefShowUserNamesOnSignIn, this);
  cros_settings_->AddSettingsObserver(kAccountsPrefAllowNewUser, this);
  cros_settings_->AddSettingsObserver(kAccountsPrefAllowGuest, this);
  cros_settings_->AddSettingsObserver(kAccountsPrefUsers, this);
}

void ExistingUserController::Init(const UserList& users) {
  time_init_ = base::Time::Now();
  UpdateLoginDisplay(users);

  LoginUtils::Get()->PrewarmAuthentication();
  DBusThreadManager::Get()->GetSessionManagerClient()->EmitLoginPromptReady();
}

void ExistingUserController::UpdateLoginDisplay(const UserList& users) {
  bool show_users_on_signin;
  UserList filtered_users;

  cros_settings_->GetBoolean(kAccountsPrefShowUserNamesOnSignIn,
                             &show_users_on_signin);
  if (show_users_on_signin) {
    for (UserList::const_iterator it = users.begin(); it != users.end(); ++it) {
      // TODO(xiyuan): Clean user profile whose email is not in whitelist.
      if (LoginUtils::IsWhitelisted((*it)->email()))
        filtered_users.push_back(*it);
    }
  }

  // If no user pods are visible, fallback to single new user pod which will
  // have guest session link.
  bool show_guest;
  cros_settings_->GetBoolean(kAccountsPrefAllowGuest, &show_guest);
  bool show_users;
  cros_settings_->GetBoolean(kAccountsPrefShowUserNamesOnSignIn, &show_users);
  show_guest &= !filtered_users.empty();
  bool show_new_user = true;
  login_display_->set_parent_window(GetNativeWindow());
  login_display_->Init(filtered_users, show_guest, show_users, show_new_user);
  host_->OnPreferencesChanged();
}

void ExistingUserController::DoAutoEnrollment() {
  do_auto_enrollment_ = true;
}

void ExistingUserController::ResumeLogin() {
  // This means the user signed-in, then auto-enrollment used his credentials
  // to enroll and succeeded.
  resume_login_callback_.Run();
}

////////////////////////////////////////////////////////////////////////////////
// ExistingUserController, content::NotificationObserver implementation:
//

void ExistingUserController::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_SYSTEM_SETTING_CHANGED) {
    // Signed settings changed notify views and update them.
    const chromeos::UserList& users = chromeos::UserManager::Get()->GetUsers();
    UpdateLoginDisplay(users);
    return;
  }
  if (type == chrome::NOTIFICATION_AUTH_SUPPLIED) {
    // Possibly the user has authenticated against a proxy server and we might
    // need the credentials for enrollment and other system requests from the
    // main |g_browser_process| request context (see bug
    // http://crosbug.com/24861). So we transfer any credentials to the global
    // request context here.
    // The issue we have here is that the NOTIFICATION_AUTH_SUPPLIED is sent
    // just after the UI is closed but before the new credentials were stored
    // in the profile. Therefore we have to give it some time to make sure it
    // has been updated before we copy it.
    LOG(INFO) << "Authentication was entered manually, possibly for proxyauth.";
    scoped_refptr<net::URLRequestContextGetter> browser_process_context_getter =
        g_browser_process->system_request_context();
    Profile* default_profile = ProfileManager::GetDefaultProfile();
    scoped_refptr<net::URLRequestContextGetter> default_profile_context_getter =
        default_profile->GetRequestContext();
    DCHECK(browser_process_context_getter.get());
    DCHECK(default_profile_context_getter.get());
    content::BrowserThread::PostDelayedTask(
        content::BrowserThread::IO, FROM_HERE,
        base::Bind(&TransferContextAuthenticationsOnIOThread,
                   default_profile_context_getter,
                   browser_process_context_getter),
        kAuthCacheTransferDelayMs);
  }
  if (type != chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED)
    return;
  login_display_->OnUserImageChanged(*content::Details<User>(details).ptr());
}

////////////////////////////////////////////////////////////////////////////////
// ExistingUserController, private:

ExistingUserController::~ExistingUserController() {
  LoginUtils::Get()->DelegateDeleted(this);

  cros_settings_->RemoveSettingsObserver(kAccountsPrefShowUserNamesOnSignIn,
                                         this);
  cros_settings_->RemoveSettingsObserver(kAccountsPrefAllowNewUser, this);
  cros_settings_->RemoveSettingsObserver(kAccountsPrefAllowGuest, this);
  cros_settings_->RemoveSettingsObserver(kAccountsPrefUsers, this);

  if (current_controller_ == this) {
    current_controller_ = NULL;
  } else {
    NOTREACHED() << "More than one controller are alive.";
  }
  DCHECK(login_display_.get());
}

////////////////////////////////////////////////////////////////////////////////
// ExistingUserController, LoginDisplay::Delegate implementation:
//

void ExistingUserController::CreateAccount() {
  guest_mode_url_ =
      google_util::AppendGoogleLocaleParam(GURL(kCreateAccountURL));
  LoginAsGuest();
}

string16 ExistingUserController::GetConnectedNetworkName() {
  return GetCurrentNetworkName(CrosLibrary::Get()->GetNetworkLibrary());
}

void ExistingUserController::FixCaptivePortal() {
  guest_mode_url_ = GURL(kCaptivePortalLaunchURL);
  LoginAsGuest();
}

void ExistingUserController::SetDisplayEmail(const std::string& email) {
  display_email_ = email;
}

void ExistingUserController::CompleteLogin(const std::string& username,
                                           const std::string& password) {
  if (!time_init_.is_null()) {
    base::TimeDelta delta = base::Time::Now() - time_init_;
    UMA_HISTOGRAM_MEDIUM_TIMES("Login.PromptToCompleteLoginTime", delta);
    time_init_ = base::Time();  // Reset to null.
  }
  host_->OnCompleteLogin();
  // Auto-enrollment must have made a decision by now. It's too late to enroll
  // if the protocol isn't done at this point.
  if (do_auto_enrollment_) {
    VLOG(1) << "Forcing auto-enrollment before completing login";
    // The only way to get out of the enrollment screen from now on is to either
    // complete enrollment, or opt-out of it. So this controller shouldn't force
    // enrollment again if it is reused for another sign-in.
    do_auto_enrollment_ = false;
    auto_enrollment_username_ = username;
    resume_login_callback_ = base::Bind(
        &ExistingUserController::CompleteLoginInternal,
        base::Unretained(this),
        username,
        password);
    ShowEnrollmentScreen(true, username);
  } else {
    CompleteLoginInternal(username, password);
  }
}

void ExistingUserController::CompleteLoginInternal(std::string username,
                                                   std::string password) {
  resume_login_callback_.Reset();

  SetOwnerUserInCryptohome();

  GaiaAuthConsumer::ClientLoginResult credentials;
  if (!login_performer_.get()) {
    LoginPerformer::Delegate* delegate = this;
    if (login_performer_delegate_.get())
      delegate = login_performer_delegate_.get();
    // Only one instance of LoginPerformer should exist at a time.
    login_performer_.reset(new LoginPerformer(delegate));
  }

  // If the device is not owned yet, successfully logged in user will be owner.
  is_owner_login_ = OwnershipService::GetSharedInstance()->GetStatus(true) ==
      OwnershipService::OWNERSHIP_NONE;

  is_login_in_progress_ = true;
  login_performer_->CompleteLogin(username, password);
  WizardAccessibilityHelper::GetInstance()->MaybeSpeak(
      l10n_util::GetStringUTF8(IDS_CHROMEOS_ACC_LOGIN_SIGNING_IN).c_str(),
      false, true);
}

void ExistingUserController::Login(const std::string& username,
                                   const std::string& password) {
  if (username.empty() || password.empty())
    return;
  SetStatusAreaEnabled(false);
  // Disable clicking on other windows.
  login_display_->SetUIEnabled(false);

  // If the device is not owned yet, successfully logged in user will be owner.
  is_owner_login_ = OwnershipService::GetSharedInstance()->GetStatus(true) ==
      OwnershipService::OWNERSHIP_NONE;

  BootTimesLoader::Get()->RecordLoginAttempted();

  if (last_login_attempt_username_ != username) {
    last_login_attempt_username_ = username;
    num_login_attempts_ = 0;
    // Also reset state variables, which are used to determine password change.
    offline_failed_ = false;
    online_succeeded_for_.clear();
  }
  num_login_attempts_++;

  // Use the same LoginPerformer for subsequent login as it has state
  // such as Authenticator instance.
  if (!login_performer_.get() || num_login_attempts_ <= 1) {
    LoginPerformer::Delegate* delegate = this;
    if (login_performer_delegate_.get())
      delegate = login_performer_delegate_.get();
    // Only one instance of LoginPerformer should exist at a time.
    login_performer_.reset(NULL);
    login_performer_.reset(new LoginPerformer(delegate));
  }
  is_login_in_progress_ = true;
  login_performer_->Login(username, password);
  WizardAccessibilityHelper::GetInstance()->MaybeSpeak(
      l10n_util::GetStringUTF8(IDS_CHROMEOS_ACC_LOGIN_SIGNING_IN).c_str(),
      false, true);
}

void ExistingUserController::LoginAsGuest() {
  SetStatusAreaEnabled(false);
  // Disable clicking on other windows.
  login_display_->SetUIEnabled(false);
  SetOwnerUserInCryptohome();

  // Check allow_guest in case this call is fired from key accelerator.
  // Must not proceed without signature verification.
  bool trusted_setting_available = cros_settings_->GetTrusted(
      kAccountsPrefAllowGuest,
      base::Bind(&ExistingUserController::LoginAsGuest,
                 weak_factory_.GetWeakPtr()));
  if (!trusted_setting_available) {
    // Value of AllowGuest setting is still not verified.
    // Another attempt will be invoked again after verification completion.
    return;
  }
  bool allow_guest;
  cros_settings_->GetBoolean(kAccountsPrefAllowGuest, &allow_guest);
  if (!allow_guest) {
    // Disallowed.
    return;
  }

  // Only one instance of LoginPerformer should exist at a time.
  login_performer_.reset(NULL);
  login_performer_.reset(new LoginPerformer(this));
  is_login_in_progress_ = true;
  login_performer_->LoginOffTheRecord();
  WizardAccessibilityHelper::GetInstance()->MaybeSpeak(
      l10n_util::GetStringUTF8(IDS_CHROMEOS_ACC_LOGIN_SIGNIN_OFFRECORD).c_str(),
      false, true);
}

void ExistingUserController::OnUserSelected(const std::string& username) {
  login_performer_.reset(NULL);
  num_login_attempts_ = 0;
}

void ExistingUserController::OnStartEnterpriseEnrollment() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableDevicePolicy)) {
    ownership_checker_.reset(new OwnershipStatusChecker());
    ownership_checker_->Check(base::Bind(
        &ExistingUserController::OnEnrollmentOwnershipCheckCompleted,
        base::Unretained(this)));
  }
}

void ExistingUserController::OnEnrollmentOwnershipCheckCompleted(
    OwnershipService::Status status,
    bool current_user_is_owner) {
  if (status == OwnershipService::OWNERSHIP_NONE)
    ShowEnrollmentScreen(false, std::string());
  ownership_checker_.reset();
}

void ExistingUserController::ShowEnrollmentScreen(bool is_auto_enrollment,
                                                  const std::string& user) {
  DictionaryValue* params = NULL;
  if (is_auto_enrollment) {
    params = new DictionaryValue;
    params->SetBoolean("is_auto_enrollment", true);
    params->SetString("user", user);
  }
  host_->StartWizard(WizardController::kEnterpriseEnrollmentScreenName, params);
  login_display_->OnFadeOut();
}

////////////////////////////////////////////////////////////////////////////////
// ExistingUserController, LoginPerformer::Delegate implementation:
//

void ExistingUserController::OnLoginFailure(const LoginFailure& failure) {
  is_login_in_progress_ = false;
  offline_failed_ = true;

  guest_mode_url_ = GURL::EmptyGURL();
  std::string error = failure.GetErrorString();

  if (!online_succeeded_for_.empty()) {
    ShowGaiaPasswordChanged(online_succeeded_for_);
  } else {
    // Check networking after trying to login in case user is
    // cached locally or the local admin account.
    bool is_known_user =
        UserManager::Get()->IsKnownUser(last_login_attempt_username_);
    NetworkLibrary* network = CrosLibrary::Get()->GetNetworkLibrary();
    if (!network) {
      ShowError(IDS_LOGIN_ERROR_NO_NETWORK_LIBRARY, error);
    } else if (!network->Connected()) {
      if (is_known_user)
        ShowError(IDS_LOGIN_ERROR_AUTHENTICATING, error);
      else
        ShowError(IDS_LOGIN_ERROR_OFFLINE_FAILED_NETWORK_NOT_CONNECTED, error);
    } else {
      // Network is connected.
      const Network* active_network = network->active_network();
      // TODO(nkostylev): Cleanup rest of ClientLogin related code.
      if (failure.reason() == LoginFailure::NETWORK_AUTH_FAILED &&
          failure.error().state() ==
              GoogleServiceAuthError::HOSTED_NOT_ALLOWED) {
        ShowError(IDS_LOGIN_ERROR_AUTHENTICATING_HOSTED, error);
      } else if ((active_network && active_network->restricted_pool()) ||
                 (failure.reason() == LoginFailure::NETWORK_AUTH_FAILED &&
                  failure.error().state() ==
                      GoogleServiceAuthError::SERVICE_UNAVAILABLE)) {
        // Use explicit captive portal state (restricted_pool()) or implicit
        // one.
        // SERVICE_UNAVAILABLE is generated in 2 cases:
        // 1. ClientLogin returns ServiceUnavailable code.
        // 2. Internet connectivity may be behind the captive portal.
        // Suggesting user to try sign in to a portal in Guest mode.
        bool allow_guest;
        cros_settings_->GetBoolean(kAccountsPrefAllowGuest, &allow_guest);
        if (allow_guest)
          ShowError(IDS_LOGIN_ERROR_CAPTIVE_PORTAL, error);
        else
          ShowError(IDS_LOGIN_ERROR_CAPTIVE_PORTAL_NO_GUEST_MODE, error);
      } else {
        if (!is_known_user)
          ShowError(IDS_LOGIN_ERROR_AUTHENTICATING_NEW, error);
        else
          ShowError(IDS_LOGIN_ERROR_AUTHENTICATING, error);
      }
    }
    // Reenable clicking on other windows and status area.
    login_display_->SetUIEnabled(true);
    SetStatusAreaEnabled(true);
  }

  if (login_status_consumer_)
    login_status_consumer_->OnLoginFailure(failure);

  // Clear the recorded displayed email so it won't affect any future attempts.
  display_email_.clear();
}

void ExistingUserController::OnLoginSuccess(
    const std::string& username,
    const std::string& password,
    const GaiaAuthConsumer::ClientLoginResult& credentials,
    bool pending_requests,
    bool using_oauth) {
  is_login_in_progress_ = false;
  offline_failed_ = false;
  bool known_user = UserManager::Get()->IsKnownUser(username);
  bool login_only =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kLoginScreen) == WizardController::kLoginScreenName;
  ready_for_browser_launch_ = known_user || login_only;

  two_factor_credentials_ = credentials.two_factor;

  bool has_cookies =
      login_performer_->auth_mode() == LoginPerformer::AUTH_MODE_EXTENSION;

  // LoginPerformer instance will delete itself once online auth result is OK.
  // In case of failure it'll bring up ScreenLock and ask for
  // correct password/display error message.
  // Even in case when following online,offline protocol and returning
  // requests_pending = false, let LoginPerformer delete itself.
  login_performer_->set_delegate(NULL);
  ignore_result(login_performer_.release());

  // Will call OnProfilePrepared() in the end.
  LoginUtils::Get()->PrepareProfile(username,
                                    display_email_,
                                    password,
                                    credentials,
                                    pending_requests,
                                    using_oauth,
                                    has_cookies,
                                    this);

  display_email_.clear();

  // Notifiy LoginDisplay to allow it provide visual feedback to user.
  login_display_->OnLoginSuccess(username);
}

void ExistingUserController::OnProfilePrepared(Profile* profile) {
  if (!ready_for_browser_launch_) {
    // Don't specify start URLs if the administrator has configured the start
    // URLs via policy.
    if (!SessionStartupPref::TypeIsManaged(profile->GetPrefs()))
      InitializeStartUrls();
#ifndef NDEBUG
    if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kOobeSkipPostLogin)) {
      ready_for_browser_launch_ = true;
      LoginUtils::DoBrowserLaunch(profile, host_);
      host_ = NULL;
    } else {
#endif
      ActivateWizard(WizardController::IsDeviceRegistered() ?
          WizardController::kUserImageScreenName :
          WizardController::kRegistrationScreenName);
#ifndef NDEBUG
    }
#endif
  } else {
    LoginUtils::DoBrowserLaunch(profile, host_);
    // Inform |login_status_consumer_| about successful login after
    // browser launch.  Set most params to empty since they're not needed.
    if (login_status_consumer_)
      login_status_consumer_->OnLoginSuccess(
          "", "", GaiaAuthConsumer::ClientLoginResult(), false, false);
    host_ = NULL;
  }
  login_display_->OnFadeOut();
}

void ExistingUserController::OnOffTheRecordLoginSuccess() {
  is_login_in_progress_ = false;
  offline_failed_ = false;
  if (WizardController::IsDeviceRegistered()) {
    LoginUtils::Get()->CompleteOffTheRecordLogin(guest_mode_url_);
  } else {
    // Postpone CompleteOffTheRecordLogin until registration completion.
    // TODO(nkostylev): Kind of hack. We have to instruct UserManager here
    // that we're actually logged in as Guest user as we'll ask UserManager
    // later in the code path whether we've signed in as Guest and depending
    // on that would either show image screen or call CompleteOffTheRecordLogin.
    UserManager::Get()->GuestUserLoggedIn();
    ActivateWizard(WizardController::kRegistrationScreenName);
  }

  if (login_status_consumer_)
    login_status_consumer_->OnOffTheRecordLoginSuccess();
}

void ExistingUserController::OnPasswordChangeDetected(
    const GaiaAuthConsumer::ClientLoginResult& credentials) {
  // Must not proceed without signature verification.
  bool trusted_setting_available = cros_settings_->GetTrusted(
      kDeviceOwner,
      base::Bind(&ExistingUserController::OnPasswordChangeDetected,
                 weak_factory_.GetWeakPtr(), credentials));

  if (!trusted_setting_available) {
    // Value of owner email is still not verified.
    // Another attempt will be invoked after verification completion.
    return;
  }

  // Passing 'false' here enables "full sync" mode in the dialog,
  // which disables the requirement for the old owner password,
  // allowing us to recover from a lost owner password/homedir.
  // TODO(gspencer): We shouldn't have to erase stateful data when
  // doing this.  See http://crosbug.com/9115 http://crosbug.com/7792
  PasswordChangedView* view = new PasswordChangedView(this, false);
  views::Widget* window = browser::CreateViewsWindow(GetNativeWindow(),
                                                     view,
                                                     STYLE_GENERIC);
  window->SetAlwaysOnTop(true);
  window->Show();

  if (login_status_consumer_)
    login_status_consumer_->OnPasswordChangeDetected(credentials);

  display_email_.clear();
}

void ExistingUserController::WhiteListCheckFailed(const std::string& email) {
  ShowError(IDS_LOGIN_ERROR_WHITELIST, email);

  // Reenable clicking on other windows and status area.
  login_display_->SetUIEnabled(true);
  SetStatusAreaEnabled(true);

  display_email_.clear();
}

void ExistingUserController::OnOnlineChecked(const std::string& username,
                                             bool success) {
  if (success && last_login_attempt_username_ == username) {
    online_succeeded_for_ = username;
    // Wait for login attempt to end, if it hasn't yet.
    if (offline_failed_ && !is_login_in_progress_)
      ShowGaiaPasswordChanged(username);
  }
}

////////////////////////////////////////////////////////////////////////////////
// ExistingUserController, PasswordChangedView::Delegate implementation:
//

void ExistingUserController::RecoverEncryptedData(
    const std::string& old_password) {
  // LoginPerformer instance has state of the user so it should exist.
  if (login_performer_.get())
    login_performer_->RecoverEncryptedData(old_password);
}

void ExistingUserController::ResyncEncryptedData() {
  // LoginPerformer instance has state of the user so it should exist.
  if (login_performer_.get())
    login_performer_->ResyncEncryptedData();
}

////////////////////////////////////////////////////////////////////////////////
// ExistingUserController, private:

void ExistingUserController::ActivateWizard(const std::string& screen_name) {
  DictionaryValue* params = NULL;
  if (chromeos::UserManager::Get()->IsLoggedInAsGuest()) {
    params = new DictionaryValue;
    params->SetString("start_url", guest_mode_url_.spec());
  }
  host_->StartWizard(screen_name, params);
}

gfx::NativeWindow ExistingUserController::GetNativeWindow() const {
  return host_->GetNativeWindow();
}

void ExistingUserController::InitializeStartUrls() const {
  std::vector<std::string> start_urls;
  PrefService* prefs = g_browser_process->local_state();
  const std::string current_locale =
      StringToLowerASCII(prefs->GetString(prefs::kApplicationLocale));
  std::string start_url;
  if (prefs->GetBoolean(prefs::kSpokenFeedbackEnabled) &&
      current_locale.find("en") != std::string::npos) {
    start_url = kChromeVoxTutorialURL;
  } else {
    const char* url = kGetStartedURLPattern;
    start_url = base::StringPrintf(url, current_locale.c_str());
    std::string params_str;
#if 0
    const char kMachineInfoBoard[] = "CHROMEOS_RELEASE_BOARD";
    std::string board;
    system::StatisticsProvider* provider =
        system::StatisticsProvider::GetInstance();
    if (!provider->GetMachineStatistic(kMachineInfoBoard, &board))
      LOG(ERROR) << "Failed to get board information";
    if (!board.empty()) {
      params_str.append(base::StringPrintf(kGetStartedBoardParam,
                                           board.c_str()));
    }
#endif
    if (is_owner_login_)
      params_str.append(kGetStartedOwnerParam);
    if (!params_str.empty()) {
      params_str.insert(0, kGetStartedParamsStartMark);
      start_url.append(params_str);
    }
  }
  start_urls.push_back(start_url);

  ServicesCustomizationDocument* customization =
      ServicesCustomizationDocument::GetInstance();
  if (!ServicesCustomizationDocument::WasApplied() &&
      customization->IsReady()) {
    std::string locale = g_browser_process->GetApplicationLocale();
    std::string initial_start_page =
        customization->GetInitialStartPage(locale);
    if (!initial_start_page.empty())
      start_urls.push_back(initial_start_page);
    customization->ApplyCustomization();
  }

  if (two_factor_credentials_) {
    // If we have a two factor error and and this is a new user,
    // load the personal settings page.
    // TODO(stevenjb): direct the user to a lightweight sync login page.
    start_urls.push_back(kSettingsSyncLoginURL);
  }

  for (size_t i = 0; i < start_urls.size(); ++i)
    CommandLine::ForCurrentProcess()->AppendArg(start_urls[i]);
}

void ExistingUserController::SetStatusAreaEnabled(bool enable) {
  if (!host_)
    return;
  host_->SetStatusAreaEnabled(enable);
}

void ExistingUserController::ShowError(int error_id,
                                       const std::string& details) {
  // TODO(dpolukhin): show detailed error info. |details| string contains
  // low level error info that is not localized and even is not user friendly.
  // For now just ignore it because error_text contains all required information
  // for end users, developers can see details string in Chrome logs.
  VLOG(1) << details;
  HelpAppLauncher::HelpTopic help_topic_id;
  switch (login_performer_->error().state()) {
    case GoogleServiceAuthError::CONNECTION_FAILED:
      help_topic_id = HelpAppLauncher::HELP_CANT_ACCESS_ACCOUNT_OFFLINE;
      break;
    case GoogleServiceAuthError::ACCOUNT_DISABLED:
      help_topic_id = HelpAppLauncher::HELP_ACCOUNT_DISABLED;
      break;
    case GoogleServiceAuthError::HOSTED_NOT_ALLOWED:
      help_topic_id = HelpAppLauncher::HELP_HOSTED_ACCOUNT;
      break;
    default:
      help_topic_id = login_performer_->login_timed_out() ?
          HelpAppLauncher::HELP_CANT_ACCESS_ACCOUNT_OFFLINE :
          HelpAppLauncher::HELP_CANT_ACCESS_ACCOUNT;
      break;
  }

  login_display_->ShowError(error_id, num_login_attempts_, help_topic_id);
}

void ExistingUserController::SetOwnerUserInCryptohome() {
  bool trusted_owner_available = cros_settings_->GetTrusted(
      kDeviceOwner,
      base::Bind(&ExistingUserController::SetOwnerUserInCryptohome,
                 weak_factory_.GetWeakPtr()));
  if (!trusted_owner_available) {
    // Value of owner email is still not verified.
    // Another attempt will be invoked after verification completion.
    return;
  }
  CryptohomeLibrary* cryptohomed = CrosLibrary::Get()->GetCryptohomeLibrary();
  std::string owner;
  cros_settings_->GetString(kDeviceOwner, &owner);
  cryptohomed->AsyncSetOwnerUser(owner, NULL);

  // Do not invoke AsyncDoAutomaticFreeDiskSpaceControl(NULL) here
  // so it does not delay the following mount. Cleanup will be
  // started in Cryptohomed by timer.
}

void ExistingUserController::ShowGaiaPasswordChanged(
    const std::string& username) {
  // Invalidate OAuth token, since it can't be correct after password is
  // changed.
  UserManager::Get()->SaveUserOAuthStatus(username,
                                          User::OAUTH_TOKEN_STATUS_INVALID);

  login_display_->SetUIEnabled(true);
  SetStatusAreaEnabled(true);
  login_display_->ShowGaiaPasswordChanged(username);
}

}  // namespace chromeos
