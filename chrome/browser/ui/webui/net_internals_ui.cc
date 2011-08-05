// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/net_internals_ui.h"

#include <algorithm>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/memory/singleton.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/string_piece.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/net/chrome_net_log.h"
#include "chrome/browser/net/connection_tester.h"
#include "chrome/browser/net/passive_log_collector.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/prefs/pref_member.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/shell_dialogs.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/browser/browser_thread.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/tab_contents_view.h"
#include "content/common/notification_details.h"
#include "grit/generated_resources.h"
#include "grit/net_internals_resources.h"
#include "net/base/escape.h"
#include "net/base/host_resolver_impl.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"
#include "net/base/sys_addrinfo.h"
#include "net/base/x509_cert_types.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_alternate_protocols.h"
#include "net/http/http_cache.h"
#include "net/http/http_network_layer.h"
#include "net/http/http_network_session.h"
#include "net/http/http_stream_factory.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

#ifdef OS_CHROMEOS
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/syslogs_library.h"
#endif
#ifdef OS_WIN
#include "chrome/browser/net/service_providers_win.h"
#endif

namespace {

// Delay between when an event occurs and when it is passed to the Javascript
// page.  All events that occur during this period are grouped together and
// sent to the page at once, which reduces context switching and CPU usage.
const int kNetLogEventDelayMilliseconds = 100;

// Returns the HostCache for |context|'s primary HostResolver, or NULL if
// there is none.
net::HostCache* GetHostResolverCache(net::URLRequestContext* context) {
  net::HostResolverImpl* host_resolver_impl =
      context->host_resolver()->GetAsHostResolverImpl();

  if (!host_resolver_impl)
    return NULL;

  return host_resolver_impl->cache();
}

// Returns the disk cache backend for |context| if there is one, or NULL.
disk_cache::Backend* GetDiskCacheBackend(net::URLRequestContext* context) {
  if (!context->http_transaction_factory())
    return NULL;

  net::HttpCache* http_cache = context->http_transaction_factory()->GetCache();
  if (!http_cache)
    return NULL;

  return http_cache->GetCurrentBackend();
}

// Returns the http network session for |context| if there is one.
// Otherwise, returns NULL.
net::HttpNetworkSession* GetHttpNetworkSession(
    net::URLRequestContext* context) {
  if (!context->http_transaction_factory())
    return NULL;

  return context->http_transaction_factory()->GetSession();
}

Value* ExperimentToValue(const ConnectionTester::Experiment& experiment) {
  DictionaryValue* dict = new DictionaryValue();

  if (experiment.url.is_valid())
    dict->SetString("url", experiment.url.spec());

  dict->SetString("proxy_settings_experiment",
                  ConnectionTester::ProxySettingsExperimentDescription(
                      experiment.proxy_settings_experiment));
  dict->SetString("host_resolver_experiment",
                  ConnectionTester::HostResolverExperimentDescription(
                      experiment.host_resolver_experiment));
  return dict;
}

class NetInternalsHTMLSource : public ChromeURLDataManager::DataSource {
 public:
  NetInternalsHTMLSource();

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id);
  virtual std::string GetMimeType(const std::string&) const;

 private:
  ~NetInternalsHTMLSource() {}
  DISALLOW_COPY_AND_ASSIGN(NetInternalsHTMLSource);
};

// This class receives javascript messages from the renderer.
// Note that the WebUI infrastructure runs on the UI thread, therefore all of
// this class's methods are expected to run on the UI thread.
//
// Since the network code we want to run lives on the IO thread, we proxy
// almost everything over to NetInternalsMessageHandler::IOThreadImpl, which
// runs on the IO thread.
//
// TODO(eroman): Can we start on the IO thread to begin with?
class NetInternalsMessageHandler
    : public WebUIMessageHandler,
      public SelectFileDialog::Listener,
      public base::SupportsWeakPtr<NetInternalsMessageHandler>,
      public NotificationObserver {
 public:
  NetInternalsMessageHandler();
  virtual ~NetInternalsMessageHandler();

  // WebUIMessageHandler implementation.
  virtual WebUIMessageHandler* Attach(WebUI* web_ui);
  virtual void RegisterMessages();

  // Executes the javascript function |function_name| in the renderer, passing
  // it the argument |value|.
  void CallJavascriptFunction(const std::wstring& function_name,
                              const Value* value);

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // Javascript message handlers.
  void OnRendererReady(const ListValue* list);
  void OnEnableHttpThrottling(const ListValue* list);
#ifdef OS_CHROMEOS
  void OnRefreshSystemLogs(const ListValue* list);
  void OnGetSystemLog(const ListValue* list);
#endif

  // SelectFileDialog::Listener implementation
  virtual void FileSelected(const FilePath& path, int index, void* params);
  virtual void FileSelectionCanceled(void* params);

  // The only callback handled on the UI thread.  As it needs to access fields
  // from |web_ui_|, it can't be called on the IO thread.
  void OnLoadLogFile(const ListValue* list);

 private:
  class IOThreadImpl;

  // Task run on the FILE thread to read the contents of a log file.  The result
  // is then passed to IOThreadImpl's CallJavascriptFunction, which sends it
  // back to the web page.  IOThreadImpl is used instead of the
  // NetInternalsMessageHandler directly because it checks if the message
  // handler has been destroyed in the meantime.
  class ReadLogFileTask : public Task {
   public:
    ReadLogFileTask(IOThreadImpl* proxy, const FilePath& path);

    virtual void Run();

   private:
    // IOThreadImpl implements existence checks already.  Simpler to reused them
    // then to reimplement them.
    scoped_refptr<IOThreadImpl> proxy_;

    // Path of the file to open.
    const FilePath path_;
  };

#ifdef OS_CHROMEOS
  // Class that is used for getting network related ChromeOS logs.
  // Logs are fetched from ChromeOS libcros on user request, and only when we
  // don't yet have a copy of logs. If a copy is present, we send back data from
  // it, else we save request and answer to it when we get logs from libcros.
  // If needed, we also send request for system logs to libcros.
  // Logs refresh has to be done explicitly, by deleting old logs and then
  // loading them again.
  class SystemLogsGetter {
   public:
    SystemLogsGetter(NetInternalsMessageHandler* handler,
                     chromeos::SyslogsLibrary* syslog_lib);
    ~SystemLogsGetter();

    // Deletes logs copy we currently have, and resets logs_requested and
    // logs_received flags.
    void DeleteSystemLogs();
    // Starts log fetching. If logs copy is present, requested logs are sent
    // back.
    // If syslogs load request hasn't been sent to libcros yet, we do that now,
    // and postpone sending response.
    // Request data is specified by args:
    //   $1 : key of the log we are interested in.
    //   $2 : string used to identify request.
    void RequestSystemLog(const ListValue* args);
    // Requests logs from libcros, but only if we don't have a copy.
    void LoadSystemLogs();
    // Processes callback from libcros containing system logs. Postponed
    // request responses are sent.
    void OnSystemLogsLoaded(chromeos::LogDictionaryType* sys_info,
                            std::string* ignored_content);

   private:
    // Struct we save postponed log request in.
    struct SystemLogRequest {
      std::string log_key;
      std::string cell_id;
    };

    // Processes request.
    void SendLogs(const SystemLogRequest& request);

    NetInternalsMessageHandler* handler_;
    chromeos::SyslogsLibrary* syslogs_library_;
    // List of postponed requests.
    std::list<SystemLogRequest> requests_;
    scoped_ptr<chromeos::LogDictionaryType> logs_;
    bool logs_received_;
    bool logs_requested_;
    CancelableRequestConsumer consumer_;
    // Libcros request handle.
    CancelableRequestProvider::Handle syslogs_request_id_;
  };
#endif

  // The pref member about whether HTTP throttling is enabled, which needs to
  // be accessed on the UI thread.
  BooleanPrefMember http_throttling_enabled_;

  // OnRendererReady invokes this callback to do the part of message handling
  // that needs to happen on the IO thread.
  scoped_ptr<WebUI::MessageCallback> renderer_ready_io_callback_;

  // This is the "real" message handler, which lives on the IO thread.
  scoped_refptr<IOThreadImpl> proxy_;

#ifdef OS_CHROMEOS
  // Class that handles getting and filtering system logs.
  scoped_ptr<SystemLogsGetter> syslogs_getter_;
#endif

  // Used for loading log files.
  scoped_refptr<SelectFileDialog> select_log_file_dialog_;

  DISALLOW_COPY_AND_ASSIGN(NetInternalsMessageHandler);
};

