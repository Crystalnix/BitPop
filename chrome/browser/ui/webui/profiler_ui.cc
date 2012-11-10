// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/profiler_ui.h"

#include <string>

// When testing the javacript code, it is cumbersome to have to keep
// re-building the resouces package and reloading the browser. To solve
// this, enable the following flag to read the webapp's source files
// directly off disk, so all you have to do is refresh the page to
// test the modifications.
// #define USE_SOURCE_FILES_DIRECTLY

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/tracked_objects.h"
#include "base/values.h"
#include "chrome/browser/metrics/tracking_synchronizer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/task_profiler/task_profiler_data_serializer.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/chrome_web_ui_data_source.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"

#ifdef USE_SOURCE_FILES_DIRECTLY
#include "base/base_paths.h"
#include "base/file_util.h"
#include "base/memory/ref_counted_memory.h"
#include "base/path_service.h"
#endif  //  USE_SOURCE_FILES_DIRECTLY

using chrome_browser_metrics::TrackingSynchronizer;
using content::BrowserThread;
using content::WebContents;
using content::WebUIMessageHandler;

namespace {

#ifdef USE_SOURCE_FILES_DIRECTLY

class ProfilerWebUIDataSource : public ChromeURLDataManager::DataSource {
 public:
  ProfilerWebUIDataSource()
      : DataSource(chrome::kChromeUIProfilerHost, MessageLoop::current()) {
  }

 protected:
  // ChromeURLDataManager
  virtual std::string GetMimeType(const std::string& path) const OVERRIDE {
    if (EndsWith(path, ".js", false))
      return "application/javascript";
    return "text/html";
  }

  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id) OVERRIDE {
    FilePath base_path;
    PathService::Get(base::DIR_SOURCE_ROOT, &base_path);
    base_path = base_path.AppendASCII("chrome");
    base_path = base_path.AppendASCII("browser");
    base_path = base_path.AppendASCII("resources");
    base_path = base_path.AppendASCII("profiler");

    // If no resource was specified, default to profiler.html.
    std::string filename = path.empty() ? "profiler.html" : path;

    FilePath file_path;
    file_path = base_path.AppendASCII(filename);

    // Read the file synchronously and send it as the response.
    base::ThreadRestrictions::ScopedAllowIO allow;
    std::string file_contents;
    if (!file_util::ReadFileToString(file_path, &file_contents))
      LOG(ERROR) << "Couldn't read file: " << file_path.value();
    scoped_refptr<base::RefCountedString> response =
        new base::RefCountedString();
    response->data() = file_contents;
    SendResponse(request_id, response);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ProfilerWebUIDataSource);
};

ChromeURLDataManager::DataSource* CreateProfilerHTMLSource() {
  return new ProfilerWebUIDataSource();
}

#else  // USE_SOURCE_FILES_DIRECTLY

ChromeWebUIDataSource* CreateProfilerHTMLSource() {
  ChromeWebUIDataSource* source =
      new ChromeWebUIDataSource(chrome::kChromeUIProfilerHost);

  source->set_json_path("strings.js");
  source->add_resource_path("profiler.js", IDR_PROFILER_JS);
  source->set_default_resource(IDR_PROFILER_HTML);
  return source;
}

#endif

// This class receives javascript messages from the renderer.
// Note that the WebUI infrastructure runs on the UI thread, therefore all of
// this class's methods are expected to run on the UI thread.
class ProfilerMessageHandler : public WebUIMessageHandler {
 public:
  ProfilerMessageHandler() {}

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // Messages.
  void OnGetData(const ListValue* list);
  void OnResetData(const ListValue* list);

 private:
  DISALLOW_COPY_AND_ASSIGN(ProfilerMessageHandler);
};

void ProfilerMessageHandler::RegisterMessages() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  web_ui()->RegisterMessageCallback("getData",
      base::Bind(&ProfilerMessageHandler::OnGetData, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("resetData",
      base::Bind(&ProfilerMessageHandler::OnResetData,
                 base::Unretained(this)));
}

void ProfilerMessageHandler::OnGetData(const ListValue* list) {
  ProfilerUI* profiler_ui = static_cast<ProfilerUI*>(web_ui()->GetController());
  profiler_ui->GetData();
}

void ProfilerMessageHandler::OnResetData(const ListValue* list) {
  tracked_objects::ThreadData::ResetAllThreadData();
}

}  // namespace

ProfilerUI::ProfilerUI(content::WebUI* web_ui)
    : WebUIController(web_ui),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)) {
  web_ui->AddMessageHandler(new ProfilerMessageHandler());

  // Set up the chrome://profiler/ source.
  Profile* profile = Profile::FromWebUI(web_ui);
  ChromeURLDataManager::AddDataSource(profile, CreateProfilerHTMLSource());
}

ProfilerUI::~ProfilerUI() {
}

void ProfilerUI::GetData() {
  TrackingSynchronizer::FetchProfilerDataAsynchronously(
      weak_ptr_factory_.GetWeakPtr());
}

void ProfilerUI::ReceivedProfilerData(
    const tracked_objects::ProcessDataSnapshot& profiler_data,
    content::ProcessType process_type) {
  // Serialize the data to JSON.
  DictionaryValue json_data;
  task_profiler::TaskProfilerDataSerializer::ToValue(profiler_data,
                                                     process_type,
                                                     &json_data);

  // Send the data to the renderer.
  web_ui()->CallJavascriptFunction("g_browserBridge.receivedData", json_data);
}
