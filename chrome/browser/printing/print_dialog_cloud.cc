// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/print_dialog_cloud.h"
#include "chrome/browser/printing/print_dialog_cloud_internal.h"

#include "base/base64.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/json/json_reader.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process_impl.h"
#include "chrome/browser/debugger/devtools_window.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/printing/cloud_print/cloud_print_url.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/dialog_style.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/print_messages.h"
#include "chrome/common/url_constants.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "webkit/glue/webpreferences.h"

#include "grit/generated_resources.h"

#if defined(USE_AURA)
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/html_dialog_view.h"
#include "ui/views/widget/widget.h"
#endif

// This module implements the UI support in Chrome for cloud printing.
// This means hosting a dialog containing HTML/JavaScript and using
// the published cloud print user interface integration APIs to get
// page setup settings from the dialog contents and provide the
// generated print data to the dialog contents for uploading to the
// cloud print service.

// Currently, the flow between these classes is as follows:

// PrintDialogCloud::CreatePrintDialogForFile is called from
// resource_message_filter_gtk.cc once the renderer has informed the
// renderer host that print data generation into the renderer host provided
// temp file has been completed.  That call is on the FILE thread.
// That, in turn, hops over to the UI thread to create an instance of
// PrintDialogCloud.

// The constructor for PrintDialogCloud creates a
// CloudPrintHtmlDialogDelegate and asks the current active browser to
// show an HTML dialog using that class as the delegate. That class
// hands in the kChromeUICloudPrintResourcesURL as the URL to visit.  That is
// recognized by the GetWebUIFactoryFunction as a signal to create an
// ExternalHtmlDialogUI.

// CloudPrintHtmlDialogDelegate also temporarily owns a
// CloudPrintFlowHandler, a class which is responsible for the actual
// interactions with the dialog contents, including handing in the
// print data and getting any page setup parameters that the dialog
// contents provides.  As part of bringing up the dialog,
// HtmlDialogUI::RenderViewCreated is called (an override of
// WebUI::RenderViewCreated).  That routine, in turn, calls the
// delegate's GetWebUIMessageHandlers routine, at which point the
// ownership of the CloudPrintFlowHandler is handed over.  A pointer
// to the flow handler is kept to facilitate communication back and
// forth between the two classes.

// The WebUI continues dialog bring-up, calling
// CloudPrintFlowHandler::RegisterMessages.  This is where the
// additional object model capabilities are registered for the dialog
// contents to use.  It is also at this time that capabilities for the
// dialog contents are adjusted to allow the dialog contents to close
// the window.  In addition, the pending URL is redirected to the
// actual cloud print service URL.  The flow controller also registers
// for notification of when the dialog contents finish loading, which
// is currently used to send the data to the dialog contents.

// In order to send the data to the dialog contents, the flow
// handler uses a CloudPrintDataSender.  It creates one, letting it
// know the name of the temporary file containing the data, and
// posts the task of reading the file
// (CloudPrintDataSender::ReadPrintDataFile) to the file thread.  That
// routine reads in the file, and then hops over to the IO thread to
// send that data to the dialog contents.

// When the dialog contents are finished (by either being cancelled or
// hitting the print button), the delegate is notified, and responds
// that the dialog should be closed, at which point things are torn
// down and released.

// TODO(scottbyer):
// http://code.google.com/p/chromium/issues/detail?id=44093 The
// high-level flow (where the data is generated before even
// bringing up the dialog) isn't what we want.

using content::BrowserThread;
using content::NavigationController;
using content::NavigationEntry;
using content::WebContents;
using content::WebUIMessageHandler;

