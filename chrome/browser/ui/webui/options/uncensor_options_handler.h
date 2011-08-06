// Copyright (c) 2011 House of Life Property ltd.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS_UNCENSOR_OPTIONS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS_UNCENSOR_OPTIONS_HANDLER_H_

#include "chrome/browser/ui/webui/options/options_ui.h"

class DictionaryValue;

class UncensorOptionsHandler : public OptionsPageUIHandler {
 public:
  UncensorOptionsHandler();
  virtual ~UncensorOptionsHandler();

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(DictionaryValue* localized_strings);
  virtual void Initialize();
  virtual void RegisterMessages();

 private:
  void TestCallback(const ListValue* args);
  
  DISALLOW_COPY_AND_ASSIGN(UncensorOptionsHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS_UNCENSOR_OPTIONS_HANDLER_H_
