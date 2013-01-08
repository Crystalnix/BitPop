// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS2_BITPOP_UNCENSOR_FILTER_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS2_BITPOP_UNCENSOR_FILTER_HANDLER_H_

#include "chrome/browser/ui/search_engines/edit_search_engine_controller.h"
#include "chrome/browser/ui/webui/options2/options_ui.h"
#include "ui/base/models/table_model_observer.h"

class KeywordEditorController;

namespace extensions {
class Extension;
}

namespace options2 {

class BitpopUncensorFilterHandler : public BitpopOptionsPageUIHandler {
 public:
  BitpopUncensorFilterHandler();
  virtual ~BitpopUncensorFilterHandler();

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(
      base::DictionaryValue* localized_strings) OVERRIDE;
  virtual void InitializeHandler() OVERRIDE;
  virtual void InitializePage() OVERRIDE;

  virtual void RegisterMessages() OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(BitpopUncensorFilterHandler);
};

}  // namespace options2

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS2_BITPOP_UNCENSOR_FILTER_HANDLER_H_
