// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/default_browser_prompt.h"

#include "base/memory/weak_ptr.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/api/infobars/confirm_infobar_delegate.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_details.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

using content::BrowserThread;

namespace {

// Calls the appropriate function for setting Chrome as the default browser.
// This requires IO access (registry) and may result in interaction with a
// modal system UI.
void SetChromeAsDefaultBrowser(bool interactive_flow, PrefService* prefs) {
  if (interactive_flow) {
    UMA_HISTOGRAM_COUNTS("DefaultBrowserWarning.SetAsDefaultUI", 1);
    if (!ShellIntegration::SetAsDefaultBrowserInteractive()) {
      UMA_HISTOGRAM_COUNTS("DefaultBrowserWarning.SetAsDefaultUIFailed", 1);
    } else if (ShellIntegration::GetDefaultBrowser() ==
               ShellIntegration::NOT_DEFAULT) {
      // If the interaction succeeded but we are still not the default browser
      // it likely means the user simply selected another browser from the
      // panel. We will respect this choice and write it down as 'no, thanks'.
      UMA_HISTOGRAM_COUNTS("DefaultBrowserWarning.DontSetAsDefault", 1);
    }
  } else {
    UMA_HISTOGRAM_COUNTS("DefaultBrowserWarning.SetAsDefault", 1);
    ShellIntegration::SetAsDefaultBrowser();
  }
}

// The delegate for the infobar shown when Chrome is not the default browser.
class DefaultBrowserInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  DefaultBrowserInfoBarDelegate(InfoBarTabHelper* infobar_helper,
                                PrefService* prefs,
                                bool interactive_flow_required);

 private:
  virtual ~DefaultBrowserInfoBarDelegate();

  void AllowExpiry() { should_expire_ = true; }

  // ConfirmInfoBarDelegate:
  virtual gfx::Image* GetIcon() const OVERRIDE;
  virtual string16 GetMessageText() const OVERRIDE;
  virtual string16 GetButtonLabel(InfoBarButton button) const OVERRIDE;
  virtual bool NeedElevation(InfoBarButton button) const OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual bool Cancel() OVERRIDE;
  virtual bool ShouldExpireInternal(
      const content::LoadCommittedDetails& details) const OVERRIDE;

  // The prefs to use.
  PrefService* prefs_;

  // Whether the user clicked one of the buttons.
  bool action_taken_;

  // Whether the info-bar should be dismissed on the next navigation.
  bool should_expire_;

  // Whether changing the default application will require entering the
  // modal-UI flow.
  const bool interactive_flow_required_;

  // Used to delay the expiration of the info-bar.
  base::WeakPtrFactory<DefaultBrowserInfoBarDelegate> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DefaultBrowserInfoBarDelegate);
};

DefaultBrowserInfoBarDelegate::DefaultBrowserInfoBarDelegate(
    InfoBarTabHelper* infobar_helper,
    PrefService* prefs,
    bool interactive_flow_required)
    : ConfirmInfoBarDelegate(infobar_helper),
      prefs_(prefs),
      action_taken_(false),
      should_expire_(false),
      interactive_flow_required_(interactive_flow_required),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)) {
  // We want the info-bar to stick-around for few seconds and then be hidden
  // on the next navigation after that.
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE, base::Bind(&DefaultBrowserInfoBarDelegate::AllowExpiry,
                            weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromSeconds(8));
}

DefaultBrowserInfoBarDelegate::~DefaultBrowserInfoBarDelegate() {
  if (!action_taken_)
    UMA_HISTOGRAM_COUNTS("DefaultBrowserWarning.Ignored", 1);
}

gfx::Image* DefaultBrowserInfoBarDelegate::GetIcon() const {
  return &ResourceBundle::GetSharedInstance().GetNativeImageNamed(
     IDR_PRODUCT_LOGO_32);
}

string16 DefaultBrowserInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(IDS_DEFAULT_BROWSER_INFOBAR_SHORT_TEXT);
}

