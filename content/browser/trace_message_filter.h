// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_TRACE_MESSAGE_FILTER_H_
#define CONTENT_BROWSER_TRACE_MESSAGE_FILTER_H_

#include <string>

#include "content/browser/browser_message_filter.h"

// This class sends and receives trace messages on the browser process.
// See also: trace_controller.h
// See also: child_trace_message_filter.h
class TraceMessageFilter : public BrowserMessageFilter {
 public:
  TraceMessageFilter();
  virtual ~TraceMessageFilter();

  // BrowserMessageFilter override.
  virtual void OnFilterAdded(IPC::Channel* channel);

  // BrowserMessageFilter implementation.
  virtual void OnChannelClosing();
  virtual bool OnMessageReceived(const IPC::Message& message,
                                 bool* message_was_ok);

  void SendBeginTracing();
  void SendEndTracing();
  void SendGetTraceBufferPercentFull();

 private:
  // Message handlers.
  void OnEndTracingAck();
  void OnTraceBufferFull();
  void OnTraceBufferPercentFullReply(float percent_full);
  void OnTraceDataCollected(const std::string& data);

  // Awaiting ack for previously sent SendEndTracing
  bool is_awaiting_end_ack_;
  bool is_awaiting_bpf_ack_;

  DISALLOW_COPY_AND_ASSIGN(TraceMessageFilter);
};

#endif  // CONTENT_BROWSER_TRACE_MESSAGE_FILTER_H_