namespace internal_cloud_print_helpers {

// From the JSON parsed value, get the entries for the page setup
// parameters.
bool GetPageSetupParameters(const std::string& json,
                            PrintMsg_Print_Params& parameters) {
  scoped_ptr<Value> parsed_value(base::JSONReader::Read(json, false));
  DLOG_IF(ERROR, (!parsed_value.get() ||
                  !parsed_value->IsType(Value::TYPE_DICTIONARY)))
      << "PageSetup call didn't have expected contents";
  if (!parsed_value.get() || !parsed_value->IsType(Value::TYPE_DICTIONARY))
    return false;

  bool result = true;
  DictionaryValue* params = static_cast<DictionaryValue*>(parsed_value.get());
  result &= params->GetDouble("dpi", &parameters.dpi);
  result &= params->GetDouble("min_shrink", &parameters.min_shrink);
  result &= params->GetDouble("max_shrink", &parameters.max_shrink);
  result &= params->GetBoolean("selection_only", &parameters.selection_only);
  return result;
}

string16 GetSwitchValueString16(const CommandLine& command_line,
                                const char* switchName) {
#ifdef OS_WIN
  CommandLine::StringType native_switch_val;
  native_switch_val = command_line.GetSwitchValueNative(switchName);
  return string16(native_switch_val);
#elif defined(OS_POSIX)
  // POSIX Command line string types are different.
  CommandLine::StringType native_switch_val;
  native_switch_val = command_line.GetSwitchValueASCII(switchName);
  // Convert the ASCII string to UTF16 to prepare to pass.
  return string16(ASCIIToUTF16(native_switch_val));
#endif
}

void CloudPrintDataSenderHelper::CallJavascriptFunction(
    const std::wstring& function_name) {
  web_ui_->CallJavascriptFunction(WideToASCII(function_name));
}

void CloudPrintDataSenderHelper::CallJavascriptFunction(
    const std::wstring& function_name, const Value& arg) {
  web_ui_->CallJavascriptFunction(WideToASCII(function_name), arg);
}

void CloudPrintDataSenderHelper::CallJavascriptFunction(
    const std::wstring& function_name, const Value& arg1, const Value& arg2) {
  web_ui_->CallJavascriptFunction(WideToASCII(function_name), arg1, arg2);
}

void CloudPrintDataSenderHelper::CallJavascriptFunction(
    const std::wstring& function_name,
    const Value& arg1,
    const Value& arg2,
    const Value& arg3) {
  web_ui_->CallJavascriptFunction(
      WideToASCII(function_name), arg1, arg2, arg3);
}

// Clears out the pointer we're using to communicate.  Either routine is
// potentially expensive enough that stopping whatever is in progress
// is worth it.
void CloudPrintDataSender::CancelPrintDataFile() {
  base::AutoLock lock(lock_);
  // We don't own helper, it was passed in to us, so no need to
  // delete, just let it go.
  helper_ = NULL;
}

CloudPrintDataSender::CloudPrintDataSender(CloudPrintDataSenderHelper* helper,
                                           const string16& print_job_title,
                                           const string16& print_ticket,
                                           const std::string& file_type)
    : helper_(helper),
      print_job_title_(print_job_title),
      print_ticket_(print_ticket),
      file_type_(file_type) {
}

CloudPrintDataSender::~CloudPrintDataSender() {}

// Grab the raw file contents and massage them into shape for
// sending to the dialog contents (and up to the cloud print server)
// by encoding it and prefixing it with the appropriate mime type.
// Once that is done, kick off the next part of the task on the IO
// thread.
void CloudPrintDataSender::ReadPrintDataFile(const FilePath& path_to_file) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  int64 file_size = 0;
  if (file_util::GetFileSize(path_to_file, &file_size) && file_size != 0) {
    std::string file_data;
    if (file_size < kuint32max) {
      file_data.reserve(static_cast<unsigned int>(file_size));
    } else {
      DLOG(WARNING) << " print data file too large to reserve space";
    }
    if (helper_ && file_util::ReadFileToString(path_to_file, &file_data)) {
      std::string base64_data;
      base::Base64Encode(file_data, &base64_data);
      std::string header("data:");
      header.append(file_type_);
      header.append(";base64,");
      base64_data.insert(0, header);
      scoped_ptr<StringValue> new_data(new StringValue(base64_data));
      print_data_.swap(new_data);
      BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE,
          base::Bind(&CloudPrintDataSender::SendPrintDataFile, this));
    }
  }
}

// We have the data in hand that needs to be pushed into the dialog
// contents; do so from the IO thread.

