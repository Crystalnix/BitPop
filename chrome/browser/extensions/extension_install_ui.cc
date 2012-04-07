// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_install_ui.h"

#include <map>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_install_dialog.h"
#include "chrome/browser/extensions/theme_installed_infobar_delegate.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/simple_message_box.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/extensions/url_pattern.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/notification_service.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

using content::WebContents;

static const int kTitleIds[ExtensionInstallUI::NUM_PROMPT_TYPES] = {
  0,
  IDS_EXTENSION_INLINE_INSTALL_PROMPT_TITLE,
  IDS_EXTENSION_RE_ENABLE_PROMPT_TITLE,
  IDS_EXTENSION_PERMISSIONS_PROMPT_TITLE
};
static const int kHeadingIds[ExtensionInstallUI::NUM_PROMPT_TYPES] = {
  IDS_EXTENSION_INSTALL_PROMPT_HEADING,
  IDS_EXTENSION_INSTALL_PROMPT_HEADING,
  IDS_EXTENSION_RE_ENABLE_PROMPT_HEADING,
  IDS_EXTENSION_PERMISSIONS_PROMPT_HEADING
};
static const int kAcceptButtonIds[ExtensionInstallUI::NUM_PROMPT_TYPES] = {
  IDS_EXTENSION_PROMPT_INSTALL_BUTTON,
  IDS_EXTENSION_PROMPT_INSTALL_BUTTON,
  IDS_EXTENSION_PROMPT_RE_ENABLE_BUTTON,
  IDS_EXTENSION_PROMPT_PERMISSIONS_BUTTON
};
static const int kAbortButtonIds[ExtensionInstallUI::NUM_PROMPT_TYPES] = {
  0,
  0,
  0,
  IDS_EXTENSION_PROMPT_PERMISSIONS_ABORT_BUTTON
};
static const int kPermissionsHeaderIds[ExtensionInstallUI::NUM_PROMPT_TYPES] = {
  IDS_EXTENSION_PROMPT_WILL_HAVE_ACCESS_TO,
  IDS_EXTENSION_PROMPT_WILL_HAVE_ACCESS_TO,
  IDS_EXTENSION_PROMPT_WILL_NOW_HAVE_ACCESS_TO,
  IDS_EXTENSION_PROMPT_WANTS_ACCESS_TO,
};

namespace {

// Size of extension icon in top left of dialog.
const int kIconSize = 69;

}  // namespace

ExtensionInstallUI::Prompt::Prompt(PromptType type)
    : type_(type),
      average_rating_(0.0),
      rating_count_(0) {
}

ExtensionInstallUI::Prompt::~Prompt() {
}

void ExtensionInstallUI::Prompt::SetPermissions(
    std::vector<string16> permissions) {
  permissions_ = permissions;
}

void ExtensionInstallUI::Prompt::SetInlineInstallWebstoreData(
    const std::string& localized_user_count,
    double average_rating,
    int rating_count) {
  CHECK_EQ(INLINE_INSTALL_PROMPT, type_);
  localized_user_count_ = localized_user_count;
  average_rating_ = average_rating;
  rating_count_ = rating_count;
}

string16 ExtensionInstallUI::Prompt::GetDialogTitle(
    const Extension* extension) const {
  if (type_ == INSTALL_PROMPT) {
    return l10n_util::GetStringUTF16(extension->is_app() ?
        IDS_EXTENSION_INSTALL_APP_PROMPT_TITLE :
        IDS_EXTENSION_INSTALL_EXTENSION_PROMPT_TITLE);
  } else {
    return l10n_util::GetStringUTF16(kTitleIds[type_]);
  }
}

string16 ExtensionInstallUI::Prompt::GetHeading(
    const std::string& extension_name) const {
  if (type_ == INLINE_INSTALL_PROMPT) {
    return UTF8ToUTF16(extension_name);
  } else {
    return l10n_util::GetStringFUTF16(
        kHeadingIds[type_], UTF8ToUTF16(extension_name));
  }
}

string16 ExtensionInstallUI::Prompt::GetAcceptButtonLabel() const {
  return l10n_util::GetStringUTF16(kAcceptButtonIds[type_]);
}

bool ExtensionInstallUI::Prompt::HasAbortButtonLabel() const {
  return kAbortButtonIds[type_] > 0;
}

string16 ExtensionInstallUI::Prompt::GetAbortButtonLabel() const {
  CHECK(HasAbortButtonLabel());
  return l10n_util::GetStringUTF16(kAbortButtonIds[type_]);
}

string16 ExtensionInstallUI::Prompt::GetPermissionsHeader() const {
  return l10n_util::GetStringUTF16(kPermissionsHeaderIds[type_]);
}

void ExtensionInstallUI::Prompt::AppendRatingStars(
    StarAppender appender, void* data) const {
  CHECK(appender);
  CHECK_EQ(INLINE_INSTALL_PROMPT, type_);
  int rating_integer = floor(average_rating_);
  double rating_fractional = average_rating_ - rating_integer;

  if (rating_fractional > 0.66) {
    rating_integer++;
  }

  if (rating_fractional < 0.33 || rating_fractional > 0.66) {
    rating_fractional = 0;
  }

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  int i;
  for (i = 0; i < rating_integer; i++) {
    appender(rb.GetBitmapNamed(IDR_EXTENSIONS_RATING_STAR_ON), data);
  }
  if (rating_fractional) {
    appender(rb.GetBitmapNamed(IDR_EXTENSIONS_RATING_STAR_HALF_LEFT), data);
    i++;
  }
  for (; i < kMaxExtensionRating; i++) {
    appender(rb.GetBitmapNamed(IDR_EXTENSIONS_RATING_STAR_OFF), data);
  }
}

