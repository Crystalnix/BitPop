// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/web_contents/web_contents_drag_win.h"

#include <windows.h>

#include <string>

#include "base/bind.h"
#include "base/file_path.h"
#include "base/message_loop.h"
#include "base/pickle.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "base/utf_string_conversions.h"
#include "content/browser/download/drag_download_file.h"
#include "content/browser/download/drag_download_util.h"
#include "content/browser/web_contents/web_drag_dest_win.h"
#include "content/browser/web_contents/web_drag_source_win.h"
#include "content/browser/web_contents/web_drag_utils_win.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_drag_dest_delegate.h"
#include "net/base/net_util.h"
#include "ui/base/clipboard/clipboard_util_win.h"
#include "ui/base/clipboard/custom_data_helper.h"
#include "ui/base/dragdrop/drag_utils.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/size.h"
#include "webkit/glue/webdropdata.h"

using content::BrowserThread;
using WebKit::WebDragOperationsMask;
using WebKit::WebDragOperationCopy;
using WebKit::WebDragOperationLink;
using WebKit::WebDragOperationMove;

namespace {

HHOOK msg_hook = NULL;
DWORD drag_out_thread_id = 0;
bool mouse_up_received = false;

LRESULT CALLBACK MsgFilterProc(int code, WPARAM wparam, LPARAM lparam) {
  if (code == base::MessagePumpForUI::kMessageFilterCode &&
      !mouse_up_received) {
    MSG* msg = reinterpret_cast<MSG*>(lparam);
    // We do not care about WM_SYSKEYDOWN and WM_SYSKEYUP because when ALT key
    // is pressed down on drag-and-drop, it means to create a link.
    if (msg->message == WM_MOUSEMOVE || msg->message == WM_LBUTTONUP ||
        msg->message == WM_KEYDOWN || msg->message == WM_KEYUP) {
      // Forward the message from the UI thread to the drag-and-drop thread.
      PostThreadMessage(drag_out_thread_id,
                        msg->message,
                        msg->wParam,
                        msg->lParam);

      // If the left button is up, we do not need to forward the message any
      // more.
      if (msg->message == WM_LBUTTONUP || !(GetKeyState(VK_LBUTTON) & 0x8000))
        mouse_up_received = true;

      return TRUE;
    }
  }
  return CallNextHookEx(msg_hook, code, wparam, lparam);
}

}  // namespace

class DragDropThread : public base::Thread {
 public:
  explicit DragDropThread(WebContentsDragWin* drag_handler)
       : base::Thread("Chrome_DragDropThread"),
         drag_handler_(drag_handler) {
  }

  virtual ~DragDropThread() {
    Thread::Stop();
  }

 protected:
  // base::Thread implementations:
  virtual void Init() {
    int ole_result = OleInitialize(NULL);
    DCHECK(ole_result == S_OK);
  }

  virtual void CleanUp() {
    OleUninitialize();
  }

 private:
  // Hold a reference count to WebContentsDragWin to make sure that it is always
  // alive in the thread lifetime.
  scoped_refptr<WebContentsDragWin> drag_handler_;

  DISALLOW_COPY_AND_ASSIGN(DragDropThread);
};

WebContentsDragWin::WebContentsDragWin(
    gfx::NativeWindow source_window,
    content::WebContents* web_contents,
    WebDragDest* drag_dest,
    const base::Callback<void()>& drag_end_callback)
    : drag_drop_thread_id_(0),
      source_window_(source_window),
      web_contents_(web_contents),
      drag_dest_(drag_dest),
      drag_ended_(false),
      old_drop_target_suspended_state_(false),
      drag_end_callback_(drag_end_callback) {
}

WebContentsDragWin::~WebContentsDragWin() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!drag_drop_thread_.get());
}