// TODO(scottbyer): If the print data ends up being larger than the
// upload limit (currently 10MB), what we need to do is upload that
// large data to google docs and set the URL in the printing
// JavaScript to that location, and make sure it gets deleted when not
// needed. - 4/1/2010
void CloudPrintDataSender::SendPrintDataFile() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::AutoLock lock(lock_);
  if (helper_ && print_data_.get()) {
    StringValue title(print_job_title_);
    StringValue ticket(print_ticket_);
    // TODO(abodenha): Change Javascript call to pass in print ticket
    // after server side support is added. Add test for it.

    // Send the print data to the dialog contents.  The JavaScript
    // function is a preliminary API for prototyping purposes and is
    // subject to change.
    const_cast<CloudPrintDataSenderHelper*>(helper_)->CallJavascriptFunction(
        L"printApp._printDataUrl", *print_data_, title);
  }
}


CloudPrintFlowHandler::CloudPrintFlowHandler(const FilePath& path_to_file,
                                             const string16& print_job_title,
                                             const string16& print_ticket,
                                             const std::string& file_type,
                                             bool close_after_signin,
                                             const base::Closure& callback)
    : dialog_delegate_(NULL),
      path_to_file_(path_to_file),
      print_job_title_(print_job_title),
      print_ticket_(print_ticket),
      file_type_(file_type),
      close_after_signin_(close_after_signin),
      callback_(callback) {
}

CloudPrintFlowHandler::~CloudPrintFlowHandler() {
  // This will also cancel any task in flight.
  CancelAnyRunningTask();
}


void CloudPrintFlowHandler::SetDialogDelegate(
    CloudPrintHtmlDialogDelegate* delegate) {
  // Even if setting a new WebUI, it means any previous task needs
  // to be cancelled, it's now invalid.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CancelAnyRunningTask();
  dialog_delegate_ = delegate;
}

// Cancels any print data sender we have in flight and removes our
// reference to it, so when the task that is calling it finishes and
// removes it's reference, it goes away.
void CloudPrintFlowHandler::CancelAnyRunningTask() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (print_data_sender_.get()) {
    print_data_sender_->CancelPrintDataFile();
    print_data_sender_ = NULL;
  }
}

