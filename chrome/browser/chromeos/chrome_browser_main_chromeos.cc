// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/chrome_browser_main_chromeos.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "chrome/browser/browser_process_impl.h"
#include "chrome/browser/chromeos/audio/audio_handler.h"
#include "chrome/browser/chromeos/bluetooth/bluetooth_manager.h"
#include "chrome/browser/chromeos/boot_times_loader.h"
#include "chrome/browser/chromeos/boot_times_loader.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/dbus/dbus_thread_manager.h"
#include "chrome/browser/chromeos/dbus/power_manager_client.h"
#include "chrome/browser/chromeos/dbus/session_manager_client.h"
#include "chrome/browser/chromeos/disks/disk_mount_manager.h"
#include "chrome/browser/chromeos/external_metrics.h"
#include "chrome/browser/chromeos/imageburner/burn_manager.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/chromeos/input_method/xkeyboard.h"
#include "chrome/browser/chromeos/login/authenticator.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/login/ownership_service.h"
#include "chrome/browser/chromeos/login/screen_locker.h"
#include "chrome/browser/chromeos/login/session_manager_observer.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/net/cros_network_change_notifier_factory.h"
#include "chrome/browser/chromeos/net/network_change_notifier_chromeos.h"
#include "chrome/browser/chromeos/power/brightness_observer.h"
#include "chrome/browser/chromeos/power/resume_observer.h"
#include "chrome/browser/chromeos/power/screen_lock_observer.h"
#include "chrome/browser/chromeos/status/status_area_view_chromeos.h"
#include "chrome/browser/chromeos/system/runtime_environment.h"
#include "chrome/browser/chromeos/system/statistics_provider.h"
#include "chrome/browser/chromeos/system_key_event_listener.h"
#include "chrome/browser/chromeos/upgrade_detector_chromeos.h"
#include "chrome/browser/chromeos/web_socket_proxy_controller.h"
#include "chrome/browser/chromeos/xinput_hierarchy_changed_event_listener.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/metrics/metrics_service.h"
#include "chrome/browser/oom_priority_manager.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/views/browser_dialogs.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/logging_chrome.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "content/public/common/main_function_params.h"
#include "grit/platform_locale_settings.h"
#include "net/base/network_change_notifier.h"
#include "net/url_request/url_request.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(TOOLKIT_USES_GTK)
#include <gtk/gtk.h>
#endif

#if defined(USE_AURA)
#include "chrome/browser/chromeos/legacy_window_manager/initial_browser_window_observer.h"
#include "chrome/browser/chromeos/power/power_button_controller_delegate_chromeos.h"
#include "chrome/browser/chromeos/power/power_button_observer.h"
#include "chrome/browser/chromeos/power/video_property_writer.h"
#endif

class MessageLoopObserver : public MessageLoopForUI::Observer {
#if defined(USE_AURA)
  virtual base::EventStatus WillProcessEvent(
      const base::NativeEvent& event) OVERRIDE {
    return base::EVENT_CONTINUE;
  }

  virtual void DidProcessEvent(
      const base::NativeEvent& event) OVERRIDE {
  }
#else
  virtual void WillProcessEvent(GdkEvent* event) {
    // On chromeos we want to map Alt-left click to right click.
    // This code only changes presses and releases. We could decide to also
    // modify drags and crossings. It doesn't seem to be a problem right now
    // with our support for context menus (the only real need we have).
    // There are some inconsistent states both with what we have and what
    // we would get if we added drags. You could get a right drag without a
    // right down for example, unless we started synthesizing events, which
    // seems like more trouble than it's worth.
    if ((event->type == GDK_BUTTON_PRESS ||
        event->type == GDK_2BUTTON_PRESS ||
        event->type == GDK_3BUTTON_PRESS ||
        event->type == GDK_BUTTON_RELEASE) &&
        event->button.button == 1 &&
        event->button.state & GDK_MOD1_MASK) {
      // Change the button to the third (right) one.
      event->button.button = 3;
      // Remove the Alt key and first button state.
      event->button.state &= ~(GDK_MOD1_MASK | GDK_BUTTON1_MASK);
      // Add the third (right) button state.
      event->button.state |= GDK_BUTTON3_MASK;
    }
  }

  virtual void DidProcessEvent(GdkEvent* event) {
  }
#endif
};

static base::LazyInstance<MessageLoopObserver> g_message_loop_observer =
    LAZY_INSTANCE_INITIALIZER;

// Login -----------------------------------------------------------------------

