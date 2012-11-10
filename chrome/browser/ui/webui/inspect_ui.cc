// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/inspect_ui.h"

#include <set>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted_memory.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/debugger/devtools_window.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/tab_contents/tab_contents_iterator.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager_backend.h"
#include "chrome/browser/ui/webui/chrome_web_ui_data_source.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/devtools_agent_host_registry.h"
#include "content/public/browser/devtools_client_host.h"
#include "content/public/browser/devtools_manager.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/worker_service.h"
#include "content/public/browser/worker_service_observer.h"
#include "content/public/common/process_type.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"
#include "ui/base/resource/resource_bundle.h"

using content::BrowserThread;
using content::ChildProcessData;
using content::DevToolsAgentHost;
using content::DevToolsAgentHostRegistry;
using content::DevToolsClientHost;
using content::DevToolsManager;
using content::RenderProcessHost;
using content::RenderViewHost;
using content::RenderViewHostDelegate;
using content::RenderWidgetHost;
using content::WebContents;
using content::WebUIMessageHandler;
using content::WorkerService;
using content::WorkerServiceObserver;

static const char kDataFile[] = "targets-data.json";

static const char kExtensionTargetType[]  = "extension";
static const char kPageTargetType[]  = "page";
static const char kWorkerTargetType[]  = "worker";

static const char kInspectCommand[]  = "inspect";
static const char kTerminateCommand[]  = "terminate";

static const char kTargetTypeField[]  = "type";
static const char kAttachedField[]  = "attached";
static const char kProcessIdField[]  = "processId";
static const char kRouteIdField[]  = "routeId";
static const char kUrlField[]  = "url";
static const char kNameField[]  = "name";
static const char kFaviconUrlField[] = "favicon_url";
static const char kPidField[]  = "pid";

namespace {

DictionaryValue* BuildTargetDescriptor(
    const std::string& target_type,
    bool attached,
    const GURL& url,
    const std::string& name,
    const GURL& favicon_url,
    int process_id,
    int route_id,
    base::ProcessHandle handle = base::kNullProcessHandle) {
  DictionaryValue* target_data = new DictionaryValue();
  target_data->SetString(kTargetTypeField, target_type);
  target_data->SetBoolean(kAttachedField, attached);
  target_data->SetInteger(kProcessIdField, process_id);
  target_data->SetInteger(kRouteIdField, route_id);
  target_data->SetString(kUrlField, url.spec());
  target_data->SetString(kNameField, net::EscapeForHTML(name));
  target_data->SetInteger(kPidField, base::GetProcId(handle));
  target_data->SetString(kFaviconUrlField, favicon_url.spec());

  return target_data;
}

bool HasClientHost(RenderViewHost* rvh) {
  if (!DevToolsAgentHostRegistry::HasDevToolsAgentHost(rvh))
    return false;

  DevToolsAgentHost* agent =
      DevToolsAgentHostRegistry::GetDevToolsAgentHost(rvh);
  return !!DevToolsManager::GetInstance()->GetDevToolsClientHostFor(agent);
}

DictionaryValue* BuildTargetDescriptor(RenderViewHost* rvh, bool is_tab) {
  WebContents* web_contents = WebContents::FromRenderViewHost(rvh);
  std::string title;
  std::string target_type = is_tab ? kPageTargetType : "";
  GURL favicon_url;
  if (web_contents) {
    title = UTF16ToUTF8(web_contents->GetTitle());
    content::NavigationController& controller = web_contents->GetController();
    content::NavigationEntry* entry = controller.GetActiveEntry();
    if (entry != NULL && entry->GetURL().is_valid())
      favicon_url = entry->GetFavicon().url;

    Profile* profile = Profile::FromBrowserContext(
        web_contents->GetBrowserContext());
    if (profile) {
      ExtensionService* extension_service = profile->GetExtensionService();
      const extensions::Extension* extension = extension_service->
          extensions()->GetByID(web_contents->GetURL().host());
      if (extension) {
        target_type = kExtensionTargetType;
        title = extension->name();
      }
    }
  }

  return BuildTargetDescriptor(target_type,
                               HasClientHost(rvh),
                               web_contents->GetURL(),
                               title,
                               favicon_url,
                               rvh->GetProcess()->GetID(),
                               rvh->GetRoutingID());
}

class InspectDataSource : public ChromeWebUIDataSource {
 public:
  InspectDataSource();

  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id);
 private:
  virtual ~InspectDataSource() {}
  void SendDescriptors(int request_id, ListValue* rvh_list);
  DISALLOW_COPY_AND_ASSIGN(InspectDataSource);
};

