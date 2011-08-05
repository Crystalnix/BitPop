// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/print_preview_handler.h"

#include <string>

#include "base/i18n/file_util_icu.h"
#include "base/json/json_reader.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/printing/background_printing_manager.h"
#include "chrome/browser/printing/printer_manager_dialog.h"
#include "chrome/browser/printing/print_preview_tab_controller.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/webui/print_preview_ui.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/print_messages.h"
#include "content/browser/browser_thread.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "printing/backend/print_backend.h"
#include "printing/metafile.h"
#include "printing/metafile_impl.h"
#include "printing/page_range.h"
#include "printing/print_job_constants.h"

#if defined(USE_CUPS)
#include <cups/cups.h>

#include "base/file_util.h"
#endif

namespace {

const bool kColorDefaultValue = false;
const bool kLandscapeDefaultValue = false;

const char kDisableColorOption[] = "disableColorOption";
const char kSetColorAsDefault[] = "setColorAsDefault";

#if defined(USE_CUPS)
const char kColorDevice[] = "ColorDevice";
#elif defined(OS_WIN)
const char kPskColor[] = "psk:Color";
#endif

enum UserActionBuckets {
  PRINT_TO_PRINTER,
  PRINT_TO_PDF,
  CANCEL,
  FALLBACK_TO_ADVANCED_SETTINGS_DIALOG,
  PREVIEW_FAILED,
  PREVIEW_STARTED,
  USERACTION_BUCKET_BOUNDARY
};

enum PrintSettingsBuckets {
  LANDSCAPE,
  PORTRAIT,
  COLOR,
  BLACK_AND_WHITE,
  COLLATE,
  SIMPLEX,
  DUPLEX,
  PRINT_SETTINGS_BUCKET_BOUNDARY
};

void ReportUserActionHistogram(enum UserActionBuckets event) {
  UMA_HISTOGRAM_ENUMERATION("PrintPreview.UserAction", event,
                            USERACTION_BUCKET_BOUNDARY);
}

void ReportPrintSettingHistogram(enum PrintSettingsBuckets setting) {
  UMA_HISTOGRAM_ENUMERATION("PrintPreview.PrintSettings", setting,
                            PRINT_SETTINGS_BUCKET_BOUNDARY);
}

// Get the print job settings dictionary from |args|. The caller takes
// ownership of the returned DictionaryValue. Returns NULL on failure.
DictionaryValue* GetSettingsDictionary(const ListValue* args) {
  std::string json_str;
  if (!args->GetString(0, &json_str)) {
    NOTREACHED() << "Could not read JSON argument";
    return NULL;
  }
  if (json_str.empty()) {
    NOTREACHED() << "Empty print job settings";
    return NULL;
  }
  scoped_ptr<DictionaryValue> settings(static_cast<DictionaryValue*>(
      base::JSONReader::Read(json_str, false)));
  if (!settings.get() || !settings->IsType(Value::TYPE_DICTIONARY)) {
    NOTREACHED() << "Print job settings must be a dictionary.";
    return NULL;
  }

  if (settings->empty()) {
    NOTREACHED() << "Print job settings dictionary is empty";
    return NULL;
  }

  return settings.release();
}

int GetPageCountFromSettingsDictionary(const DictionaryValue& settings) {
  int count = 0;
  ListValue* page_range_array;
  if (settings.GetList(printing::kSettingPageRange, &page_range_array)) {
    for (size_t index = 0; index < page_range_array->GetSize(); ++index) {
      DictionaryValue* dict;
      if (!page_range_array->GetDictionary(index, &dict))
        continue;

      printing::PageRange range;
      if (!dict->GetInteger(printing::kSettingPageRangeFrom, &range.from) ||
          !dict->GetInteger(printing::kSettingPageRangeTo, &range.to)) {
        continue;
      }
      count += (range.to - range.from) + 1;
    }
  }
  return count;
}

// Track the popularity of print settings and report the stats.
void ReportPrintSettingsStats(const DictionaryValue& settings) {
  bool landscape;
  if (settings.GetBoolean(printing::kSettingLandscape, &landscape))
    ReportPrintSettingHistogram(landscape ? LANDSCAPE : PORTRAIT);

  bool collate;
  if (settings.GetBoolean(printing::kSettingCollate, &collate) && collate)
    ReportPrintSettingHistogram(COLLATE);

  int duplex_mode;
  if (settings.GetInteger(printing::kSettingDuplexMode, &duplex_mode))
    ReportPrintSettingHistogram(duplex_mode ? DUPLEX : SIMPLEX);

  bool is_color;
  if (settings.GetBoolean(printing::kSettingColor, &is_color))
    ReportPrintSettingHistogram(is_color ? COLOR : BLACK_AND_WHITE);
}

}  // namespace

