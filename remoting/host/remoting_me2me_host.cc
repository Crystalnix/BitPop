// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file implements a standalone host process for Me2Me.

#include <string>

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/files/file_path_watcher.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/scoped_native_library.h"
#include "base/string_util.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "base/utf_string_conversions.h"
#include "base/win/windows_version.h"
#include "build/build_config.h"
#include "crypto/nss_util.h"
#include "net/base/network_change_notifier.h"
#include "net/socket/ssl_server_socket.h"
#include "remoting/base/breakpad.h"
#include "remoting/base/constants.h"
#include "remoting/host/branding.h"
#include "remoting/host/chromoting_host.h"
#include "remoting/host/chromoting_host_context.h"
#include "remoting/host/composite_host_config.h"
#include "remoting/host/constants.h"
#include "remoting/host/desktop_environment.h"
#include "remoting/host/event_executor.h"
#include "remoting/host/heartbeat_sender.h"
#include "remoting/host/host_config.h"
#include "remoting/host/host_event_logger.h"
#include "remoting/host/host_user_interface.h"
#include "remoting/host/log_to_server.h"
#include "remoting/host/network_settings.h"
#include "remoting/host/policy_hack/policy_watcher.h"
#include "remoting/host/session_manager_factory.h"
#include "remoting/host/signaling_connector.h"
#include "remoting/host/usage_stats_consent.h"
#include "remoting/host/video_frame_capturer.h"
#include "remoting/jingle_glue/xmpp_signal_strategy.h"
#include "remoting/protocol/me2me_host_authenticator_factory.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#include "remoting/host/sighup_listener_mac.h"
#endif
// N.B. OS_WIN is defined by including src/base headers.
#if defined(OS_WIN)
#include <commctrl.h>
#endif
#if defined(TOOLKIT_GTK)
#include "ui/gfx/gtk_util.h"
#endif

namespace {

// This is used for tagging system event logs.
const char kApplicationName[] = "chromoting";

// These are used for parsing the config-file locations from the command line,
// and for defining the default locations if the switches are not present.
const char kAuthConfigSwitchName[] = "auth-config";
const char kHostConfigSwitchName[] = "host-config";

const FilePath::CharType kDefaultHostConfigFile[] =
    FILE_PATH_LITERAL("host.json");

const int kMinPortNumber = 12400;
const int kMaxPortNumber = 12409;

const char kUnofficialOAuth2ClientId[] =
    "440925447803-2pi3v45bff6tp1rde2f7q6lgbor3o5uj.apps.googleusercontent.com";
const char kUnofficialOAuth2ClientSecret[] = "W2ieEsG-R1gIA4MMurGrgMc_";

const char kOfficialOAuth2ClientId[] =
    "440925447803-avn2sj1kc099s0r7v62je5s339mu0am1.apps.googleusercontent.com";
const char kOfficialOAuth2ClientSecret[] = "Bgur6DFiOMM1h8x-AQpuTQlK";

}  // namespace

