// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/service/cloud_print/cloud_print_proxy_backend.h"

#include <map>
#include <vector>

#include "base/file_util.h"
#include "base/md5.h"
#include "base/rand_util.h"
#include "base/stringprintf.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/net/gaia/gaia_oauth_client.h"
#include "chrome/service/cloud_print/cloud_print_consts.h"
#include "chrome/service/cloud_print/cloud_print_helpers.h"
#include "chrome/service/cloud_print/cloud_print_token_store.h"
#include "chrome/service/cloud_print/cloud_print_url_fetcher.h"
#include "chrome/service/cloud_print/printer_job_handler.h"
#include "chrome/service/gaia/service_gaia_authenticator.h"
#include "chrome/service/net/service_url_request_context.h"
#include "chrome/service/service_process.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "jingle/notifier/base/notifier_options.h"
#include "jingle/notifier/listener/mediator_thread_impl.h"
#include "jingle/notifier/listener/talk_mediator_impl.h"
#include "net/url_request/url_request_status.h"
#include "ui/base/l10n/l10n_util.h"

// The real guts of CloudPrintProxyBackend, to keep the public client API clean.
class CloudPrintProxyBackend::Core
    : public base::RefCountedThreadSafe<CloudPrintProxyBackend::Core>,
      public CloudPrintURLFetcherDelegate,
      public cloud_print::PrintServerWatcherDelegate,
      public PrinterJobHandlerDelegate,
      public notifier::TalkMediator::Delegate,
      public gaia::GaiaOAuthClient::Delegate {
 public:
  // It is OK for print_server_url to be empty. In this case system should
  // use system default (local) print server.
  explicit Core(CloudPrintProxyBackend* backend,
                const GURL& cloud_print_server_url,
                const DictionaryValue* print_system_settings,
                const gaia::OAuthClientInfo& oauth_client_info,
                bool enable_job_poll);

  // Note:
  //
  // The Do* methods are the various entry points from CloudPrintProxyBackend
  // It calls us on a dedicated thread to actually perform synchronous
  // (and potentially blocking) operations.
  //
  // Called on the CloudPrintProxyBackend core_thread_ to perform
  // initialization. When we are passed in an LSID we authenticate using that
  // and retrieve new auth tokens.
  void DoInitializeWithLsid(const std::string& lsid,
                            const std::string& proxy_id,
                            const std::string& last_robot_refresh_token,
                            const std::string& last_robot_email,
                            const std::string& last_user_email);

  void DoInitializeWithToken(const std::string cloud_print_token,
                             const std::string& proxy_id);
  void DoInitializeWithRobotToken(const std::string& robot_oauth_refresh_token,
                                  const std::string& robot_email,
                                  const std::string& proxy_id);
  void DoInitializeWithRobotAuthCode(const std::string& robot_oauth_auth_code,
                                     const std::string& robot_email,
                                     const std::string& proxy_id);

  // Called on the CloudPrintProxyBackend core_thread_ to perform
  // shutdown.
  void DoShutdown();
  void DoRegisterSelectedPrinters(
      const printing::PrinterList& printer_list);

  // CloudPrintURLFetcher::Delegate implementation.
  virtual CloudPrintURLFetcher::ResponseAction HandleJSONData(
      const URLFetcher* source,
      const GURL& url,
      DictionaryValue* json_data,
      bool succeeded);

  virtual void OnRequestAuthError();

  // cloud_print::PrintServerWatcherDelegate implementation
  virtual void OnPrinterAdded();
  // PrinterJobHandler::Delegate implementation
  virtual void OnPrinterJobHandlerShutdown(PrinterJobHandler* job_handler,
                                   const std::string& printer_id);
  virtual void OnAuthError();
  virtual void OnPrinterNotFound(const std::string& printer_name,
                                 bool* delete_from_server);

  // notifier::TalkMediator::Delegate implementation.
  virtual void OnNotificationStateChange(
      bool notifications_enabled);
  virtual void OnIncomingNotification(
      const notifier::Notification& notification);
  virtual void OnOutgoingNotification();

  // gaia::GaiaOAuthClient::Delegate implementation.
  virtual void OnGetTokensResponse(const std::string& refresh_token,
                                   const std::string& access_token,
                                   int expires_in_seconds);
  virtual void OnRefreshTokenResponse(const std::string& access_token,
                                      int expires_in_seconds);
  virtual void OnOAuthError();
  virtual void OnNetworkError(int response_code);

 private:
  // Prototype for a response handler.
  typedef CloudPrintURLFetcher::ResponseAction
      (CloudPrintProxyBackend::Core::*ResponseHandler)(
          const URLFetcher* source,
          const GURL& url,
          DictionaryValue* json_data,
          bool succeeded);
  // Begin response handlers
  CloudPrintURLFetcher::ResponseAction HandlePrinterListResponse(
      const URLFetcher* source,
      const GURL& url,
      DictionaryValue* json_data,
      bool succeeded);

  CloudPrintURLFetcher::ResponseAction HandleRegisterPrinterResponse(
      const URLFetcher* source,
      const GURL& url,
      DictionaryValue* json_data,
      bool succeeded);

  CloudPrintURLFetcher::ResponseAction HandleRegisterFailedStatusResponse(
      const URLFetcher* source,
      const GURL& url,
      DictionaryValue* json_data,
      bool succeeded);

  CloudPrintURLFetcher::ResponseAction HandlePrintSystemUnavailableResponse(
      const URLFetcher* source,
      const GURL& url,
      DictionaryValue* json_data,
      bool succeeded);

  CloudPrintURLFetcher::ResponseAction HandleEnumPrintersFailedResponse(
      const URLFetcher* source,
      const GURL& url,
      DictionaryValue* json_data,
      bool succeeded);

  CloudPrintURLFetcher::ResponseAction HandleGetAuthCodeResponse(
      const URLFetcher* source,
      const GURL& url,
      DictionaryValue* json_data,
      bool succeeded);

  // End response handlers

  // NotifyXXX is how the Core communicates with the frontend across
  // threads.
  void NotifyPrinterListAvailable(
      const printing::PrinterList& printer_list);
  void NotifyAuthenticated(
    const std::string& robot_oauth_refresh_token,
    const std::string& robot_email,
    const std::string& user_email);
  void NotifyAuthenticationFailed();
  void NotifyPrintSystemUnavailable();

  // Once we have robot credentials, this method gets the ball rolling.
  void PostAuthInitialization();
  // Starts a new printer registration process.
  void StartRegistration();
  // Ends the printer registration process.
  void EndRegistration();
  // Registers printer capabilities and defaults for the next printer in the
  // list with the cloud print server.
  void RegisterNextPrinter();
  // Retrieves the list of registered printers for this user/proxy combination
  // from the cloud print server.
  void GetRegisteredPrinters();
  // Removes the given printer from the list. Returns false if the printer
  // did not exist in the list.
  bool RemovePrinterFromList(const std::string& printer_name);
  // Initializes a job handler object for the specified printer. The job
  // handler is responsible for checking for pending print jobs for this
  // printer and print them.
  void InitJobHandlerForPrinter(DictionaryValue* printer_data);
  // Reports a diagnostic message to the server.
  void ReportUserMessage(const std::string& message_id,
                         const std::string& failure_message,
                         ResponseHandler handler);
  // Make a GAIA request to refresh the access token.
  void RefreshAccessToken();

  // Callback method for GetPrinterCapsAndDefaults.
  void OnReceivePrinterCaps(
      bool succeeded,
      const std::string& printer_name,
      const printing::PrinterCapsAndDefaults& caps_and_defaults);

  void HandlePrinterNotification(const std::string& printer_id);
  void PollForJobs();
  // Schedules a task to poll for jobs. Does nothing if a task is already
  // scheduled.
  void ScheduleJobPoll();
  CloudPrintTokenStore* GetTokenStore();

  // Our parent CloudPrintProxyBackend
  CloudPrintProxyBackend* backend_;

  GURL cloud_print_server_url_;
  gaia::OAuthClientInfo oauth_client_info_;
  scoped_ptr<DictionaryValue> print_system_settings_;
  // Pointer to current print system.
  scoped_refptr<cloud_print::PrintSystem> print_system_;
  // The list of printers to be registered with the cloud print server.
  // To begin with,this list is initialized with the list of local and network
  // printers available. Then we query the server for the list of printers
  // already registered. We trim this list to remove the printers already
  // registered. We then pass a copy of this list to the frontend to give the
  // user a chance to further trim the list. When the frontend gives us the
  // final list we make a copy into this so that we can start registering.
  printing::PrinterList printer_list_;
  // Indicates whether the printers in printer_list_ is the complete set of
  // printers to be registered for this proxy.
  bool complete_list_available_;
  // The CloudPrintURLFetcher instance for the current request.
  scoped_refptr<CloudPrintURLFetcher> request_;
  // The index of the nex printer to be uploaded.
  size_t next_upload_index_;
  // The unique id for this proxy
  std::string proxy_id_;
  // The OAuth2 refresh token for the robot.
  std::string refresh_token_;
  // The email address of the user. This is only used during initial
  // authentication with an LSID. This is only used for storing in prefs for
  // display purposes.
  std::string user_email_;
  // The email address of the robot account.
  std::string robot_email_;
  // Cached info about the last printer that we tried to upload. We cache this
  // so we won't have to requery the printer if the upload fails and we need
  // to retry.
  std::string last_uploaded_printer_name_;
  printing::PrinterCapsAndDefaults last_uploaded_printer_info_;
  // A map of printer id to job handler.
  typedef std::map<std::string, scoped_refptr<PrinterJobHandler> >
      JobHandlerMap;
  JobHandlerMap job_handler_map_;
  ResponseHandler next_response_handler_;
  scoped_refptr<cloud_print::PrintSystem::PrintServerWatcher>
      print_server_watcher_;
  bool new_printers_available_;
  bool registration_in_progress_;
  // Notification (xmpp) handler.
  scoped_ptr<notifier::TalkMediator> talk_mediator_;
  // Indicates whether XMPP notifications are currently enabled.
  bool notifications_enabled_;
  // The time when notifications were enabled. Valid only when
  // notifications_enabled_ is true.
  base::TimeTicks notifications_enabled_since_;
  // Indicates whether a task to poll for jobs has been scheduled.
  bool job_poll_scheduled_;
  // Indicates whether we should poll for jobs when we lose XMPP connection.
  bool enable_job_poll_;
  scoped_ptr<gaia::GaiaOAuthClient> oauth_client_;
  scoped_ptr<CloudPrintTokenStore> token_store_;

  DISALLOW_COPY_AND_ASSIGN(Core);
};