class PrintSystemTaskProxy
    : public base::RefCountedThreadSafe<PrintSystemTaskProxy,
                                        BrowserThread::DeleteOnUIThread> {
 public:
  PrintSystemTaskProxy(const base::WeakPtr<PrintPreviewHandler>& handler,
                       printing::PrintBackend* print_backend,
                       bool has_logged_printers_count)
      : handler_(handler),
        print_backend_(print_backend),
        has_logged_printers_count_(has_logged_printers_count) {
  }

  void GetDefaultPrinter() {
    VLOG(1) << "Get default printer start";
    StringValue* default_printer =
        new StringValue(print_backend_->GetDefaultPrinterName());
    VLOG(1) << "Get default printer finished, found: " << default_printer;

    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this,
                          &PrintSystemTaskProxy::SendDefaultPrinter,
                          default_printer));
  }

  void SendDefaultPrinter(StringValue* default_printer) {
    if (handler_)
      handler_->SendDefaultPrinter(*default_printer);
    delete default_printer;
  }

  void EnumeratePrinters() {
    VLOG(1) << "Enumerate printers start";
    ListValue* printers = new ListValue;
    int default_printer_index = -1;

    printing::PrinterList printer_list;
    print_backend_->EnumeratePrinters(&printer_list);

    if (!has_logged_printers_count_) {
      // Record the total number of printers.
      UMA_HISTOGRAM_COUNTS("PrintPreview.NumberOfPrinters",
                           printer_list.size());
    }

    int i = 0;
    for (printing::PrinterList::iterator iter = printer_list.begin();
         iter != printer_list.end(); ++iter, ++i) {
      DictionaryValue* printer_info = new DictionaryValue;
      std::string printerName;
  #if defined(OS_MACOSX)
      // On Mac, |iter->printer_description| specifies the printer name and
      // |iter->printer_name| specifies the device name / printer queue name.
      printerName = iter->printer_description;
  #else
      printerName = iter->printer_name;
  #endif
      printer_info->SetString(printing::kSettingPrinterName, printerName);
      printer_info->SetString(printing::kSettingDeviceName, iter->printer_name);
      printers->Append(printer_info);
      if (iter->is_default)
        default_printer_index = i;
    }
    VLOG(1) << "Enumerate printers finished, found " << i << " printers";

    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this,
                          &PrintSystemTaskProxy::SendPrinterList,
                          printers,
                          new FundamentalValue(default_printer_index)));
  }

  void SendPrinterList(ListValue* printers,
                       FundamentalValue* default_printer_index) {
    if (handler_)
      handler_->SendPrinterList(*printers, *default_printer_index);
    delete printers;
    delete default_printer_index;
  }

  void GetPrinterCapabilities(const std::string& printer_name) {
    VLOG(1) << "Get printer capabilities start for " << printer_name;
    printing::PrinterCapsAndDefaults printer_info;
    bool supports_color = true;
    if (!print_backend_->GetPrinterCapsAndDefaults(printer_name,
                                                   &printer_info)) {
      return;
    }

#if defined(USE_CUPS)
    FilePath ppd_file_path;
    if (!file_util::CreateTemporaryFile(&ppd_file_path))
      return;

    int data_size = printer_info.printer_capabilities.length();
    if (data_size != file_util::WriteFile(
                         ppd_file_path,
                         printer_info.printer_capabilities.data(),
                         data_size)) {
      file_util::Delete(ppd_file_path, false);
      return;
    }

    ppd_file_t* ppd = ppdOpenFile(ppd_file_path.value().c_str());
    if (ppd) {
      ppd_attr_t* attr = ppdFindAttr(ppd, kColorDevice, NULL);
      if (attr && attr->value)
        supports_color = ppd->color_device;
      ppdClose(ppd);
    }
    file_util::Delete(ppd_file_path, false);
#elif defined(OS_WIN)
    // According to XPS 1.0 spec, only color printers have psk:Color.
    // Therefore we don't need to parse the whole XML file, we just need to
    // search the string.  The spec can be found at:
    // http://msdn.microsoft.com/en-us/windows/hardware/gg463431.
    supports_color = (printer_info.printer_capabilities.find(kPskColor) !=
                      std::string::npos);
#else
  NOTIMPLEMENTED();
#endif

    DictionaryValue settings_info;
    settings_info.SetBoolean(kDisableColorOption, !supports_color);
    settings_info.SetBoolean(kSetColorAsDefault, false);
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this,
                          &PrintSystemTaskProxy::SendPrinterCapabilities,
                          settings_info.DeepCopy()));
  }

  void SendPrinterCapabilities(DictionaryValue* settings_info) {
    if (handler_)
      handler_->SendPrinterCapabilities(*settings_info);
    delete settings_info;
  }

 private:
  friend struct BrowserThread::DeleteOnThread<BrowserThread::UI>;
  friend class DeleteTask<PrintSystemTaskProxy>;

  ~PrintSystemTaskProxy() {}

  base::WeakPtr<PrintPreviewHandler> handler_;

  scoped_refptr<printing::PrintBackend> print_backend_;

  bool has_logged_printers_count_;

  DISALLOW_COPY_AND_ASSIGN(PrintSystemTaskProxy);
};