// This class is the "real" message handler. It is allocated and destroyed on
// the UI thread.  With the exception of OnAddEntry, OnWebUIDeleted, and
// CallJavascriptFunction, its methods are all expected to be called from the IO
// thread.  OnAddEntry and CallJavascriptFunction can be called from any thread,
// and OnWebUIDeleted can only be called from the UI thread.
class NetInternalsMessageHandler::IOThreadImpl
    : public base::RefCountedThreadSafe<
          NetInternalsMessageHandler::IOThreadImpl,
          BrowserThread::DeleteOnUIThread>,
      public ChromeNetLog::ThreadSafeObserver,
      public ConnectionTester::Delegate {
 public:
  // Type for methods that can be used as MessageHandler callbacks.
  typedef void (IOThreadImpl::*MessageHandler)(const ListValue*);

  // Creates a proxy for |handler| that will live on the IO thread.
  // |handler| is a weak pointer, since it is possible for the
  // WebUIMessageHandler to be deleted on the UI thread while we were executing
  // on the IO thread. |io_thread| is the global IOThread (it is passed in as
  // an argument since we need to grab it from the UI thread).
  IOThreadImpl(
      const base::WeakPtr<NetInternalsMessageHandler>& handler,
      IOThread* io_thread,
      net::URLRequestContextGetter* context_getter);

  // Creates a callback that will run |method| on the IO thread.
  //
  // This can be used with WebUI::RegisterMessageCallback() to bind to a method
  // on the IO thread.
  WebUI::MessageCallback* CreateCallback(MessageHandler method);

  // Called once the WebUI has been deleted (i.e. renderer went away), on the
  // IO thread.
  void Detach();

  // Sends all passive log entries in |passive_entries| to the Javascript
  // handler, called on the IO thread.
  void SendPassiveLogEntries(const ChromeNetLog::EntryList& passive_entries);

  // Called when the WebUI is deleted.  Prevents calling Javascript functions
  // afterwards.  Called on UI thread.
  void OnWebUIDeleted();

  //--------------------------------
  // Javascript message handlers:
  //--------------------------------

  void OnRendererReady(const ListValue* list);

  void OnGetProxySettings(const ListValue* list);
  void OnReloadProxySettings(const ListValue* list);
  void OnGetBadProxies(const ListValue* list);
  void OnClearBadProxies(const ListValue* list);
  void OnGetHostResolverInfo(const ListValue* list);
  void OnClearHostResolverCache(const ListValue* list);
  void OnEnableIPv6(const ListValue* list);
  void OnStartConnectionTests(const ListValue* list);
  void OnHSTSQuery(const ListValue* list);
  void OnHSTSAdd(const ListValue* list);
  void OnHSTSDelete(const ListValue* list);
  void OnGetHttpCacheInfo(const ListValue* list);
  void OnGetSocketPoolInfo(const ListValue* list);
  void OnCloseIdleSockets(const ListValue* list);
  void OnFlushSocketPools(const ListValue* list);
  void OnGetSpdySessionInfo(const ListValue* list);
  void OnGetSpdyStatus(const ListValue* list);
  void OnGetSpdyAlternateProtocolMappings(const ListValue* list);
#ifdef OS_WIN
  void OnGetServiceProviders(const ListValue* list);
#endif
  void OnSetLogLevel(const ListValue* list);

  // ChromeNetLog::ThreadSafeObserver implementation:
  virtual void OnAddEntry(net::NetLog::EventType type,
                          const base::TimeTicks& time,
                          const net::NetLog::Source& source,
                          net::NetLog::EventPhase phase,
                          net::NetLog::EventParameters* params);

  // ConnectionTester::Delegate implementation:
  virtual void OnStartConnectionTestSuite();
  virtual void OnStartConnectionTestExperiment(
      const ConnectionTester::Experiment& experiment);
  virtual void OnCompletedConnectionTestExperiment(
      const ConnectionTester::Experiment& experiment,
      int result);
  virtual void OnCompletedConnectionTestSuite();

  // Helper that executes |function_name| in the attached renderer.
  // The function takes ownership of |arg|.  Note that this can be called from
  // any thread.
  void CallJavascriptFunction(const std::wstring& function_name, Value* arg);

 private:
  friend struct BrowserThread::DeleteOnThread<BrowserThread::UI>;
  friend class DeleteTask<IOThreadImpl>;

  ~IOThreadImpl();

  class CallbackHelper;

  // Helper that runs |method| with |arg|, and deletes |arg| on completion.
  void DispatchToMessageHandler(ListValue* arg, MessageHandler method);

  // Adds |entry| to the queue of pending log entries to be sent to the page via
  // Javascript.  Must be called on the IO Thread.  Also creates a delayed task
  // that will call PostPendingEntries, if there isn't one already.
  void AddEntryToQueue(Value* entry);

  // Sends all pending entries to the page via Javascript, and clears the list
  // of pending entries.  Sending multiple entries at once results in a
  // significant reduction of CPU usage when a lot of events are happening.
  // Must be called on the IO Thread.
  void PostPendingEntries();

  // Pointer to the UI-thread message handler. Only access this from
  // the UI thread.
  base::WeakPtr<NetInternalsMessageHandler> handler_;

  // The global IOThread, which contains the global NetLog to observer.
  IOThread* io_thread_;

  scoped_refptr<net::URLRequestContextGetter> context_getter_;

  // Helper that runs the suite of connection tests.
  scoped_ptr<ConnectionTester> connection_tester_;

  // True if the Web UI has been deleted.  This is used to prevent calling
  // Javascript functions after the Web UI is destroyed.  On refresh, the
  // messages can end up being sent to the refreshed page, causing duplicate
  // or partial entries.
  //
  // This is only read and written to on the UI thread.
  bool was_webui_deleted_;

  // True if we have attached an observer to the NetLog already.
  bool is_observing_log_;

  // Log entries that have yet to be passed along to Javascript page.  Non-NULL
  // when and only when there is a pending delayed task to call
  // PostPendingEntries.  Read and written to exclusively on the IO Thread.
  scoped_ptr<ListValue> pending_entries_;
};