namespace remoting {

class HostProcess
    : public HeartbeatSender::Listener {
 public:
  HostProcess()
      : message_loop_(MessageLoop::TYPE_UI),
#ifdef OFFICIAL_BUILD
        oauth_use_official_client_id_(true),
#else
        oauth_use_official_client_id_(false),
#endif
        allow_nat_traversal_(true),
        restarting_(false),
        shutting_down_(false),
        exit_code_(kSuccessExitCode) {
    context_.reset(
        new ChromotingHostContext(message_loop_.message_loop_proxy()));
    context_->Start();
    network_change_notifier_.reset(net::NetworkChangeNotifier::Create());
    config_updated_timer_.reset(new base::DelayTimer<HostProcess>(
        FROM_HERE, base::TimeDelta::FromSeconds(2), this,
        &HostProcess::ConfigUpdatedDelayed));
  }

  bool InitWithCommandLine(const CommandLine* cmd_line) {
    FilePath default_config_dir = remoting::GetConfigDir();
    if (cmd_line->HasSwitch(kAuthConfigSwitchName)) {
      FilePath path = cmd_line->GetSwitchValuePath(kAuthConfigSwitchName);
      config_.AddConfigPath(path);
    }

    host_config_path_ = default_config_dir.Append(kDefaultHostConfigFile);
    if (cmd_line->HasSwitch(kHostConfigSwitchName)) {
      host_config_path_ = cmd_line->GetSwitchValuePath(kHostConfigSwitchName);
    }
    config_.AddConfigPath(host_config_path_);

    return true;
  }

  void ConfigUpdated() {
    DCHECK(message_loop_.message_loop_proxy()->BelongsToCurrentThread());

    // Call ConfigUpdatedDelayed after a short delay, so that this object won't
    // try to read the updated configuration file before it has been
    // completely written.
    // If the writer moves the new configuration file into place atomically,
    // this delay may not be necessary.
    config_updated_timer_->Reset();
  }

  void ConfigUpdatedDelayed() {
    DCHECK(message_loop_.message_loop_proxy()->BelongsToCurrentThread());

    if (LoadConfig()) {
      // PostTask to create new authenticator factory in case PIN has changed.
      context_->network_task_runner()->PostTask(
          FROM_HERE,
          base::Bind(&HostProcess::CreateAuthenticatorFactory,
                     base::Unretained(this)));
    } else {
      LOG(ERROR) << "Invalid configuration.";
    }
  }

#if defined(OS_WIN)
  class ConfigChangedDelegate : public base::files::FilePathWatcher::Delegate {
   public:
    ConfigChangedDelegate(
        scoped_refptr<base::SingleThreadTaskRunner> task_runner,
        const base::Closure& callback)
        : task_runner_(task_runner),
          callback_(callback) {
    }

    void OnFilePathChanged(const FilePath& path) OVERRIDE {
      task_runner_->PostTask(FROM_HERE, callback_);
    }

    void OnFilePathError(const FilePath& path) OVERRIDE {
    }

   private:
    scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
    base::Closure callback_;

    DISALLOW_COPY_AND_ASSIGN(ConfigChangedDelegate);
  };
#endif  // defined(OS_WIN)

  void ListenForConfigChanges() {
#if defined(OS_MACOSX)
    remoting::RegisterHupSignalHandler(
        base::Bind(&HostProcess::ConfigUpdatedDelayed, base::Unretained(this)));
#elif defined(OS_WIN)
    scoped_refptr<base::files::FilePathWatcher::Delegate> delegate(
        new ConfigChangedDelegate(
            message_loop_.message_loop_proxy(),
            base::Bind(&HostProcess::ConfigUpdated, base::Unretained(this))));
    config_watcher_.reset(new base::files::FilePathWatcher());
    if (!config_watcher_->Watch(host_config_path_, delegate)) {
      LOG(ERROR) << "Couldn't watch file " << host_config_path_.value();
    }
#endif
  }

  void CreateAuthenticatorFactory() {
    DCHECK(context_->network_task_runner()->BelongsToCurrentThread());
    scoped_ptr<protocol::AuthenticatorFactory> factory(
        new protocol::Me2MeHostAuthenticatorFactory(
            key_pair_.GenerateCertificate(),
            *key_pair_.private_key(), host_secret_hash_));
    host_->SetAuthenticatorFactory(factory.Pass());
  }

  int Run() {
    if (!LoadConfig()) {
      return kInvalidHostConfigurationExitCode;
    }

#if defined(OS_MACOSX) || defined(OS_WIN)
     bool want_user_interface = true;

#if defined(OS_MACOSX)
     // Don't try to display any UI on top of the system's login screen as this
     // is rejected by the Window Server on OS X 10.7.4, and prevents the
     // capturer from working (http://crbug.com/140984).

     // TODO(lambroslambrou): Use a better technique of detecting whether we're
     // running in the LoginWindow context, and refactor this into a separate
     // function to be used here and in CurtainMode::ActivateCurtain().
     if (getuid() == 0) {
       want_user_interface = false;
     }
#endif  // OS_MACOSX

     if (want_user_interface) {
       host_user_interface_.reset(new HostUserInterface(context_.get()));
     }
#endif  // OS_MACOSX || OS_WIN

    StartWatchingPolicy();

#if defined(OS_MACOSX) || defined(OS_WIN)
    context_->file_task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&HostProcess::ListenForConfigChanges,
                   base::Unretained(this)));
#endif
    message_loop_.Run();

#if defined(OS_MACOSX) || defined(OS_WIN)
    host_user_interface_.reset();
#endif