// Class is used to login using passed username and password.
// The instance will be deleted upon success or failure.
class StubLogin : public chromeos::LoginStatusConsumer,
                  public chromeos::LoginUtils::Delegate {
 public:
  StubLogin(std::string username, std::string password)
      : pending_requests_(false),
        profile_prepared_(false) {
    authenticator_ = chromeos::LoginUtils::Get()->CreateAuthenticator(this);
    authenticator_.get()->AuthenticateToLogin(
        g_browser_process->profile_manager()->GetDefaultProfile(),
        username,
        password,
        std::string(),
        std::string());
  }

  ~StubLogin() {
    chromeos::LoginUtils::Get()->DelegateDeleted(this);
  }

  void OnLoginFailure(const chromeos::LoginFailure& error) {
    LOG(ERROR) << "Login Failure: " << error.GetErrorString();
    delete this;
  }

  void OnLoginSuccess(const std::string& username,
                      const std::string& password,
                      const GaiaAuthConsumer::ClientLoginResult& credentials,
                      bool pending_requests,
                      bool using_oauth) {
    pending_requests_ = pending_requests;
    if (!profile_prepared_) {
      // Will call OnProfilePrepared in the end.
      chromeos::LoginUtils::Get()->PrepareProfile(username,
                                                  std::string(),
                                                  password,
                                                  credentials,
                                                  pending_requests,
                                                  using_oauth,
                                                  false,
                                                  this);
    } else if (!pending_requests) {
      delete this;
    }
  }

  // LoginUtils::Delegate implementation:
  virtual void OnProfilePrepared(Profile* profile) {
    profile_prepared_ = true;
    chromeos::LoginUtils::DoBrowserLaunch(profile, NULL);
    if (!pending_requests_)
      delete this;
  }

  scoped_refptr<chromeos::Authenticator> authenticator_;
  bool pending_requests_;
  bool profile_prepared_;
};

void OptionallyRunChromeOSLoginManager(const CommandLine& parsed_command_line,
                                       Profile* profile) {
  if (parsed_command_line.HasSwitch(switches::kLoginManager)) {
    std::string first_screen =
        parsed_command_line.GetSwitchValueASCII(switches::kLoginScreen);
    std::string size_arg =
        parsed_command_line.GetSwitchValueASCII(
            switches::kLoginScreenSize);
    gfx::Size size(0, 0);
    // Allow the size of the login window to be set explicitly. If not set,
    // default to the entire screen. This is mostly useful for testing.
    if (size_arg.size()) {
      std::vector<std::string> dimensions;
      base::SplitString(size_arg, ',', &dimensions);
      if (dimensions.size() == 2) {
        int width, height;
        if (base::StringToInt(dimensions[0], &width) &&
            base::StringToInt(dimensions[1], &height))
          size.SetSize(width, height);
      }
    }
    browser::ShowLoginWizard(first_screen, size);
  } else if (parsed_command_line.HasSwitch(switches::kLoginUser) &&
      parsed_command_line.HasSwitch(switches::kLoginPassword)) {
    chromeos::BootTimesLoader::Get()->RecordLoginAttempted();
    new StubLogin(
        parsed_command_line.GetSwitchValueASCII(switches::kLoginUser),
        parsed_command_line.GetSwitchValueASCII(switches::kLoginPassword));
  } else {
    if (!parsed_command_line.HasSwitch(switches::kTestName)) {
      // We did not log in (we crashed or are debugging), so we need to
      // restore Sync.
      chromeos::LoginUtils::Get()->RestoreAuthenticationSession(profile);
    }
  }
}

// ChromeBrowserMainPartsChromeos ----------------------------------------------

ChromeBrowserMainPartsChromeos::ChromeBrowserMainPartsChromeos(
    const content::MainFunctionParams& parameters)
    : ChromeBrowserMainPartsLinux(parameters) {
}

ChromeBrowserMainPartsChromeos::~ChromeBrowserMainPartsChromeos() {
  chromeos::imageburner::BurnManager::Shutdown();

  chromeos::disks::DiskMountManager::Shutdown();

  // CrosLibrary is shut down before DBusThreadManager even though the former
  // is initialized before the latter becuase some of its libraries depend
  // on DBus clients.
  // TODO(hashimoto): Resolve this situation by removing CrosLibrary.
  // (crosbug.com/26160)
  if (!parameters().ui_task && chromeos::CrosLibrary::Get())
    chromeos::CrosLibrary::Shutdown();

  chromeos::DBusThreadManager::Shutdown();

  // To be precise, logout (browser shutdown) is not yet done, but the
  // remaining work is negligible, hence we say LogoutDone here.
  chromeos::BootTimesLoader::Get()->AddLogoutTimeMarker("LogoutDone",
                                                        false);
  chromeos::BootTimesLoader::Get()->WriteLogoutTimes();
}