void WebContentsDragWin::StartDragging(const WebDropData& drop_data,
                                       WebDragOperationsMask ops,
                                       const gfx::ImageSkia& image,
                                       const gfx::Point& image_offset) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  drag_source_ = new WebDragSource(source_window_, web_contents_);

  const GURL& page_url = web_contents_->GetURL();
  const std::string& page_encoding = web_contents_->GetEncoding();

  // If it is not drag-out, do the drag-and-drop in the current UI thread.
  if (drop_data.download_metadata.empty()) {
    DoDragging(drop_data, ops, page_url, page_encoding, image, image_offset);
    EndDragging(false);
    return;
  }

  // We do not want to drag and drop the download to itself.
  old_drop_target_suspended_state_ = drag_dest_->suspended();
  drag_dest_->set_suspended(true);

  // Start a background thread to do the drag-and-drop.
  DCHECK(!drag_drop_thread_.get());
  drag_drop_thread_.reset(new DragDropThread(this));
  base::Thread::Options options;
  options.message_loop_type = MessageLoop::TYPE_UI;
  if (drag_drop_thread_->StartWithOptions(options)) {
    drag_drop_thread_->message_loop()->PostTask(
        FROM_HERE,
        base::Bind(&WebContentsDragWin::StartBackgroundDragging, this,
                   drop_data, ops, page_url, page_encoding, image,
                   image_offset));
  }

  // Install a hook procedure to monitor the messages so that we can forward
  // the appropriate ones to the background thread.
  drag_out_thread_id = drag_drop_thread_->thread_id();
  mouse_up_received = false;
  DCHECK(!msg_hook);
  msg_hook = SetWindowsHookEx(WH_MSGFILTER,
                              MsgFilterProc,
                              NULL,
                              GetCurrentThreadId());

  // Attach the input state of the background thread to the UI thread so that
  // SetCursor can work from the background thread.
  AttachThreadInput(drag_out_thread_id, GetCurrentThreadId(), TRUE);
}

void WebContentsDragWin::StartBackgroundDragging(
    const WebDropData& drop_data,
    WebDragOperationsMask ops,
    const GURL& page_url,
    const std::string& page_encoding,
    const SkBitmap& image,
    const gfx::Point& image_offset) {
  drag_drop_thread_id_ = base::PlatformThread::CurrentId();

  DoDragging(drop_data, ops, page_url, page_encoding, image, image_offset);
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&WebContentsDragWin::EndDragging, this, true));
}

void WebContentsDragWin::PrepareDragForDownload(
    const WebDropData& drop_data,
    ui::OSExchangeData* data,
    const GURL& page_url,
    const std::string& page_encoding) {
  // Parse the download metadata.
  string16 mime_type;
  FilePath file_name;
  GURL download_url;
  if (!drag_download_util::ParseDownloadMetadata(drop_data.download_metadata,
                                                 &mime_type,
                                                 &file_name,
                                                 &download_url))
    return;

  // Generate the file name based on both mime type and proposed file name.
  std::string default_name =
      content::GetContentClient()->browser()->GetDefaultDownloadName();
  FilePath generated_download_file_name =
      net::GenerateFileName(download_url,
                            std::string(),
                            std::string(),
                            UTF16ToUTF8(file_name.value()),
                            UTF16ToUTF8(mime_type),
                            default_name);

  // Provide the data as file (CF_HDROP). A temporary download file with the
  // Zone.Identifier ADS (Alternate Data Stream) attached will be created.
  linked_ptr<net::FileStream> empty_file_stream;
  scoped_refptr<DragDownloadFile> download_file =
      new DragDownloadFile(
          generated_download_file_name,
          empty_file_stream,
          download_url,
          content::Referrer(page_url, drop_data.referrer_policy),
          page_encoding,
          web_contents_);
  ui::OSExchangeData::DownloadFileInfo file_download(FilePath(),
                                                     download_file.get());
  data->SetDownloadFileInfo(file_download);

  // Enable asynchronous operation.
  ui::OSExchangeDataProviderWin::GetIAsyncOperation(*data)->SetAsyncMode(TRUE);
}

void WebContentsDragWin::PrepareDragForFileContents(
    const WebDropData& drop_data, ui::OSExchangeData* data) {
  static const int kMaxFilenameLength = 255;  // FAT and NTFS
  FilePath file_name(drop_data.file_description_filename);

  // Images without ALT text will only have a file extension so we need to
  // synthesize one from the provided extension and URL.
  if (file_name.BaseName().RemoveExtension().empty()) {
    const string16 extension = file_name.Extension();
    // Retrieve the name from the URL.
    file_name = FilePath(
        net::GetSuggestedFilename(drop_data.url, "", "", "", "", ""));
    if (file_name.value().size() + extension.size() > kMaxFilenameLength) {
      file_name = FilePath(file_name.value().substr(
          0, kMaxFilenameLength - extension.size()));
    }
    file_name = file_name.ReplaceExtension(extension);
  }
  data->SetFileContents(file_name, drop_data.file_contents);
}

