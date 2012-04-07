// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/workers_ui.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted_memory.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/values.h"
#include "chrome/browser/debugger/devtools_window.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager_backend.h"
#include "chrome/browser/ui/webui/chrome_web_ui_data_source.h"
#include "chrome/common/url_constants.h"
#include "content/browser/worker_host/worker_process_host.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/devtools_agent_host_registry.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/worker_service.h"
#include "content/public/browser/worker_service_observer.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/common/process_type.h"
#include "grit/generated_resources.h"
#include "grit/workers_resources.h"
#include "ui/base/resource/resource_bundle.h"

using content::BrowserThread;
using content::ChildProcessData;
using content::DevToolsAgentHost;
using content::DevToolsAgentHostRegistry;
using content::WebContents;
using content::WebUIMessageHandler;
using content::WorkerService;
using content::WorkerServiceObserver;

static const char kWorkersDataFile[] = "workers_data.json";

static const char kOpenDevToolsCommand[]  = "openDevTools";
static const char kTerminateWorkerCommand[]  = "terminateWorker";

static const char kWorkerProcessHostIdField[]  = "workerProcessHostId";
static const char kWorkerRouteIdField[]  = "workerRouteId";
static const char kUrlField[]  = "url";
static const char kNameField[]  = "name";
static const char kPidField[]  = "pid";

namespace {


DictionaryValue* BuildWorkerData(const ChildProcessData& data,
    const WorkerProcessHost::WorkerInstance& instance) {
  DictionaryValue* worker_data = new DictionaryValue();
  worker_data->SetInteger(kWorkerProcessHostIdField, data.id);
  worker_data->SetInteger(kWorkerRouteIdField, instance.worker_route_id());
  worker_data->SetString(kUrlField, instance.url().spec());
  worker_data->SetString(kNameField, instance.name());
  worker_data->SetInteger(kPidField, base::GetProcId(data.handle));
  return worker_data;
}

class WorkersUIHTMLSource : public ChromeWebUIDataSource {
 public:
  WorkersUIHTMLSource();

  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id);
 private:
  ~WorkersUIHTMLSource() {}
  void SendSharedWorkersData(int request_id);
  DISALLOW_COPY_AND_ASSIGN(WorkersUIHTMLSource);
};

WorkersUIHTMLSource::WorkersUIHTMLSource()
    : ChromeWebUIDataSource(chrome::kChromeUIWorkersHost, NULL) {
  add_resource_path("workers.js", IDR_WORKERS_INDEX_JS);
  set_default_resource(IDR_WORKERS_INDEX_HTML);
}

void WorkersUIHTMLSource::StartDataRequest(const std::string& path,
                                           bool is_incognito,
                                           int request_id) {
  if (path == kWorkersDataFile) {
    SendSharedWorkersData(request_id);
  } else {
    ChromeWebUIDataSource::StartDataRequest(path, is_incognito, request_id);
  }
}

void WorkersUIHTMLSource::SendSharedWorkersData(int request_id) {
    ListValue workers_list;
    for (WorkerProcessHostIterator iter; !iter.Done(); ++iter) {
      const WorkerProcessHost::Instances& instances = iter->instances();
      for (WorkerProcessHost::Instances::const_iterator i = instances.begin();
           i != instances.end(); ++i) {
         workers_list.Append(BuildWorkerData(iter.GetData(), *i));
      }
    }

    std::string json_string;
    base::JSONWriter::Write(&workers_list, false, &json_string);

    SendResponse(request_id, base::RefCountedString::TakeString(&json_string));
}

class WorkersDOMHandler : public WebUIMessageHandler {
 public:
  WorkersDOMHandler() {}
  virtual ~WorkersDOMHandler() {}

 private:
  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // Callback for "openDevTools" message.
  void HandleOpenDevTools(const ListValue* args);
  void HandleTerminateWorker(const ListValue* args);

  DISALLOW_COPY_AND_ASSIGN(WorkersDOMHandler);
};

void WorkersDOMHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(kOpenDevToolsCommand,
      base::Bind(&WorkersDOMHandler::HandleOpenDevTools,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kTerminateWorkerCommand,
      base::Bind(&WorkersDOMHandler::HandleTerminateWorker,
                 base::Unretained(this)));
}