CloudPrintProxyBackend::CloudPrintProxyBackend(
    CloudPrintProxyFrontend* frontend,
    const GURL& cloud_print_server_url,
    const DictionaryValue* print_system_settings,
    const gaia::OAuthClientInfo& oauth_client_info,
    bool enable_job_poll)
      : core_thread_("Chrome_CloudPrintProxyCoreThread"),
        frontend_loop_(MessageLoop::current()),
        frontend_(frontend) {
  DCHECK(frontend_);
  core_ = new Core(this,
                   cloud_print_server_url,
                   print_system_settings,
                   oauth_client_info,
                   enable_job_poll);
}

CloudPrintProxyBackend::~CloudPrintProxyBackend() {
  DCHECK(!core_);
}

bool CloudPrintProxyBackend::InitializeWithLsid(
    const std::string& lsid,
    const std::string& proxy_id,
    const std::string& last_robot_refresh_token,
    const std::string& last_robot_email,
    const std::string& last_user_email) {
  if (!core_thread_.Start())
    return false;
  core_thread_.message_loop()->PostTask(FROM_HERE,
      NewRunnableMethod(
        core_.get(),
        &CloudPrintProxyBackend::Core::DoInitializeWithLsid,
        lsid,
        proxy_id,
        last_robot_refresh_token,
        last_robot_email,
        last_user_email));
  return true;
}