// Helper class for a WebUI::MessageCallback which when executed calls
// instance->*method(value) on the IO thread.
class NetInternalsMessageHandler::IOThreadImpl::CallbackHelper
    : public WebUI::MessageCallback {
 public:
  CallbackHelper(IOThreadImpl* instance, IOThreadImpl::MessageHandler method)
      : instance_(instance),
        method_(method) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  }

  virtual void RunWithParams(const Tuple1<const ListValue*>& params) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    // We need to make a copy of the value in order to pass it over to the IO
    // thread. We will delete this in IOThreadImpl::DispatchMessageHandler().
    ListValue* list_copy = static_cast<ListValue*>(
        params.a ? params.a->DeepCopy() : NULL);

    if (!BrowserThread::PostTask(
            BrowserThread::IO, FROM_HERE,
            NewRunnableMethod(instance_.get(),
                              &IOThreadImpl::DispatchToMessageHandler,
                              list_copy, method_))) {
      // Failed posting the task, avoid leaking |list_copy|.
      delete list_copy;
    }
  }

 private:
  scoped_refptr<IOThreadImpl> instance_;
  IOThreadImpl::MessageHandler method_;
};

////////////////////////////////////////////////////////////////////////////////
//
// NetInternalsHTMLSource
//
////////////////////////////////////////////////////////////////////////////////

NetInternalsHTMLSource::NetInternalsHTMLSource()
    : DataSource(chrome::kChromeUINetInternalsHost, MessageLoop::current()) {
}

void NetInternalsHTMLSource::StartDataRequest(const std::string& path,
                                              bool is_incognito,
                                              int request_id) {
  DictionaryValue localized_strings;
  SetFontAndTextDirection(&localized_strings);

  // The provided "path" may contain a fragment, or query section. We only
  // care about the path itself, and will disregard anything else.
  std::string filename =
      GURL(std::string("chrome://net/") + path).path().substr(1);

  // The source for the net internals page is flattened during compilation, so
  // the only resource that should legitimately be requested is the main file.
  // Note that users can type anything into the address bar, though, so we must
  // handle arbitrary input.
  if (filename.empty() || filename == "index.html") {
    base::StringPiece html(
        ResourceBundle::GetSharedInstance().GetRawDataResource(
            IDR_NET_INTERNALS_INDEX_HTML));
    std::string full_html(html.data(), html.size());
    jstemplate_builder::AppendJsonHtml(&localized_strings, &full_html);
    jstemplate_builder::AppendI18nTemplateSourceHtml(&full_html);
    jstemplate_builder::AppendI18nTemplateProcessHtml(&full_html);
    jstemplate_builder::AppendJsTemplateSourceHtml(&full_html);

    scoped_refptr<RefCountedBytes> html_bytes(new RefCountedBytes);
    html_bytes->data.resize(full_html.size());
    std::copy(full_html.begin(), full_html.end(), html_bytes->data.begin());
    SendResponse(request_id, html_bytes);
    return;
  }

  const std::string data_string("<p style='color:red'>Failed to read resource" +
      EscapeForHTML(filename) + "</p>");
  scoped_refptr<RefCountedBytes> bytes(new RefCountedBytes);
  bytes->data.resize(data_string.size());
  std::copy(data_string.begin(), data_string.end(), bytes->data.begin());
  SendResponse(request_id, bytes);
}

std::string NetInternalsHTMLSource::GetMimeType(const std::string&) const {
  return "text/html";
}

////////////////////////////////////////////////////////////////////////////////
//
// NetInternalsMessageHandler
//
////////////////////////////////////////////////////////////////////////////////

NetInternalsMessageHandler::NetInternalsMessageHandler() {}

NetInternalsMessageHandler::~NetInternalsMessageHandler() {
  if (proxy_) {
    proxy_.get()->OnWebUIDeleted();
    // Notify the handler on the IO thread that the renderer is gone.
    BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
        NewRunnableMethod(proxy_.get(), &IOThreadImpl::Detach));
  }
  if (select_log_file_dialog_)
    select_log_file_dialog_->ListenerDestroyed();
}

WebUIMessageHandler* NetInternalsMessageHandler::Attach(WebUI* web_ui) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  PrefService* pref_service = web_ui->GetProfile()->GetPrefs();
  http_throttling_enabled_.Init(prefs::kHttpThrottlingEnabled, pref_service,
                                this);

  proxy_ = new IOThreadImpl(this->AsWeakPtr(), g_browser_process->io_thread(),
                            web_ui->GetProfile()->GetRequestContext());
#ifdef OS_CHROMEOS
  syslogs_getter_.reset(new SystemLogsGetter(this,
      chromeos::CrosLibrary::Get()->GetSyslogsLibrary()));
#endif
  renderer_ready_io_callback_.reset(
      proxy_->CreateCallback(&IOThreadImpl::OnRendererReady));

  WebUIMessageHandler* result = WebUIMessageHandler::Attach(web_ui);
  return result;
}

void NetInternalsMessageHandler::FileSelected(
    const FilePath& path, int index, void* params) {
  select_log_file_dialog_.release();
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      new ReadLogFileTask(proxy_.get(), path));
}

void NetInternalsMessageHandler::FileSelectionCanceled(void* params) {
  select_log_file_dialog_.release();
}

void NetInternalsMessageHandler::OnLoadLogFile(const ListValue* list) {
  // Only allow a single dialog at a time.
  if (select_log_file_dialog_.get())
    return;
  select_log_file_dialog_ = SelectFileDialog::Create(this);
  select_log_file_dialog_->SelectFile(
      SelectFileDialog::SELECT_OPEN_FILE, string16(), FilePath(), NULL, 0,
      FILE_PATH_LITERAL(""), web_ui_->tab_contents(),
      web_ui_->tab_contents()->view()->GetTopLevelNativeWindow(), NULL);
}

void NetInternalsMessageHandler::RegisterMessages() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Only callback handled on UI thread.
  web_ui_->RegisterMessageCallback(
      "loadLogFile",
      NewCallback(this, &NetInternalsMessageHandler::OnLoadLogFile));

  web_ui_->RegisterMessageCallback(
      "notifyReady",
      NewCallback(this, &NetInternalsMessageHandler::OnRendererReady));
  web_ui_->RegisterMessageCallback(
      "getProxySettings",
      proxy_->CreateCallback(&IOThreadImpl::OnGetProxySettings));
  web_ui_->RegisterMessageCallback(
      "reloadProxySettings",
      proxy_->CreateCallback(&IOThreadImpl::OnReloadProxySettings));
  web_ui_->RegisterMessageCallback(
      "getBadProxies",
      proxy_->CreateCallback(&IOThreadImpl::OnGetBadProxies));
  web_ui_->RegisterMessageCallback(
      "clearBadProxies",
      proxy_->CreateCallback(&IOThreadImpl::OnClearBadProxies));
  web_ui_->RegisterMessageCallback(
      "getHostResolverInfo",
      proxy_->CreateCallback(&IOThreadImpl::OnGetHostResolverInfo));
  web_ui_->RegisterMessageCallback(
      "clearHostResolverCache",
      proxy_->CreateCallback(&IOThreadImpl::OnClearHostResolverCache));
  web_ui_->RegisterMessageCallback(
      "enableIPv6",
      proxy_->CreateCallback(&IOThreadImpl::OnEnableIPv6));
  web_ui_->RegisterMessageCallback(
      "startConnectionTests",
      proxy_->CreateCallback(&IOThreadImpl::OnStartConnectionTests));
  web_ui_->RegisterMessageCallback(
      "hstsQuery",
      proxy_->CreateCallback(&IOThreadImpl::OnHSTSQuery));
  web_ui_->RegisterMessageCallback(
      "hstsAdd",
      proxy_->CreateCallback(&IOThreadImpl::OnHSTSAdd));
  web_ui_->RegisterMessageCallback(
      "hstsDelete",
      proxy_->CreateCallback(&IOThreadImpl::OnHSTSDelete));
  web_ui_->RegisterMessageCallback(
      "getHttpCacheInfo",
      proxy_->CreateCallback(&IOThreadImpl::OnGetHttpCacheInfo));
  web_ui_->RegisterMessageCallback(
      "getSocketPoolInfo",
      proxy_->CreateCallback(&IOThreadImpl::OnGetSocketPoolInfo));
  web_ui_->RegisterMessageCallback(
      "closeIdleSockets",
      proxy_->CreateCallback(&IOThreadImpl::OnCloseIdleSockets));
  web_ui_->RegisterMessageCallback(
      "flushSocketPools",
      proxy_->CreateCallback(&IOThreadImpl::OnFlushSocketPools));
  web_ui_->RegisterMessageCallback(
      "getSpdySessionInfo",
      proxy_->CreateCallback(&IOThreadImpl::OnGetSpdySessionInfo));
  web_ui_->RegisterMessageCallback(
      "getSpdyStatus",
      proxy_->CreateCallback(&IOThreadImpl::OnGetSpdyStatus));
  web_ui_->RegisterMessageCallback(
      "getSpdyAlternateProtocolMappings",
      proxy_->CreateCallback(
          &IOThreadImpl::OnGetSpdyAlternateProtocolMappings));