void CloudPrintFlowHandler::RegisterMessages() {
  // TODO(scottbyer) - This is where we will register messages for the
  // UI JS to use.  Needed: Call to update page setup parameters.
  web_ui()->RegisterMessageCallback("ShowDebugger",
      base::Bind(&CloudPrintFlowHandler::HandleShowDebugger,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("SendPrintData",
      base::Bind(&CloudPrintFlowHandler::HandleSendPrintData,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("SetPageParameters",
      base::Bind(&CloudPrintFlowHandler::HandleSetPageParameters,
                 base::Unretained(this)));

  // Register for appropriate notifications, and re-direct the URL
  // to the real server URL, now that we've gotten an HTML dialog
  // going.
  NavigationController* controller =
      &web_ui()->GetWebContents()->GetController();
  NavigationEntry* pending_entry = controller->GetPendingEntry();
  if (pending_entry) {
    Profile* profile = Profile::FromWebUI(web_ui());
    if (close_after_signin_) {
      pending_entry->SetURL(
          CloudPrintURL(profile).GetCloudPrintSigninURL());
    } else {
      pending_entry->SetURL(
          CloudPrintURL(profile).GetCloudPrintServiceDialogURL());
    }
  }
  registrar_.Add(this, content::NOTIFICATION_LOAD_STOP,
                 content::Source<NavigationController>(controller));
  if (close_after_signin_) {
    registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
                   content::Source<NavigationController>(controller));
  }
}

void CloudPrintFlowHandler::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == content::NOTIFICATION_LOAD_STOP) {
    // Take the opportunity to set some (minimal) additional
    // script permissions required for the web UI.
    GURL url = web_ui()->GetWebContents()->GetURL();
    GURL dialog_url = CloudPrintURL(
        Profile::FromWebUI(web_ui())).GetCloudPrintServiceDialogURL();
    if (url.host() == dialog_url.host() &&
        url.path() == dialog_url.path() &&
        url.scheme() == dialog_url.scheme()) {
      RenderViewHost* rvh = web_ui()->GetWebContents()->GetRenderViewHost();
      if (rvh && rvh->delegate()) {
        WebPreferences webkit_prefs = rvh->delegate()->GetWebkitPrefs();
        webkit_prefs.allow_scripts_to_close_windows = true;
        rvh->UpdateWebkitPreferences(webkit_prefs);
      } else {
        DCHECK(false);
      }
    }

    // Choose one or the other.  If you need to debug, bring up the
    // debugger.  You can then use the various chrome.send()
    // registrations above to kick of the various function calls,
    // including chrome.send("SendPrintData") in the javaScript
    // console and watch things happen with:
    // HandleShowDebugger(NULL);
    HandleSendPrintData(NULL);
  }
  if (close_after_signin_ &&
      type == content::NOTIFICATION_NAV_ENTRY_COMMITTED) {
    GURL url = web_ui()->GetWebContents()->GetURL();
    GURL dialog_url = CloudPrintURL(
        Profile::FromWebUI(web_ui())).GetCloudPrintServiceURL();

    if (url.host() == dialog_url.host() &&
        url.path() == dialog_url.path() &&
        url.scheme() == dialog_url.scheme()) {
      StoreDialogClientSize();
      web_ui()->GetWebContents()->GetRenderViewHost()->ClosePage();
      callback_.Run();
    }
  }
}

void CloudPrintFlowHandler::HandleShowDebugger(const ListValue* args) {
  ShowDebugger();
}

void CloudPrintFlowHandler::ShowDebugger() {
  if (web_ui()) {
    RenderViewHost* rvh = web_ui()->GetWebContents()->GetRenderViewHost();
    if (rvh)
      DevToolsWindow::OpenDevToolsWindow(rvh);
  }
}

scoped_refptr<CloudPrintDataSender>
CloudPrintFlowHandler::CreateCloudPrintDataSender() {
  DCHECK(web_ui());
  print_data_helper_.reset(new CloudPrintDataSenderHelper(web_ui()));
  return new CloudPrintDataSender(print_data_helper_.get(),
                                  print_job_title_,
                                  print_ticket_,
                                  file_type_);
}

void CloudPrintFlowHandler::HandleSendPrintData(const ListValue* args) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // This will cancel any ReadPrintDataFile() or SendPrintDataFile()
  // requests in flight (this is anticipation of when setting page
  // setup parameters becomes asynchronous and may be set while some
  // data is in flight).  Then we can clear out the print data.
  CancelAnyRunningTask();
  if (web_ui()) {
    print_data_sender_ = CreateCloudPrintDataSender();
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        base::Bind(&CloudPrintDataSender::ReadPrintDataFile,
                   print_data_sender_.get(), path_to_file_));
  }
}

void CloudPrintFlowHandler::HandleSetPageParameters(const ListValue* args) {
  std::string json;
  bool ret = args->GetString(0, &json);
  if (!ret || json.empty()) {
    NOTREACHED() << "Empty json string";
    return;
  }

  // These are backstop default values - 72 dpi to match the screen,
  // 8.5x11 inch paper with margins subtracted (1/4 inch top, left,
  // right and 0.56 bottom), and the min page shrink and max page
  // shrink values appear all over the place with no explanation.

  // TODO(scottbyer): Get a Linux/ChromeOS edge for PrintSettings
  // working so that we can get the default values from there.  Fix up
  // PrintWebViewHelper to do the same.
  const int kDPI = 72;
  const int kWidth = static_cast<int>((8.5-0.25-0.25)*kDPI);
  const int kHeight = static_cast<int>((11-0.25-0.56)*kDPI);
  const double kMinPageShrink = 1.25;
  const double kMaxPageShrink = 2.0;

  PrintMsg_Print_Params default_settings;
  default_settings.content_size = gfx::Size(kWidth, kHeight);
  default_settings.printable_area = gfx::Rect(0, 0, kWidth, kHeight);
  default_settings.dpi = kDPI;
  default_settings.min_shrink = kMinPageShrink;
  default_settings.max_shrink = kMaxPageShrink;
  default_settings.desired_dpi = kDPI;
  default_settings.document_cookie = 0;
  default_settings.selection_only = false;
  default_settings.preview_request_id = 0;
  default_settings.is_first_request = true;
  default_settings.print_to_pdf = false;

  if (!GetPageSetupParameters(json, default_settings)) {
    NOTREACHED();
    return;
  }

  // TODO(scottbyer) - Here is where we would kick the originating
  // renderer thread with these new parameters in order to get it to
  // re-generate the PDF data and hand it back to us.  window.print() is
  // currently synchronous, so there's a lot of work to do to get to
  // that point.
}

