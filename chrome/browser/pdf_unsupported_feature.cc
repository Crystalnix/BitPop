// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pdf_unsupported_feature.h"

#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/chrome_interstitial_page.h"
#include "chrome/browser/tab_contents/confirm_infobar_delegate.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/chrome_content_client.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/pref_names.h"
#include "content/browser/plugin_service.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/user_metrics.h"
#include "content/common/view_messages.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "webkit/plugins/npapi/plugin_group.h"
#include "webkit/plugins/npapi/plugin_list.h"
#include "webkit/plugins/npapi/webplugininfo.h"

using webkit::npapi::PluginGroup;
using webkit::npapi::PluginList;
using webkit::npapi::WebPluginInfo;

namespace {

// Only launch Adobe Reader X or later.
static const uint16 kMinReaderVersionToUse = 10;

static const char kReaderUpdateUrl[] =
    "http://www.adobe.com/go/getreader_chrome";

// The info bar delegate used to ask the user if they want to use Adobe Reader
// by default.  We want the infobar to have [No][Yes], so we swap the text on
// the buttons, and the meaning of the delegate callbacks.
class PDFEnableAdobeReaderInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  explicit PDFEnableAdobeReaderInfoBarDelegate(TabContents* tab_contents);
  virtual ~PDFEnableAdobeReaderInfoBarDelegate();

  // ConfirmInfoBarDelegate
  virtual void InfoBarDismissed() OVERRIDE;
  virtual Type GetInfoBarType() const OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual bool Cancel() OVERRIDE;
  virtual string16 GetButtonLabel(InfoBarButton button) const OVERRIDE;
  virtual string16 GetMessageText() const OVERRIDE;

 private:
  void OnYes();
  void OnNo();

  TabContents* tab_contents_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(PDFEnableAdobeReaderInfoBarDelegate);
};

PDFEnableAdobeReaderInfoBarDelegate::PDFEnableAdobeReaderInfoBarDelegate(
    TabContents* tab_contents)
    : ConfirmInfoBarDelegate(tab_contents),
      tab_contents_(tab_contents) {
  UserMetrics::RecordAction(UserMetricsAction("PDF_EnableReaderInfoBarShown"));
}

PDFEnableAdobeReaderInfoBarDelegate::~PDFEnableAdobeReaderInfoBarDelegate() {
}

void PDFEnableAdobeReaderInfoBarDelegate::InfoBarDismissed() {
  OnNo();
}

InfoBarDelegate::Type
    PDFEnableAdobeReaderInfoBarDelegate::GetInfoBarType() const {
  return PAGE_ACTION_TYPE;
}

bool PDFEnableAdobeReaderInfoBarDelegate::Accept() {
  tab_contents_->profile()->GetPrefs()->SetBoolean(
      prefs::kPluginsShowSetReaderDefaultInfobar, false);
  OnNo();
  return true;
}

bool PDFEnableAdobeReaderInfoBarDelegate::Cancel() {
  OnYes();
  return true;
}

string16 PDFEnableAdobeReaderInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_PDF_INFOBAR_NEVER_USE_READER_BUTTON :
      IDS_PDF_INFOBAR_ALWAYS_USE_READER_BUTTON);
}

string16 PDFEnableAdobeReaderInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(IDS_PDF_INFOBAR_QUESTION_ALWAYS_USE_READER);
}

void PDFEnableAdobeReaderInfoBarDelegate::OnYes() {
  UserMetrics::RecordAction(UserMetricsAction("PDF_EnableReaderInfoBarOK"));
  webkit::npapi::PluginList::Singleton()->EnableGroup(false,
      ASCIIToUTF16(chrome::ChromeContentClient::kPDFPluginName));
  webkit::npapi::PluginList::Singleton()->EnableGroup(true,
      ASCIIToUTF16(webkit::npapi::PluginGroup::kAdobeReaderGroupName));
}

void PDFEnableAdobeReaderInfoBarDelegate::OnNo() {
  UserMetrics::RecordAction(UserMetricsAction("PDF_EnableReaderInfoBarCancel"));
}

// Launch the url to get the latest Adbobe Reader installer.
void OpenReaderUpdateURL(TabContents* tab) {
  tab->OpenURL(GURL(kReaderUpdateUrl), GURL(), CURRENT_TAB,
               PageTransition::LINK);
}