bool CloudPrintProxyBackend::InitializeWithToken(
    const std::string& cloud_print_token,
    const std::string& proxy_id) {
  if (!core_thread_.Start())
    return false;
  core_thread_.message_loop()->PostTask(FROM_HERE,
      NewRunnableMethod(
        core_.get(), &CloudPrintProxyBackend::Core::DoInitializeWithToken,
        cloud_print_token, proxy_id));
  return true;
}

bool CloudPrintProxyBackend::InitializeWithRobotToken(
    const std::string& robot_oauth_refresh_token,
    const std::string& robot_email,
    const std::string& proxy_id) {
  if (!core_thread_.Start())
    return false;
  core_thread_.message_loop()->PostTask(FROM_HERE,
      NewRunnableMethod(
        core_.get(),
        &CloudPrintProxyBackend::Core::DoInitializeWithRobotToken,
        robot_oauth_refresh_token,
        robot_email,
        proxy_id));
  return true;
}

bool CloudPrintProxyBackend::InitializeWithRobotAuthCode(
    const std::string& robot_oauth_auth_code,
    const std::string& robot_email,
    const std::string& proxy_id) {
  if (!core_thread_.Start())
    return false;
  core_thread_.message_loop()->PostTask(FROM_HERE,
      NewRunnableMethod(
        core_.get(),
        &CloudPrintProxyBackend::Core::DoInitializeWithRobotAuthCode,
        robot_oauth_auth_code,
        robot_email,
        proxy_id));
  return true;
}

void CloudPrintProxyBackend::Shutdown() {
  core_thread_.message_loop()->PostTask(FROM_HERE,
      NewRunnableMethod(core_.get(),
                        &CloudPrintProxyBackend::Core::DoShutdown));
  core_thread_.Stop();
  core_ = NULL;  // Releases reference to core_.
}

void CloudPrintProxyBackend::RegisterPrinters(
    const printing::PrinterList& printer_list) {
  core_thread_.message_loop()->PostTask(FROM_HERE,
      NewRunnableMethod(
          core_.get(),
          &CloudPrintProxyBackend::Core::DoRegisterSelectedPrinters,
          printer_list));
}

CloudPrintProxyBackend::Core::Core(
    CloudPrintProxyBackend* backend,
    const GURL& cloud_print_server_url,
    const DictionaryValue* print_system_settings,
    const gaia::OAuthClientInfo& oauth_client_info,
    bool enable_job_poll)
      : backend_(backend),
        cloud_print_server_url_(cloud_print_server_url),
        oauth_client_info_(oauth_client_info),
        complete_list_available_(false),
        next_upload_index_(0),
        next_response_handler_(NULL),
        new_printers_available_(false),
        registration_in_progress_(false),
        notifications_enabled_(false),
        job_poll_scheduled_(false),
        enable_job_poll_(enable_job_poll) {
  if (print_system_settings) {
    // It is possible to have no print settings specified.
    print_system_settings_.reset(print_system_settings->DeepCopy());
  }
}

void CloudPrintProxyBackend::Core::DoInitializeWithLsid(
    const std::string& lsid,
    const std::string& proxy_id,
    const std::string& last_robot_refresh_token,
    const std::string& last_robot_email,
    const std::string& last_user_email) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  // Note: The GAIA login is synchronous but that should be OK because we are in
  // the CloudPrintProxyCoreThread and we cannot really do anything else until
  // the GAIA signin is successful.
  std::string user_agent = "ChromiumBrowser";
  scoped_refptr<ServiceGaiaAuthenticator> gaia_auth_for_print(
      new ServiceGaiaAuthenticator(
          user_agent, kCloudPrintGaiaServiceId, kGaiaUrl,
          g_service_process->io_thread()->message_loop_proxy()));
  gaia_auth_for_print->set_message_loop(MessageLoop::current());
  if (gaia_auth_for_print->AuthenticateWithLsid(lsid)) {
    // Stash away the user email so we can save it in prefs.
    user_email_ = gaia_auth_for_print->email();
    // If the same user is re-enabling Cloud Print and we have stashed robot
    // credentials, we will use those.
    if ((0 == base::strcasecmp(user_email_.c_str(), last_user_email.c_str())) &&
        !last_robot_refresh_token.empty() &&
        !last_robot_email.empty()) {
      DoInitializeWithRobotToken(last_robot_refresh_token,
                                 last_robot_email,
                                 proxy_id);
    }
    DoInitializeWithToken(gaia_auth_for_print->auth_token(),
                          proxy_id);
  } else {
    // Let the frontend know the of authentication failure.
    OnAuthError();
  }
}