string16 ExtensionInstallUI::Prompt::GetRatingCount() const {
  CHECK_EQ(INLINE_INSTALL_PROMPT, type_);
  return l10n_util::GetStringFUTF16(
        IDS_EXTENSION_RATING_COUNT,
        UTF8ToUTF16(base::IntToString(rating_count_)));
}

string16 ExtensionInstallUI::Prompt::GetUserCount() const {
  CHECK_EQ(INLINE_INSTALL_PROMPT, type_);
  return l10n_util::GetStringFUTF16(
      IDS_EXTENSION_USER_COUNT,
      UTF8ToUTF16(localized_user_count_));
}

size_t ExtensionInstallUI::Prompt::GetPermissionCount() const {
  return permissions_.size();
}

string16 ExtensionInstallUI::Prompt::GetPermission(int index) const {
  return l10n_util::GetStringFUTF16(
      IDS_EXTENSION_PERMISSION_LINE, permissions_[index]);
}

ExtensionInstallUI::ExtensionInstallUI(Profile* profile)
    : profile_(profile),
      ui_loop_(MessageLoop::current()),
      previous_using_native_theme_(false),
      extension_(NULL),
      delegate_(NULL),
      prompt_type_(NUM_PROMPT_TYPES),
      ALLOW_THIS_IN_INITIALIZER_LIST(tracker_(this)),
      use_app_installed_bubble_(false),
      skip_post_install_ui_(false) {
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
  permissions_ = extension->GetActivePermissions();
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
  permissions_ = extension->GetActivePermissions();
  delegate_ = delegate;

  ShowConfirmation(RE_ENABLE_PROMPT);
}

void ExtensionInstallUI::ConfirmPermissions(
    Delegate* delegate,
    const Extension* extension,
    const ExtensionPermissionSet* permissions) {
  DCHECK(ui_loop_ == MessageLoop::current());
  extension_ = extension;
  permissions_ = permissions;
  delegate_ = delegate;

  ShowConfirmation(PERMISSIONS_PROMPT);
}

void ExtensionInstallUI::OnInstallSuccess(const Extension* extension,
                                          SkBitmap* icon) {
  if (skip_post_install_ui_)
    return;

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

  bool use_bubble_for_apps = false;

#if defined(TOOLKIT_VIEWS)
  CommandLine* cmdline = CommandLine::ForCurrentProcess();
  use_bubble_for_apps = (use_app_installed_bubble_ ||
                         cmdline->HasSwitch(switches::kAppsNewInstallBubble));
#endif

  if (extension->is_app() && !use_bubble_for_apps) {
    ExtensionInstallUI::OpenAppInstalledNTP(browser, extension->id());
    return;
  }

  browser::ShowExtensionInstalledBubble(extension, browser, icon_, profile);
}

namespace {

bool disable_failure_ui_for_tests = false;

}  // namespace

void ExtensionInstallUI::OnInstallFailure(const string16& error) {
  DCHECK(ui_loop_ == MessageLoop::current());

  Browser* browser = BrowserList::GetLastActiveWithProfile(profile_);
  if (disable_failure_ui_for_tests)
    return;
  browser::ShowErrorBox(
      browser ? browser->window()->GetNativeHandle() : NULL,
      l10n_util::GetStringUTF16(IDS_EXTENSION_INSTALL_FAILURE_TITLE),
      error);
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
    case PERMISSIONS_PROMPT:
    case RE_ENABLE_PROMPT:
    case INSTALL_PROMPT: {
      content::NotificationService* service =
          content::NotificationService::current();
      service->Notify(chrome::NOTIFICATION_EXTENSION_WILL_SHOW_CONFIRM_DIALOG,
          content::Source<ExtensionInstallUI>(this),
          content::NotificationService::NoDetails());

      Prompt prompt(prompt_type_);
      prompt.SetPermissions(permissions_->GetWarningMessages());
      ShowExtensionInstallDialog(
          profile_, delegate_, extension_, &icon_, prompt);
      break;
    }
    default:
      NOTREACHED() << "Unknown message";
      break;
  }
}

// static
void ExtensionInstallUI::OpenAppInstalledNTP(Browser* browser,
                                             const std::string& app_id) {
  browser::NavigateParams params =
      browser->GetSingletonTabNavigateParams(GURL(chrome::kChromeUINewTabURL));
  browser::Navigate(&params);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_APP_INSTALLED_TO_NTP,
      content::Source<WebContents>(params.target_contents->web_contents()),
      content::Details<const std::string>(&app_id));
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
  InfoBarTabHelper* infobar_helper = tab_contents->infobar_tab_helper();

  // First find any previous theme preview infobars.
  InfoBarDelegate* old_delegate = NULL;
  for (size_t i = 0; i < infobar_helper->infobar_count(); ++i) {
    InfoBarDelegate* delegate = infobar_helper->GetInfoBarDelegateAt(i);
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
      tab_contents, new_theme, previous_theme_id, previous_using_native_theme);

  if (old_delegate)
    infobar_helper->ReplaceInfoBar(old_delegate, new_delegate);
  else
    infobar_helper->AddInfoBar(new_delegate);
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
    TabContentsWrapper* tab_contents,
    const Extension* new_theme,
    const std::string& previous_theme_id,
    bool previous_using_native_theme) {
  Profile* profile = tab_contents->profile();
  return new ThemeInstalledInfoBarDelegate(
      tab_contents->infobar_tab_helper(),
      profile->GetExtensionService(),
      ThemeServiceFactory::GetForProfile(profile),
      new_theme,
      previous_theme_id,
      previous_using_native_theme);
}