// A Task implementation that stores a PDF file on disk.
class PrintToPdfTask : public Task {
 public:
  // Takes ownership of |metafile|.
  PrintToPdfTask(printing::Metafile* metafile, const FilePath& path)
      : metafile_(metafile), path_(path) {
    DCHECK(metafile);
  }

  ~PrintToPdfTask() {
    // This has to get deleted on the same thread it was created.
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
        new DeleteTask<printing::Metafile>(metafile_.release()));
  }

  // Task implementation
  virtual void Run() {
    metafile_->SaveTo(path_);
  }

 private:
  // The metafile holding the PDF data.
  scoped_ptr<printing::Metafile> metafile_;

  // The absolute path where the file will be saved.
  FilePath path_;
};

// static
FilePath* PrintPreviewHandler::last_saved_path_ = NULL;

PrintPreviewHandler::PrintPreviewHandler()
    : print_backend_(printing::PrintBackend::CreateInstance(NULL)),
      regenerate_preview_request_count_(0),
      manage_printers_dialog_request_count_(0),
      reported_failed_preview_(false),
      has_logged_printers_count_(false) {
  ReportUserActionHistogram(PREVIEW_STARTED);
}

PrintPreviewHandler::~PrintPreviewHandler() {
  if (select_file_dialog_.get())
    select_file_dialog_->ListenerDestroyed();
}

void PrintPreviewHandler::RegisterMessages() {
  web_ui_->RegisterMessageCallback("getDefaultPrinter",
      NewCallback(this, &PrintPreviewHandler::HandleGetDefaultPrinter));
  web_ui_->RegisterMessageCallback("getPrinters",
      NewCallback(this, &PrintPreviewHandler::HandleGetPrinters));
  web_ui_->RegisterMessageCallback("getPreview",
      NewCallback(this, &PrintPreviewHandler::HandleGetPreview));
  web_ui_->RegisterMessageCallback("print",
      NewCallback(this, &PrintPreviewHandler::HandlePrint));
  web_ui_->RegisterMessageCallback("getPrinterCapabilities",
      NewCallback(this, &PrintPreviewHandler::HandleGetPrinterCapabilities));
  web_ui_->RegisterMessageCallback("showSystemDialog",
      NewCallback(this, &PrintPreviewHandler::HandleShowSystemDialog));
  web_ui_->RegisterMessageCallback("managePrinters",
      NewCallback(this, &PrintPreviewHandler::HandleManagePrinters));
  web_ui_->RegisterMessageCallback("closePrintPreviewTab",
      NewCallback(this, &PrintPreviewHandler::HandleClosePreviewTab));
}

TabContents* PrintPreviewHandler::preview_tab() {
  return web_ui_->tab_contents();
}

void PrintPreviewHandler::HandleGetDefaultPrinter(const ListValue*) {
  scoped_refptr<PrintSystemTaskProxy> task =
      new PrintSystemTaskProxy(AsWeakPtr(),
                               print_backend_.get(),
                               has_logged_printers_count_);
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(task.get(),
                        &PrintSystemTaskProxy::GetDefaultPrinter));
}

void PrintPreviewHandler::HandleGetPrinters(const ListValue*) {
  scoped_refptr<PrintSystemTaskProxy> task =
      new PrintSystemTaskProxy(AsWeakPtr(),
                               print_backend_.get(),
                               has_logged_printers_count_);
  has_logged_printers_count_ = true;

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(task.get(),
                        &PrintSystemTaskProxy::EnumeratePrinters));
}