#ifdef OS_WIN
  web_ui_->RegisterMessageCallback(
      "getServiceProviders",
      proxy_->CreateCallback(&IOThreadImpl::OnGetServiceProviders));
#endif
#ifdef OS_CHROMEOS
  web_ui_->RegisterMessageCallback(
      "refreshSystemLogs",
      NewCallback(this, &NetInternalsMessageHandler::OnRefreshSystemLogs));
  web_ui_->RegisterMessageCallback(
      "getSystemLog",
      NewCallback(this, &NetInternalsMessageHandler::OnGetSystemLog));
#endif
  web_ui_->RegisterMessageCallback(
      "setLogLevel",
      proxy_->CreateCallback(&IOThreadImpl::OnSetLogLevel));

  web_ui_->RegisterMessageCallback(
      "enableHttpThrottling",
      NewCallback(this, &NetInternalsMessageHandler::OnEnableHttpThrottling));
}

void NetInternalsMessageHandler::CallJavascriptFunction(
    const std::wstring& function_name,
    const Value* value) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (value) {
    web_ui_->CallJavascriptFunction(WideToASCII(function_name), *value);
  } else {
    web_ui_->CallJavascriptFunction(WideToASCII(function_name));
  }
}

void NetInternalsMessageHandler::Observe(NotificationType type,
                                         const NotificationSource& source,
                                         const NotificationDetails& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_EQ(type.value, NotificationType::PREF_CHANGED);

  std::string* pref_name = Details<std::string>(details).ptr();
  if (*pref_name == prefs::kHttpThrottlingEnabled) {
    scoped_ptr<Value> enabled(
        Value::CreateBooleanValue(*http_throttling_enabled_));

    CallJavascriptFunction(
        L"g_browser.receivedHttpThrottlingEnabledPrefChanged", enabled.get());
  }
}

void NetInternalsMessageHandler::OnRendererReady(const ListValue* list) {
  CHECK(renderer_ready_io_callback_.get());
  renderer_ready_io_callback_->Run(list);

  scoped_ptr<Value> enabled(
      Value::CreateBooleanValue(*http_throttling_enabled_));
  CallJavascriptFunction(
      L"g_browser.receivedHttpThrottlingEnabledPrefChanged", enabled.get());
}

void NetInternalsMessageHandler::OnEnableHttpThrottling(const ListValue* list) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  bool enable = false;
  if (!list->GetBoolean(0, &enable)) {
    NOTREACHED();
    return;
  }

  http_throttling_enabled_.SetValue(enable);
}

////////////////////////////////////////////////////////////////////////////////
//
// NetInternalsMessageHandler::ReadLogFileTask
//
////////////////////////////////////////////////////////////////////////////////

NetInternalsMessageHandler::ReadLogFileTask::ReadLogFileTask(
    IOThreadImpl* proxy, const FilePath& path)
    : proxy_(proxy), path_(path) {
}

void NetInternalsMessageHandler::ReadLogFileTask::Run() {
  std::string file_contents;
  if (!file_util::ReadFileToString(path_, &file_contents))
    return;
  proxy_->CallJavascriptFunction(L"g_browser.loadedLogFile",
                                 new StringValue(file_contents));
}

#ifdef OS_CHROMEOS
////////////////////////////////////////////////////////////////////////////////
//
// NetInternalsMessageHandler::SystemLogsGetter
//
////////////////////////////////////////////////////////////////////////////////

NetInternalsMessageHandler::SystemLogsGetter::SystemLogsGetter(
    NetInternalsMessageHandler* handler,
    chromeos::SyslogsLibrary* syslog_lib)
    : handler_(handler),
      syslogs_library_(syslog_lib),
      logs_(NULL),
      logs_received_(false),
      logs_requested_(false) {
  if (!syslogs_library_)
    LOG(ERROR) << "System logs library not loaded";
}

NetInternalsMessageHandler::SystemLogsGetter::~SystemLogsGetter() {
  DeleteSystemLogs();
}

void NetInternalsMessageHandler::SystemLogsGetter::DeleteSystemLogs() {
  if (syslogs_library_ && logs_requested_ && !logs_received_) {
    syslogs_library_->CancelRequest(syslogs_request_id_);
  }
  logs_requested_ = false;
  logs_received_ = false;
  logs_.reset();
}

void NetInternalsMessageHandler::SystemLogsGetter::RequestSystemLog(
    const ListValue* args) {
  if (!logs_requested_) {
    DCHECK(!logs_received_);
    LoadSystemLogs();
  }
  SystemLogRequest log_request;
  args->GetString(0, &log_request.log_key);
  args->GetString(1, &log_request.cell_id);

  if (logs_received_) {
    SendLogs(log_request);
  } else {
    requests_.push_back(log_request);
  }
}

void NetInternalsMessageHandler::SystemLogsGetter::LoadSystemLogs() {
  if (logs_requested_ || !syslogs_library_)
    return;
  logs_requested_ = true;
  syslogs_request_id_ = syslogs_library_->RequestSyslogs(
      false,  // compress logs.
      chromeos::SyslogsLibrary::SYSLOGS_NETWORK,
      &consumer_,
      NewCallback(
          this,
          &NetInternalsMessageHandler::SystemLogsGetter::OnSystemLogsLoaded));
}

void NetInternalsMessageHandler::SystemLogsGetter::OnSystemLogsLoaded(
    chromeos::LogDictionaryType* sys_info, std::string* ignored_content) {
  DCHECK(!ignored_content);
  logs_.reset(sys_info);
  logs_received_ = true;
  for (std::list<SystemLogRequest>::iterator request_it = requests_.begin();
       request_it != requests_.end();
       ++request_it) {
    SendLogs(*request_it);
  }
  requests_.clear();
}

