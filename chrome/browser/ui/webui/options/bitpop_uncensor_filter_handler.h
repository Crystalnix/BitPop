// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS_BITPOP_UNCENSOR_FILTER_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS_BITPOP_UNCENSOR_FILTER_HANDLER_H_

#include "chrome/browser/ui/webui/options/bitpop_options_ui.h"

class KeywordEditorController;

namespace extensions {
class Extension;
}

namespace base {
	class ListValue;
}

namespace options {

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

 	void ChangeUncensorExceptions(const base::ListValue* params);

  DISALLOW_COPY_AND_ASSIGN(BitpopUncensorFilterHandler);
};

}  // namespace options

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS_BITPOP_UNCENSOR_FILTER_HANDLER_H_