void CloudPrintProxyBackend::Core::DoInitializeWithToken(
    const std::string cloud_print_token,
    const std::string& proxy_id) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  VLOG(1) << "CP_PROXY: Starting proxy, id: " << proxy_id;
  proxy_id_ = proxy_id;
  GetTokenStore()->SetToken(cloud_print_token, false);

  // We need to get the credentials of the robot here.
  GURL get_authcode_url =
      CloudPrintHelpers::GetUrlForGetAuthCode(cloud_print_server_url_,
                                              oauth_client_info_.client_id,
                                              proxy_id_);
  next_response_handler_ =
      &CloudPrintProxyBackend::Core::HandleGetAuthCodeResponse;
  request_ = new CloudPrintURLFetcher;
  request_->StartGetRequest(get_authcode_url,
                            this,
                            kCloudPrintAPIMaxRetryCount,
                            std::string());
}

void CloudPrintProxyBackend::Core::DoInitializeWithRobotToken(
    const std::string& robot_oauth_refresh_token,
    const std::string& robot_email,
    const std::string& proxy_id) {
  robot_email_ = robot_email;
  proxy_id_ = proxy_id;
  refresh_token_ = robot_oauth_refresh_token;
  RefreshAccessToken();
}

void CloudPrintProxyBackend::Core::DoInitializeWithRobotAuthCode(
    const std::string& robot_oauth_auth_code,
    const std::string& robot_email,
    const std::string& proxy_id) {
  robot_email_ = robot_email;
  proxy_id_ = proxy_id;
  // Now that we have an auth code we need to get the refresh and access tokens.
  oauth_client_.reset(new gaia::GaiaOAuthClient(
      gaia::kGaiaOAuth2Url,
      g_service_process->GetServiceURLRequestContextGetter()));
  oauth_client_->GetTokensFromAuthCode(oauth_client_info_,
                                       robot_oauth_auth_code,
                                       kCloudPrintAPIMaxRetryCount,
                                       this);
}

void CloudPrintProxyBackend::Core::PostAuthInitialization() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  // Now we can get down to registering printers.
  print_system_ =
      cloud_print::PrintSystem::CreateInstance(print_system_settings_.get());
  if (!print_system_.get()) {
    NOTREACHED();
    return;  // No print system available, fail initalization.
  }
  cloud_print::PrintSystem::PrintSystemResult result = print_system_->Init();

  if (result.succeeded()) {
    notifier::NotifierOptions notifier_options;
    notifier_options.request_context_getter =
        g_service_process->GetServiceURLRequestContextGetter();
    notifier_options.auth_mechanism = "X-OAUTH2";
    talk_mediator_.reset(new notifier::TalkMediatorImpl(
        new notifier::MediatorThreadImpl(notifier_options),
        notifier_options));
    notifier::Subscription subscription;
    subscription.channel = kCloudPrintPushNotificationsSource;
    subscription.from = kCloudPrintPushNotificationsSource;
    talk_mediator_->AddSubscription(subscription);
    talk_mediator_->SetDelegate(this);
    talk_mediator_->SetAuthToken(
        robot_email_,
        CloudPrintTokenStore::current()->token(),
        kSyncGaiaServiceId);
    talk_mediator_->Login();

    print_server_watcher_ = print_system_->CreatePrintServerWatcher();
    print_server_watcher_->StartWatching(this);

    StartRegistration();
  } else {
    // We could not initialize the print system. We need to notify the server.
    ReportUserMessage(
        kPrintSystemFailedMessageId,
        result.message(),
        &CloudPrintProxyBackend::Core::HandlePrintSystemUnavailableResponse);
  }
}

void CloudPrintProxyBackend::Core::StartRegistration() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  printer_list_.clear();
  cloud_print::PrintSystem::PrintSystemResult result =
      print_system_->EnumeratePrinters(&printer_list_);
  complete_list_available_ = result.succeeded();
  registration_in_progress_ = true;
  if (!result.succeeded()) {
    std::string message = result.message();
    if (message.empty())
      message = l10n_util::GetStringUTF8(IDS_CLOUD_PRINT_ENUM_FAILED);
    // There was a failure enumerating printers. Send a message to the server.
    ReportUserMessage(
        kEnumPrintersFailedMessageId,
        message,
        &CloudPrintProxyBackend::Core::HandleEnumPrintersFailedResponse);
  } else {
    // Now we need to ask the server about printers that were registered on the
    // server so that we can trim this list.
    GetRegisteredPrinters();
  }
}

void CloudPrintProxyBackend::Core::EndRegistration() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  request_ = NULL;
  registration_in_progress_ = false;
  if (new_printers_available_) {
    new_printers_available_ = false;
    StartRegistration();
  }
}

void CloudPrintProxyBackend::Core::DoShutdown() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  VLOG(1) << "CP_PROXY: Shutdown proxy, id: " << proxy_id_;
  if (print_server_watcher_ != NULL)
    print_server_watcher_->StopWatching();

  // Need to kill all running jobs.
  while (!job_handler_map_.empty()) {
    JobHandlerMap::iterator index = job_handler_map_.begin();
    // Shutdown will call our OnPrinterJobHandlerShutdown method which will
    // remove this from the map.
    index->second->Shutdown();
  }
  // Important to delete the TalkMediator on this thread.
  if (talk_mediator_.get())
    talk_mediator_->Logout();
  talk_mediator_.reset();
  notifications_enabled_ = false;
  notifications_enabled_since_ = base::TimeTicks();
  request_ = NULL;
  token_store_.reset();
}

void CloudPrintProxyBackend::Core::DoRegisterSelectedPrinters(
    const printing::PrinterList& printer_list) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  if (!print_system_.get())
    return;  // No print system available.
  printer_list_.assign(printer_list.begin(), printer_list.end());
  next_upload_index_ = 0;
  RegisterNextPrinter();
}