void WebContentsDragWin::PrepareDragForUrl(const WebDropData& drop_data,
                                           ui::OSExchangeData* data) {
  if (drag_dest_->delegate() &&
      drag_dest_->delegate()->AddDragData(drop_data, data)) {
    return;
  }

  data->SetURL(drop_data.url, drop_data.url_title);
}

void WebContentsDragWin::DoDragging(const WebDropData& drop_data,
                                    WebDragOperationsMask ops,
                                    const GURL& page_url,
                                    const std::string& page_encoding,
                                    const gfx::ImageSkia& image,
                                    const gfx::Point& image_offset) {
  ui::OSExchangeData data;

  // TODO(dcheng): Figure out why this is mutually exclusive.
  if (!drop_data.download_metadata.empty()) {
    PrepareDragForDownload(drop_data, &data, page_url, page_encoding);

    // Set the observer.
    ui::OSExchangeDataProviderWin::GetDataObjectImpl(data)->set_observer(this);
  } else {
    // We set the file contents before the URL because the URL also sets file
    // contents (to a .URL shortcut).  We want to prefer file content data over
    // a shortcut so we add it first.
    if (!drop_data.file_contents.empty())
      PrepareDragForFileContents(drop_data, &data);
    if (!drop_data.html.string().empty())
      data.SetHtml(drop_data.html.string(), drop_data.html_base_url);
    // We set the text contents before the URL because the URL also sets text
    // content.
    if (!drop_data.text.string().empty())
      data.SetString(drop_data.text.string());
    if (drop_data.url.is_valid())
      PrepareDragForUrl(drop_data, &data);
    if (!drop_data.custom_data.empty()) {
      Pickle pickle;
      ui::WriteCustomDataToPickle(drop_data.custom_data, &pickle);
      data.SetPickledData(ui::ClipboardUtil::GetWebCustomDataFormat()->cfFormat,
                          pickle);
    }
  }

  // Set drag image.
  if (!image.isNull()) {
    drag_utils::SetDragImageOnDataObject(image,
        gfx::Size(image.width(), image.height()), image_offset, &data);
  }

  // We need to enable recursive tasks on the message loop so we can get
  // updates while in the system DoDragDrop loop.
  DWORD effect;
  {
    MessageLoop::ScopedNestableTaskAllower allow(MessageLoop::current());
    DoDragDrop(ui::OSExchangeDataProviderWin::GetIDataObject(data),
               drag_source_,
               web_drag_utils_win::WebDragOpMaskToWinDragOpMask(ops),
               &effect);
  }

  // This works because WebDragSource::OnDragSourceDrop uses PostTask to
  // dispatch the actual event.
  drag_source_->set_effect(effect);
}

void WebContentsDragWin::EndDragging(bool restore_suspended_state) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (drag_ended_)
    return;
  drag_ended_ = true;

  if (restore_suspended_state)
    drag_dest_->set_suspended(old_drop_target_suspended_state_);

  if (msg_hook) {
    AttachThreadInput(drag_out_thread_id, GetCurrentThreadId(), FALSE);
    UnhookWindowsHookEx(msg_hook);
    msg_hook = NULL;
  }

  drag_end_callback_.Run();
}

void WebContentsDragWin::CancelDrag() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  drag_source_->CancelDrag();
}

void WebContentsDragWin::CloseThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  drag_drop_thread_.reset();
}

void WebContentsDragWin::OnWaitForData() {
  DCHECK(drag_drop_thread_id_ == base::PlatformThread::CurrentId());

  // When the left button is release and we start to wait for the data, end
  // the dragging before DoDragDrop returns. This makes the page leave the drag
  // mode so that it can start to process the normal input events.
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&WebContentsDragWin::EndDragging, this, true));
}

void WebContentsDragWin::OnDataObjectDisposed() {
  DCHECK(drag_drop_thread_id_ == base::PlatformThread::CurrentId());

  // The drag-and-drop thread is only closed after OLE is done with
  // DataObjectImpl.
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&WebContentsDragWin::CloseThread, this));
}