void NetInternalsMessageHandler::SystemLogsGetter::SendLogs(
    const SystemLogRequest& request) {
  scoped_ptr<DictionaryValue> result(new DictionaryValue());
  chromeos::LogDictionaryType::iterator log_it = logs_->find(request.log_key);
  if (log_it != logs_->end()) {
    if (!log_it->second.empty()) {
      result->SetString("log", log_it->second);
    } else {
      result->SetString("log", "<no relevant lines found>");
    }
  } else {
    result->SetString("log", "<invalid log name>");
  }
  result->SetString("cellId", request.cell_id);

  handler_->CallJavascriptFunction(L"g_browser.getSystemLogCallback",
                                   result.get());
}
#endif
////////////////////////////////////////////////////////////////////////////////
//
// NetInternalsMessageHandler::IOThreadImpl
//
////////////////////////////////////////////////////////////////////////////////

NetInternalsMessageHandler::IOThreadImpl::IOThreadImpl(
    const base::WeakPtr<NetInternalsMessageHandler>& handler,
    IOThread* io_thread,
    net::URLRequestContextGetter* context_getter)
    : ThreadSafeObserver(net::NetLog::LOG_ALL_BUT_BYTES),
      handler_(handler),
      io_thread_(io_thread),
      context_getter_(context_getter),
      was_webui_deleted_(false),
      is_observing_log_(false) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

NetInternalsMessageHandler::IOThreadImpl::~IOThreadImpl() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

WebUI::MessageCallback*
NetInternalsMessageHandler::IOThreadImpl::CreateCallback(
    MessageHandler method) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return new CallbackHelper(this, method);
}

void NetInternalsMessageHandler::IOThreadImpl::Detach() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  // Unregister with network stack to observe events.
  if (is_observing_log_)
    io_thread_->net_log()->RemoveObserver(this);

  // Cancel any in-progress connection tests.
  connection_tester_.reset();
}

void NetInternalsMessageHandler::IOThreadImpl::SendPassiveLogEntries(
    const ChromeNetLog::EntryList& passive_entries) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  ListValue* dict_list = new ListValue();
  for (size_t i = 0; i < passive_entries.size(); ++i) {
    const ChromeNetLog::Entry& e = passive_entries[i];
    dict_list->Append(net::NetLog::EntryToDictionaryValue(e.type,
                                                          e.time,
                                                          e.source,
                                                          e.phase,
                                                          e.params,
                                                          false));
  }

  CallJavascriptFunction(L"g_browser.receivedPassiveLogEntries", dict_list);
}

void NetInternalsMessageHandler::IOThreadImpl::OnWebUIDeleted() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  was_webui_deleted_ = true;
}

