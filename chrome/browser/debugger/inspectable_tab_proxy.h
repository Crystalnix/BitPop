// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEBUGGER_INSPECTABLE_TAB_PROXY_H_
#define CHROME_BROWSER_DEBUGGER_INSPECTABLE_TAB_PROXY_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "chrome/browser/debugger/devtools_client_host.h"

class DebuggerRemoteService;
class DevToolsClientHost;
class DevToolsClientHostImpl;
class NavigationController;
struct DevToolsMessageData;

// Proxies debugged tabs' NavigationControllers using their UIDs.
// Keeps track of tabs being debugged so that we can detach from
// them on remote debugger connection loss.
class InspectableTabProxy {
 public:
  typedef base::hash_map<int32, NavigationController*> ControllersMap;
  typedef base::hash_map<int32, DevToolsClientHostImpl*> IdToClientHostMap;

  InspectableTabProxy();
  virtual ~InspectableTabProxy();

  // Returns a map of NavigationControllerKeys to NavigationControllers
  // for all Browser instances. Clients should not keep the result around
  // for extended periods of time as tabs might get closed thus invalidating
  // the map.
  const ControllersMap& controllers_map();

  // Returns a DevToolsClientHostImpl for the given tab |id|.
  DevToolsClientHostImpl* ClientHostForTabId(int32 id);

  // Creates a new DevToolsClientHost implementor instance.
  // |id| is the UID of the tab to debug.
  // |service| is the DebuggerRemoteService instance the DevToolsClient
  //         messages shall be dispatched to.
  DevToolsClientHost* NewClientHost(int32 id,
                                    DebuggerRemoteService* service);

  // Gets invoked when a remote debugger is detached. In this case we should
  // send the corresponding message to the V8 debugger for each of the tabs
  // the debugger is attached to, and invoke InspectedTabClosing().
  void OnRemoteDebuggerDetached();

 private:
  ControllersMap controllers_map_;
  IdToClientHostMap id_to_client_host_map_;
  DISALLOW_COPY_AND_ASSIGN(InspectableTabProxy);
};


// An internal implementation of DevToolsClientHost that delegates
// messages sent for DevToolsClient to a DebuggerShell instance.
class DevToolsClientHostImpl : public DevToolsClientHost {
 public:
  DevToolsClientHostImpl(
    int32 id,
    DebuggerRemoteService* service,
    InspectableTabProxy::IdToClientHostMap* map);
  ~DevToolsClientHostImpl();

  DebuggerRemoteService* debugger_remote_service() {
    return service_;
  }

  void Close();

  // DevToolsClientHost interface
  virtual void InspectedTabClosing();
  virtual void SendMessageToClient(const IPC::Message& msg);
  virtual void TabReplaced(TabContentsWrapper* new_tab);

 private:
  // Message handling routines
  void OnDebuggerOutput(const std::string& msg);
  virtual void FrameNavigating(const std::string& url);
  void TabClosed();

  int32 id_;
  DebuggerRemoteService* service_;
  InspectableTabProxy::IdToClientHostMap* map_;
};

#endif  // CHROME_BROWSER_DEBUGGER_INSPECTABLE_TAB_PROXY_H_
