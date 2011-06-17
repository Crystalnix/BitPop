// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_DEVTOOLS_CLIENT_H_
#define CHROME_RENDERER_DEVTOOLS_CLIENT_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "content/renderer/render_view_observer.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDevToolsFrontendClient.h"

class MessageLoop;

namespace WebKit {
class WebDevToolsFrontend;
class WebString;
}

struct DevToolsMessageData;

// Developer tools UI end of communication channel between the render process of
// the page being inspected and tools UI renderer process. All messages will
// go through browser process. On the side of the inspected page there's
// corresponding DevToolsAgent object.
// TODO(yurys): now the client is almost empty later it will delegate calls to
// code in glue
class DevToolsClient : public RenderViewObserver,
                       public WebKit::WebDevToolsFrontendClient {
 public:
  explicit DevToolsClient(RenderView* render_view);
  virtual ~DevToolsClient();

 private:
  // RenderView::Observer implementation.
  virtual bool OnMessageReceived(const IPC::Message& message);

  // WebDevToolsFrontendClient implementation
  virtual void sendFrontendLoaded();
  virtual void sendMessageToBackend(const WebKit::WebString&);
  virtual void sendDebuggerCommandToAgent(const WebKit::WebString& command);

  virtual void activateWindow();
  virtual void closeWindow();
  virtual void requestDockWindow();
  virtual void requestUndockWindow();

  virtual bool shouldHideScriptsPanel();

  void OnDispatchOnInspectorFrontend(const std::string& message);

  // Sends message to DevToolsAgent.
  void SendToAgent(const IPC::Message& tools_agent_message);

  scoped_ptr<WebKit::WebDevToolsFrontend> web_tools_frontend_;

  DISALLOW_COPY_AND_ASSIGN(DevToolsClient);
};

#endif  // CHROME_RENDERER_DEVTOOLS_CLIENT_H_
