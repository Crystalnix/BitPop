// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_install_ui.h"

#include <map>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_install_dialog.h"
#include "chrome/browser/extensions/theme_installed_infobar_delegate.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/extensions/url_pattern.h"
#include "chrome/common/url_constants.h"
#include "content/common/notification_service.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

// static
const int ExtensionInstallUI::kTitleIds[NUM_PROMPT_TYPES] = {
  IDS_EXTENSION_INSTALL_PROMPT_TITLE,
  IDS_EXTENSION_RE_ENABLE_PROMPT_TITLE
};
// static
const int ExtensionInstallUI::kHeadingIds[NUM_PROMPT_TYPES] = {
  IDS_EXTENSION_INSTALL_PROMPT_HEADING,
  IDS_EXTENSION_RE_ENABLE_PROMPT_HEADING
};
// static
const int ExtensionInstallUI::kButtonIds[NUM_PROMPT_TYPES] = {
  IDS_EXTENSION_PROMPT_INSTALL_BUTTON,
  IDS_EXTENSION_PROMPT_RE_ENABLE_BUTTON
};
// static
const int ExtensionInstallUI::kWarningIds[NUM_PROMPT_TYPES] = {
  IDS_EXTENSION_PROMPT_WILL_HAVE_ACCESS_TO,
  IDS_EXTENSION_PROMPT_WILL_NOW_HAVE_ACCESS_TO
};

namespace {

// Size of extension icon in top left of dialog.
const int kIconSize = 69;

// Shows the application install animation on the new tab page for the app
// with |app_id|. If a NTP already exists on the active |browser|, this will
// select that tab and show the animation there. Otherwise, it will create
// a new NTP.
void ShowAppInstalledAnimation(Browser* browser, const std::string& app_id) {
  // Select an already open NTP, if there is one. Existing NTPs will
  // automatically show the install animation for any new apps.
  for (int i = 0; i < browser->tab_count(); ++i) {
    GURL url = browser->GetTabContentsAt(i)->GetURL();
    if (web_ui_util::ChromeURLHostEquals(url, chrome::kChromeUINewTabHost)) {
      browser->ActivateTabAt(i, false);
      return;
    }
  }

  // If there isn't an NTP, open one and pass it the ID of the installed app.
  std::string url = base::StringPrintf(
      "%s#app-id=%s", chrome::kChromeUINewTabURL, app_id.c_str());
  browser->AddSelectedTabWithURL(GURL(url), PageTransition::TYPED);
}

}  // namespace

ExtensionInstallUI::ExtensionInstallUI(Profile* profile)
    : profile_(profile),
      ui_loop_(MessageLoop::current()),
      previous_using_native_theme_(false),
      extension_(NULL),
      delegate_(NULL),
      prompt_type_(NUM_PROMPT_TYPES),
      ALLOW_THIS_IN_INITIALIZER_LIST(tracker_(this)) {
  // Remember the current theme in case the user presses undo.
  if (profile_) {
    const Extension* previous_theme =
        ThemeServiceFactory::GetThemeForProfile(profile_);
    if (previous_theme)
      previous_theme_id_ = previous_theme->id();
    previous_using_native_theme_ =
        ThemeServiceFactory::GetForProfile(profile_)->UsingNativeTheme();
  }
}

ExtensionInstallUI::~ExtensionInstallUI() {
}

void ExtensionInstallUI::ConfirmInstall(Delegate* delegate,
                                        const Extension* extension) {
  DCHECK(ui_loop_ == MessageLoop::current());
  extension_ = extension;
  delegate_ = delegate;

  // We special-case themes to not show any confirm UI. Instead they are
  // immediately installed, and then we show an infobar (see OnInstallSuccess)
  // to allow the user to revert if they don't like it.
  if (extension->is_theme()) {
    delegate->InstallUIProceed();
    return;
  }

  ShowConfirmation(INSTALL_PROMPT);
}

void ExtensionInstallUI::ConfirmReEnable(Delegate* delegate,
                                         const Extension* extension) {
  DCHECK(ui_loop_ == MessageLoop::current());
  extension_ = extension;
  delegate_ = delegate;

  ShowConfirmation(RE_ENABLE_PROMPT);
}

void ExtensionInstallUI::OnInstallSuccess(const Extension* extension,
                                          SkBitmap* icon) {
  extension_ = extension;
  SetIcon(icon);

  if (extension->is_theme()) {
    ShowThemeInfoBar(previous_theme_id_, previous_using_native_theme_,
                     extension, profile_);
    return;
  }

  // Extensions aren't enabled by default in incognito so we confirm
  // the install in a normal window.
  Profile* profile = profile_->GetOriginalProfile();
  Browser* browser = Browser::GetOrCreateTabbedBrowser(profile);
  if (browser->tab_count() == 0)
    browser->AddBlankTab(true);
  browser->window()->Show();

  if (extension->GetFullLaunchURL().is_valid()) {
    ShowAppInstalledAnimation(browser, extension->id());
    return;
  }

  browser::ShowExtensionInstalledBubble(extension, browser, icon_, profile);
}

