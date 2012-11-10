// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/print_preview_message_handler.h"

#include <vector>

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/memory/ref_counted_memory.h"
#include "base/shared_memory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/printing/print_job_manager.h"
#include "chrome/browser/printing/print_preview_tab_controller.h"
#include "chrome/browser/printing/print_view_manager.h"
#include "chrome/browser/printing/printer_query.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/webui/print_preview/print_preview_ui.h"
#include "chrome/common/print_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "printing/page_size_margins.h"
#include "printing/print_job_constants.h"

using content::BrowserThread;
using content::NavigationController;
using content::WebContents;

namespace {

void StopWorker(int document_cookie) {
  if (document_cookie <= 0)
    return;

  printing::PrintJobManager* print_job_manager =
      g_browser_process->print_job_manager();
  scoped_refptr<printing::PrinterQuery> printer_query;
  print_job_manager->PopPrinterQuery(document_cookie, &printer_query);
  if (printer_query.get()) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&printing::PrinterQuery::StopWorker, printer_query.get()));
  }
}

base::RefCountedBytes* GetDataFromHandle(base::SharedMemoryHandle handle,
                                         uint32 data_size) {
  scoped_ptr<base::SharedMemory> shared_buf(
      new base::SharedMemory(handle, true));
  if (!shared_buf->Map(data_size)) {
    NOTREACHED();
    return NULL;
  }

  char* preview_data = static_cast<char*>(shared_buf->memory());
  std::vector<unsigned char> data(data_size);
  memcpy(&data[0], preview_data, data_size);
  return base::RefCountedBytes::TakeVector(&data);
}

}  // namespace

