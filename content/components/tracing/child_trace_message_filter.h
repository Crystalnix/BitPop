// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMPONENTS_TRACING_CHILD_TRACE_MESSAGE_FILTER_H_
#define CONTENT_COMPONENTS_TRACING_CHILD_TRACE_MESSAGE_FILTER_H_

#include "ipc/ipc_channel_proxy.h"

namespace base {
class MessageLoopProxy;
}

namespace content {

// This class sends and receives trace messages on child processes.
class ChildTraceMessageFilter : public IPC::ChannelProxy::MessageFilter {
 public:
  explicit ChildTraceMessageFilter(base::MessageLoopProxy* ipc_message_loop);

  // IPC::ChannelProxy::MessageFilter implementation.
  virtual void OnFilterAdded(IPC::Channel* channel) OVERRIDE;
  virtual void OnFilterRemoved() OVERRIDE;
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

 protected:
  virtual ~ChildTraceMessageFilter();

 private:
  // Message handlers.
  void OnBeginTracing(const std::vector<std::string>& included_categories,
                      const std::vector<std::string>& excluded_categories,
                      base::TimeTicks browser_time);
  void OnEndTracing();
  void OnGetTraceBufferPercentFull();
  void OnSetWatchEvent(const std::string& category_name,
                       const std::string& event_name);
  void OnCancelWatchEvent();

  // Callback from trace subsystem.
  void OnTraceDataCollected(
      const scoped_refptr<base::RefCountedString>& events_str_ptr);
  void OnTraceNotification(int notification);

  IPC::Channel* channel_;
  base::MessageLoopProxy* ipc_message_loop_;

  DISALLOW_COPY_AND_ASSIGN(ChildTraceMessageFilter);
};

} // namespace content

#endif  // CONTENT_COMPONENTS_TRACING_CHILD_TRACE_MESSAGE_FILTER_H_