void CloudPrintProxyBackend::Core::GetRegisteredPrinters() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  GURL printer_list_url =
      CloudPrintHelpers::GetUrlForPrinterList(cloud_print_server_url_,
                                              proxy_id_);
  next_response_handler_ =
      &CloudPrintProxyBackend::Core::HandlePrinterListResponse;
  request_ = new CloudPrintURLFetcher;
  request_->StartGetRequest(printer_list_url,
                            this,
                            kCloudPrintAPIMaxRetryCount,
                            std::string());
}

void CloudPrintProxyBackend::Core::RegisterNextPrinter() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  // For the next printer to be uploaded, create a multi-part post request to
  // upload the printer capabilities and the printer defaults.
  if (next_upload_index_ < printer_list_.size()) {
    const printing::PrinterBasicInfo& info =
        printer_list_.at(next_upload_index_);
    // If we are retrying a previous upload, we don't need to fetch the caps
    // and defaults again.
    if (info.printer_name != last_uploaded_printer_name_) {
      cloud_print::PrintSystem::PrinterCapsAndDefaultsCallback* callback =
           NewCallback(this,
                       &CloudPrintProxyBackend::Core::OnReceivePrinterCaps);
        // Asnchronously fetch the printer caps and defaults. The story will
        // continue in OnReceivePrinterCaps.
        print_system_->GetPrinterCapsAndDefaults(
            info.printer_name.c_str(), callback);
    } else {
      OnReceivePrinterCaps(true,
                           last_uploaded_printer_name_,
                           last_uploaded_printer_info_);
    }
  } else {
    EndRegistration();
  }
}

void CloudPrintProxyBackend::Core::OnReceivePrinterCaps(
    bool succeeded,
    const std::string& printer_name,
    const printing::PrinterCapsAndDefaults& caps_and_defaults) {
  DCHECK(next_upload_index_ < printer_list_.size());
  if (succeeded) {
    const printing::PrinterBasicInfo& info =
        printer_list_.at(next_upload_index_);

    last_uploaded_printer_name_ = info.printer_name;
    last_uploaded_printer_info_ = caps_and_defaults;

    std::string mime_boundary;
    CloudPrintHelpers::CreateMimeBoundaryForUpload(&mime_boundary);
    std::string post_data;

    CloudPrintHelpers::AddMultipartValueForUpload(kProxyIdValue, proxy_id_,
                                                  mime_boundary,
                                                  std::string(), &post_data);
    CloudPrintHelpers::AddMultipartValueForUpload(kPrinterNameValue,
                                                  info.printer_name,
                                                  mime_boundary,
                                                  std::string(), &post_data);
    CloudPrintHelpers::AddMultipartValueForUpload(kPrinterDescValue,
                                                  info.printer_description,
                                                  mime_boundary,
                                                  std::string() , &post_data);
    CloudPrintHelpers::AddMultipartValueForUpload(
        kPrinterStatusValue, base::StringPrintf("%d", info.printer_status),
        mime_boundary, std::string(), &post_data);
    // Add printer options as tags.
    CloudPrintHelpers::GenerateMultipartPostDataForPrinterTags(info.options,
                                                               mime_boundary,
                                                               &post_data);

    CloudPrintHelpers::AddMultipartValueForUpload(
        kPrinterCapsValue, last_uploaded_printer_info_.printer_capabilities,
        mime_boundary, last_uploaded_printer_info_.caps_mime_type,
        &post_data);
    CloudPrintHelpers::AddMultipartValueForUpload(
        kPrinterDefaultsValue, last_uploaded_printer_info_.printer_defaults,
        mime_boundary, last_uploaded_printer_info_.defaults_mime_type,
        &post_data);
    // Send a hash of the printer capabilities to the server. We will use this
    // later to check if the capabilities have changed
    CloudPrintHelpers::AddMultipartValueForUpload(
        kPrinterCapsHashValue,
        MD5String(last_uploaded_printer_info_.printer_capabilities),
        mime_boundary, std::string(), &post_data);
    GURL post_url = CloudPrintHelpers::GetUrlForPrinterRegistration(
        cloud_print_server_url_);

    next_response_handler_ =
        &CloudPrintProxyBackend::Core::HandleRegisterPrinterResponse;
    // Terminate the request body
    post_data.append("--" + mime_boundary + "--\r\n");
    std::string mime_type("multipart/form-data; boundary=");
    mime_type += mime_boundary;
    request_ = new CloudPrintURLFetcher;
    request_->StartPostRequest(post_url,
                               this,
                               kCloudPrintAPIMaxRetryCount,
                               mime_type,
                               post_data,
                               std::string());
  } else {
    LOG(ERROR) << "CP_PROXY: Failed to get printer info for: " <<
        printer_name;
    // This printer failed to register, notify the server of this failure.
    string16 printer_name_utf16 = UTF8ToUTF16(printer_name);
    std::string status_message = l10n_util::GetStringFUTF8(
        IDS_CLOUD_PRINT_REGISTER_PRINTER_FAILED,
        printer_name_utf16);
    ReportUserMessage(
        kGetPrinterCapsFailedMessageId,
        status_message,
        &CloudPrintProxyBackend::Core::HandleRegisterFailedStatusResponse);
  }
}

void CloudPrintProxyBackend::Core::HandlePrinterNotification(
    const std::string& printer_id) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  VLOG(1) << "CP_PROXY: Handle printer notification, id: " << printer_id;
  JobHandlerMap::iterator index = job_handler_map_.find(printer_id);
  if (index != job_handler_map_.end())
    index->second->CheckForJobs(kJobFetchReasonNotified);
}

