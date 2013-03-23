// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DEBUGGER_DEVTOOLS_TRACING_HANDLER_H_
#define CONTENT_BROWSER_DEBUGGER_DEVTOOLS_TRACING_HANDLER_H_

#include "content/browser/debugger/devtools_browser_target.h"
#include "content/public/browser/trace_subscriber.h"

namespace content {

// This class bridges DevTools remote debugging server with the trace
// infrastructure.
class DevToolsTracingHandler
    : public TraceSubscriber,
      public DevToolsBrowserTarget::Handler {
 public:
  DevToolsTracingHandler();
  virtual ~DevToolsTracingHandler();

  // TraceSubscriber:
  virtual void OnEndTracingComplete() OVERRIDE;;
  virtual void OnTraceDataCollected(
      const scoped_refptr<base::RefCountedString>& trace_fragment) OVERRIDE;

  // DevToolBrowserTarget::Handler:
  virtual std::string Domain() OVERRIDE;
  virtual base::Value* OnProtocolCommand(
      const std::string& method,
      const base::DictionaryValue* params,
      base::Value** error_out) OVERRIDE;

 private:
  base::Value* Start(const base::DictionaryValue* params);
  base::Value* End(const base::DictionaryValue* params);
  base::Value* HasCompleted(const base::DictionaryValue* params);
  base::Value* GetTraceAndReset(const base::DictionaryValue* params);

  bool has_completed_;

  std::vector<std::string> buffer_;
  int buffer_data_size_;

  DISALLOW_COPY_AND_ASSIGN(DevToolsTracingHandler);
};

}  // namespace content

#endif  // CONTENT_BROWSER_DEBUGGER_DEVTOOLS_TRACING_HANDLER_H_