// content::BrowserMainParts and ChromeBrowserMainExtraParts overrides ---------

void ChromeBrowserMainPartsChromeos::PreEarlyInitialization() {
  if (parsed_command_line().HasSwitch(switches::kGuestSession)) {
    // Disable sync and extensions if we're in "browse without sign-in" mode.
    CommandLine* singleton_command_line = CommandLine::ForCurrentProcess();
    singleton_command_line->AppendSwitch(switches::kDisableSync);
    singleton_command_line->AppendSwitch(switches::kDisableExtensions);
    browser_defaults::bookmarks_enabled = false;
  }

  ChromeBrowserMainPartsLinux::PreEarlyInitialization();
}

void ChromeBrowserMainPartsChromeos::PreMainMessageLoopStart() {
  // Initialize CrosLibrary only for the browser, unless running tests
  // (which do their own CrosLibrary setup).
  if (!parameters().ui_task) {
    bool use_stub = parameters().command_line.HasSwitch(switches::kStubCros);
    chromeos::CrosLibrary::Initialize(use_stub);
  }
  // Replace the default NetworkChangeNotifierFactory with ChromeOS specific
  // implementation.
  net::NetworkChangeNotifier::SetFactory(
      new chromeos::CrosNetworkChangeNotifierFactory());

  ChromeBrowserMainPartsLinux::PreMainMessageLoopStart();
}

void ChromeBrowserMainPartsChromeos::PostMainMessageLoopStart() {
  MessageLoopForUI* message_loop = MessageLoopForUI::current();
  message_loop->AddObserver(g_message_loop_observer.Pointer());

  // Initialize DBusThreadManager for the browser. This must be done after
  // the main message loop is started, as it uses the message loop.
  chromeos::DBusThreadManager::Initialize();

  // Initialize the session manager observer so that we'll take actions
  // per signals sent from the session manager.
  session_manager_observer_.reset(new chromeos::SessionManagerObserver);
  chromeos::DBusThreadManager::Get()->GetSessionManagerClient()->
      AddObserver(session_manager_observer_.get());

  // Initialize the disk mount manager.
  chromeos::disks::DiskMountManager::Initialize();

  // Initialize the burn manager.
  chromeos::imageburner::BurnManager::Initialize();

  // Initialize the network change notifier for Chrome OS. The network
  // change notifier starts to monitor changes from the power manager and
  // the network manager.
  chromeos::CrosNetworkChangeNotifierFactory::GetInstance()->Init();

  // Likewise, initialize the upgrade detector for Chrome OS. The upgrade
  // detector starts to monitor changes from the update engine.
  UpgradeDetectorChromeos::GetInstance()->Init();

  if (chromeos::system::runtime_environment::IsRunningOnChromeOS()) {
    // Enable Num Lock on X start up for http://crosbug.com/p/5795 and
    // http://crosbug.com/p/6245. We don't do this for Chromium OS since many
    // netbooks do not work as intended when Num Lock is on (e.g. On a netbook
    // with a small keyboard, u, i, o, p, ... keys might be repurposed as
    // cursor keys when Num Lock is on).
#if defined(GOOGLE_CHROME_BUILD)
      chromeos::input_method::InputMethodManager::GetInstance()->
          GetXKeyboard()->SetNumLockEnabled(true);
#endif

#if defined(USE_AURA)
      initial_browser_window_observer_.reset(
          new chromeos::InitialBrowserWindowObserver);
#endif
  }

  ChromeBrowserMainPartsLinux::PostMainMessageLoopStart();
}

// Threads are initialized MainMessageLoopStart and MainMessageLoopRun.

void ChromeBrowserMainPartsChromeos::PreMainMessageLoopRun() {
  // Initialize the audio handler on ChromeOS.
  chromeos::AudioHandler::Initialize();

  // Listen for system key events so that the user will be able to adjust the
  // volume on the login screen, if Chrome is running on Chrome OS
  // (i.e. not Linux desktop), and in non-test mode.
  // Note: SystemKeyEventListener depends on the DBus thread.
  if (chromeos::system::runtime_environment::IsRunningOnChromeOS() &&
      !parameters().ui_task) {  // ui_task is non-NULL when running tests.
    chromeos::SystemKeyEventListener::Initialize();
  }

  ChromeBrowserMainPartsLinux::PreMainMessageLoopRun();
}