void CloudPrintProxyBackend::Core::PollForJobs() {
  VLOG(1) << "CP_PROXY: Polling for jobs.";
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  for (JobHandlerMap::iterator index = job_handler_map_.begin();
       index != job_handler_map_.end(); index++) {
    // If notifications are on, then we should poll for this printer only if
    // the last time it fetched jobs was before notifications were last enabled.
    bool should_poll =
        !notifications_enabled_ ||
        (index->second->last_job_fetch_time() <= notifications_enabled_since_);
    if (should_poll)
      index->second->CheckForJobs(kJobFetchReasonPoll);
  }
  job_poll_scheduled_ = false;
  // If we don't have notifications and job polling is enabled, poll again
  // after a while.
  if (!notifications_enabled_ && enable_job_poll_)
    ScheduleJobPoll();
}

void CloudPrintProxyBackend::Core::ScheduleJobPoll() {
  if (!job_poll_scheduled_) {
    int interval_in_seconds = base::RandInt(kMinJobPollIntervalSecs,
                                            kMaxJobPollIntervalSecs);
    MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        NewRunnableMethod(this, &CloudPrintProxyBackend::Core::PollForJobs),
        interval_in_seconds * 1000);
    job_poll_scheduled_ = true;
  }
}

CloudPrintTokenStore* CloudPrintProxyBackend::Core::GetTokenStore() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  if (!token_store_.get())
    token_store_.reset(new CloudPrintTokenStore);
  return token_store_.get();
}

// CloudPrintURLFetcher::Delegate implementation.
CloudPrintURLFetcher::ResponseAction
CloudPrintProxyBackend::Core::HandleJSONData(
    const URLFetcher* source,
    const GURL& url,
    DictionaryValue* json_data,
    bool succeeded) {
  DCHECK(next_response_handler_);
  return (this->*next_response_handler_)(source, url, json_data, succeeded);
}

void CloudPrintProxyBackend::Core::OnRequestAuthError() {
  OnAuthError();
}

void CloudPrintProxyBackend::Core::NotifyPrinterListAvailable(
    const printing::PrinterList& printer_list) {
  DCHECK(MessageLoop::current() == backend_->frontend_loop_);
  backend_->frontend_->OnPrinterListAvailable(printer_list);
}

void CloudPrintProxyBackend::Core::NotifyAuthenticated(
    const std::string& robot_oauth_refresh_token,
    const std::string& robot_email,
    const std::string& user_email) {
  DCHECK(MessageLoop::current() == backend_->frontend_loop_);
  backend_->frontend_->OnAuthenticated(robot_oauth_refresh_token,
                                       robot_email,
                                       user_email);
}

void CloudPrintProxyBackend::Core::NotifyAuthenticationFailed() {
  DCHECK(MessageLoop::current() == backend_->frontend_loop_);
  backend_->frontend_->OnAuthenticationFailed();
}

void CloudPrintProxyBackend::Core::NotifyPrintSystemUnavailable() {
  DCHECK(MessageLoop::current() == backend_->frontend_loop_);
  backend_->frontend_->OnPrintSystemUnavailable();
}

CloudPrintURLFetcher::ResponseAction
CloudPrintProxyBackend::Core::HandleGetAuthCodeResponse(
    const URLFetcher* source,
    const GURL& url,
    DictionaryValue* json_data,
    bool succeeded) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  if (!succeeded) {
    OnAuthError();
    return CloudPrintURLFetcher::STOP_PROCESSING;
  }
  std::string auth_code;
  if (!json_data->GetString(kOAuthCodeValue, &auth_code)) {
    OnAuthError();
    return CloudPrintURLFetcher::STOP_PROCESSING;
  }
  json_data->GetString(kXMPPJidValue, &robot_email_);
  // Now that we have an auth code we need to get the refresh and access tokens.
  oauth_client_.reset(new gaia::GaiaOAuthClient(
      gaia::kGaiaOAuth2Url,
      g_service_process->GetServiceURLRequestContextGetter()));
  oauth_client_->GetTokensFromAuthCode(oauth_client_info_,
                                       auth_code,
                                       kCloudPrintAPIMaxRetryCount,
                                       this);

  return CloudPrintURLFetcher::STOP_PROCESSING;
}

CloudPrintURLFetcher::ResponseAction
CloudPrintProxyBackend::Core::HandlePrinterListResponse(
    const URLFetcher* source,
    const GURL& url,
    DictionaryValue* json_data,
    bool succeeded) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  if (!succeeded) {
    NOTREACHED();
    return CloudPrintURLFetcher::RETRY_REQUEST;
  }
  ListValue* printer_list = NULL;
  json_data->GetList(kPrinterListValue, &printer_list);
  // There may be no "printers" value in the JSON
  if (printer_list) {
    for (size_t index = 0; index < printer_list->GetSize(); index++) {
      DictionaryValue* printer_data = NULL;
      if (printer_list->GetDictionary(index, &printer_data)) {
        std::string printer_name;
        printer_data->GetString(kNameValue, &printer_name);
        RemovePrinterFromList(printer_name);
        InitJobHandlerForPrinter(printer_data);
      } else {
        NOTREACHED();
      }
    }
  }
  request_ = NULL;
  if (!printer_list_.empty()) {
    // Let the frontend know that we have a list of printers available.
    backend_->frontend_loop_->PostTask(FROM_HERE, NewRunnableMethod(this,
        &Core::NotifyPrinterListAvailable, printer_list_));
  } else {
    // No more work to be done here.
    MessageLoop::current()->PostTask(
        FROM_HERE, NewRunnableMethod(this, &Core::EndRegistration));
  }
  return CloudPrintURLFetcher::STOP_PROCESSING;
}

