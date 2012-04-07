// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_page_capture_api.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/extensions/extension_messages.h"
#include "content/browser/child_process_security_policy.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/download/mhtml_generation_manager.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"

using content::BrowserThread;
using content::WebContents;

// Error messages.
const char* const kFileTooBigError = "The MHTML file generated is too big.";
const char* const kMHTMLGenerationFailedError = "Failed to generate MHTML.";
const char* const kSizeRetrievalError =
    "Failed to retrieve size of generated MHTML.";
const char* const kTemporaryFileError = "Failed to create a temporary file.";
const char* const kTabClosedError = "Cannot find the tab for thie request.";

static PageCaptureSaveAsMHTMLFunction::TestDelegate* test_delegate_ = NULL;

PageCaptureSaveAsMHTMLFunction::PageCaptureSaveAsMHTMLFunction() : tab_id_(0) {
}

PageCaptureSaveAsMHTMLFunction::~PageCaptureSaveAsMHTMLFunction() {
}

void PageCaptureSaveAsMHTMLFunction::SetTestDelegate(TestDelegate* delegate) {
  test_delegate_ = delegate;
}

bool PageCaptureSaveAsMHTMLFunction::RunImpl() {
  DictionaryValue* args;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &args));

  if (!args->HasKey("tabId"))
    return false;

  EXTENSION_FUNCTION_VALIDATE(args->GetInteger("tabId", &tab_id_));

  AddRef();  // Balanced in ReturnFailure/ReturnSuccess()

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&PageCaptureSaveAsMHTMLFunction::CreateTemporaryFile, this));
  return true;
}

bool PageCaptureSaveAsMHTMLFunction::OnMessageReceivedFromRenderView(
    const IPC::Message& message) {
  if (message.type() != ExtensionHostMsg_ResponseAck::ID)
    return false;

  int message_request_id;
  void* iter = NULL;
  if (!message.ReadInt(&iter, &message_request_id)) {
    NOTREACHED() << "malformed extension message";
    return true;
  }

  if (message_request_id != request_id())
    return false;

  // The extension process has processed the response and has created a
  // reference to the blob, it is safe for us to go away.
  Release();  // Balanced in Run()

  return true;
}

void PageCaptureSaveAsMHTMLFunction::CreateTemporaryFile() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  bool success = file_util::CreateTemporaryFile(&mhtml_path_);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&PageCaptureSaveAsMHTMLFunction::TemporaryFileCreated, this,
                 success));
}

void PageCaptureSaveAsMHTMLFunction::TemporaryFileCreated(bool success) {
  if (!success) {
    ReturnFailure(kTemporaryFileError);
    return;
  }

  if (test_delegate_)
    test_delegate_->OnTemporaryFileCreated(mhtml_path_);

  // Sets a DeletableFileReference so the temporary file gets deleted once it is
  // no longer used.
  mhtml_file_ = webkit_blob::DeletableFileReference::GetOrCreate(mhtml_path_,
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE));

  WebContents* web_contents = GetWebContents();
  if (!web_contents) {
    ReturnFailure(kTabClosedError);
    return;
  }

  MHTMLGenerationManager::GenerateMHTMLCallback callback =
      base::Bind(&PageCaptureSaveAsMHTMLFunction::MHTMLGenerated, this);

  g_browser_process->mhtml_generation_manager()->GenerateMHTML(
      web_contents, mhtml_path_, callback);
}

void PageCaptureSaveAsMHTMLFunction::MHTMLGenerated(const FilePath& file_path,
                                                    int64 mhtml_file_size) {
  DCHECK(mhtml_path_ == file_path);
  if (mhtml_file_size <= 0) {
    ReturnFailure(kMHTMLGenerationFailedError);
    return;
  }

  if (mhtml_file_size > std::numeric_limits<int>::max()) {
    ReturnFailure(kFileTooBigError);
    return;
  }

  ReturnSuccess(mhtml_file_size);
}

void PageCaptureSaveAsMHTMLFunction::ReturnFailure(const std::string& error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  error_ = error;

  SendResponse(false);

  Release();  // Balanced in Run()
}

void PageCaptureSaveAsMHTMLFunction::ReturnSuccess(int64 file_size) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  WebContents* web_contents = GetWebContents();
  if (!web_contents || !render_view_host()) {
    ReturnFailure(kTabClosedError);
    return;
  }

  int child_id = render_view_host()->process()->GetID();
  ChildProcessSecurityPolicy::GetInstance()->GrantReadFile(
      child_id, mhtml_path_);

  DictionaryValue* dict = new DictionaryValue();
  result_.reset(dict);
  dict->SetString("mhtmlFilePath", mhtml_path_.value());
  dict->SetInteger("mhtmlFileLength", file_size);

  SendResponse(true);

  // Note that we'll wait for a response ack message received in
  // OnMessageReceivedFromRenderView before we call Release() (to prevent the
  // blob file from being deleted).
}

WebContents* PageCaptureSaveAsMHTMLFunction::GetWebContents() {
  Browser* browser = NULL;
  TabContentsWrapper* tab_contents_wrapper = NULL;

  if (!ExtensionTabUtil::GetTabById(tab_id_, profile(), include_incognito(),
      &browser, NULL, &tab_contents_wrapper, NULL)) {
    return NULL;
  }
  return tab_contents_wrapper->web_contents();
}