// Opens the PDF using Adobe Reader.
void OpenUsingReader(TabContentsWrapper* tab,
                     const WebPluginInfo& reader_plugin,
                     InfoBarDelegate* old_delegate,
                     InfoBarDelegate* new_delegate) {
  PluginService::OverriddenPlugin plugin;
  plugin.render_process_id = tab->render_view_host()->process()->id();
  plugin.render_view_id = tab->render_view_host()->routing_id();
  plugin.url = tab->tab_contents()->GetURL();
  plugin.plugin = reader_plugin;
  // The plugin is disabled, so enable it to get around the renderer check.
  // Also give it a new version so that the renderer doesn't show the blocked
  // plugin UI if it's vulnerable, since we already went through the
  // interstitial.
  plugin.plugin.enabled = WebPluginInfo::USER_ENABLED;
  plugin.plugin.version = ASCIIToUTF16("11.0.0.0");

  PluginService::GetInstance()->OverridePluginForTab(plugin);
  tab->render_view_host()->Send(new ViewMsg_ReloadFrame(
      tab->render_view_host()->routing_id()));

  if (new_delegate) {
    if (old_delegate) {
      tab->ReplaceInfoBar(old_delegate, new_delegate);
    } else {
      tab->AddInfoBar(new_delegate);
    }
  }
}

// An interstitial to be used when the user chooses to open a PDF using Adobe
// Reader, but it is out of date.
class PDFUnsupportedFeatureInterstitial : public ChromeInterstitialPage {
 public:
  PDFUnsupportedFeatureInterstitial(
      TabContentsWrapper* tab,
      const WebPluginInfo& reader_webplugininfo)
      : ChromeInterstitialPage(
            tab->tab_contents(), false, tab->tab_contents()->GetURL()),
        tab_contents_(tab),
        reader_webplugininfo_(reader_webplugininfo) {
    UserMetrics::RecordAction(UserMetricsAction("PDF_ReaderInterstitialShown"));
  }

 protected:
  // ChromeInterstitialPage implementation.
  virtual std::string GetHTMLContents() {
    DictionaryValue strings;
    strings.SetString(
        "title",
        l10n_util::GetStringUTF16(IDS_READER_OUT_OF_DATE_BLOCKING_PAGE_TITLE));
    strings.SetString(
        "headLine",
        l10n_util::GetStringUTF16(IDS_READER_OUT_OF_DATE_BLOCKING_PAGE_BODY));
    strings.SetString(
        "update",
        l10n_util::GetStringUTF16(IDS_READER_OUT_OF_DATE_BLOCKING_PAGE_UPDATE));
    strings.SetString(
        "open_with_reader",
        l10n_util::GetStringUTF16(
            IDS_READER_OUT_OF_DATE_BLOCKING_PAGE_PROCEED));
    strings.SetString(
        "ok",
        l10n_util::GetStringUTF16(IDS_READER_OUT_OF_DATE_BLOCKING_PAGE_OK));
    strings.SetString(
        "cancel",
        l10n_util::GetStringUTF16(IDS_READER_OUT_OF_DATE_BLOCKING_PAGE_CANCEL));

    base::StringPiece html(ResourceBundle::GetSharedInstance().
        GetRawDataResource(IDR_READER_OUT_OF_DATE_HTML));

    return jstemplate_builder::GetI18nTemplateHtml(html, &strings);
  }

  virtual void CommandReceived(const std::string& command) {
    if (command == "0") {
      UserMetrics::RecordAction(
          UserMetricsAction("PDF_ReaderInterstitialCancel"));
      DontProceed();
      return;
    }

    if (command == "1") {
      UserMetrics::RecordAction(
          UserMetricsAction("PDF_ReaderInterstitialUpdate"));
      OpenReaderUpdateURL(tab());
    } else if (command == "2") {
      UserMetrics::RecordAction(
          UserMetricsAction("PDF_ReaderInterstitialIgnore"));
      OpenUsingReader(tab_contents_, reader_webplugininfo_, NULL, NULL);
    } else {
      NOTREACHED();
    }
    Proceed();
  }

 private:
  TabContentsWrapper* tab_contents_;
  WebPluginInfo reader_webplugininfo_;

  DISALLOW_COPY_AND_ASSIGN(PDFUnsupportedFeatureInterstitial);
};

// The info bar delegate used to inform the user that we don't support a feature
// in the PDF.  See the comment about how we swap buttons for
// PDFEnableAdobeReaderInfoBarDelegate.
class PDFUnsupportedFeatureInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  // |reader_group| is NULL if Adobe Reader isn't installed.
  PDFUnsupportedFeatureInfoBarDelegate(TabContentsWrapper* tab_contents,
                                       PluginGroup* reader_group);
  virtual ~PDFUnsupportedFeatureInfoBarDelegate();

  // ConfirmInfoBarDelegate
  virtual void InfoBarDismissed() OVERRIDE;
  virtual Type GetInfoBarType() const OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual bool Cancel() OVERRIDE;
  virtual string16 GetButtonLabel(InfoBarButton button) const OVERRIDE;
  virtual string16 GetMessageText() const OVERRIDE;

 private:
  bool OnYes();
  void OnNo();

  TabContentsWrapper* tab_contents_;
  bool reader_installed_;
  bool reader_vulnerable_;
  WebPluginInfo reader_webplugininfo_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(PDFUnsupportedFeatureInfoBarDelegate);
};

