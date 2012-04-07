// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/host_event_logger.h"

#include "net/base/ip_endpoint.h"
#include "remoting/host/chromoting_host.h"
#include "remoting/host/system_event_logger.h"

namespace remoting {

HostEventLogger::HostEventLogger(ChromotingHost* host,
                                 const std::string& application_name)
    : host_(host),
      system_event_logger_(SystemEventLogger::Create(application_name)) {
  host_->AddStatusObserver(this);
}

HostEventLogger::~HostEventLogger() {
  host_->RemoveStatusObserver(this);
}

void HostEventLogger::OnClientAuthenticated(const std::string& jid) {
  Log("Client connected: " + jid);
}

void HostEventLogger::OnClientDisconnected(const std::string& jid) {
  Log("Client disconnected: " + jid);
}

void HostEventLogger::OnAccessDenied(const std::string& jid) {
  Log("Access denied for client: " + jid);
}

void HostEventLogger::OnClientIpAddress(const std::string& jid,
                                        const std::string& channel_name,
                                        const net::IPEndPoint& end_point) {
  Log("Channel IP for client: " + jid + " ip='" + end_point.ToString() +
      "' channel='" + channel_name + "'");
}

void HostEventLogger::OnShutdown() {
}

void HostEventLogger::Log(const std::string& message) {
  system_event_logger_->Log(message);
}

}  // namespace remoting