void CloudPrintFlowHandler::StoreDialogClientSize() const {
  if (web_ui() && web_ui()->GetWebContents() &&
      web_ui()->GetWebContents()->GetView()) {
    gfx::Size size = web_ui()->GetWebContents()->GetView()->GetContainerSize();
    Profile* profile = Profile::FromWebUI(web_ui());
    profile->GetPrefs()->SetInteger(prefs::kCloudPrintDialogWidth,
                                    size.width());
    profile->GetPrefs()->SetInteger(prefs::kCloudPrintDialogHeight,
                                    size.height());
  }
}

CloudPrintHtmlDialogDelegate::CloudPrintHtmlDialogDelegate(
    const FilePath& path_to_file,
    int width, int height,
    const std::string& json_arguments,
    const string16& print_job_title,
    const string16& print_ticket,
    const std::string& file_type,
    bool modal,
    bool delete_on_close,
    bool close_after_signin,
    const base::Closure& callback)
    : delete_on_close_(delete_on_close),
      flow_handler_(new CloudPrintFlowHandler(path_to_file,
                                              print_job_title,
                                              print_ticket,
                                              file_type,
                                              close_after_signin,
                                              callback)),
      modal_(modal),
      owns_flow_handler_(true),
      path_to_file_(path_to_file) {
  Init(width, height, json_arguments);
}

// For unit testing.
CloudPrintHtmlDialogDelegate::CloudPrintHtmlDialogDelegate(
    CloudPrintFlowHandler* flow_handler,
    int width, int height,
    const std::string& json_arguments,
    bool modal,
    bool delete_on_close)
    : delete_on_close_(delete_on_close),
      flow_handler_(flow_handler),
      modal_(modal),
      owns_flow_handler_(true) {
  Init(width, height, json_arguments);
}

void CloudPrintHtmlDialogDelegate::Init(int width, int height,
                                        const std::string& json_arguments) {
  // This information is needed to show the dialog HTML content.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  params_.url = GURL(chrome::kChromeUICloudPrintResourcesURL);
  params_.height = height;
  params_.width = width;
  params_.json_input = json_arguments;

  flow_handler_->SetDialogDelegate(this);
  // If we're not modal we can show the dialog with no browser.
  // We need this to keep Chrome alive while our dialog is up.
  if (!modal_)
    BrowserList::StartKeepAlive();
}

CloudPrintHtmlDialogDelegate::~CloudPrintHtmlDialogDelegate() {
  // If the flow_handler_ is about to outlive us because we don't own
  // it anymore, we need to have it remove it's reference to us.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  flow_handler_->SetDialogDelegate(NULL);
  if (owns_flow_handler_) {
    delete flow_handler_;
  }
}

ui::ModalType CloudPrintHtmlDialogDelegate::GetDialogModalType() const {
    return modal_ ? ui::MODAL_TYPE_WINDOW : ui::MODAL_TYPE_NONE;
}

string16 CloudPrintHtmlDialogDelegate::GetDialogTitle() const {
  return string16();
}

GURL CloudPrintHtmlDialogDelegate::GetDialogContentURL() const {
  return params_.url;
}

void CloudPrintHtmlDialogDelegate::GetWebUIMessageHandlers(
    std::vector<WebUIMessageHandler*>* handlers) const {
  handlers->push_back(flow_handler_);
  // We don't own flow_handler_ anymore, but it sticks around until at
  // least right after OnDialogClosed() is called (and this object is
  // destroyed).
  owns_flow_handler_ = false;
}

void CloudPrintHtmlDialogDelegate::GetDialogSize(gfx::Size* size) const {
  size->set_width(params_.width);
  size->set_height(params_.height);
}

std::string CloudPrintHtmlDialogDelegate::GetDialogArgs() const {
  return params_.json_input;
}

