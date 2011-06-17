// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_PEPPER_MESSAGE_FILTER_H_
#define CONTENT_BROWSER_RENDERER_HOST_PEPPER_MESSAGE_FILTER_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/process.h"
#include "base/time.h"
#include "content/browser/browser_message_filter.h"
#include "ppapi/c/private/ppb_flash_net_connector.h"

namespace content {
class ResourceContext;
}

namespace net {
class AddressList;
}

class PepperMessageFilter : public BrowserMessageFilter {
 public:
  explicit PepperMessageFilter(
      const content::ResourceContext* resource_context);
  virtual ~PepperMessageFilter();

 private:
  // BrowserMessageFilter methods.
  virtual bool OnMessageReceived(const IPC::Message& message,
                                 bool* message_was_ok);

#if defined(ENABLE_FLAPPER_HACKS)
  // Message handlers.
  void OnConnectTcp(int routing_id,
                    int request_id,
                    const std::string& host,
                    uint16 port);
  void OnConnectTcpAddress(int routing_id,
                           int request_id,
                           const PP_Flash_NetAddress& address);

  // |Send()| a |PepperMsg_ConnectTcpACK|, which reports an error.
  bool SendConnectTcpACKError(int routing_id,
                              int request_id);

  // Used by |OnConnectTcp()| (below).
  class LookupRequest;
  friend class LookupRequest;

  // Continuation of |OnConnectTcp()|.
  void ConnectTcpLookupFinished(int routing_id,
                                int request_id,
                                const net::AddressList& addresses);
  void ConnectTcpOnWorkerThread(int routing_id,
                                int request_id,
                                net::AddressList addresses);

  // Continuation of |OnConnectTcpAddress()|.
  void ConnectTcpAddressOnWorkerThread(int routing_id,
                                       int request_id,
                                       PP_Flash_NetAddress addr);
#endif  // ENABLE_FLAPPER_HACKS

  void OnGetLocalTimeZoneOffset(base::Time t, double* result);

  const content::ResourceContext* const resource_context_;
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_PEPPER_MESSAGE_FILTER_H_