void PrintPreviewHandler::HandleGetPreview(const ListValue* args) {
  // Increment request count.
  ++regenerate_preview_request_count_;

  TabContents* initiator_tab = GetInitiatorTab();
  if (!initiator_tab) {
    if (!reported_failed_preview_) {
      ReportUserActionHistogram(PREVIEW_FAILED);
      reported_failed_preview_ = true;
    }
    web_ui_->CallJavascriptFunction("printPreviewFailed");
    return;
  }
  scoped_ptr<DictionaryValue> settings(GetSettingsDictionary(args));
  if (!settings.get())
    return;

  VLOG(1) << "Print preview request start";
  RenderViewHost* rvh = initiator_tab->render_view_host();
  rvh->Send(new PrintMsg_PrintPreview(rvh->routing_id(), *settings));
}

void PrintPreviewHandler::HandlePrint(const ListValue* args) {
  ReportStats();

  // Record the number of times the user requests to regenerate preview data
  // before printing.
  UMA_HISTOGRAM_COUNTS("PrintPreview.RegeneratePreviewRequest.BeforePrint",
                       regenerate_preview_request_count_);

  TabContents* initiator_tab = GetInitiatorTab();
  if (initiator_tab) {
    RenderViewHost* rvh = initiator_tab->render_view_host();
    rvh->Send(new PrintMsg_ResetScriptedPrintCount(rvh->routing_id()));
  }

  scoped_ptr<DictionaryValue> settings(GetSettingsDictionary(args));
  if (!settings.get())
    return;

  bool print_to_pdf = false;
  settings->GetBoolean(printing::kSettingPrintToPDF, &print_to_pdf);

  TabContentsWrapper* preview_tab_wrapper =
      TabContentsWrapper::GetCurrentWrapperForContents(preview_tab());

  if (print_to_pdf) {
    ReportUserActionHistogram(PRINT_TO_PDF);
    UMA_HISTOGRAM_COUNTS("PrintPreview.PageCount.PrintToPDF",
                         GetPageCountFromSettingsDictionary(*settings));

    // Pre-populating select file dialog with print job title.
    string16 print_job_title_utf16 =
        preview_tab_wrapper->print_view_manager()->RenderSourceName();

#if defined(OS_WIN)
    FilePath::StringType print_job_title(print_job_title_utf16);
#elif defined(OS_POSIX)
    FilePath::StringType print_job_title = UTF16ToUTF8(print_job_title_utf16);
#endif

    file_util::ReplaceIllegalCharactersInPath(&print_job_title, '_');
    FilePath default_filename(print_job_title);
    default_filename =
        default_filename.ReplaceExtension(FILE_PATH_LITERAL("pdf"));

    SelectFile(default_filename);
  } else {
    ReportPrintSettingsStats(*settings);
    ReportUserActionHistogram(PRINT_TO_PRINTER);
    UMA_HISTOGRAM_COUNTS("PrintPreview.PageCount.PrintToPrinter",
                         GetPageCountFromSettingsDictionary(*settings));
    g_browser_process->background_printing_manager()->OwnTabContents(
        preview_tab_wrapper);

    // The PDF being printed contains only the pages that the user selected,
    // so ignore the page range and print all pages.
    settings->Remove(printing::kSettingPageRange, NULL);
    RenderViewHost* rvh = web_ui_->GetRenderViewHost();
    rvh->Send(new PrintMsg_PrintForPrintPreview(rvh->routing_id(), *settings));
  }
}

void PrintPreviewHandler::HandleGetPrinterCapabilities(
    const ListValue* args) {
  std::string printer_name;
  bool ret = args->GetString(0, &printer_name);
  if (!ret || printer_name.empty())
    return;

  scoped_refptr<PrintSystemTaskProxy> task =
      new PrintSystemTaskProxy(AsWeakPtr(),
                               print_backend_.get(),
                               has_logged_printers_count_);

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(task.get(),
                        &PrintSystemTaskProxy::GetPrinterCapabilities,
                        printer_name));
}

void PrintPreviewHandler::HandleShowSystemDialog(const ListValue* args) {
  ReportStats();
  ReportUserActionHistogram(FALLBACK_TO_ADVANCED_SETTINGS_DIALOG);

  TabContents* initiator_tab = GetInitiatorTab();
  if (!initiator_tab)
    return;
  initiator_tab->Activate();

  TabContentsWrapper* wrapper =
      TabContentsWrapper::GetCurrentWrapperForContents(initiator_tab);
  wrapper->print_view_manager()->PrintNow();

  ClosePrintPreviewTab();
}