void CloudPrintProxyBackend::Core::InitJobHandlerForPrinter(
    DictionaryValue* printer_data) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  DCHECK(printer_data);
  PrinterJobHandler::PrinterInfoFromCloud printer_info_cloud;
  printer_data->GetString(kIdValue, &printer_info_cloud.printer_id);
  DCHECK(!printer_info_cloud.printer_id.empty());
  VLOG(1) << "CP_PROXY: Init job handler for printer id: "
          << printer_info_cloud.printer_id;
  JobHandlerMap::iterator index = job_handler_map_.find(
      printer_info_cloud.printer_id);
  // We might already have a job handler for this printer
  if (index == job_handler_map_.end()) {
    printing::PrinterBasicInfo printer_info;
    printer_data->GetString(kNameValue, &printer_info.printer_name);
    DCHECK(!printer_info.printer_name.empty());
    printer_data->GetString(kPrinterDescValue,
                            &printer_info.printer_description);
    // Printer status is a string value which actually contains an integer.
    std::string printer_status;
    if (printer_data->GetString(kPrinterStatusValue, &printer_status)) {
      base::StringToInt(printer_status, &printer_info.printer_status);
    }
    printer_data->GetString(kPrinterCapsHashValue,
        &printer_info_cloud.caps_hash);
    ListValue* tags_list = NULL;
    printer_data->GetList(kTagsValue, &tags_list);
    if (tags_list) {
      for (size_t index = 0; index < tags_list->GetSize(); index++) {
        std::string tag;
        tags_list->GetString(index, &tag);
        if (StartsWithASCII(tag, kTagsHashTagName, false)) {
          std::vector<std::string> tag_parts;
          base::SplitStringDontTrim(tag, '=', &tag_parts);
          DCHECK_EQ(tag_parts.size(), 2U);
          if (tag_parts.size() == 2)
            printer_info_cloud.tags_hash = tag_parts[1];
        }
      }
    }
    scoped_refptr<PrinterJobHandler> job_handler;
    job_handler = new PrinterJobHandler(printer_info,
                                        printer_info_cloud,
                                        cloud_print_server_url_,
                                        print_system_.get(),
                                        this);
    job_handler_map_[printer_info_cloud.printer_id] = job_handler;
    job_handler->Initialize();
  }
}

void CloudPrintProxyBackend::Core::ReportUserMessage(
    const std::string& message_id,
    const std::string& failure_message,
    ResponseHandler handler) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  std::string mime_boundary;
  CloudPrintHelpers::CreateMimeBoundaryForUpload(&mime_boundary);
  GURL post_url = CloudPrintHelpers::GetUrlForUserMessage(
      cloud_print_server_url_,
      message_id);
  std::string post_data;
  CloudPrintHelpers::AddMultipartValueForUpload(kMessageTextValue,
                                                failure_message,
                                                mime_boundary,
                                                std::string(),
                                                &post_data);
  next_response_handler_ = handler;
  // Terminate the request body
  post_data.append("--" + mime_boundary + "--\r\n");
  std::string mime_type("multipart/form-data; boundary=");
  mime_type += mime_boundary;
  request_ = new CloudPrintURLFetcher;
  request_->StartPostRequest(post_url,
                             this,
                             kCloudPrintAPIMaxRetryCount,
                             mime_type,
                             post_data,
                             std::string());
}

CloudPrintURLFetcher::ResponseAction
CloudPrintProxyBackend::Core::HandleRegisterPrinterResponse(
    const URLFetcher* source,
    const GURL& url,
    DictionaryValue* json_data,
    bool succeeded) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  if (succeeded) {
    ListValue* printer_list = NULL;
    json_data->GetList(kPrinterListValue, &printer_list);
    // There should be a "printers" value in the JSON
    DCHECK(printer_list);
    if (printer_list) {
      DictionaryValue* printer_data = NULL;
      if (printer_list->GetDictionary(0, &printer_data))
        InitJobHandlerForPrinter(printer_data);
    }
  }
  next_upload_index_++;
  MessageLoop::current()->PostTask(
      FROM_HERE,
      NewRunnableMethod(this,
                        &CloudPrintProxyBackend::Core::RegisterNextPrinter));
  return CloudPrintURLFetcher::STOP_PROCESSING;
}

CloudPrintURLFetcher::ResponseAction
CloudPrintProxyBackend::Core::HandleRegisterFailedStatusResponse(
    const URLFetcher* source,
    const GURL& url,
    DictionaryValue* json_data,
    bool succeeded) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  next_upload_index_++;
  MessageLoop::current()->PostTask(
      FROM_HERE,
      NewRunnableMethod(this,
                        &CloudPrintProxyBackend::Core::RegisterNextPrinter));
  return CloudPrintURLFetcher::STOP_PROCESSING;
}

CloudPrintURLFetcher::ResponseAction
CloudPrintProxyBackend::Core::HandlePrintSystemUnavailableResponse(
    const URLFetcher* source,
    const GURL& url,
    DictionaryValue* json_data,
    bool succeeded) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  // Let the frontend know that we do not have a print system.
  backend_->frontend_loop_->PostTask(
      FROM_HERE,
      NewRunnableMethod(this,
                        &Core::NotifyPrintSystemUnavailable));
  return CloudPrintURLFetcher::STOP_PROCESSING;
}

CloudPrintURLFetcher::ResponseAction
CloudPrintProxyBackend::Core::HandleEnumPrintersFailedResponse(
    const URLFetcher* source,
    const GURL& url,
    DictionaryValue* json_data,
    bool succeeded) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  // Now proceed with printer registration.
  GetRegisteredPrinters();
  return CloudPrintURLFetcher::STOP_PROCESSING;
}


