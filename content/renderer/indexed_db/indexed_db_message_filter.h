// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_INDEXED_DB_INDEXED_DB_MESSAGE_FILTER_H_
#define CONTENT_RENDERER_INDEXED_DB_INDEXED_DB_MESSAGE_FILTER_H_
#pragma once

#include "ipc/ipc_channel_proxy.h"

class IndexedDBDispatcher;

class IndexedDBMessageFilter : public IPC::ChannelProxy::MessageFilter {
 public:
  IndexedDBMessageFilter();
  virtual ~IndexedDBMessageFilter();

  // IPC::Channel::Listener implementation.
  virtual bool OnMessageReceived(const IPC::Message& msg) OVERRIDE;

 private:
  void DispatchMessage(const IPC::Message& msg);
  scoped_refptr<base::MessageLoopProxy> main_thread_loop_proxy_;

  DISALLOW_COPY_AND_ASSIGN(IndexedDBMessageFilter);
};

#endif  // CONTENT_RENDERER_INDEXED_DB_INDEXED_DB_DISPATCHER_H_
