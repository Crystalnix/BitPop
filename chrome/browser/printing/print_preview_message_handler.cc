// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/print_preview_message_handler.h"

#include "base/memory/ref_counted.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/printing/print_job_manager.h"
#include "chrome/browser/printing/print_preview_tab_controller.h"
#include "chrome/browser/printing/print_view_manager.h"
#include "chrome/browser/printing/printer_query.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/webui/print_preview_handler.h"
#include "chrome/browser/ui/webui/print_preview_ui.h"
#include "chrome/common/print_messages.h"
#include "content/browser/browser_thread.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/content_restriction.h"

namespace {

void StopWorker(int document_cookie) {
  printing::PrintJobManager* print_job_manager =
      g_browser_process->print_job_manager();
  scoped_refptr<printing::PrinterQuery> printer_query;
  print_job_manager->PopPrinterQuery(document_cookie, &printer_query);
  if (printer_query.get()) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        NewRunnableMethod(printer_query.get(),
                          &printing::PrinterQuery::StopWorker));
  }
}

}  // namespace

namespace printing {

PrintPreviewMessageHandler::PrintPreviewMessageHandler(
    TabContents* tab_contents)
    : TabContentsObserver(tab_contents) {
  DCHECK(tab_contents);
}

PrintPreviewMessageHandler::~PrintPreviewMessageHandler() {
}

TabContents* PrintPreviewMessageHandler::GetPrintPreviewTab() {
  // Get/Create preview tab for initiator tab.
  printing::PrintPreviewTabController* tab_controller =
      printing::PrintPreviewTabController::GetInstance();
  if (!tab_controller)
    return NULL;
  return tab_controller->GetPrintPreviewForTab(tab_contents());
}

void PrintPreviewMessageHandler::OnRequestPrintPreview() {
  PrintPreviewTabController::PrintPreview(tab_contents());
}

void PrintPreviewMessageHandler::OnPagesReadyForPreview(
    const PrintHostMsg_DidPreviewDocument_Params& params) {
  // Always need to stop the worker and send PrintMsg_PrintingDone.
  StopWorker(params.document_cookie);

  RenderViewHost* rvh = tab_contents()->render_view_host();
  rvh->Send(new PrintMsg_PrintingDone(rvh->routing_id(), true));

  // Get the print preview tab.
  TabContents* print_preview_tab = GetPrintPreviewTab();
  // User might have closed it already.
  if (!print_preview_tab)
    return;

  base::SharedMemory* shared_buf =
      new base::SharedMemory(params.metafile_data_handle, true);
  if (!shared_buf->Map(params.data_size)) {
    NOTREACHED();
    delete shared_buf;
    return;
  }

  TabContentsWrapper* wrapper =
      TabContentsWrapper::GetCurrentWrapperForContents(print_preview_tab);
  wrapper->print_view_manager()->OverrideTitle(tab_contents());

  PrintPreviewUI* print_preview_ui =
      static_cast<PrintPreviewUI*>(print_preview_tab->web_ui());

  char* preview_data = static_cast<char*>(shared_buf->memory());
  uint32 preview_data_size = params.data_size;

  scoped_refptr<RefCountedBytes> html_bytes(new RefCountedBytes);
  html_bytes->data.resize(preview_data_size);
  std::vector<unsigned char>::iterator it = html_bytes->data.begin();
  for (uint32 i = 0; i < preview_data_size; ++i, ++it)
    *it = *(preview_data + i);

  print_preview_ui->SetPrintPreviewData(html_bytes.get());
  print_preview_ui->OnPreviewDataIsAvailable(
      params.expected_pages_count,
      wrapper->print_view_manager()->RenderSourceName(),
      params.modifiable);
}

void PrintPreviewMessageHandler::OnPrintPreviewFailed(int document_cookie) {
  // Always need to stop the worker.
  StopWorker(document_cookie);

  // Inform the print preview tab of the failure.
  TabContents* print_preview_tab = GetPrintPreviewTab();
  // User might have closed it already.
  if (!print_preview_tab)
    return;
  print_preview_tab->web_ui()->CallJavascriptFunction("printPreviewFailed");
}

bool PrintPreviewMessageHandler::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PrintPreviewMessageHandler, message)
    IPC_MESSAGE_HANDLER(PrintHostMsg_RequestPrintPreview,
                        OnRequestPrintPreview)
    IPC_MESSAGE_HANDLER(PrintHostMsg_PagesReadyForPreview,
                        OnPagesReadyForPreview)
    IPC_MESSAGE_HANDLER(PrintHostMsg_PrintPreviewFailed,
                        OnPrintPreviewFailed)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PrintPreviewMessageHandler::DidStartLoading() {
  if (tab_contents()->delegate() &&
      printing::PrintPreviewTabController::IsPrintPreviewTab(tab_contents())) {
    tab_contents()->SetContentRestrictions(CONTENT_RESTRICTION_PRINT);
  }
}

}  // namespace printing
