// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/print_job_worker.h"

#include "base/message_loop.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/printing/print_job.h"
#include "content/browser/browser_thread.h"
#include "content/common/notification_service.h"
#include "printing/printed_document.h"
#include "printing/printed_page.h"

namespace printing {

class PrintJobWorker::NotificationTask : public Task {
 public:
  NotificationTask() : print_job_(NULL), details_(NULL) {
  }
  ~NotificationTask() {
  }

  // Initializes the object. This object can't be initialized in the constructor
  // since it is not created directly.
  void Init(PrintJobWorkerOwner* print_job,
            JobEventDetails::Type detail_type,
            PrintedDocument* document,
            PrintedPage* page) {
    DCHECK(!print_job_);
    DCHECK(!details_);
    print_job_ = print_job;
    details_ = new JobEventDetails(detail_type, document, page);
  }

  virtual void Run() {
    // Send the notification in the right thread.
    NotificationService::current()->Notify(
        NotificationType::PRINT_JOB_EVENT,
        // We know that is is a PrintJob object in this circumstance.
        Source<PrintJob>(static_cast<PrintJob*>(print_job_.get())),
        Details<JobEventDetails>(details_));
  }

  // The job which originates this notification.
  scoped_refptr<PrintJobWorkerOwner> print_job_;
  scoped_refptr<JobEventDetails> details_;
};


PrintJobWorker::PrintJobWorker(PrintJobWorkerOwner* owner)
    : Thread("Printing_Worker"),
      owner_(owner) {
  // The object is created in the IO thread.
  DCHECK_EQ(owner_->message_loop(), MessageLoop::current());

  printing_context_.reset(PrintingContext::Create(
      g_browser_process->GetApplicationLocale()));
}

PrintJobWorker::~PrintJobWorker() {
  // The object is normally deleted in the UI thread, but when the user
  // cancels printing or in the case of print preview, the worker is destroyed
  // on the I/O thread.
  DCHECK_EQ(owner_->message_loop(), MessageLoop::current());
}

void PrintJobWorker::SetNewOwner(PrintJobWorkerOwner* new_owner) {
  DCHECK(page_number_ == PageNumber::npos());
  owner_ = new_owner;
}

void PrintJobWorker::GetSettings(bool ask_user_for_settings,
                                 gfx::NativeView parent_view,
                                 int document_page_count,
                                 bool has_selection,
                                 bool use_overlays) {
  DCHECK_EQ(message_loop(), MessageLoop::current());
  DCHECK_EQ(page_number_, PageNumber::npos());

  // Recursive task processing is needed for the dialog in case it needs to be
  // destroyed by a task.
  // TODO(thestig): this code is wrong, SetNestableTasksAllowed(true) is needed
  // on the thread where the PrintDlgEx is called, and definitely both calls
  // should happen on the same thread. See http://crbug.com/73466
  // MessageLoop::current()->SetNestableTasksAllowed(true);
  printing_context_->set_use_overlays(use_overlays);

  if (ask_user_for_settings) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this, &PrintJobWorker::GetSettingsWithUI,
                          parent_view, document_page_count,
                          has_selection));
  } else {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this, &PrintJobWorker::UseDefaultSettings));
  }
}

void PrintJobWorker::SetSettings(const DictionaryValue* const new_settings) {
  DCHECK_EQ(message_loop(), MessageLoop::current());

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      NewRunnableMethod(this, &PrintJobWorker::UpdatePrintSettings,
                        new_settings));
}

void PrintJobWorker::UpdatePrintSettings(
    const DictionaryValue* const new_settings) {
  // Create new PageRanges based on |new_settings|.
  PageRanges new_ranges;
  ListValue* page_range_array;
  if (new_settings->GetList("pageRange", &page_range_array)) {
    for (size_t index = 0; index < page_range_array->GetSize(); ++index) {
      DictionaryValue* dict;
      if (page_range_array->GetDictionary(index, &dict)) {
        PageRange range;
        if (dict->GetInteger("from", &range.from) &&
            dict->GetInteger("to", &range.to)) {
          // Page numbers are 0-based.
          range.from--;
          range.to--;
          new_ranges.push_back(range);
        }
      }
    }
  }
  PrintingContext::Result result =
      printing_context_->UpdatePrintSettings(*new_settings, new_ranges);
  delete new_settings;
  GetSettingsDone(result);
}

void PrintJobWorker::GetSettingsDone(PrintingContext::Result result) {
  // Most PrintingContext functions may start a message loop and process
  // message recursively, so disable recursive task processing.
  // TODO(thestig): see above comment.  SetNestableTasksAllowed(false) needs to
  // be called on the same thread as the previous call.  See
  // http://crbug.com/73466
  // MessageLoop::current()->SetNestableTasksAllowed(false);

  // We can't use OnFailure() here since owner_ may not support notifications.

  // PrintJob will create the new PrintedDocument.
  owner_->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
      owner_,
      &PrintJobWorkerOwner::GetSettingsDone,
      printing_context_->settings(),
      result));
}

void PrintJobWorker::GetSettingsWithUI(gfx::NativeView parent_view,
                                       int document_page_count,
                                       bool has_selection) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  printing_context_->AskUserForSettings(
      parent_view,
      document_page_count,
      has_selection,
      NewCallback(this, &PrintJobWorker::GetSettingsWithUIDone));
}

void PrintJobWorker::GetSettingsWithUIDone(PrintingContext::Result result) {
  message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
      this, &PrintJobWorker::GetSettingsDone, result));
}

void PrintJobWorker::UseDefaultSettings() {
  PrintingContext::Result result = printing_context_->UseDefaultSettings();
  GetSettingsDone(result);
}

