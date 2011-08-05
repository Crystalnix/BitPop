// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_WORKER_WEBWORKERCLIENT_PROXY_H_
#define CONTENT_WORKER_WEBWORKERCLIENT_PROXY_H_
#pragma once

#include "base/basictypes.h"
#include "base/task.h"
#include "ipc/ipc_channel.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileSystem.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebWorkerClient.h"

namespace WebKit {
class WebApplicationCacheHost;
class WebApplicationCacheHostClient;
class WebFrame;
class WebWorker;
}

class WebWorkerStubBase;
class WorkerDevToolsAgent;

// This class receives IPCs from the renderer and calls the WebCore::Worker
// implementation (after the data types have been converted by glue code).  It
// is also called by the worker code and converts these function calls into
// IPCs that are sent to the renderer, where they're converted back to function
// calls by WebWorkerProxy.
class WebWorkerClientProxy : public WebKit::WebWorkerClient {
 public:
  WebWorkerClientProxy(int route_id, WebWorkerStubBase* stub);
  virtual ~WebWorkerClientProxy();

  // WebWorkerClient implementation.
  virtual void postMessageToWorkerObject(
      const WebKit::WebString& message,
      const WebKit::WebMessagePortChannelArray& channel);
  virtual void postExceptionToWorkerObject(
      const WebKit::WebString& error_message,
      int line_number,
      const WebKit::WebString& source_url);
  // TODO(caseq): The overload before is obsolete and is preserved for
  // WebKit/chromium compatibility only (pure virtual is base class).
  // Should be removed once WebKit part is updated.
  virtual void postConsoleMessageToWorkerObject(
      int destination,
      int source,
      int type,
      int level,
      const WebKit::WebString& message,
      int line_number,
      const WebKit::WebString& source_url) {
  }
  virtual void postConsoleMessageToWorkerObject(
      int source,
      int type,
      int level,
      const WebKit::WebString& message,
      int line_number,
      const WebKit::WebString& source_url);
  virtual void confirmMessageFromWorkerObject(bool has_pending_activity);
  virtual void reportPendingActivity(bool has_pending_activity);
  virtual void workerContextClosed();
  virtual void workerContextDestroyed();
  virtual WebKit::WebWorker* createWorker(WebKit::WebWorkerClient* client);

  virtual WebKit::WebNotificationPresenter* notificationPresenter();

  virtual WebKit::WebApplicationCacheHost* createApplicationCacheHost(
      WebKit::WebApplicationCacheHostClient* client);

  virtual bool allowDatabase(WebKit::WebFrame* frame,
                             const WebKit::WebString& name,
                             const WebKit::WebString& display_name,
                             unsigned long estimated_size);
  virtual bool allowFileSystem();
  virtual void openFileSystem(WebKit::WebFileSystem::Type type,
                              long long size,
                              bool create,
                              WebKit::WebFileSystemCallbacks* callbacks);
  virtual void dispatchDevToolsMessage(const WebKit::WebString&);

  void EnsureWorkerContextTerminates();

  void set_devtools_agent(WorkerDevToolsAgent* devtools_agent) {
    devtools_agent_ = devtools_agent;
  }

 private:
  bool Send(IPC::Message* message);

  int route_id_;
  int appcache_host_id_;
  WebWorkerStubBase* stub_;
  ScopedRunnableMethodFactory<WebWorkerClientProxy> kill_process_factory_;
  WorkerDevToolsAgent* devtools_agent_;

  DISALLOW_COPY_AND_ASSIGN(WebWorkerClientProxy);
};

#endif  // CONTENT_WORKER_WEBWORKERCLIENT_PROXY_H_
