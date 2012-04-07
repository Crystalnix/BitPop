// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DEBUGGER_DEVTOOLS_HTTP_HANDLER_IMPL_H_
#define CONTENT_BROWSER_DEBUGGER_DEVTOOLS_HTTP_HANDLER_IMPL_H_
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "content/common/content_export.h"
#include "content/public/browser/devtools_http_handler.h"
#include "net/server/http_server.h"
#include "net/url_request/url_request.h"

namespace net {
class URLRequestContext;
}

namespace content {

class DevToolsClientHost;

class DevToolsHttpHandlerImpl
    : public DevToolsHttpHandler,
      public base::RefCountedThreadSafe<DevToolsHttpHandlerImpl>,
      public net::HttpServer::Delegate,
      public net::URLRequest::Delegate {
 private:
  friend class base::RefCountedThreadSafe<DevToolsHttpHandlerImpl>;
  friend class DevToolsHttpHandler;
  DevToolsHttpHandlerImpl(const std::string& ip,
                          int port,
                          const std::string& frontend_url,
                          DevToolsHttpHandlerDelegate* delegate);
  virtual ~DevToolsHttpHandlerImpl();
  void Start();

  // DevToolsHttpHandler implementation.
  virtual void Stop() OVERRIDE;

  // net::HttpServer::Delegate implementation.
  virtual void OnHttpRequest(int connection_id,
                             const net::HttpServerRequestInfo& info) OVERRIDE;
  virtual void OnWebSocketRequest(
      int connection_id,
      const net::HttpServerRequestInfo& info) OVERRIDE;
  virtual void OnWebSocketMessage(int connection_id,
                                  const std::string& data) OVERRIDE;
  virtual void OnClose(int connection_id) OVERRIDE;

  virtual void OnJsonRequestUI(int connection_id,
                               const net::HttpServerRequestInfo& info);
  virtual void OnWebSocketRequestUI(int connection_id,
                                    const net::HttpServerRequestInfo& info);
  virtual void OnWebSocketMessageUI(int connection_id,
                                    const std::string& data);
  virtual void OnCloseUI(int connection_id);

  // net::URLRequest::Delegate implementation.
  virtual void OnResponseStarted(net::URLRequest* request) OVERRIDE;
  virtual void OnReadCompleted(net::URLRequest* request,
                               int bytes_read) OVERRIDE;

  void Init();
  void TeardownAndRelease();
  void Bind(net::URLRequest* request, int connection_id);
  void RequestCompleted(net::URLRequest* request);

  void Send200(int connection_id,
               const std::string& data,
               const std::string& mime_type = "text/html");
  void Send404(int connection_id);
  void Send500(int connection_id,
               const std::string& message);
  void AcceptWebSocket(int connection_id,
                       const net::HttpServerRequestInfo& request);

  std::string ip_;
  int port_;
  std::string overridden_frontend_url_;
  scoped_refptr<net::HttpServer> server_;
  typedef std::map<net::URLRequest*, int>
      RequestToSocketMap;
  RequestToSocketMap request_to_connection_io_;
  typedef std::map<int, std::set<net::URLRequest*> >
      ConnectionToRequestsMap;
  ConnectionToRequestsMap connection_to_requests_io_;
  typedef std::map<net::URLRequest*, scoped_refptr<net::IOBuffer> >
      BuffersMap;
  BuffersMap request_to_buffer_io_;
  typedef std::map<int, content::DevToolsClientHost*>
      ConnectionToClientHostMap;
  ConnectionToClientHostMap connection_to_client_host_ui_;
  scoped_ptr<DevToolsHttpHandlerDelegate> delegate_;
  DISALLOW_COPY_AND_ASSIGN(DevToolsHttpHandlerImpl);
};

}  // namespace content

#endif  // CONTENT_BROWSER_DEBUGGER_DEVTOOLS_HTTP_HANDLER_IMPL_H_
