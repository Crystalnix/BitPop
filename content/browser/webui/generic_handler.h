// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_WEBUI_GENERIC_HANDLER_H_
#define CONTENT_BROWSER_WEBUI_GENERIC_HANDLER_H_
#pragma once

#include "content/browser/webui/web_ui.h"

class ListValue;

// A place to add handlers for messages shared across all WebUI pages.
class GenericHandler : public WebUIMessageHandler {
 public:
  GenericHandler();
  virtual ~GenericHandler();

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages();

 private:
  void HandleNavigateToUrl(const ListValue* args);

  DISALLOW_COPY_AND_ASSIGN(GenericHandler);
};

#endif  // CONTENT_BROWSER_WEBUI_GENERIC_HANDLER_H_
