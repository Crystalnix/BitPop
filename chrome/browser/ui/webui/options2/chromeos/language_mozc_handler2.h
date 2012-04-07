// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS2_CHROMEOS_LANGUAGE_MOZC_HANDLER2_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS2_CHROMEOS_LANGUAGE_MOZC_HANDLER2_H_
#pragma once

#include "base/compiler_specific.h"
#include "chrome/browser/ui/webui/options2/options_ui2.h"

namespace base {
class DictionaryValue;
}

namespace chromeos {
namespace options2 {

// Mozc options page UI handler.
class LanguageMozcHandler : public ::options2::OptionsPageUIHandler {
 public:
  LanguageMozcHandler();
  virtual ~LanguageMozcHandler();

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(
      base::DictionaryValue* localized_strings) OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(LanguageMozcHandler);
};

}  // namespace options2
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS2_CHROMEOS_LANGUAGE_MOZC_HANDLER2_H_