void NetInternalsMessageHandler::IOThreadImpl::OnRendererReady(
    const ListValue* list) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!is_observing_log_) << "notifyReady called twice";

  // Tell the javascript about the relationship between event type enums and
  // their symbolic name.
  {
    std::vector<net::NetLog::EventType> event_types =
        net::NetLog::GetAllEventTypes();

    DictionaryValue* dict = new DictionaryValue();

    for (size_t i = 0; i < event_types.size(); ++i) {
      const char* name = net::NetLog::EventTypeToString(event_types[i]);
      dict->SetInteger(name, static_cast<int>(event_types[i]));
    }

    CallJavascriptFunction(L"g_browser.receivedLogEventTypeConstants", dict);
  }

  // Tell the javascript about the version of the client and its
  // command line arguments.
  {
    DictionaryValue* dict = new DictionaryValue();

    chrome::VersionInfo version_info;

    if (!version_info.is_valid()) {
      DLOG(ERROR) << "Unable to create chrome::VersionInfo";
    } else {
      // We have everything we need to send the right values.
      dict->SetString("version", version_info.Version());
      dict->SetString("cl", version_info.LastChange());
      dict->SetString("version_mod",
                      platform_util::GetVersionStringModifier());
      dict->SetString("official",
          l10n_util::GetStringUTF16(
              version_info.IsOfficialBuild() ?
                IDS_ABOUT_VERSION_OFFICIAL
              : IDS_ABOUT_VERSION_UNOFFICIAL));

      dict->SetString("command_line",
          CommandLine::ForCurrentProcess()->command_line_string());
    }

    CallJavascriptFunction(L"g_browser.receivedClientInfo",
                           dict);
  }

  // Tell the javascript about the relationship between load flag enums and
  // their symbolic name.
  {
    DictionaryValue* dict = new DictionaryValue();

#define LOAD_FLAG(label, value) \
    dict->SetInteger(# label, static_cast<int>(value));
#include "net/base/load_flags_list.h"
#undef LOAD_FLAG

    CallJavascriptFunction(L"g_browser.receivedLoadFlagConstants", dict);
  }

  // Tell the javascript about the relationship between net error codes and
  // their symbolic name.
  {
    DictionaryValue* dict = new DictionaryValue();

#define NET_ERROR(label, value) \
    dict->SetInteger(# label, static_cast<int>(value));
#include "net/base/net_error_list.h"
#undef NET_ERROR

    CallJavascriptFunction(L"g_browser.receivedNetErrorConstants", dict);
  }

  // Tell the javascript about the relationship between event phase enums and
  // their symbolic name.
  {
    DictionaryValue* dict = new DictionaryValue();

    dict->SetInteger("PHASE_BEGIN", net::NetLog::PHASE_BEGIN);
    dict->SetInteger("PHASE_END", net::NetLog::PHASE_END);
    dict->SetInteger("PHASE_NONE", net::NetLog::PHASE_NONE);

    CallJavascriptFunction(L"g_browser.receivedLogEventPhaseConstants", dict);
  }

  // Tell the javascript about the relationship between source type enums and
  // their symbolic names.
  {
    DictionaryValue* dict = new DictionaryValue();

#define SOURCE_TYPE(label, value) dict->SetInteger(# label, value);
#include "net/base/net_log_source_type_list.h"
#undef SOURCE_TYPE

    CallJavascriptFunction(L"g_browser.receivedLogSourceTypeConstants", dict);
  }

  // Tell the javascript about the relationship between LogLevel enums and their
  // symbolic names.
  {
    DictionaryValue* dict = new DictionaryValue();

    dict->SetInteger("LOG_ALL", net::NetLog::LOG_ALL);
    dict->SetInteger("LOG_ALL_BUT_BYTES", net::NetLog::LOG_ALL_BUT_BYTES);
    dict->SetInteger("LOG_BASIC", net::NetLog::LOG_BASIC);

    CallJavascriptFunction(L"g_browser.receivedLogLevelConstants", dict);
  }

  // Tell the javascript about the relationship between address family enums and
  // their symbolic names.
  {
    DictionaryValue* dict = new DictionaryValue();

    dict->SetInteger("ADDRESS_FAMILY_UNSPECIFIED",
                     net::ADDRESS_FAMILY_UNSPECIFIED);
    dict->SetInteger("ADDRESS_FAMILY_IPV4",
                     net::ADDRESS_FAMILY_IPV4);
    dict->SetInteger("ADDRESS_FAMILY_IPV6",
                     net::ADDRESS_FAMILY_IPV6);

    CallJavascriptFunction(L"g_browser.receivedAddressFamilyConstants", dict);
  }

  // Tell the javascript how the "time ticks" values we have given it relate to
  // actual system times. (We used time ticks throughout since they are stable
  // across system clock changes).
  {
    int64 cur_time_ms = (base::Time::Now() - base::Time()).InMilliseconds();

    int64 cur_time_ticks_ms =
        (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds();

    // If we add this number to a time tick value, it gives the timestamp.
    int64 tick_to_time_ms = cur_time_ms - cur_time_ticks_ms;

    // Chrome on all platforms stores times using the Windows epoch
    // (Jan 1 1601), but the javascript wants a unix epoch.
    // TODO(eroman): Getting the timestamp relative the to unix epoch should
    //               be part of the time library.
    const int64 kUnixEpochMs = 11644473600000LL;
    int64 tick_to_unix_time_ms = tick_to_time_ms - kUnixEpochMs;

    // Pass it as a string, since it may be too large to fit in an integer.
    CallJavascriptFunction(L"g_browser.receivedTimeTickOffset",
                           Value::CreateStringValue(
                               base::Int64ToString(tick_to_unix_time_ms)));
  }

  // Register with network stack to observe events.
  is_observing_log_ = true;
  ChromeNetLog::EntryList entries;
  io_thread_->net_log()->AddObserverAndGetAllPassivelyCapturedEvents(this,
                                                                     &entries);
  SendPassiveLogEntries(entries);
}

void NetInternalsMessageHandler::IOThreadImpl::OnGetProxySettings(
    const ListValue* list) {
  net::URLRequestContext* context = context_getter_->GetURLRequestContext();
  net::ProxyService* proxy_service = context->proxy_service();

  DictionaryValue* dict = new DictionaryValue();
  if (proxy_service->fetched_config().is_valid())
    dict->Set("original", proxy_service->fetched_config().ToValue());
  if (proxy_service->config().is_valid())
    dict->Set("effective", proxy_service->config().ToValue());

  CallJavascriptFunction(L"g_browser.receivedProxySettings", dict);
}

void NetInternalsMessageHandler::IOThreadImpl::OnReloadProxySettings(
    const ListValue* list) {
  net::URLRequestContext* context = context_getter_->GetURLRequestContext();
  context->proxy_service()->ForceReloadProxyConfig();

  // Cause the renderer to be notified of the new values.
  OnGetProxySettings(NULL);
}

void NetInternalsMessageHandler::IOThreadImpl::OnGetBadProxies(
    const ListValue* list) {
  net::URLRequestContext* context = context_getter_->GetURLRequestContext();

  const net::ProxyRetryInfoMap& bad_proxies_map =
      context->proxy_service()->proxy_retry_info();

  ListValue* dict_list = new ListValue();

  for (net::ProxyRetryInfoMap::const_iterator it = bad_proxies_map.begin();
       it != bad_proxies_map.end(); ++it) {
    const std::string& proxy_uri = it->first;
    const net::ProxyRetryInfo& retry_info = it->second;

    DictionaryValue* dict = new DictionaryValue();
    dict->SetString("proxy_uri", proxy_uri);
    dict->SetString("bad_until",
                    net::NetLog::TickCountToString(retry_info.bad_until));

    dict_list->Append(dict);
  }

  CallJavascriptFunction(L"g_browser.receivedBadProxies", dict_list);
}

void NetInternalsMessageHandler::IOThreadImpl::OnClearBadProxies(
    const ListValue* list) {
  net::URLRequestContext* context = context_getter_->GetURLRequestContext();
  context->proxy_service()->ClearBadProxiesCache();

  // Cause the renderer to be notified of the new values.
  OnGetBadProxies(NULL);
}

void NetInternalsMessageHandler::IOThreadImpl::OnGetHostResolverInfo(
    const ListValue* list) {
  net::URLRequestContext* context = context_getter_->GetURLRequestContext();
  net::HostResolverImpl* host_resolver_impl =
      context->host_resolver()->GetAsHostResolverImpl();
  net::HostCache* cache = GetHostResolverCache(context);

  if (!host_resolver_impl || !cache) {
    CallJavascriptFunction(L"g_browser.receivedHostResolverInfo", NULL);
    return;
  }

  DictionaryValue* dict = new DictionaryValue();

  dict->SetInteger(
      "default_address_family",
      static_cast<int>(host_resolver_impl->GetDefaultAddressFamily()));

  DictionaryValue* cache_info_dict = new DictionaryValue();

  cache_info_dict->SetInteger(
      "capacity",
      static_cast<int>(cache->max_entries()));
  cache_info_dict->SetInteger(
      "ttl_success_ms",
      static_cast<int>(cache->success_entry_ttl().InMilliseconds()));
  cache_info_dict->SetInteger(
      "ttl_failure_ms",
      static_cast<int>(cache->failure_entry_ttl().InMilliseconds()));

  ListValue* entry_list = new ListValue();

  for (net::HostCache::EntryMap::const_iterator it =
       cache->entries().begin();
       it != cache->entries().end();
       ++it) {
    const net::HostCache::Key& key = it->first;
    const net::HostCache::Entry* entry = it->second.get();

    DictionaryValue* entry_dict = new DictionaryValue();

    entry_dict->SetString("hostname", key.hostname);
    entry_dict->SetInteger("address_family",
        static_cast<int>(key.address_family));
    entry_dict->SetString("expiration",
                          net::NetLog::TickCountToString(entry->expiration));

    if (entry->error != net::OK) {
      entry_dict->SetInteger("error", entry->error);
    } else {
      // Append all of the resolved addresses.
      ListValue* address_list = new ListValue();
      const struct addrinfo* current_address = entry->addrlist.head();
      while (current_address) {
        address_list->Append(Value::CreateStringValue(
            net::NetAddressToStringWithPort(current_address)));
        current_address = current_address->ai_next;
      }
      entry_dict->Set("addresses", address_list);
    }

    entry_list->Append(entry_dict);
  }

  cache_info_dict->Set("entries", entry_list);
  dict->Set("cache", cache_info_dict);

  CallJavascriptFunction(L"g_browser.receivedHostResolverInfo", dict);
}

void NetInternalsMessageHandler::IOThreadImpl::OnClearHostResolverCache(
    const ListValue* list) {
  net::HostCache* cache =
      GetHostResolverCache(context_getter_->GetURLRequestContext());

  if (cache)
    cache->clear();

  // Cause the renderer to be notified of the new values.
  OnGetHostResolverInfo(NULL);
}

void NetInternalsMessageHandler::IOThreadImpl::OnEnableIPv6(
    const ListValue* list) {
  net::URLRequestContext* context = context_getter_->GetURLRequestContext();
  net::HostResolverImpl* host_resolver_impl =
      context->host_resolver()->GetAsHostResolverImpl();

  if (host_resolver_impl) {
    host_resolver_impl->SetDefaultAddressFamily(
        net::ADDRESS_FAMILY_UNSPECIFIED);
  }

  // Cause the renderer to be notified of the new value.
  OnGetHostResolverInfo(NULL);
}

void NetInternalsMessageHandler::IOThreadImpl::OnStartConnectionTests(
    const ListValue* list) {
  // |value| should be: [<URL to test>].
  string16 url_str;
  CHECK(list->GetString(0, &url_str));

  // Try to fix-up the user provided URL into something valid.
  // For example, turn "www.google.com" into "http://www.google.com".
  GURL url(URLFixerUpper::FixupURL(UTF16ToUTF8(url_str), std::string()));

  connection_tester_.reset(new ConnectionTester(
      this, io_thread_->globals()->proxy_script_fetcher_context.get()));
  connection_tester_->RunAllTests(url);
}

void NetInternalsMessageHandler::IOThreadImpl::OnHSTSQuery(
    const ListValue* list) {
  // |list| should be: [<domain to query>].
  std::string domain;
  CHECK(list->GetString(0, &domain));
  DictionaryValue* result = new(DictionaryValue);

  if (!IsStringASCII(domain)) {
    result->SetString("error", "non-ASCII domain name");
  } else {
    net::TransportSecurityState* transport_security_state =
        context_getter_->GetURLRequestContext()->transport_security_state();
    if (!transport_security_state) {
      result->SetString("error", "no TransportSecurityState active");
    } else {
      net::TransportSecurityState::DomainState state;
      const bool found = transport_security_state->HasMetadata(
          &state, domain, true);

      result->SetBoolean("result", found);
      if (found) {
        result->SetInteger("mode", static_cast<int>(state.mode));
        result->SetBoolean("subdomains", state.include_subdomains);
        result->SetBoolean("preloaded", state.preloaded);
        result->SetString("domain", state.domain);

        std::vector<std::string> parts;
        for (std::vector<net::SHA1Fingerprint>::const_iterator
             i = state.public_key_hashes.begin();
             i != state.public_key_hashes.end(); i++) {
          std::string part = "sha1/";
          std::string hash_str(reinterpret_cast<const char*>(i->data),
                               sizeof(i->data));
          std::string b64;
          base::Base64Encode(hash_str, &b64);
          part += b64;
          parts.push_back(part);
        }
        result->SetString("public_key_hashes", JoinString(parts, ','));
      }
    }
  }

  CallJavascriptFunction(L"g_browser.receivedHSTSResult", result);
}

void NetInternalsMessageHandler::IOThreadImpl::OnHSTSAdd(
    const ListValue* list) {
  // |list| should be: [<domain to query>, <include subdomains>, <cert pins>].
  std::string domain;
  CHECK(list->GetString(0, &domain));
  if (!IsStringASCII(domain)) {
    // Silently fail. The user will get a helpful error if they query for the
    // name.
    return;
  }
  bool include_subdomains;
  CHECK(list->GetBoolean(1, &include_subdomains));
  std::string hashes_str;
  CHECK(list->GetString(2, &hashes_str));

  net::TransportSecurityState* transport_security_state =
      context_getter_->GetURLRequestContext()->transport_security_state();
  if (!transport_security_state)
    return;

  net::TransportSecurityState::DomainState state;
  state.expiry = state.created + base::TimeDelta::FromDays(1000);
  state.include_subdomains = include_subdomains;
  state.public_key_hashes.clear();
  if (!hashes_str.empty()) {
    std::vector<std::string> type_and_b64s;
    base::SplitString(hashes_str, ',', &type_and_b64s);
    for (std::vector<std::string>::const_iterator
         i = type_and_b64s.begin(); i != type_and_b64s.end(); i++) {
      std::string type_and_b64;
      RemoveChars(*i, " \t\r\n", &type_and_b64);
      if (type_and_b64.find("sha1/") != 0)
        continue;
      std::string b64 = type_and_b64.substr(5, type_and_b64.size() - 5);
      std::string hash_str;
      if (!base::Base64Decode(b64, &hash_str))
        continue;
      net::SHA1Fingerprint hash;
      if (hash_str.size() != sizeof(hash.data))
        continue;
      memcpy(hash.data, hash_str.data(), sizeof(hash.data));
      state.public_key_hashes.push_back(hash);
    }
  }

  transport_security_state->EnableHost(domain, state);
}

void NetInternalsMessageHandler::IOThreadImpl::OnHSTSDelete(
    const ListValue* list) {
  // |list| should be: [<domain to query>].
  std::string domain;
  CHECK(list->GetString(0, &domain));
  if (!IsStringASCII(domain)) {
    // There cannot be a unicode entry in the HSTS set.
    return;
  }
  net::TransportSecurityState* transport_security_state =
      context_getter_->GetURLRequestContext()->transport_security_state();
  if (!transport_security_state)
    return;

  transport_security_state->DeleteHost(domain);
}

void NetInternalsMessageHandler::IOThreadImpl::OnGetHttpCacheInfo(
    const ListValue* list) {
  DictionaryValue* info_dict = new DictionaryValue();
  DictionaryValue* stats_dict = new DictionaryValue();

  disk_cache::Backend* disk_cache = GetDiskCacheBackend(
      context_getter_->GetURLRequestContext());

  if (disk_cache) {
    // Extract the statistics key/value pairs from the backend.
    std::vector<std::pair<std::string, std::string> > stats;
    disk_cache->GetStats(&stats);
    for (size_t i = 0; i < stats.size(); ++i) {
      stats_dict->Set(stats[i].first,
                      Value::CreateStringValue(stats[i].second));
    }
  }

  info_dict->Set("stats", stats_dict);

  CallJavascriptFunction(L"g_browser.receivedHttpCacheInfo", info_dict);
}

void NetInternalsMessageHandler::IOThreadImpl::OnGetSocketPoolInfo(
    const ListValue* list) {
  net::HttpNetworkSession* http_network_session =
      GetHttpNetworkSession(context_getter_->GetURLRequestContext());

  Value* socket_pool_info = NULL;
  if (http_network_session)
    socket_pool_info = http_network_session->SocketPoolInfoToValue();

  CallJavascriptFunction(L"g_browser.receivedSocketPoolInfo", socket_pool_info);
}


void NetInternalsMessageHandler::IOThreadImpl::OnFlushSocketPools(
    const ListValue* list) {
  net::HttpNetworkSession* http_network_session =
      GetHttpNetworkSession(context_getter_->GetURLRequestContext());

  if (http_network_session)
    http_network_session->CloseAllConnections();
}

void NetInternalsMessageHandler::IOThreadImpl::OnCloseIdleSockets(
    const ListValue* list) {
  net::HttpNetworkSession* http_network_session =
      GetHttpNetworkSession(context_getter_->GetURLRequestContext());

  if (http_network_session)
    http_network_session->CloseIdleConnections();
}

void NetInternalsMessageHandler::IOThreadImpl::OnGetSpdySessionInfo(
    const ListValue* list) {
  net::HttpNetworkSession* http_network_session =
      GetHttpNetworkSession(context_getter_->GetURLRequestContext());

  Value* spdy_info = NULL;
  if (http_network_session) {
    spdy_info = http_network_session->SpdySessionPoolInfoToValue();
  }

  CallJavascriptFunction(L"g_browser.receivedSpdySessionInfo", spdy_info);
}

void NetInternalsMessageHandler::IOThreadImpl::OnGetSpdyStatus(
    const ListValue* list) {
  DictionaryValue* status_dict = new DictionaryValue();

  status_dict->Set("spdy_enabled",
                   Value::CreateBooleanValue(
                       net::HttpStreamFactory::spdy_enabled()));
  status_dict->Set("use_alternate_protocols",
                   Value::CreateBooleanValue(
                       net::HttpStreamFactory::use_alternate_protocols()));
  status_dict->Set("force_spdy_over_ssl",
                   Value::CreateBooleanValue(
                       net::HttpStreamFactory::force_spdy_over_ssl()));
  status_dict->Set("force_spdy_always",
                   Value::CreateBooleanValue(
                       net::HttpStreamFactory::force_spdy_always()));
  status_dict->Set("next_protos",
                   Value::CreateStringValue(
                       *net::HttpStreamFactory::next_protos()));

  CallJavascriptFunction(L"g_browser.receivedSpdyStatus", status_dict);
}

void
NetInternalsMessageHandler::IOThreadImpl::OnGetSpdyAlternateProtocolMappings(
    const ListValue* list) {
  net::HttpNetworkSession* http_network_session =
      GetHttpNetworkSession(context_getter_->GetURLRequestContext());

  ListValue* dict_list = new ListValue();

  if (http_network_session) {
    const net::HttpAlternateProtocols& http_alternate_protocols =
        http_network_session->alternate_protocols();
    const net::HttpAlternateProtocols::ProtocolMap& map =
        http_alternate_protocols.protocol_map();

    for (net::HttpAlternateProtocols::ProtocolMap::const_iterator it =
             map.begin();
         it != map.end(); ++it) {
      DictionaryValue* dict = new DictionaryValue();
      dict->SetString("host_port_pair", it->first.ToString());
      dict->SetString("alternate_protocol", it->second.ToString());
      dict_list->Append(dict);
    }
  }

  CallJavascriptFunction(L"g_browser.receivedSpdyAlternateProtocolMappings",
                         dict_list);
}

#ifdef OS_WIN
void NetInternalsMessageHandler::IOThreadImpl::OnGetServiceProviders(
    const ListValue* list) {

  DictionaryValue* service_providers = new DictionaryValue();

  WinsockLayeredServiceProviderList layered_providers;
  GetWinsockLayeredServiceProviders(&layered_providers);
  ListValue* layered_provider_list = new ListValue();
  for (size_t i = 0; i < layered_providers.size(); ++i) {
    DictionaryValue* service_dict = new DictionaryValue();
    service_dict->SetString("name", layered_providers[i].name);
    service_dict->SetInteger("version", layered_providers[i].version);
    service_dict->SetInteger("chain_length", layered_providers[i].chain_length);
    service_dict->SetInteger("socket_type", layered_providers[i].socket_type);
    service_dict->SetInteger("socket_protocol",
        layered_providers[i].socket_protocol);
    service_dict->SetString("path", layered_providers[i].path);

    layered_provider_list->Append(service_dict);
  }
  service_providers->Set("service_providers", layered_provider_list);

  WinsockNamespaceProviderList namespace_providers;
  GetWinsockNamespaceProviders(&namespace_providers);
  ListValue* namespace_list = new ListValue;
  for (size_t i = 0; i < namespace_providers.size(); ++i) {
    DictionaryValue* namespace_dict = new DictionaryValue();
    namespace_dict->SetString("name", namespace_providers[i].name);
    namespace_dict->SetBoolean("active", namespace_providers[i].active);
    namespace_dict->SetInteger("version", namespace_providers[i].version);
    namespace_dict->SetInteger("type", namespace_providers[i].type);

    namespace_list->Append(namespace_dict);
  }
  service_providers->Set("namespace_providers", namespace_list);

  CallJavascriptFunction(L"g_browser.receivedServiceProviders",
                         service_providers);
}
#endif

#ifdef OS_CHROMEOS
void NetInternalsMessageHandler::OnRefreshSystemLogs(const ListValue* list) {
  DCHECK(syslogs_getter_.get());
  syslogs_getter_->DeleteSystemLogs();
  syslogs_getter_->LoadSystemLogs();
}

void NetInternalsMessageHandler::OnGetSystemLog(const ListValue* list) {
  DCHECK(syslogs_getter_.get());
  syslogs_getter_->RequestSystemLog(list);
}
#endif

void NetInternalsMessageHandler::IOThreadImpl::OnSetLogLevel(
    const ListValue* list) {
  int log_level;
  std::string log_level_string;
  if (!list->GetString(0, &log_level_string) ||
      !base::StringToInt(log_level_string, &log_level)) {
    NOTREACHED();
    return;
  }

  DCHECK_GE(log_level, net::NetLog::LOG_ALL);
  DCHECK_LE(log_level, net::NetLog::LOG_BASIC);
  SetLogLevel(static_cast<net::NetLog::LogLevel>(log_level));
}

// Note that unlike other methods of IOThreadImpl, this function
// can be called from ANY THREAD.
void NetInternalsMessageHandler::IOThreadImpl::OnAddEntry(
    net::NetLog::EventType type,
    const base::TimeTicks& time,
    const net::NetLog::Source& source,
    net::NetLog::EventPhase phase,
    net::NetLog::EventParameters* params) {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      NewRunnableMethod(
          this, &IOThreadImpl::AddEntryToQueue,
          net::NetLog::EntryToDictionaryValue(type, time, source, phase,
                                              params, false)));
}

void NetInternalsMessageHandler::IOThreadImpl::AddEntryToQueue(Value* entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!pending_entries_.get()) {
    pending_entries_.reset(new ListValue());
    BrowserThread::PostDelayedTask(
        BrowserThread::IO, FROM_HERE,
        NewRunnableMethod(this, &IOThreadImpl::PostPendingEntries),
        kNetLogEventDelayMilliseconds);
  }
  pending_entries_->Append(entry);
}