    base::WaitableEvent done_event(true, false);
    policy_watcher_->StopWatching(&done_event);
    done_event.Wait();
    policy_watcher_.reset();

    return exit_code_;
  }

  // Overridden from HeartbeatSender::Listener
  virtual void OnUnknownHostIdError() OVERRIDE {
    LOG(ERROR) << "Host ID not found.";
    Shutdown(kInvalidHostIdExitCode);
  }

 private:
  void StartWatchingPolicy() {
    policy_watcher_.reset(
        policy_hack::PolicyWatcher::Create(context_->file_task_runner()));
    policy_watcher_->StartWatching(
        base::Bind(&HostProcess::OnPolicyUpdate, base::Unretained(this)));
  }

  // Read host config, returning true if successful.
  bool LoadConfig() {
    DCHECK(message_loop_.message_loop_proxy()->BelongsToCurrentThread());

    // TODO(sergeyu): There is a potential race condition: this function is
    // called on the main thread while the class members it mutates are used on
    // the network thread. Fix it. http://crbug.com/140986 .

    if (!config_.Read()) {
      LOG(ERROR) << "Failed to read config file.";
      return false;
    }

    if (!config_.GetString(kHostIdConfigPath, &host_id_)) {
      LOG(ERROR) << "host_id is not defined in the config.";
      return false;
    }

    if (!key_pair_.Load(config_)) {
      return false;
    }

    std::string host_secret_hash_string;
    if (!config_.GetString(kHostSecretHashConfigPath,
                           &host_secret_hash_string)) {
      host_secret_hash_string = "plain:";
    }

    if (!host_secret_hash_.Parse(host_secret_hash_string)) {
      LOG(ERROR) << "Invalid host_secret_hash.";
      return false;
    }

    // Use an XMPP connection to the Talk network for session signalling.
    if (!config_.GetString(kXmppLoginConfigPath, &xmpp_login_) ||
        !(config_.GetString(kXmppAuthTokenConfigPath, &xmpp_auth_token_) ||
          config_.GetString(kOAuthRefreshTokenConfigPath,
                            &oauth_refresh_token_))) {
      LOG(ERROR) << "XMPP credentials are not defined in the config.";
      return false;
    }

    // It is okay to not have this value and we will use the default value
    // depending on whether this is an official build or not.
    // If the client-Id type to use is not specified we default based on
    // the build type.
    config_.GetBoolean(kOAuthUseOfficialClientIdConfigPath,
                       &oauth_use_official_client_id_);

    if (!oauth_refresh_token_.empty()) {
      xmpp_auth_token_ = "";  // This will be set to the access token later.
      xmpp_auth_service_ = "oauth2";
    } else if (!config_.GetString(kXmppAuthServiceConfigPath,
                                  &xmpp_auth_service_)) {
      // For the me2me host, we default to ClientLogin token for chromiumsync
      // because earlier versions of the host had no HTTP stack with which to
      // request an OAuth2 access token.
      xmpp_auth_service_ = kChromotingTokenDefaultServiceName;
    }
    return true;
  }

  void OnPolicyUpdate(scoped_ptr<base::DictionaryValue> policies) {
    if (!context_->network_task_runner()->BelongsToCurrentThread()) {
      context_->network_task_runner()->PostTask(FROM_HERE, base::Bind(
          &HostProcess::OnPolicyUpdate, base::Unretained(this),
          base::Passed(&policies)));
      return;
    }

    bool bool_value;
    std::string string_value;
    if (policies->GetString(policy_hack::PolicyWatcher::kHostDomainPolicyName,
                            &string_value)) {
      OnHostDomainPolicyUpdate(string_value);
    }
    if (policies->GetBoolean(policy_hack::PolicyWatcher::kNatPolicyName,
                             &bool_value)) {
      OnNatPolicyUpdate(bool_value);
    }
  }

  void OnHostDomainPolicyUpdate(const std::string& host_domain) {
    if (!context_->network_task_runner()->BelongsToCurrentThread()) {
      context_->network_task_runner()->PostTask(FROM_HERE, base::Bind(
          &HostProcess::OnHostDomainPolicyUpdate, base::Unretained(this),
          host_domain));
      return;
    }

    if (!host_domain.empty() &&
        !EndsWith(xmpp_login_, std::string("@") + host_domain, false)) {
      Shutdown(kInvalidHostDomainExitCode);
    }
  }

  void OnNatPolicyUpdate(bool nat_traversal_enabled) {
    if (!context_->network_task_runner()->BelongsToCurrentThread()) {
      context_->network_task_runner()->PostTask(FROM_HERE, base::Bind(
          &HostProcess::OnNatPolicyUpdate, base::Unretained(this),
          nat_traversal_enabled));
      return;
    }

    bool policy_changed = allow_nat_traversal_ != nat_traversal_enabled;
    allow_nat_traversal_ = nat_traversal_enabled;

    if (host_) {
      // Restart the host if the policy has changed while the host was
      // online.
      if (policy_changed)
        RestartHost();
    } else {
      // Just start the host otherwise.
      StartHost();
    }
  }

  void StartHost() {
    DCHECK(context_->network_task_runner()->BelongsToCurrentThread());
    DCHECK(!host_);

    if (shutting_down_)
      return;

    if (!signal_strategy_.get()) {
      signal_strategy_.reset(
          new XmppSignalStrategy(context_->url_request_context_getter(),
                                 xmpp_login_, xmpp_auth_token_,
                                 xmpp_auth_service_));

      signaling_connector_.reset(new SignalingConnector(
          signal_strategy_.get(),
          base::Bind(&HostProcess::OnAuthFailed, base::Unretained(this))));

      if (!oauth_refresh_token_.empty()) {
        OAuthClientInfo client_info = {
          kUnofficialOAuth2ClientId,
          kUnofficialOAuth2ClientSecret
        };

#ifdef OFFICIAL_BUILD
        if (oauth_use_official_client_id_) {
          OAuthClientInfo official_client_info = {
            kOfficialOAuth2ClientId,
            kOfficialOAuth2ClientSecret
          };

          client_info = official_client_info;
        }
#endif  // OFFICIAL_BUILD

        scoped_ptr<SignalingConnector::OAuthCredentials> oauth_credentials(
            new SignalingConnector::OAuthCredentials(
                xmpp_login_, oauth_refresh_token_, client_info));
        signaling_connector_->EnableOAuth(
            oauth_credentials.Pass(),
            context_->url_request_context_getter());
      }
    }

    if (!desktop_environment_.get()) {
      desktop_environment_ =
          DesktopEnvironment::CreateForService(context_.get());
    }

    NetworkSettings network_settings(
        allow_nat_traversal_ ?
        NetworkSettings::NAT_TRAVERSAL_ENABLED :
        NetworkSettings::NAT_TRAVERSAL_DISABLED);
    if (!allow_nat_traversal_) {
      network_settings.min_port = kMinPortNumber;
      network_settings.max_port = kMaxPortNumber;
    }

    host_ = new ChromotingHost(
        context_.get(), signal_strategy_.get(), desktop_environment_.get(),
        CreateHostSessionManager(network_settings,
                                 context_->url_request_context_getter()));

    // TODO(simonmorris): Get the maximum session duration from a policy.
#if defined(OS_LINUX)
    host_->SetMaximumSessionDuration(base::TimeDelta::FromHours(20));
#endif

    heartbeat_sender_.reset(new HeartbeatSender(
        this, host_id_, signal_strategy_.get(), &key_pair_));

    log_to_server_.reset(
        new LogToServer(host_, ServerLogEntry::ME2ME, signal_strategy_.get()));
    host_event_logger_ = HostEventLogger::Create(host_, kApplicationName);

#if defined(OS_MACOSX) || defined(OS_WIN)
    if (host_user_interface_.get()) {
      host_user_interface_->Start(
          host_, base::Bind(&HostProcess::OnDisconnectRequested,
                            base::Unretained(this)));
    }
#endif

    host_->Start();

    CreateAuthenticatorFactory();
  }

  void OnAuthFailed() {
    Shutdown(kInvalidOauthCredentialsExitCode);
  }

  // Invoked when the user uses the Disconnect windows to terminate
  // the sessions.
  void OnDisconnectRequested() {
    DCHECK(message_loop_.message_loop_proxy()->BelongsToCurrentThread());

    host_->DisconnectAllClients();
  }

  void RestartHost() {
    DCHECK(context_->network_task_runner()->BelongsToCurrentThread());

    if (restarting_ || shutting_down_)
      return;

    restarting_ = true;
    host_->Shutdown(base::Bind(
        &HostProcess::RestartOnHostShutdown, base::Unretained(this)));
  }

  void RestartOnHostShutdown() {
    DCHECK(context_->network_task_runner()->BelongsToCurrentThread());

    if (shutting_down_)
      return;

    restarting_ = false;
    host_ = NULL;
    log_to_server_.reset();
    host_event_logger_.reset();
    heartbeat_sender_.reset();

    StartHost();
  }

  void Shutdown(int exit_code) {
    DCHECK(context_->network_task_runner()->BelongsToCurrentThread());

    if (shutting_down_)
      return;

    shutting_down_ = true;
    exit_code_ = exit_code;
    if (host_) {
      host_->Shutdown(base::Bind(
          &HostProcess::OnShutdownFinished, base::Unretained(this)));
    } else {
      OnShutdownFinished();
    }
  }

  void OnShutdownFinished() {
    DCHECK(context_->network_task_runner()->BelongsToCurrentThread());

    // Destroy networking objects while we are on the network thread.
    host_ = NULL;
    host_event_logger_.reset();
    log_to_server_.reset();
    heartbeat_sender_.reset();
    signaling_connector_.reset();
    signal_strategy_.reset();

    message_loop_.PostTask(FROM_HERE, MessageLoop::QuitClosure());
  }

  MessageLoop message_loop_;
  scoped_ptr<ChromotingHostContext> context_;
  scoped_ptr<net::NetworkChangeNotifier> network_change_notifier_;

  FilePath host_config_path_;
  CompositeHostConfig config_;

  std::string host_id_;
  HostKeyPair key_pair_;
  protocol::SharedSecretHash host_secret_hash_;
  std::string xmpp_login_;
  std::string xmpp_auth_token_;
  std::string xmpp_auth_service_;

  std::string oauth_refresh_token_;
  bool oauth_use_official_client_id_;

  scoped_ptr<policy_hack::PolicyWatcher> policy_watcher_;
  bool allow_nat_traversal_;
  scoped_ptr<base::files::FilePathWatcher> config_watcher_;
  scoped_ptr<base::DelayTimer<HostProcess> > config_updated_timer_;

  bool restarting_;
  bool shutting_down_;

  scoped_ptr<XmppSignalStrategy> signal_strategy_;
  scoped_ptr<SignalingConnector> signaling_connector_;
  scoped_ptr<DesktopEnvironment> desktop_environment_;
  scoped_ptr<HeartbeatSender> heartbeat_sender_;
  scoped_ptr<LogToServer> log_to_server_;
  scoped_ptr<HostEventLogger> host_event_logger_;

