// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/clipboard_message_filter.h"

#include "base/stl_util-inl.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/clipboard_dispatcher.h"
#include "content/common/clipboard_messages.h"
#include "googleurl/src/gurl.h"
#include "ipc/ipc_message_macros.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/zlib/zlib.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/size.h"

namespace {

// Completes a clipboard write initiated by the renderer. The write must be
// performed on the UI thread because the clipboard service from the IO thread
// cannot create windows so it cannot be the "owner" of the clipboard's
// contents.
class WriteClipboardTask : public Task {
 public:
  explicit WriteClipboardTask(ui::Clipboard::ObjectMap* objects)
      : objects_(objects) {}
  ~WriteClipboardTask() {}

  void Run() {
    g_browser_process->clipboard()->WriteObjects(*objects_.get());
  }

 private:
  scoped_ptr<ui::Clipboard::ObjectMap> objects_;
};

}  // namespace

ClipboardMessageFilter::ClipboardMessageFilter() {
}

void ClipboardMessageFilter::OverrideThreadForMessage(
    const IPC::Message& message, BrowserThread::ID* thread) {
#if defined(OS_WIN)
  if (message.type() == ClipboardHostMsg_ReadImage::ID)
    *thread = BrowserThread::FILE;
#elif defined(USE_X11)
  if (IPC_MESSAGE_CLASS(message) == ClipboardMsgStart)
    *thread = BrowserThread::UI;
#endif
}

bool ClipboardMessageFilter::OnMessageReceived(const IPC::Message& message,
                                               bool* message_was_ok) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(ClipboardMessageFilter, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_WriteObjectsAsync, OnWriteObjectsAsync)
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_WriteObjectsSync, OnWriteObjectsSync)
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_IsFormatAvailable, OnIsFormatAvailable)
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_ReadAvailableTypes,
                        OnReadAvailableTypes)
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_ReadText, OnReadText)
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_ReadAsciiText, OnReadAsciiText)
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_ReadHTML, OnReadHTML)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(ClipboardHostMsg_ReadImage, OnReadImage)
#if defined(OS_MACOSX)
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_FindPboardWriteStringAsync,
                        OnFindPboardWriteString)
#endif
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_ReadData, OnReadData)
    IPC_MESSAGE_HANDLER(ClipboardHostMsg_ReadFilenames, OnReadFilenames)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

ClipboardMessageFilter::~ClipboardMessageFilter() {
}

void ClipboardMessageFilter::OnWriteObjectsSync(
    const ui::Clipboard::ObjectMap& objects,
    base::SharedMemoryHandle bitmap_handle) {
  DCHECK(base::SharedMemory::IsHandleValid(bitmap_handle))
      << "Bad bitmap handle";
  // We cannot write directly from the IO thread, and cannot service the IPC
  // on the UI thread. We'll copy the relevant data and get a handle to any
  // shared memory so it doesn't go away when we resume the renderer, and post
  // a task to perform the write on the UI thread.
  ui::Clipboard::ObjectMap* long_living_objects =
      new ui::Clipboard::ObjectMap(objects);

  // Splice the shared memory handle into the clipboard data.
  ui::Clipboard::ReplaceSharedMemHandle(long_living_objects, bitmap_handle,
                                        peer_handle());

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      new WriteClipboardTask(long_living_objects));
}

void ClipboardMessageFilter::OnWriteObjectsAsync(
    const ui::Clipboard::ObjectMap& objects) {
  // We cannot write directly from the IO thread, and cannot service the IPC
  // on the UI thread. We'll copy the relevant data and post a task to preform
  // the write on the UI thread.
  ui::Clipboard::ObjectMap* long_living_objects =
      new ui::Clipboard::ObjectMap(objects);

  // This async message doesn't support shared-memory based bitmaps; they must
  // be removed otherwise we might dereference a rubbish pointer.
  long_living_objects->erase(ui::Clipboard::CBF_SMBITMAP);

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      new WriteClipboardTask(long_living_objects));
}

void ClipboardMessageFilter::OnIsFormatAvailable(
    ui::Clipboard::FormatType format, ui::Clipboard::Buffer buffer,
    bool* result) {
  *result = GetClipboard()->IsFormatAvailable(format, buffer);
}

void ClipboardMessageFilter::OnReadText(
    ui::Clipboard::Buffer buffer, string16* result) {
  GetClipboard()->ReadText(buffer, result);
}

void ClipboardMessageFilter::OnReadAsciiText(
    ui::Clipboard::Buffer buffer, std::string* result) {
  GetClipboard()->ReadAsciiText(buffer, result);
}

void ClipboardMessageFilter::OnReadHTML(
    ui::Clipboard::Buffer buffer, string16* markup, GURL* url) {
  std::string src_url_str;
  GetClipboard()->ReadHTML(buffer, markup, &src_url_str);
  *url = GURL(src_url_str);
}

void ClipboardMessageFilter::OnReadImage(
    ui::Clipboard::Buffer buffer, IPC::Message* reply_msg) {
  SkBitmap bitmap = GetClipboard()->ReadImage(buffer);

#if defined(USE_X11)
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          this, &ClipboardMessageFilter::OnReadImageReply, bitmap, reply_msg));
#else
  OnReadImageReply(bitmap, reply_msg);
#endif
}

void ClipboardMessageFilter::OnReadImageReply(
    SkBitmap bitmap, IPC::Message* reply_msg) {
  base::SharedMemoryHandle image_handle = base::SharedMemory::NULLHandle();
  uint32 image_size = 0;
  std::string reply_data;
  if (!bitmap.isNull()) {
    std::vector<unsigned char> png_data;
    SkAutoLockPixels lock(bitmap);
    if (gfx::PNGCodec::EncodeWithCompressionLevel(
            static_cast<const unsigned char*>(bitmap.getPixels()),
            gfx::PNGCodec::FORMAT_BGRA,
            gfx::Size(bitmap.width(), bitmap.height()),
            bitmap.rowBytes(),
            false,
            std::vector<gfx::PNGCodec::Comment>(),
            Z_BEST_SPEED,
            &png_data)) {
      base::SharedMemory buffer;
      if (buffer.CreateAndMapAnonymous(png_data.size())) {
        memcpy(buffer.memory(), vector_as_array(&png_data), png_data.size());
        if (buffer.GiveToProcess(peer_handle(), &image_handle)) {
          image_size = png_data.size();
        }
      }
    }
  }
  ClipboardHostMsg_ReadImage::WriteReplyParams(reply_msg, image_handle,
                                               image_size);
  Send(reply_msg);
}

void ClipboardMessageFilter::OnReadAvailableTypes(
    ui::Clipboard::Buffer buffer, std::vector<string16>* types,
    bool* contains_filenames) {
  GetClipboard()->ReadAvailableTypes(buffer, types, contains_filenames);
}

void ClipboardMessageFilter::OnReadData(
    ui::Clipboard::Buffer buffer, const string16& type, bool* succeeded,
    string16* data, string16* metadata) {
  *succeeded = ClipboardDispatcher::ReadData(buffer, type, data, metadata);
}

void ClipboardMessageFilter::OnReadFilenames(
    ui::Clipboard::Buffer buffer, bool* succeeded,
    std::vector<string16>* filenames) {
  *succeeded = ClipboardDispatcher::ReadFilenames(buffer, filenames);
}

// static
ui::Clipboard* ClipboardMessageFilter::GetClipboard() {
  // We have a static instance of the clipboard service for use by all message
  // filters.  This instance lives for the life of the browser processes.
  static ui::Clipboard* clipboard = new ui::Clipboard;

  return clipboard;
}