void CloudPrintHtmlDialogDelegate::OnDialogClosed(
    const std::string& json_retval) {
  // Get the final dialog size and store it.
  flow_handler_->StoreDialogClientSize();

  if (delete_on_close_) {
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        base::Bind(&internal_cloud_print_helpers::Delete, path_to_file_));
  }

  // If we're modal we can show the dialog with no browser.
  // End the keep-alive so that Chrome can exit.
  if (!modal_)
    BrowserList::EndKeepAlive();
  delete this;
}

void CloudPrintHtmlDialogDelegate::OnCloseContents(WebContents* source,
                                                   bool* out_close_dialog) {
  if (out_close_dialog)
    *out_close_dialog = true;
}

bool CloudPrintHtmlDialogDelegate::ShouldShowDialogTitle() const {
  return false;
}

bool CloudPrintHtmlDialogDelegate::HandleContextMenu(
    const ContextMenuParams& params) {
  return true;
}

void CreatePrintDialogForBytesImpl(scoped_refptr<RefCountedBytes> data,
                                   const string16& print_job_title,
                                   const string16& print_ticket,
                                   const std::string& file_type,
                                   bool modal) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  // TODO(abodenha@chromium.org) Writing the PDF to a file before printing
  // is wasteful.  Modify the dialog flow to pull PDF data from memory.
  // See http://code.google.com/p/chromium/issues/detail?id=44093
  FilePath path;
  if (file_util::CreateTemporaryFile(&path)) {
    file_util::WriteFile(path,
                         reinterpret_cast<const char*>(data->front()),
                         data->size());
  }
  print_dialog_cloud::CreatePrintDialogForFile(path,
                                               print_job_title,
                                               print_ticket,
                                               file_type,
                                               modal,
                                               true);
}

// Called from the UI thread, starts up the dialog.
void CreateDialogImpl(const FilePath& path_to_file,
                      const string16& print_job_title,
                      const string16& print_ticket,
                      const std::string& file_type,
                      bool modal,
                      bool delete_on_close,
                      bool close_after_signin,
                      const base::Closure& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  Browser* browser = BrowserList::GetLastActive();

  const int kDefaultWidth = 912;
  const int kDefaultHeight = 633;
  string16 job_title = print_job_title;
  Profile* profile = NULL;
  if (modal) {
    if (job_title.empty()) {
      WebContents* web_contents = browser->GetSelectedWebContents();
      if (web_contents)
        job_title = web_contents->GetTitle();
    }
    profile = browser->GetProfile();
  } else {
    std::vector<Profile*> loaded_profiles =
        g_browser_process->profile_manager()->GetLoadedProfiles();
    DCHECK_GT(loaded_profiles.size(), 0U);
    profile = loaded_profiles[0];
    browser = BrowserList::GetLastActiveWithProfile(profile);
  }
  DCHECK(profile);
  PrefService* pref_service = profile->GetPrefs();
  DCHECK(pref_service);
  if (!pref_service->FindPreference(prefs::kCloudPrintDialogWidth)) {
    pref_service->RegisterIntegerPref(prefs::kCloudPrintDialogWidth,
                                      kDefaultWidth,
                                      PrefService::UNSYNCABLE_PREF);
  }
  if (!pref_service->FindPreference(prefs::kCloudPrintDialogHeight)) {
    pref_service->RegisterIntegerPref(prefs::kCloudPrintDialogHeight,
                                      kDefaultHeight,
                                      PrefService::UNSYNCABLE_PREF);
  }

  int width = pref_service->GetInteger(prefs::kCloudPrintDialogWidth);
  int height = pref_service->GetInteger(prefs::kCloudPrintDialogHeight);

  HtmlDialogUIDelegate* dialog_delegate =
      new internal_cloud_print_helpers::CloudPrintHtmlDialogDelegate(
          path_to_file, width, height, std::string(), job_title, print_ticket,
          file_type, modal, delete_on_close, close_after_signin,
          callback);
  if (modal) {
    DCHECK(browser);
#if defined(USE_AURA)
    HtmlDialogView* html_view =
        new HtmlDialogView(profile, browser, dialog_delegate);
    views::Widget::CreateWindowWithParent(html_view,
        browser->window()->GetNativeHandle());
    html_view->InitDialog();
    html_view->GetWidget()->Show();
#else
    browser->BrowserShowHtmlDialog(dialog_delegate, NULL, STYLE_GENERIC);
#endif
  } else {
    browser::ShowHtmlDialog(NULL,
                            profile,
                            browser,
                            dialog_delegate,
                            STYLE_GENERIC);
  }
}