#if defined(OS_MACOSX) || defined(OS_WIN)
  scoped_ptr<HostUserInterface> host_user_interface_;
#endif

  scoped_refptr<ChromotingHost> host_;

  int exit_code_;
};

}  // namespace remoting

int main(int argc, char** argv) {
#if defined(OS_MACOSX)
  // Needed so we don't leak objects when threads are created.
  base::mac::ScopedNSAutoreleasePool pool;
#endif

  CommandLine::Init(argc, argv);

  // This object instance is required by Chrome code (for example,
  // LazyInstance, MessageLoop).
  base::AtExitManager exit_manager;

  // Initialize logging with an appropriate log-file location, and default to
  // log to that on Windows, or to standard error output otherwise.
  FilePath debug_log = remoting::GetConfigDir().
      Append(FILE_PATH_LITERAL("debug.log"));
  InitLogging(debug_log.value().c_str(),
#if defined(OS_WIN)
              logging::LOG_ONLY_TO_FILE,
#else
              logging::LOG_ONLY_TO_SYSTEM_DEBUG_LOG,
#endif
              logging::DONT_LOCK_LOG_FILE,
              logging::APPEND_TO_OLD_LOG_FILE,
              logging::DISABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS);

  const CommandLine* cmd_line = CommandLine::ForCurrentProcess();

#if defined(TOOLKIT_GTK)
  // Required for any calls into GTK functions, such as the Disconnect and
  // Continue windows, though these should not be used for the Me2Me case
  // (crbug.com/104377).
  gfx::GtkInitFromCommandLine(*cmd_line);
#endif  // TOOLKIT_GTK

  // Enable support for SSL server sockets, which must be done while still
  // single-threaded.
  net::EnableSSLServerSockets();

#if defined(OS_LINUX)
    remoting::VideoFrameCapturer::EnableXDamage(true);
#endif

  remoting::HostProcess me2me_host;
  if (!me2me_host.InitWithCommandLine(cmd_line)) {
    return remoting::kInvalidHostConfigurationExitCode;
  }

  return me2me_host.Run();
}