void NetInternalsMessageHandler::IOThreadImpl::PostPendingEntries() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  CallJavascriptFunction(
      L"g_browser.receivedLogEntries",
      pending_entries_.release());
}

void NetInternalsMessageHandler::IOThreadImpl::OnStartConnectionTestSuite() {
  CallJavascriptFunction(L"g_browser.receivedStartConnectionTestSuite", NULL);
}

void NetInternalsMessageHandler::IOThreadImpl::OnStartConnectionTestExperiment(
    const ConnectionTester::Experiment& experiment) {
  CallJavascriptFunction(
      L"g_browser.receivedStartConnectionTestExperiment",
      ExperimentToValue(experiment));
}

void
NetInternalsMessageHandler::IOThreadImpl::OnCompletedConnectionTestExperiment(
    const ConnectionTester::Experiment& experiment,
    int result) {
  DictionaryValue* dict = new DictionaryValue();

  dict->Set("experiment", ExperimentToValue(experiment));
  dict->SetInteger("result", result);

  CallJavascriptFunction(
      L"g_browser.receivedCompletedConnectionTestExperiment",
      dict);
}

void
NetInternalsMessageHandler::IOThreadImpl::OnCompletedConnectionTestSuite() {
  CallJavascriptFunction(
      L"g_browser.receivedCompletedConnectionTestSuite",
      NULL);
}