PDFUnsupportedFeatureInfoBarDelegate::PDFUnsupportedFeatureInfoBarDelegate(
    TabContentsWrapper* tab_contents,
    PluginGroup* reader_group)
    : ConfirmInfoBarDelegate(tab_contents->tab_contents()),
      tab_contents_(tab_contents),
      reader_installed_(!!reader_group),
      reader_vulnerable_(false) {
  if (!reader_installed_) {
    UserMetrics::RecordAction(
        UserMetricsAction("PDF_InstallReaderInfoBarShown"));
    return;
  }

  UserMetrics::RecordAction(UserMetricsAction("PDF_UseReaderInfoBarShown"));
  std::vector<WebPluginInfo> plugins = reader_group->web_plugin_infos();
  DCHECK_EQ(plugins.size(), 1u);
  reader_webplugininfo_ = plugins[0];

  reader_vulnerable_ = reader_group->IsVulnerable();
  if (!reader_vulnerable_) {
    scoped_ptr<Version> version(PluginGroup::CreateVersionFromString(
        reader_webplugininfo_.version));
    reader_vulnerable_ =
        version.get() && (version->components()[0] < kMinReaderVersionToUse);
  }
}

PDFUnsupportedFeatureInfoBarDelegate::~PDFUnsupportedFeatureInfoBarDelegate() {
}

void PDFUnsupportedFeatureInfoBarDelegate::InfoBarDismissed() {
  OnNo();
}

InfoBarDelegate::Type
    PDFUnsupportedFeatureInfoBarDelegate::GetInfoBarType() const {
  return PAGE_ACTION_TYPE;
}

bool PDFUnsupportedFeatureInfoBarDelegate::Accept() {
  OnNo();
  return true;
}

bool PDFUnsupportedFeatureInfoBarDelegate::Cancel() {
  return OnYes();
}

string16 PDFUnsupportedFeatureInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_CONFIRM_MESSAGEBOX_NO_BUTTON_LABEL :
      IDS_CONFIRM_MESSAGEBOX_YES_BUTTON_LABEL);
}

string16 PDFUnsupportedFeatureInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(reader_installed_ ?
      IDS_PDF_INFOBAR_QUESTION_READER_INSTALLED :
      IDS_PDF_INFOBAR_QUESTION_READER_NOT_INSTALLED);
}

bool PDFUnsupportedFeatureInfoBarDelegate::OnYes() {
  if (!reader_installed_) {
    UserMetrics::RecordAction(UserMetricsAction("PDF_InstallReaderInfoBarOK"));
    OpenReaderUpdateURL(tab_contents_->tab_contents());
    return true;
  }

  UserMetrics::RecordAction(UserMetricsAction("PDF_UseReaderInfoBarOK"));

  if (reader_vulnerable_) {
    PDFUnsupportedFeatureInterstitial* interstitial =
        new PDFUnsupportedFeatureInterstitial(tab_contents_,
                                              reader_webplugininfo_);
    interstitial->Show();
    return true;
  }

  if (tab_contents_->profile()->GetPrefs()->GetBoolean(
      prefs::kPluginsShowSetReaderDefaultInfobar)) {
    InfoBarDelegate* bar =
        new PDFEnableAdobeReaderInfoBarDelegate(tab_contents_->tab_contents());
    OpenUsingReader(tab_contents_, reader_webplugininfo_, this, bar);
    return false;
  }

  OpenUsingReader(tab_contents_, reader_webplugininfo_, NULL, NULL);
  return true;
}

void PDFUnsupportedFeatureInfoBarDelegate::OnNo() {
  UserMetrics::RecordAction(UserMetricsAction(reader_installed_ ?
      "PDF_UseReaderInfoBarCancel" : "PDF_InstallReaderInfoBarCancel"));
}

}  // namespace

void PDFHasUnsupportedFeature(TabContentsWrapper* tab) {
#if !defined(OS_WIN)
  // Only works for Windows for now.  For Mac, we'll have to launch the file
  // externally since Adobe Reader doesn't work inside Chrome.
  return;
#endif
  string16 reader_group_name(ASCIIToUTF16(PluginGroup::kAdobeReaderGroupName));

  // If the Reader plugin is disabled by policy, don't prompt them.
  if (PluginGroup::IsPluginNameDisabledByPolicy(reader_group_name))
    return;

  PluginGroup* reader_group = NULL;
  std::vector<PluginGroup> plugin_groups;
  PluginList::Singleton()->GetPluginGroups(
      false, &plugin_groups);
  for (size_t i = 0; i < plugin_groups.size(); ++i) {
    if (plugin_groups[i].GetGroupName() == reader_group_name) {
      reader_group = &plugin_groups[i];
      break;
    }
  }

  tab->AddInfoBar(new PDFUnsupportedFeatureInfoBarDelegate(tab, reader_group));
}