InspectDataSource::InspectDataSource()
    : ChromeWebUIDataSource(chrome::kChromeUIInspectHost,
                            MessageLoop::current()) {
  add_resource_path("inspect.css", IDR_INSPECT_CSS);
  add_resource_path("inspect.js", IDR_INSPECT_JS);
  set_default_resource(IDR_INSPECT_HTML);
}

void InspectDataSource::StartDataRequest(const std::string& path,
                                         bool is_incognito,
                                         int request_id) {
  if (path != kDataFile) {
    ChromeWebUIDataSource::StartDataRequest(path, is_incognito, request_id);
    return;
  }

  std::set<RenderViewHost*> tab_rvhs;
  for (TabContentsIterator it; !it.done(); ++it)
    tab_rvhs.insert(it->web_contents()->GetRenderViewHost());

  scoped_ptr<ListValue> rvh_list(new ListValue());

  for (RenderProcessHost::iterator it(RenderProcessHost::AllHostsIterator());
       !it.IsAtEnd(); it.Advance()) {
    RenderProcessHost* render_process_host = it.GetCurrentValue();
    DCHECK(render_process_host);

    // Ignore processes that don't have a connection, such as crashed tabs.
    if (!render_process_host->HasConnection())
      continue;

    RenderProcessHost::RenderWidgetHostsIterator rwit(
        render_process_host->GetRenderWidgetHostsIterator());
    for (; !rwit.IsAtEnd(); rwit.Advance()) {
      const RenderWidgetHost* widget = rwit.GetCurrentValue();
      DCHECK(widget);
      if (!widget || !widget->IsRenderView())
        continue;

      RenderViewHost* rvh =
          RenderViewHost::From(const_cast<RenderWidgetHost*>(widget));

      bool is_tab = tab_rvhs.find(rvh) != tab_rvhs.end();
      rvh_list->Append(BuildTargetDescriptor(rvh, is_tab));
    }
  }

  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&InspectDataSource::SendDescriptors,
                 this,
                 request_id,
                 base::Owned(rvh_list.release())));
}

void InspectDataSource::SendDescriptors(int request_id,
                                        ListValue* rvh_list) {
  std::vector<WorkerService::WorkerInfo> worker_info =
      WorkerService::GetInstance()->GetWorkers();
  for (size_t i = 0; i < worker_info.size(); ++i) {
    rvh_list->Append(BuildTargetDescriptor(
        kWorkerTargetType,
        false,
        worker_info[i].url,
        UTF16ToUTF8(worker_info[i].name),
        GURL(),
        worker_info[i].process_id,
        worker_info[i].route_id,
        worker_info[i].handle));
  }

  std::string json_string;
  base::JSONWriter::Write(rvh_list, &json_string);

  SendResponse(request_id, base::RefCountedString::TakeString(&json_string));
}

class InspectMessageHandler : public WebUIMessageHandler {
 public:
  InspectMessageHandler() {}
  virtual ~InspectMessageHandler() {}

 private:
  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // Callback for "openDevTools" message.
  void HandleInspectCommand(const ListValue* args);
  void HandleTerminateCommand(const ListValue* args);

  DISALLOW_COPY_AND_ASSIGN(InspectMessageHandler);
};

void InspectMessageHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(kInspectCommand,
      base::Bind(&InspectMessageHandler::HandleInspectCommand,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kTerminateCommand,
      base::Bind(&InspectMessageHandler::HandleTerminateCommand,
                 base::Unretained(this)));
}