bool CloudPrintProxyBackend::Core::RemovePrinterFromList(
    const std::string& printer_name) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  for (printing::PrinterList::iterator index = printer_list_.begin();
       index != printer_list_.end(); index++) {
    if (0 == base::strcasecmp(index->printer_name.c_str(),
                              printer_name.c_str())) {
      index = printer_list_.erase(index);
      return true;
    }
  }
  return false;
}

void CloudPrintProxyBackend::Core::RefreshAccessToken() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  oauth_client_.reset(new gaia::GaiaOAuthClient(
      gaia::kGaiaOAuth2Url,
      g_service_process->GetServiceURLRequestContextGetter()));
  oauth_client_->RefreshToken(oauth_client_info_,
                              refresh_token_,
                              kCloudPrintAPIMaxRetryCount,
                              this);
}

void CloudPrintProxyBackend::Core::OnNotificationStateChange(
    bool notification_enabled) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  notifications_enabled_ = notification_enabled;
  if (notifications_enabled_) {
    notifications_enabled_since_ = base::TimeTicks::Now();
    VLOG(1) << "Notifications for proxy " << proxy_id_ << " were enabled at "
            << notifications_enabled_since_.ToInternalValue();
  } else {
    LOG(ERROR) << "Notifications for proxy " << proxy_id_ << " disabled.";
    notifications_enabled_since_ = base::TimeTicks();
  }
  // A state change means one of two cases.
  // Case 1: We just lost notifications. This this case we want to schedule a
  // job poll if enable_job_poll_ is true.
  // Case 2: Notifications just got re-enabled. In this case we want to schedule
  // a poll once for jobs we might have missed when we were dark.
  // Note that ScheduleJobPoll will not schedule again if a job poll task is
  // already scheduled.
  if (enable_job_poll_ || notifications_enabled_)
    ScheduleJobPoll();
}


void CloudPrintProxyBackend::Core::OnIncomingNotification(
    const notifier::Notification& notification) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  VLOG(1) << "CP_PROXY: Incoming notification.";
  if (0 == base::strcasecmp(kCloudPrintPushNotificationsSource,
                            notification.channel.c_str()))
    HandlePrinterNotification(notification.data);
}

void CloudPrintProxyBackend::Core::OnOutgoingNotification() {}

// cloud_print::PrinterChangeNotifier::Delegate implementation
void CloudPrintProxyBackend::Core::OnPrinterAdded() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  if (registration_in_progress_)
    new_printers_available_ = true;
  else
    StartRegistration();
}

// PrinterJobHandler::Delegate implementation
void CloudPrintProxyBackend::Core::OnPrinterJobHandlerShutdown(
    PrinterJobHandler* job_handler, const std::string& printer_id) {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  VLOG(1) << "CP_PROXY: Printer job handle shutdown, id " << printer_id;
  job_handler_map_.erase(printer_id);
}

void CloudPrintProxyBackend::Core::OnAuthError() {
  DCHECK(MessageLoop::current() == backend_->core_thread_.message_loop());
  VLOG(1) << "CP_PROXY: Auth Error";
  backend_->frontend_loop_->PostTask(FROM_HERE, NewRunnableMethod(this,
      &Core::NotifyAuthenticationFailed));
}

void CloudPrintProxyBackend::Core::OnPrinterNotFound(
    const std::string& printer_name,
    bool* delete_from_server) {
  // If we have a complete list of local printers, then this needs to be deleted
  // from the server.
  *delete_from_server = complete_list_available_;
}

  // gaia::GaiaOAuthClient::Delegate implementation.
void CloudPrintProxyBackend::Core::OnGetTokensResponse(
    const std::string& refresh_token,
    const std::string& access_token,
    int expires_in_seconds) {
  refresh_token_ = refresh_token;
  // After saving the refresh token, this is just like having just refreshed
  // the access token. Just call OnRefreshTokenResponse.
  OnRefreshTokenResponse(access_token, expires_in_seconds);
}

void CloudPrintProxyBackend::Core::OnRefreshTokenResponse(
    const std::string& access_token, int expires_in_seconds) {
  // If our current token is not OAuth, we either have no token at all or we
  // have a ClientLogin token which we just exchanged for an OAuth token.
  // In this case we need to do the startup initialiazation.
  // TODO(sanjeevr): Use an enum for state instead of using this as a signal.
  // I will do this in a follow-up change.
  CloudPrintTokenStore* token_store = GetTokenStore();
  bool first_time = !token_store->token_is_oauth();
  token_store->SetToken(access_token, true);
  // Let the frontend know that we have authenticated.
  backend_->frontend_loop_->PostTask(FROM_HERE, NewRunnableMethod(this,
      &Core::NotifyAuthenticated, refresh_token_, robot_email_, user_email_));
  if (first_time) {
    PostAuthInitialization();
  } else {
    // If we are refreshing a token, update the XMPP token too.
    DCHECK(talk_mediator_.get());
    talk_mediator_->SetAuthToken(robot_email_,
                                 access_token,
                                 kSyncGaiaServiceId);
  }
  // Schedule a task to refresh the access token again when it is about to
  // expire.
  DCHECK(expires_in_seconds > kTokenRefreshGracePeriodSecs);
  int64 refresh_delay =
      (expires_in_seconds - kTokenRefreshGracePeriodSecs)*1000;
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      NewRunnableMethod(this, &Core::RefreshAccessToken),
      refresh_delay);
}

void CloudPrintProxyBackend::Core::OnOAuthError() {
  OnAuthError();
}

void CloudPrintProxyBackend::Core::OnNetworkError(int response_code) {
  // Since we specify inifinite retries on network errors, this should never
  // be called.
  NOTREACHED() <<
      "OnNetworkError invoked when not expected, response code is " <<
      response_code;
}