namespace printing {

PrintPreviewMessageHandler::PrintPreviewMessageHandler(
    WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {
  DCHECK(web_contents);
}

PrintPreviewMessageHandler::~PrintPreviewMessageHandler() {
}

TabContents* PrintPreviewMessageHandler::GetPrintPreviewTab() {
  PrintPreviewTabController* tab_controller =
      PrintPreviewTabController::GetInstance();
  if (!tab_controller)
    return NULL;

  return tab_controller->GetPrintPreviewForTab(tab_contents());
}

TabContents* PrintPreviewMessageHandler::tab_contents() {
  return TabContents::FromWebContents(web_contents());
}

PrintPreviewUI* PrintPreviewMessageHandler::GetPrintPreviewUI() {
  TabContents* tab = GetPrintPreviewTab();
  if (!tab || !tab->web_contents()->GetWebUI())
    return NULL;
  return static_cast<PrintPreviewUI*>(
      tab->web_contents()->GetWebUI()->GetController());
}

void PrintPreviewMessageHandler::OnRequestPrintPreview(
    bool source_is_modifiable, bool webnode_only) {
  TabContents* tab = tab_contents();
  if (webnode_only)
    tab->print_view_manager()->PrintPreviewForWebNode();
  PrintPreviewTabController::PrintPreview(tab);
  PrintPreviewUI::SetSourceIsModifiable(GetPrintPreviewTab(),
                                        source_is_modifiable);
}

void PrintPreviewMessageHandler::OnDidGetPreviewPageCount(
    const PrintHostMsg_DidGetPreviewPageCount_Params& params) {
  if (params.page_count <= 0) {
    NOTREACHED();
    return;
  }

  PrintPreviewUI* print_preview_ui = GetPrintPreviewUI();
  if (!print_preview_ui)
    return;

  if (!params.is_modifiable || params.clear_preview_data)
    print_preview_ui->ClearAllPreviewData();

  print_preview_ui->OnDidGetPreviewPageCount(params);
}

void PrintPreviewMessageHandler::OnDidPreviewPage(
    const PrintHostMsg_DidPreviewPage_Params& params) {
  int page_number = params.page_number;
  if (page_number < FIRST_PAGE_INDEX || !params.data_size)
    return;

  PrintPreviewUI* print_preview_ui = GetPrintPreviewUI();
  if (!print_preview_ui)
    return;

  base::RefCountedBytes* data_bytes =
      GetDataFromHandle(params.metafile_data_handle, params.data_size);
  DCHECK(data_bytes);

  print_preview_ui->SetPrintPreviewDataForIndex(page_number, data_bytes);
  print_preview_ui->OnDidPreviewPage(page_number, params.preview_request_id);
}

void PrintPreviewMessageHandler::OnMetafileReadyForPrinting(
    const PrintHostMsg_DidPreviewDocument_Params& params) {
  // Always try to stop the worker.
  StopWorker(params.document_cookie);

  if (params.expected_pages_count <= 0) {
    NOTREACHED();
    return;
  }

  PrintPreviewUI* print_preview_ui = GetPrintPreviewUI();
  if (!print_preview_ui)
    return;

  if (params.reuse_existing_data) {
    // Need to match normal rendering where we are expected to send this.
    PrintHostMsg_DidGetPreviewPageCount_Params temp_params;
    temp_params.page_count = params.expected_pages_count;
    temp_params.document_cookie = params.document_cookie;
    temp_params.is_modifiable = params.modifiable;
    temp_params.preview_request_id = params.preview_request_id;
    print_preview_ui->OnDidGetPreviewPageCount(temp_params);
    print_preview_ui->OnReusePreviewData(params.preview_request_id);
    return;
  }

  // TODO(joth): This seems like a good match for using RefCountedStaticMemory
  // to avoid the memory copy, but the SetPrintPreviewData call chain below
  // needs updating to accept the RefCountedMemory* base class.
  base::RefCountedBytes* data_bytes =
      GetDataFromHandle(params.metafile_data_handle, params.data_size);
  if (!data_bytes)
    return;

  print_preview_ui->SetPrintPreviewDataForIndex(COMPLETE_PREVIEW_DOCUMENT_INDEX,
                                                data_bytes);
  print_preview_ui->OnPreviewDataIsAvailable(
      params.expected_pages_count, params.preview_request_id);
}

void PrintPreviewMessageHandler::OnPrintPreviewFailed(int document_cookie) {
  StopWorker(document_cookie);

  PrintPreviewUI* print_preview_ui = GetPrintPreviewUI();
  if (!print_preview_ui)
    return;
  print_preview_ui->OnPrintPreviewFailed();
}

void PrintPreviewMessageHandler::OnDidGetDefaultPageLayout(
    const PageSizeMargins& page_layout_in_points,
    const gfx::Rect& printable_area_in_points,
    bool has_custom_page_size_style) {
  PrintPreviewUI* print_preview_ui = GetPrintPreviewUI();
  if (!print_preview_ui)
    return;
  print_preview_ui->OnDidGetDefaultPageLayout(page_layout_in_points,
                                              printable_area_in_points,
                                              has_custom_page_size_style);
}

void PrintPreviewMessageHandler::OnPrintPreviewCancelled(int document_cookie) {
  // Always need to stop the worker.
  StopWorker(document_cookie);
}

void PrintPreviewMessageHandler::OnInvalidPrinterSettings(int document_cookie) {
  StopWorker(document_cookie);
  PrintPreviewUI* print_preview_ui = GetPrintPreviewUI();
  if (!print_preview_ui)
    return;
  print_preview_ui->OnInvalidPrinterSettings();
}

void PrintPreviewMessageHandler::OnPrintPreviewScalingDisabled() {
  PrintPreviewUI* print_preview_ui = GetPrintPreviewUI();
  if (!print_preview_ui)
    return;
  print_preview_ui->OnPrintPreviewScalingDisabled();
}

bool PrintPreviewMessageHandler::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PrintPreviewMessageHandler, message)
    IPC_MESSAGE_HANDLER(PrintHostMsg_RequestPrintPreview,
                        OnRequestPrintPreview)
    IPC_MESSAGE_HANDLER(PrintHostMsg_DidGetPreviewPageCount,
                        OnDidGetPreviewPageCount)
    IPC_MESSAGE_HANDLER(PrintHostMsg_DidPreviewPage,
                        OnDidPreviewPage)
    IPC_MESSAGE_HANDLER(PrintHostMsg_MetafileReadyForPrinting,
                        OnMetafileReadyForPrinting)
    IPC_MESSAGE_HANDLER(PrintHostMsg_PrintPreviewFailed,
                        OnPrintPreviewFailed)
    IPC_MESSAGE_HANDLER(PrintHostMsg_DidGetDefaultPageLayout,
                        OnDidGetDefaultPageLayout)
    IPC_MESSAGE_HANDLER(PrintHostMsg_PrintPreviewCancelled,
                        OnPrintPreviewCancelled)
    IPC_MESSAGE_HANDLER(PrintHostMsg_PrintPreviewInvalidPrinterSettings,
                        OnInvalidPrinterSettings)
    IPC_MESSAGE_HANDLER(PrintHostMsg_PrintPreviewScalingDisabled,
                        OnPrintPreviewScalingDisabled)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PrintPreviewMessageHandler::NavigateToPendingEntry(
    const GURL& url,
    NavigationController::ReloadType reload_type) {
  TabContents* tab = tab_contents();
  TabContents* preview_tab = GetPrintPreviewTab();
  if (tab == preview_tab) {
    // Cloud print sign-in reloads the page.
    DCHECK(PrintPreviewTabController::IsPrintPreviewURL(url));
    DCHECK_EQ(NavigationController::RELOAD, reload_type);
    return;
  }
  // If |tab| is navigating and it has a print preview tab, notify |tab| to
  // consider print preview done so it unfreezes the renderer in the case of
  // window.print().
  if (preview_tab)
    tab->print_view_manager()->PrintPreviewDone();
}

}  // namespace printing