void ChromeBrowserMainPartsChromeos::PreProfileInit() {
  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- immediately before Profile creation().

  // Now that the file thread exists we can record our stats.
  chromeos::BootTimesLoader::Get()->RecordChromeMainStats();

#if defined(TOOLKIT_USES_GTK)
  // Read locale-specific GTK resource information.
  std::string gtkrc = l10n_util::GetStringUTF8(IDS_LOCALE_GTKRC);
  if (!gtkrc.empty())
    gtk_rc_parse_string(gtkrc.c_str());
#else
  // TODO(saintlou): Need to provide an Aura equivalent.
  NOTIMPLEMENTED();
#endif

  // Trigger prefetching of ownership status.
  chromeos::OwnershipService::GetSharedInstance()->Prewarm();

  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- just before CreateProfile().

  // Initialize the screen locker now so that it can receive
  // LOGIN_USER_CHANGED notification from UserManager.
  chromeos::ScreenLocker::InitClass();

  // This forces the ProfileManager to be created and register for the
  // notification it needs to track the logged in user.
  g_browser_process->profile_manager();

  // TODO(abarth): Should this move to InitializeNetworkOptions()?
  // Allow access to file:// on ChromeOS for tests.
  if (parsed_command_line().HasSwitch(switches::kAllowFileAccess))
    net::URLRequest::AllowFileAccess();

  // There are two use cases for kLoginUser:
  //   1) if passed in tandem with kLoginPassword, to drive a "StubLogin"
  //   2) if passed alone, to signal that the indicated user has already
  //      logged in and we should behave accordingly.
  // This handles case 2.
  if (parsed_command_line().HasSwitch(switches::kLoginUser) &&
      !parsed_command_line().HasSwitch(switches::kLoginPassword)) {
    std::string username =
        parsed_command_line().GetSwitchValueASCII(switches::kLoginUser);
    VLOG(1) << "Relaunching browser for user: " << username;
    chromeos::UserManager::Get()->UserLoggedIn(username);

    // Redirects Chrome logging to the user data dir.
    logging::RedirectChromeLogging(parsed_command_line());

    // Initialize user policy before creating the profile so the profile
    // initialization code sees policy settings.
    g_browser_process->browser_policy_connector()->InitializeUserPolicy(
        username, false  /* wait_for_policy_fetch */);
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_SESSION_STARTED,
        content::NotificationService::AllSources(),
        content::NotificationService::NoDetails());
  } else if (parsed_command_line().HasSwitch(switches::kLoginManager)) {
    // Initialize status area mode early on.
    chromeos::StatusAreaViewChromeos::
        SetScreenMode(chromeos::StatusAreaViewChromeos::LOGIN_MODE_WEBUI);
  }

  // In Aura builds this will initialize ash::Shell.
  ChromeBrowserMainPartsLinux::PreProfileInit();
}

void ChromeBrowserMainPartsChromeos::PostProfileInit() {
  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- just after CreateProfile().

  if (parsed_command_line().HasSwitch(switches::kLoginUser) &&
      !parsed_command_line().HasSwitch(switches::kLoginPassword)) {
    // Pass the TokenService pointer to the policy connector so user policy can
    // grab a token and register with the policy server.
    // TODO(mnissler): Remove once OAuth is the only authentication mechanism.
    g_browser_process->browser_policy_connector()->SetUserPolicyTokenService(
        profile()->GetTokenService());

    // Make sure we flip every profile to not share proxies if the user hasn't
    // specified so explicitly.
    const PrefService::Preference* use_shared_proxies_pref =
        profile()->GetPrefs()->FindPreference(prefs::kUseSharedProxies);
    if (use_shared_proxies_pref->IsDefaultValue())
      profile()->GetPrefs()->SetBoolean(prefs::kUseSharedProxies, false);
  }

  // Tests should be able to tune login manager before showing it.
  // Thus only show login manager in normal (non-testing) mode.
  if (!parameters().ui_task)
    OptionallyRunChromeOSLoginManager(parsed_command_line(), profile());

  // These observers must be initialized after the profile because
  // they use the profile to dispatch extension events.
  //
  // Initialize the brightness observer so that we'll display an onscreen
  // indication of brightness changes during login.
  brightness_observer_.reset(new chromeos::BrightnessObserver());
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->AddObserver(
      brightness_observer_.get());
  resume_observer_.reset(new chromeos::ResumeObserver());
  screen_lock_observer_.reset(new chromeos::ScreenLockObserver());

  ChromeBrowserMainPartsLinux::PostProfileInit();
}