namespace {

bool disable_failure_ui_for_tests = false;

}  // namespace

void ExtensionInstallUI::OnInstallFailure(const std::string& error) {
  DCHECK(ui_loop_ == MessageLoop::current());

  Browser* browser = BrowserList::GetLastActiveWithProfile(profile_);
  if (disable_failure_ui_for_tests)
    return;
  platform_util::SimpleErrorBox(
      browser ? browser->window()->GetNativeHandle() : NULL,
      l10n_util::GetStringUTF16(IDS_EXTENSION_INSTALL_FAILURE_TITLE),
      UTF8ToUTF16(error));
}

void ExtensionInstallUI::SetIcon(SkBitmap* image) {
  if (image)
    icon_ = *image;
  else
    icon_ = SkBitmap();
  if (icon_.empty())
    icon_ = Extension::GetDefaultIcon(extension_->is_app());
}

void ExtensionInstallUI::OnImageLoaded(
    SkBitmap* image, const ExtensionResource& resource, int index) {
  SetIcon(image);

  switch (prompt_type_) {
    case RE_ENABLE_PROMPT:
    case INSTALL_PROMPT: {
      // TODO(jcivelli): http://crbug.com/44771 We should not show an install
      //                 dialog when installing an app from the gallery.
      NotificationService* service = NotificationService::current();
      service->Notify(NotificationType::EXTENSION_WILL_SHOW_CONFIRM_DIALOG,
          Source<ExtensionInstallUI>(this),
          NotificationService::NoDetails());

      std::vector<string16> warnings =
          extension_->GetPermissionMessageStrings();
      ShowExtensionInstallDialog(
          profile_, delegate_, extension_, &icon_, warnings, prompt_type_);
      break;
    }
    default:
      NOTREACHED() << "Unknown message";
      break;
  }
}

// static
void ExtensionInstallUI::DisableFailureUIForTests() {
  disable_failure_ui_for_tests = true;
}

void ExtensionInstallUI::ShowThemeInfoBar(const std::string& previous_theme_id,
                                          bool previous_using_native_theme,
                                          const Extension* new_theme,
                                          Profile* profile) {
  if (!new_theme->is_theme())
    return;

  // Get last active tabbed browser of profile.
  Browser* browser = BrowserList::FindTabbedBrowser(profile, true);
  if (!browser)
    return;

  TabContentsWrapper* tab_contents = browser->GetSelectedTabContentsWrapper();
  if (!tab_contents)
    return;

  // First find any previous theme preview infobars.
  InfoBarDelegate* old_delegate = NULL;
  for (size_t i = 0; i < tab_contents->infobar_count(); ++i) {
    InfoBarDelegate* delegate = tab_contents->GetInfoBarDelegateAt(i);
    ThemeInstalledInfoBarDelegate* theme_infobar =
        delegate->AsThemePreviewInfobarDelegate();
    if (theme_infobar) {
      // If the user installed the same theme twice, ignore the second install
      // and keep the first install info bar, so that they can easily undo to
      // get back the previous theme.
      if (theme_infobar->MatchesTheme(new_theme))
        return;
      old_delegate = delegate;
      break;
    }
  }

  // Then either replace that old one or add a new one.
  InfoBarDelegate* new_delegate = GetNewThemeInstalledInfoBarDelegate(
      tab_contents->tab_contents(), new_theme, previous_theme_id,
      previous_using_native_theme);

  if (old_delegate)
    tab_contents->ReplaceInfoBar(old_delegate, new_delegate);
  else
    tab_contents->AddInfoBar(new_delegate);
}

void ExtensionInstallUI::ShowConfirmation(PromptType prompt_type) {
  // Load the image asynchronously. For the response, check OnImageLoaded.
  prompt_type_ = prompt_type;
  ExtensionResource image =
      extension_->GetIconResource(Extension::EXTENSION_ICON_LARGE,
                                  ExtensionIconSet::MATCH_EXACTLY);
  tracker_.LoadImage(extension_, image,
                     gfx::Size(kIconSize, kIconSize),
                     ImageLoadingTracker::DONT_CACHE);
}

InfoBarDelegate* ExtensionInstallUI::GetNewThemeInstalledInfoBarDelegate(
    TabContents* tab_contents,
    const Extension* new_theme,
    const std::string& previous_theme_id,
    bool previous_using_native_theme) {
  return new ThemeInstalledInfoBarDelegate(tab_contents, new_theme,
                                           previous_theme_id,
                                           previous_using_native_theme);
}