void NetInternalsMessageHandler::IOThreadImpl::DispatchToMessageHandler(
    ListValue* arg, MessageHandler method) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  (this->*method)(arg);
  delete arg;
}

// Note that this can be called from ANY THREAD.
void NetInternalsMessageHandler::IOThreadImpl::CallJavascriptFunction(
    const std::wstring& function_name,
    Value* arg) {
  if (BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    if (handler_ && !was_webui_deleted_) {
      // We check |handler_| in case it was deleted on the UI thread earlier
      // while we were running on the IO thread.
      handler_->CallJavascriptFunction(function_name, arg);
    }
    delete arg;
    return;
  }

  if (!BrowserThread::PostTask(
           BrowserThread::UI, FROM_HERE,
           NewRunnableMethod(
               this,
               &IOThreadImpl::CallJavascriptFunction,
               function_name, arg))) {
    // Failed posting the task, avoid leaking.
    delete arg;
  }
}

}  // namespace


////////////////////////////////////////////////////////////////////////////////
//
// NetInternalsUI
//
////////////////////////////////////////////////////////////////////////////////

NetInternalsUI::NetInternalsUI(TabContents* contents) : WebUI(contents) {
  AddMessageHandler((new NetInternalsMessageHandler())->Attach(this));

  NetInternalsHTMLSource* html_source = new NetInternalsHTMLSource();

  // Set up the chrome://net-internals/ source.
  contents->profile()->GetChromeURLDataManager()->AddDataSource(html_source);
}