void ChromeBrowserMainPartsChromeos::PreBrowserStart() {
  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- just before MetricsService::LogNeedForCleanShutdown().

  g_browser_process->metrics_service()->StartExternalMetrics();

  // Listen for XI_HierarchyChanged events. Note: if this is moved to
  // PreMainMessageLoopRun() then desktopui_PageCyclerTests fail for unknown
  // reasons, see http://crosbug.com/24833.
  chromeos::XInputHierarchyChangedEventListener::GetInstance();

  // -- This used to be in ChromeBrowserMainParts::PreMainMessageLoopRun()
  // -- immediately after ChildProcess::WaitForDebugger().

  // Start the out-of-memory priority manager here so that we give the most
  // amount of time for the other services to start up before we start
  // adjusting the oom priority.
  g_browser_process->oom_priority_manager()->Start();

  ChromeBrowserMainPartsLinux::PreBrowserStart();
}

void ChromeBrowserMainPartsChromeos::PostBrowserStart() {
  // FILE thread is created in ChromeBrowserMainParts::PreMainMessageLoopRun().

  // Get the statistics provider instance here to start loading statistcs
  // on the background FILE thread.
  chromeos::system::StatisticsProvider::GetInstance();

  // Initialize the Chrome OS bluetooth subsystem.
  // We defer this to PreMainMessageLoopRun because we don't want to check the
  // parsed command line until after about_flags::ConvertFlagsToSwitches has
  // been called.
  // TODO(vlaviano): Move this back to PostMainMessageLoopStart when we remove
  // the --enable-bluetooth flag.
  if (parsed_command_line().HasSwitch(switches::kEnableBluetooth)) {
    chromeos::BluetoothManager::Initialize();
  }

#if defined(USE_AURA)
  // These are dependent on the ash::Shell singleton already having been
  // initialized.
  power_button_observer_.reset(new chromeos::PowerButtonObserver);
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->AddObserver(
      power_button_observer_.get());

  video_property_writer_.reset(new chromeos::VideoPropertyWriter);
#endif

  ChromeBrowserMainPartsLinux::PostBrowserStart();
}

// Shut down services before the browser process, etc are destroyed.
void ChromeBrowserMainPartsChromeos::PostMainMessageLoopRun() {
  chromeos::BootTimesLoader::Get()->AddLogoutTimeMarker("UIMessageLoopEnded",
                                                        true);

  g_browser_process->oom_priority_manager()->Stop();

  // Stops LoginUtils background fetchers. This is needed because IO thread is
  // going to stop soon after this function. The pending background jobs could
  // cause it to crash during shutdown.
  chromeos::LoginUtils::Get()->StopBackgroundFetchers();

  // Shutdown the upgrade detector for Chrome OS. The upgrade detector
  // stops monitoring changes from the update engine.
  if (UpgradeDetectorChromeos::GetInstance())
    UpgradeDetectorChromeos::GetInstance()->Shutdown();

  // Shutdown the network change notifier for Chrome OS. The network
  // change notifier stops monitoring changes from the power manager and
  // the network manager.
  if (chromeos::CrosNetworkChangeNotifierFactory::GetInstance())
    chromeos::CrosNetworkChangeNotifierFactory::GetInstance()->Shutdown();

  // We should remove observers attached to D-Bus clients before
  // DBusThreadManager is shut down.
  if (session_manager_observer_.get()) {
    chromeos::DBusThreadManager::Get()->GetSessionManagerClient()->
        RemoveObserver(session_manager_observer_.get());
  }
  screen_lock_observer_.reset();
  resume_observer_.reset();
  if (brightness_observer_.get()) {
    chromeos::DBusThreadManager::Get()->GetPowerManagerClient()
        ->RemoveObserver(brightness_observer_.get());
  }

  // Shut these down here instead of in the destructor in case we exited before
  // running BrowserMainLoop::RunMainMessageLoopParts() and never initialized
  // these.
  chromeos::BluetoothManager::Shutdown();

  // The XInput2 event listener needs to be shut down earlier than when
  // Singletons are finally destroyed in AtExitManager.
  chromeos::XInputHierarchyChangedEventListener::GetInstance()->Stop();

  // chromeos::SystemKeyEventListener::Shutdown() is always safe to call,
  // even if Initialize() wasn't called.
  chromeos::SystemKeyEventListener::Shutdown();
  chromeos::AudioHandler::Shutdown();

  chromeos::WebSocketProxyController::Shutdown();

#if defined(USE_AURA)
  // Let VideoPropertyWriter unregister itself as an observer of the ash::Shell
  // singleton before the shell is destroyed.
  video_property_writer_.reset();
#endif

  ChromeBrowserMainPartsLinux::PostMainMessageLoopRun();
}