#if defined(OS_WIN)
HMODULE g_hModule = NULL;

int CALLBACK WinMain(HINSTANCE instance,
                     HINSTANCE previous_instance,
                     LPSTR command_line,
                     int show_command) {
#ifdef OFFICIAL_BUILD
  if (remoting::IsUsageStatsAllowed()) {
    remoting::InitializeCrashReporting();
  }
#endif  // OFFICIAL_BUILD

  g_hModule = instance;

  // Register and initialize common controls.
  INITCOMMONCONTROLSEX info;
  info.dwSize = sizeof(info);
  info.dwICC = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&info);

  // Mark the process as DPI-aware, so Windows won't scale coordinates in APIs.
  // N.B. This API exists on Vista and above.
  if (base::win::GetVersion() >= base::win::VERSION_VISTA) {
    FilePath path(base::GetNativeLibraryName(UTF8ToUTF16("user32")));
    base::ScopedNativeLibrary user32(path);
    CHECK(user32.is_valid());

    typedef BOOL (WINAPI * SetProcessDPIAwareFn)();
    SetProcessDPIAwareFn set_process_dpi_aware =
        static_cast<SetProcessDPIAwareFn>(
            user32.GetFunctionPointer("SetProcessDPIAware"));
    set_process_dpi_aware();
  }

  // CommandLine::Init() ignores the passed |argc| and |argv| on Windows getting
  // the command line from GetCommandLineW(), so we can safely pass NULL here.
  return main(0, NULL);
}

#endif  // defined(OS_WIN)