string16 DefaultBrowserInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_SET_AS_DEFAULT_INFOBAR_BUTTON_LABEL :
      IDS_DONT_ASK_AGAIN_INFOBAR_BUTTON_LABEL);
}

bool DefaultBrowserInfoBarDelegate::NeedElevation(InfoBarButton button) const {
  return button == BUTTON_OK;
}

bool DefaultBrowserInfoBarDelegate::Accept() {
  action_taken_ = true;
  BrowserThread::PostTask(
      BrowserThread::FILE,
      FROM_HERE,
      base::Bind(&SetChromeAsDefaultBrowser, interactive_flow_required_,
                 prefs_));

  return true;
}

bool DefaultBrowserInfoBarDelegate::Cancel() {
  action_taken_ = true;
  UMA_HISTOGRAM_COUNTS("DefaultBrowserWarning.DontSetAsDefault", 1);
  // User clicked "Don't ask me again", remember that.
  prefs_->SetBoolean(prefs::kCheckDefaultBrowser, false);
  return true;
}

bool DefaultBrowserInfoBarDelegate::ShouldExpireInternal(
    const content::LoadCommittedDetails& details) const {
  return should_expire_;
}

void NotifyNotDefaultBrowserCallback(chrome::HostDesktopType desktop_type) {
  Browser* browser = chrome::FindLastActiveWithHostDesktopType(desktop_type);
  if (!browser)
    return;  // Reached during ui tests.

  // In ChromeBot tests, there might be a race. This line appears to get
  // called during shutdown and |tab| can be NULL.
  content::WebContents* web_contents = chrome::GetActiveWebContents(browser);
  if (!web_contents)
    return;

  // Don't show the info-bar if there are already info-bars showing.
  InfoBarTabHelper* infobar_helper =
      InfoBarTabHelper::FromWebContents(web_contents);
  if (infobar_helper->GetInfoBarCount() > 0)
    return;

  bool interactive_flow = ShellIntegration::CanSetAsDefaultBrowser() ==
      ShellIntegration::SET_DEFAULT_INTERACTIVE;
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  infobar_helper->AddInfoBar(
      new DefaultBrowserInfoBarDelegate(infobar_helper,
                                        profile->GetPrefs(),
                                        interactive_flow));
}

void CheckDefaultBrowserCallback(chrome::HostDesktopType desktop_type) {
  if (ShellIntegration::GetDefaultBrowser() == ShellIntegration::NOT_DEFAULT) {
    ShellIntegration::DefaultWebClientSetPermission default_change_mode =
        ShellIntegration::CanSetAsDefaultBrowser();

    if (default_change_mode != ShellIntegration::SET_DEFAULT_NOT_ALLOWED) {
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
          base::Bind(&NotifyNotDefaultBrowserCallback, desktop_type));
    }
  }
}

}  // namespace

namespace chrome {

void ShowDefaultBrowserPrompt(Profile* profile, HostDesktopType desktop_type) {
  // We do not check if we are the default browser if:
  // - the user said "don't ask me again" on the infobar earlier.
  // - There is a policy in control of this setting.
  if (!profile->GetPrefs()->GetBoolean(prefs::kCheckDefaultBrowser))
    return;

  if (g_browser_process->local_state()->IsManagedPreference(
      prefs::kDefaultBrowserSettingEnabled)) {
    if (g_browser_process->local_state()->GetBoolean(
        prefs::kDefaultBrowserSettingEnabled)) {
      BrowserThread::PostTask(
          BrowserThread::FILE, FROM_HERE,
          base::Bind(
              base::IgnoreResult(&ShellIntegration::SetAsDefaultBrowser)));
    } else {
      // TODO(pastarmovj): We can't really do anything meaningful here yet but
      // just prevent showing the infobar.
    }
    return;
  }
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          base::Bind(&CheckDefaultBrowserCallback,
                                     desktop_type));

}

#if !defined(OS_WIN)
bool ShowFirstRunDefaultBrowserPrompt(Profile* profile) {
  return false;
}
#endif

}  // namespace chrome
