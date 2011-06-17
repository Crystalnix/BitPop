// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/file_system/webfilesystem_impl.h"

#include "content/common/child_thread.h"
#include "content/common/file_system/file_system_dispatcher.h"
#include "content/common/file_system/webfilesystem_callback_dispatcher.h"
#include "content/common/file_system/webfilewriter_impl.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileInfo.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileSystemCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebString.h"
#include "webkit/glue/webkit_glue.h"

using WebKit::WebFileInfo;
using WebKit::WebFileSystemCallbacks;
using WebKit::WebFileSystemEntry;
using WebKit::WebString;
using WebKit::WebVector;

WebFileSystemImpl::WebFileSystemImpl() {
}

void WebFileSystemImpl::move(const WebString& src_path,
                             const WebString& dest_path,
                             WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->Move(GURL(src_path),
                   GURL(dest_path),
                   new WebFileSystemCallbackDispatcher(callbacks));
}

void WebFileSystemImpl::copy(const WebString& src_path,
                             const WebString& dest_path,
                             WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->Copy(GURL(src_path),
                   GURL(dest_path),
                   new WebFileSystemCallbackDispatcher(callbacks));
}

void WebFileSystemImpl::remove(const WebString& path,
                               WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->Remove(GURL(path),
                     false /* recursive */,
                     new WebFileSystemCallbackDispatcher(callbacks));
}

void WebFileSystemImpl::removeRecursively(const WebString& path,
                                          WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->Remove(GURL(path),
                     true /* recursive */,
                     new WebFileSystemCallbackDispatcher(callbacks));
}

void WebFileSystemImpl::readMetadata(const WebString& path,
                                     WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->ReadMetadata(GURL(path),
                           new WebFileSystemCallbackDispatcher(callbacks));
}

void WebFileSystemImpl::createFile(const WebString& path,
                                   bool exclusive,
                                   WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->Create(GURL(path), exclusive, false,
                     false, new WebFileSystemCallbackDispatcher(callbacks));
}

void WebFileSystemImpl::createDirectory(const WebString& path,
                                        bool exclusive,
                                        WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->Create(GURL(path), exclusive, true,
                     false, new WebFileSystemCallbackDispatcher(callbacks));
}

void WebFileSystemImpl::fileExists(const WebString& path,
                                   WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->Exists(GURL(path), false,
                     new WebFileSystemCallbackDispatcher(callbacks));
}

void WebFileSystemImpl::directoryExists(const WebString& path,
                                        WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->Exists(GURL(path), true,
                     new WebFileSystemCallbackDispatcher(callbacks));
}

void WebFileSystemImpl::readDirectory(const WebString& path,
                                      WebFileSystemCallbacks* callbacks) {
  FileSystemDispatcher* dispatcher =
      ChildThread::current()->file_system_dispatcher();
  dispatcher->ReadDirectory(GURL(path),
                            new WebFileSystemCallbackDispatcher(callbacks));
}

WebKit::WebFileWriter* WebFileSystemImpl::createFileWriter(
    const WebString& path, WebKit::WebFileWriterClient* client) {
  return new WebFileWriterImpl(GURL(path), client);
}
