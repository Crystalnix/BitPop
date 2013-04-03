// Copyright (c) 2012-2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012-2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS2_BITPOP_OPTIONS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS2_BITPOP_OPTIONS_HANDLER_H_

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/prefs/pref_member.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/webui/options2/bitpop_options_ui.h"
#include "ui/base/models/table_model_observer.h"

#include "chrome/browser/prefs/pref_set_observer.h"

namespace options2 {

// Chrome browser options page UI handler.
class BitpopOptionsHandler
    : public BitpopOptionsPageUIHandler {
 public:
  BitpopOptionsHandler();
  virtual ~BitpopOptionsHandler();

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(DictionaryValue* values) OVERRIDE;
  virtual void InitializeHandler() OVERRIDE;
  virtual void InitializePage() OVERRIDE;
  virtual void RegisterMessages() OVERRIDE;

 private:
  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  void OpenFacebookNotificationsOptions(const base::ListValue * params);

  DISALLOW_COPY_AND_ASSIGN(BitpopOptionsHandler);
};

}  // namespace options2

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS2_BITPOP_OPTIONS_HANDLER_H_