void CreateDialogSigninImpl(const base::Closure& callback) {
  CreateDialogImpl(FilePath(), string16(), string16(), std::string(),
                   true, false, true, callback);
}

void CreateDialogFullImpl(const FilePath& path_to_file,
                      const string16& print_job_title,
                      const string16& print_ticket,
                      const std::string& file_type,
                      bool modal,
                      bool delete_on_close) {
  CreateDialogImpl(path_to_file, print_job_title, print_ticket, file_type,
                   modal, delete_on_close, false, base::Closure());
}



// Provides a runnable function to delete a file.
void Delete(const FilePath& file_path) {
  file_util::Delete(file_path, false);
}

}  // namespace internal_cloud_print_helpers

namespace print_dialog_cloud {

// Called on the FILE or UI thread.  This is the main entry point into creating
// the dialog.

// TODO(scottbyer): The signature here will need to change as the
// workflow through the printing code changes to allow for dynamically
// changing page setup parameters while the dialog is active.
void CreatePrintDialogForFile(const FilePath& path_to_file,
                              const string16& print_job_title,
                              const string16& print_ticket,
                              const std::string& file_type,
                              bool modal,
                              bool delete_on_close) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&internal_cloud_print_helpers::CreateDialogFullImpl,
                 path_to_file, print_job_title, print_ticket, file_type, modal,
                 delete_on_close));
}

void CreateCloudPrintSigninDialog(const base::Closure& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&internal_cloud_print_helpers::CreateDialogSigninImpl,
                 callback));
}

void CreatePrintDialogForBytes(scoped_refptr<RefCountedBytes> data,
                               const string16& print_job_title,
                               const string16& print_ticket,
                               const std::string& file_type,
                               bool modal) {
  // TODO(abodenha@chromium.org) Avoid cloning the PDF data.  Make use of a
  // shared memory object instead.
  // http://code.google.com/p/chromium/issues/detail?id=44093
  scoped_refptr<RefCountedBytes> cloned_data(new RefCountedBytes(data->data()));
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&internal_cloud_print_helpers::CreatePrintDialogForBytesImpl,
                 cloned_data, print_job_title, print_ticket, file_type, modal));
}

bool CreatePrintDialogFromCommandLine(const CommandLine& command_line) {
  DCHECK(command_line.HasSwitch(switches::kCloudPrintFile));
  if (!command_line.GetSwitchValuePath(switches::kCloudPrintFile).empty()) {
    FilePath cloud_print_file;
    cloud_print_file =
        command_line.GetSwitchValuePath(switches::kCloudPrintFile);
    if (!cloud_print_file.empty()) {
      string16 print_job_title;
      string16 print_job_print_ticket;
      if (command_line.HasSwitch(switches::kCloudPrintJobTitle)) {
        print_job_title =
          internal_cloud_print_helpers::GetSwitchValueString16(
              command_line, switches::kCloudPrintJobTitle);
      }
      if (command_line.HasSwitch(switches::kCloudPrintPrintTicket)) {
        print_job_print_ticket =
          internal_cloud_print_helpers::GetSwitchValueString16(
              command_line, switches::kCloudPrintPrintTicket);
      }
      std::string file_type = "application/pdf";
      if (command_line.HasSwitch(switches::kCloudPrintFileType)) {
        file_type = command_line.GetSwitchValueASCII(
            switches::kCloudPrintFileType);
      }

      bool delete_on_close = CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kCloudPrintDeleteFile);

      print_dialog_cloud::CreatePrintDialogForFile(cloud_print_file,
                                                   print_job_title,
                                                   print_job_print_ticket,
                                                   file_type,
                                                   false,
                                                   delete_on_close);
      return true;
    }
  }
  return false;
}

}  // end namespace