void PrintPreviewHandler::HandleManagePrinters(const ListValue* args) {
  ++manage_printers_dialog_request_count_;
  printing::PrinterManagerDialog::ShowPrinterManagerDialog();
}

void PrintPreviewHandler::HandleClosePreviewTab(const ListValue* args) {
  ReportStats();
  ReportUserActionHistogram(CANCEL);

  // Record the number of times the user requests to regenerate preview data
  // before cancelling.
  UMA_HISTOGRAM_COUNTS("PrintPreview.RegeneratePreviewRequest.BeforeCancel",
                       regenerate_preview_request_count_);

  ActivateInitiatorTabAndClosePreviewTab();
}

void PrintPreviewHandler::ReportStats() {
  UMA_HISTOGRAM_COUNTS("PrintPreview.ManagePrinters",
                       manage_printers_dialog_request_count_);
}

void PrintPreviewHandler::ActivateInitiatorTabAndClosePreviewTab() {
  TabContents* initiator_tab = GetInitiatorTab();
  if (initiator_tab)
    initiator_tab->Activate();
  ClosePrintPreviewTab();
}

void PrintPreviewHandler::SendPrinterCapabilities(
    const DictionaryValue& settings_info) {
  VLOG(1) << "Get printer capabilities finished";
  web_ui_->CallJavascriptFunction("updateWithPrinterCapabilities",
                                  settings_info);
}

void PrintPreviewHandler::SendDefaultPrinter(
    const StringValue& default_printer) {
  web_ui_->CallJavascriptFunction("setDefaultPrinter", default_printer);
}

void PrintPreviewHandler::SendPrinterList(
    const ListValue& printers,
    const FundamentalValue& default_printer_index) {
  web_ui_->CallJavascriptFunction("setPrinters", printers,
                                  default_printer_index);
}

TabContents* PrintPreviewHandler::GetInitiatorTab() {
  printing::PrintPreviewTabController* tab_controller =
      printing::PrintPreviewTabController::GetInstance();
  if (!tab_controller)
    return NULL;
  return tab_controller->GetInitiatorTab(preview_tab());
}

void PrintPreviewHandler::ClosePrintPreviewTab() {
  Browser* preview_tab_browser = BrowserList::FindBrowserWithID(
      preview_tab()->controller().window_id().id());
  if (!preview_tab_browser)
    return;
  TabStripModel* tabstrip = preview_tab_browser->tabstrip_model();

  // Keep print preview tab out of the recently closed tab list, because
  // re-opening that page will just display a non-functional print preview page.
  tabstrip->CloseTabContentsAt(tabstrip->GetIndexOfController(
      &preview_tab()->controller()), TabStripModel::CLOSE_NONE);
}

void PrintPreviewHandler::SelectFile(const FilePath& default_filename) {
  SelectFileDialog::FileTypeInfo file_type_info;
  file_type_info.extensions.resize(1);
  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("pdf"));

  // Initializing last_saved_path_ if it is not already initialized.
  if (!last_saved_path_) {
    last_saved_path_ = new FilePath();
    // Allowing IO operation temporarily. It is ok to do so here because
    // the select file dialog performs IO anyway in order to display the
    // folders and also it is modal.
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    PathService::Get(chrome::DIR_USER_DOCUMENTS, last_saved_path_);
  }

  if (!select_file_dialog_.get())
    select_file_dialog_ = SelectFileDialog::Create(this);

  select_file_dialog_->SelectFile(
      SelectFileDialog::SELECT_SAVEAS_FILE,
      string16(),
      last_saved_path_->Append(default_filename),
      &file_type_info,
      0,
      FILE_PATH_LITERAL(""),
      preview_tab(),
      platform_util::GetTopLevel(preview_tab()->GetNativeView()),
      NULL);
}

void PrintPreviewHandler::FileSelected(const FilePath& path,
                                       int index, void* params) {
  PrintPreviewUI* print_preview_ui = static_cast<PrintPreviewUI*>(web_ui_);
  scoped_refptr<RefCountedBytes> data(new RefCountedBytes());
  print_preview_ui->GetPrintPreviewData(&data);
  if (!data->front()) {
    NOTREACHED();
    return;
  }

  printing::PreviewMetafile* metafile = new printing::PreviewMetafile;
  metafile->InitFromData(static_cast<const void*>(data->front()), data->size());

  // Updating last_saved_path_ to the newly selected folder.
  *last_saved_path_ = path.DirName();

  PrintToPdfTask* task = new PrintToPdfTask(metafile, path);
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE, task);

  ActivateInitiatorTabAndClosePreviewTab();
}