void PrintJobWorker::StartPrinting(PrintedDocument* new_document) {
  DCHECK_EQ(message_loop(), MessageLoop::current());
  DCHECK_EQ(page_number_, PageNumber::npos());
  DCHECK_EQ(document_, new_document);
  DCHECK(document_.get());
  DCHECK(new_document->settings().Equals(printing_context_->settings()));

  if (!document_.get() || page_number_ != PageNumber::npos() ||
      document_ != new_document) {
    return;
  }

  PrintingContext::Result result =
      printing_context_->NewDocument(document_->name());
  if (result != PrintingContext::OK) {
    OnFailure();
    return;
  }

  // Try to print already cached data. It may already have been generated for
  // the print preview.
  OnNewPage();
  // Don't touch this anymore since the instance could be destroyed. It happens
  // if all the pages are printed a one sweep and the client doesn't have a
  // handle to us anymore. There's a timing issue involved between the worker
  // thread and the UI thread. Take no chance.
}

void PrintJobWorker::OnDocumentChanged(PrintedDocument* new_document) {
  DCHECK_EQ(message_loop(), MessageLoop::current());
  DCHECK_EQ(page_number_, PageNumber::npos());
  DCHECK(!new_document ||
         new_document->settings().Equals(printing_context_->settings()));

  if (page_number_ != PageNumber::npos())
    return;

  document_ = new_document;
}

void PrintJobWorker::OnNewPage() {
  if (!document_.get()) {
    // Spurious message.
    return;
  }
  // message_loop() could return NULL when the print job is cancelled.
  DCHECK_EQ(message_loop(), MessageLoop::current());

  if (page_number_ == PageNumber::npos()) {
    // Find first page to print.
    int page_count = document_->page_count();
    if (!page_count) {
      // We still don't know how many pages the document contains. We can't
      // start to print the document yet since the header/footer may refer to
      // the document's page count.
      return;
    }
    // We have enough information to initialize page_number_.
    page_number_.Init(document_->settings(), page_count);
  }
  DCHECK_NE(page_number_, PageNumber::npos());

  for (;;) {
    // Is the page available?
    scoped_refptr<PrintedPage> page;
    if (!document_->GetPage(page_number_.ToInt(), &page)) {
      // We need to wait for the page to be available.
      MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          NewRunnableMethod(this, &PrintJobWorker::OnNewPage),
          500);
      break;
    }
    // The page is there, print it.
    SpoolPage(*page);
    ++page_number_;
    if (page_number_ == PageNumber::npos()) {
      OnDocumentDone();
      // Don't touch this anymore since the instance could be destroyed.
      break;
    }
  }
}

void PrintJobWorker::Cancel() {
  // This is the only function that can be called from any thread.
  printing_context_->Cancel();
  // Cannot touch any member variable since we don't know in which thread
  // context we run.
}

void PrintJobWorker::OnDocumentDone() {
  DCHECK_EQ(message_loop(), MessageLoop::current());
  DCHECK_EQ(page_number_, PageNumber::npos());
  DCHECK(document_.get());

  if (printing_context_->DocumentDone() != PrintingContext::OK) {
    OnFailure();
    return;
  }

  // Tell everyone!
  NotificationTask* task = new NotificationTask();
  task->Init(owner_,
             JobEventDetails::DOC_DONE,
             document_.get(),
             NULL);
  owner_->message_loop()->PostTask(FROM_HERE, task);

  // Makes sure the variables are reinitialized.
  document_ = NULL;
}

void PrintJobWorker::SpoolPage(PrintedPage& page) {
  DCHECK_EQ(message_loop(), MessageLoop::current());
  DCHECK_NE(page_number_, PageNumber::npos());

  // Signal everyone that the page is about to be printed.
  NotificationTask* task = new NotificationTask();
  task->Init(owner_,
             JobEventDetails::NEW_PAGE,
             document_.get(),
             &page);
  owner_->message_loop()->PostTask(FROM_HERE, task);

  // Preprocess.
  if (printing_context_->NewPage() != PrintingContext::OK) {
    OnFailure();
    return;
  }

  // Actual printing.
#if defined(OS_WIN) || defined(OS_MACOSX)
  document_->RenderPrintedPage(page, printing_context_->context());
#elif defined(OS_POSIX)
  document_->RenderPrintedPage(page, printing_context_.get());
#endif

  // Postprocess.
  if (printing_context_->PageDone() != PrintingContext::OK) {
    OnFailure();
    return;
  }

  // Signal everyone that the page is printed.
  task = new NotificationTask();
  task->Init(owner_,
             JobEventDetails::PAGE_DONE,
             document_.get(),
             &page);
  owner_->message_loop()->PostTask(FROM_HERE, task);
}

void PrintJobWorker::OnFailure() {
  DCHECK_EQ(message_loop(), MessageLoop::current());

  // We may loose our last reference by broadcasting the FAILED event.
  scoped_refptr<PrintJobWorkerOwner> handle(owner_);

  NotificationTask* task = new NotificationTask();
  task->Init(owner_,
             JobEventDetails::FAILED,
             document_.get(),
             NULL);
  owner_->message_loop()->PostTask(FROM_HERE, task);
  Cancel();

  // Makes sure the variables are reinitialized.
  document_ = NULL;
  page_number_ = PageNumber::npos();
}

}  // namespace printing

void RunnableMethodTraits<printing::PrintJobWorker>::RetainCallee(
    printing::PrintJobWorker* obj) {
  DCHECK(!owner_.get());
  owner_ = obj->owner_;
}

void RunnableMethodTraits<printing::PrintJobWorker>::ReleaseCallee(
    printing::PrintJobWorker* obj) {
  DCHECK_EQ(owner_, obj->owner_);
  owner_ = NULL;
}