void InspectMessageHandler::HandleInspectCommand(const ListValue* args) {
  std::string process_id_str;
  std::string route_id_str;
  int process_id;
  int route_id;
  CHECK(args->GetSize() == 2);
  CHECK(args->GetString(0, &process_id_str));
  CHECK(args->GetString(1, &route_id_str));
  CHECK(base::StringToInt(process_id_str,
                          &process_id));
  CHECK(base::StringToInt(route_id_str, &route_id));

  Profile* profile = Profile::FromWebUI(web_ui());
  if (!profile)
    return;

  RenderViewHost* rvh = RenderViewHost::FromID(process_id, route_id);
  if (rvh) {
    DevToolsWindow::OpenDevToolsWindow(rvh);
    return;
  }

  DevToolsAgentHost* agent_host =
      DevToolsAgentHostRegistry::GetDevToolsAgentHostForWorker(process_id,
                                                               route_id);
  if (agent_host)
    DevToolsWindow::OpenDevToolsWindowForWorker(profile, agent_host);
}

static void TerminateWorker(int process_id, int route_id) {
  WorkerService::GetInstance()->TerminateWorker(process_id, route_id);
}

void InspectMessageHandler::HandleTerminateCommand(const ListValue* args) {
  std::string process_id_str;
  std::string route_id_str;
  int process_id;
  int route_id;
  CHECK(args->GetSize() == 2);
  CHECK(args->GetString(0, &process_id_str));
  CHECK(args->GetString(1, &route_id_str));
  CHECK(base::StringToInt(process_id_str,
                          &process_id));
  CHECK(base::StringToInt(route_id_str, &route_id));

  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
      base::Bind(&TerminateWorker, process_id, route_id));
}

}  // namespace

class InspectUI::WorkerCreationDestructionListener
    : public WorkerServiceObserver,
      public base::RefCountedThreadSafe<WorkerCreationDestructionListener> {
 public:
  explicit WorkerCreationDestructionListener(InspectUI* workers_ui)
      : discovery_ui_(workers_ui) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&WorkerCreationDestructionListener::RegisterObserver,
                   this));
  }

  void InspectUIDestroyed() {
    discovery_ui_ = NULL;
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&WorkerCreationDestructionListener::UnregisterObserver,
                   this));
  }

 private:
  friend class base::RefCountedThreadSafe<WorkerCreationDestructionListener>;
  virtual ~WorkerCreationDestructionListener() {}

  virtual void WorkerCreated(
      const GURL& url,
      const string16& name,
      int process_id,
      int route_id) OVERRIDE {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&WorkerCreationDestructionListener::NotifyItemsChanged,
                   this));
  }

  virtual void WorkerDestroyed(int process_id, int route_id) OVERRIDE {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&WorkerCreationDestructionListener::NotifyItemsChanged,
                   this));
  }

  void NotifyItemsChanged() {
    if (discovery_ui_)
      discovery_ui_->RefreshUI();
  }

  void RegisterObserver() {
    WorkerService::GetInstance()->AddObserver(this);
  }

  void UnregisterObserver() {
    WorkerService::GetInstance()->RemoveObserver(this);
  }

  InspectUI* discovery_ui_;
};

InspectUI::InspectUI(content::WebUI* web_ui)
    : WebUIController(web_ui),
      observer_(new WorkerCreationDestructionListener(this)) {
  web_ui->AddMessageHandler(new InspectMessageHandler());

  InspectDataSource* html_source = new InspectDataSource();

  Profile* profile = Profile::FromWebUI(web_ui);
  ChromeURLDataManager::AddDataSource(profile, html_source);

  registrar_.Add(this,
                 content::NOTIFICATION_WEB_CONTENTS_CONNECTED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 content::NOTIFICATION_WEB_CONTENTS_DISCONNECTED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                 content::NotificationService::AllSources());
}

InspectUI::~InspectUI() {
  StopListeningNotifications();
}

void InspectUI::RefreshUI() {
  web_ui()->CallJavascriptFunction("populateLists");
}

void InspectUI::Observe(int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (source == content::Source<WebContents>(web_ui()->GetWebContents())) {
    if (type == content::NOTIFICATION_WEB_CONTENTS_DISCONNECTED)
        StopListeningNotifications();
    return;
  }
  RefreshUI();
}

void InspectUI::StopListeningNotifications()
{
  if (!observer_)
    return;
  observer_->InspectUIDestroyed();
  observer_ = NULL;
  registrar_.RemoveAll();
}
