// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/obsolete_os_prompt.h"

#include "base/logging.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/startup/obsolete_os_info_bar.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace chrome {

void ShowObsoleteOSPrompt(Browser* browser) {
  // We've deprecated support for Ubuntu Hardy.  Rather than attempting to
  // determine whether you're using that, we instead key off the GTK version;
  // this will also deprecate other distributions (including variants of Ubuntu)
  // that are of a similar age.
  // Version key:
  //   Ubuntu Hardy: GTK 2.12
  //   RHEL 6:       GTK 2.18
  //   Ubuntu Lucid: GTK 2.20
  if (gtk_check_version(2, 18, 0)) {
    string16 message = l10n_util::GetStringUTF16(IDS_SYSTEM_OBSOLETE_MESSAGE);
    // Link to an article in the help center on minimum system requirements.
    const char* kLearnMoreURL =
        "http://www.google.com/support/chrome/bin/answer.py?answer=95411";
    content::WebContents* web_contents = chrome::GetActiveWebContents(browser);
    if (!web_contents)
      return;
    InfoBarTabHelper* infobar_tab_helper =
        InfoBarTabHelper::FromWebContents(web_contents);
    infobar_tab_helper->AddInfoBar(new ObsoleteOSInfoBar(infobar_tab_helper,
                                   message,
                                   GURL(kLearnMoreURL)));
  }
}

}  // namespace chrome