void WorkersDOMHandler::HandleOpenDevTools(const ListValue* args) {
  std::string worker_process_host_id_str;
  std::string worker_route_id_str;
  int worker_process_host_id;
  int worker_route_id;
  CHECK(args->GetSize() == 2);
  CHECK(args->GetString(0, &worker_process_host_id_str));
  CHECK(args->GetString(1, &worker_route_id_str));
  CHECK(base::StringToInt(worker_process_host_id_str,
                          &worker_process_host_id));
  CHECK(base::StringToInt(worker_route_id_str, &worker_route_id));

  Profile* profile = Profile::FromWebUI(web_ui());
  if (!profile)
    return;
  DevToolsAgentHost* agent_host =
      DevToolsAgentHostRegistry::GetDevToolsAgentHostForWorker(
          worker_process_host_id,
          worker_route_id);
  DevToolsWindow::OpenDevToolsWindowForWorker(profile, agent_host);
}

static void TerminateWorker(int worker_process_id, int worker_route_id) {
  for (WorkerProcessHostIterator iter; !iter.Done(); ++iter) {
      if (iter.GetData().id == worker_process_id) {
      iter->TerminateWorker(worker_route_id);
      return;
    }
  }
}

void WorkersDOMHandler::HandleTerminateWorker(const ListValue* args) {
  std::string worker_process_host_id_str;
  std::string worker_route_id_str;
  int worker_process_host_id;
  int worker_route_id;
  CHECK(args->GetSize() == 2);
  CHECK(args->GetString(0, &worker_process_host_id_str));
  CHECK(args->GetString(1, &worker_route_id_str));
  CHECK(base::StringToInt(worker_process_host_id_str,
                          &worker_process_host_id));
  CHECK(base::StringToInt(worker_route_id_str, &worker_route_id));

  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
      base::Bind(&TerminateWorker, worker_process_host_id, worker_route_id));
}

}  // namespace

class WorkersUI::WorkerCreationDestructionListener
    : public WorkerServiceObserver,
      public base::RefCountedThreadSafe<WorkerCreationDestructionListener> {
 public:
  explicit WorkerCreationDestructionListener(WorkersUI* workers_ui)
      : workers_ui_(workers_ui) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&WorkerCreationDestructionListener::RegisterObserver,
                   this));
  }

  void WorkersUIDestroyed() {
    workers_ui_ = NULL;
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&WorkerCreationDestructionListener::UnregisterObserver,
                   this));
  }

 private:
  friend class base::RefCountedThreadSafe<WorkerCreationDestructionListener>;
  virtual ~WorkerCreationDestructionListener() {
  }

  virtual void WorkerCreated(
      WorkerProcessHost* process,
      const WorkerProcessHost::WorkerInstance& instance) OVERRIDE {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(
            &WorkerCreationDestructionListener::NotifyWorkerCreated,
            this, base::Owned(BuildWorkerData(process->GetData(), instance))));
  }
  virtual void WorkerDestroyed(
      WorkerProcessHost* process,
      int worker_route_id) OVERRIDE {
    DictionaryValue* worker_data = new DictionaryValue();
    worker_data->SetInteger(kWorkerProcessHostIdField, process->GetData().id);
    worker_data->SetInteger(kWorkerRouteIdField, worker_route_id);

    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&WorkerCreationDestructionListener::NotifyWorkerDestroyed,
                   this, base::Owned(worker_data)));
  }
  virtual void WorkerContextStarted(WorkerProcessHost*, int) OVERRIDE {}

  void NotifyWorkerCreated(DictionaryValue* worker_data) {
    if (workers_ui_) {
      workers_ui_->web_ui()->CallJavascriptFunction(
          "workerCreated", *worker_data);
    }
  }

  void NotifyWorkerDestroyed(DictionaryValue* worker_data) {
    if (workers_ui_) {
      workers_ui_->web_ui()->CallJavascriptFunction(
          "workerDestroyed", *worker_data);
    }
  }

  void RegisterObserver() {
    WorkerService::GetInstance()->AddObserver(this);
  }
  void UnregisterObserver() {
    WorkerService::GetInstance()->RemoveObserver(this);
  }

  WorkersUI* workers_ui_;
};

WorkersUI::WorkersUI(content::WebUI* web_ui)
    : WebUIController(web_ui),
      observer_(new WorkerCreationDestructionListener(this)){
  web_ui->AddMessageHandler(new WorkersDOMHandler());

  WorkersUIHTMLSource* html_source = new WorkersUIHTMLSource();

  // Set up the chrome://workers/ source.
  Profile* profile = Profile::FromWebUI(web_ui);
  profile->GetChromeURLDataManager()->AddDataSource(html_source);
}

WorkersUI::~WorkersUI() {
  observer_->WorkersUIDestroyed();
  observer_ = NULL;
}
