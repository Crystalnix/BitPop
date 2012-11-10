// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tab_contents/tab_contents.h"

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "chrome/browser/autocomplete_history_manager.h"
#include "chrome/browser/autofill/autofill_external_delegate.h"
#include "chrome/browser/autofill/autofill_manager.h"
#include "chrome/browser/automation/automation_tab_helper.h"
#include "chrome/browser/captive_portal/captive_portal_tab_helper.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_api.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/external_protocol/external_protocol_observer.h"
#include "chrome/browser/favicon/favicon_tab_helper.h"
#include "chrome/browser/history/history_tab_helper.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/net/cache_stats.h"
#include "chrome/browser/omnibox_search_hint.h"
#include "chrome/browser/password_manager/password_manager.h"
#include "chrome/browser/password_manager/password_manager_delegate_impl.h"
#include "chrome/browser/plugin_observer.h"
#include "chrome/browser/prerender/prerender_tab_helper.h"
#include "chrome/browser/printing/print_preview_message_handler.h"
#include "chrome/browser/printing/print_view_manager.h"
#include "chrome/browser/safe_browsing/safe_browsing_tab_observer.h"
#include "chrome/browser/sessions/restore_tab_helper.h"
#include "chrome/browser/tab_contents/navigation_metrics_recorder.h"
#include "chrome/browser/tab_contents/tab_contents_ssl_helper.h"
#include "chrome/browser/tab_contents/thumbnail_generator.h"
#include "chrome/browser/translate/translate_tab_helper.h"
#include "chrome/browser/ui/alternate_error_tab_observer.h"
#include "chrome/browser/ui/blocked_content/blocked_content_tab_helper.h"
#include "chrome/browser/ui/bookmarks/bookmark_tab_helper.h"
#include "chrome/browser/ui/constrained_window_tab_helper.h"
#include "chrome/browser/ui/find_bar/find_tab_helper.h"
#include "chrome/browser/ui/hung_plugin_tab_helper.h"
#include "chrome/browser/ui/intents/web_intent_picker_controller.h"
#include "chrome/browser/ui/metro_pin_tab_helper.h"
#include "chrome/browser/ui/pdf/pdf_tab_observer.h"
#include "chrome/browser/ui/prefs/prefs_tab_helper.h"
#include "chrome/browser/ui/sad_tab_helper.h"
#include "chrome/browser/ui/search/search.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/browser/ui/search_engines/search_engine_tab_helper.h"
#include "chrome/browser/ui/snapshot_tab_helper.h"
#include "chrome/browser/ui/sync/one_click_signin_helper.h"
#include "chrome/browser/ui/sync/tab_contents_synced_tab_delegate.h"
#include "chrome/browser/ui/tab_contents/core_tab_helper.h"
#include "chrome/browser/ui/zoom/zoom_controller.h"
#include "chrome/browser/view_type_utils.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/thumbnail_support.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"

using content::WebContents;

namespace {

static base::LazyInstance<base::PropertyAccessor<TabContents*> >
    g_tab_contents_property_accessor = LAZY_INSTANCE_INITIALIZER;

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// TabContents, public:

TabContents::TabContents(WebContents* contents)
    : content::WebContentsObserver(contents),
      in_destructor_(false),
      web_contents_(contents) {
  DCHECK(contents);
  DCHECK(!FromWebContents(contents));

  chrome::SetViewType(contents, chrome::VIEW_TYPE_TAB_CONTENTS);

  // Stash this in the property bag so it can be retrieved without having to
  // go to a Browser.
  property_accessor()->SetProperty(contents->GetPropertyBag(), this);

  // Create the tab helpers.
  // restore_tab_helper because it sets up the tab ID, and other helpers may
  // rely on that.
  restore_tab_helper_.reset(new RestoreTabHelper(contents));

  autocomplete_history_manager_.reset(new AutocompleteHistoryManager(contents));
  autofill_manager_ = new AutofillManager(this);
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kExternalAutofillPopup)) {
    autofill_external_delegate_.reset(
        AutofillExternalDelegate::Create(this, autofill_manager_.get()));
    autofill_manager_->SetExternalDelegate(autofill_external_delegate_.get());
    autocomplete_history_manager_->SetExternalDelegate(
        autofill_external_delegate_.get());
  }
#if defined(ENABLE_AUTOMATION)
  automation_tab_helper_.reset(new AutomationTabHelper(contents));
#endif
  blocked_content_tab_helper_.reset(new BlockedContentTabHelper(this));
  bookmark_tab_helper_.reset(new BookmarkTabHelper(this));
  cache_stats_tab_helper_.reset(
      new chrome_browser_net::CacheStatsTabHelper(this));
#if defined(ENABLE_CAPTIVE_PORTAL_DETECTION)
  captive_portal_tab_helper_.reset(
      new captive_portal::CaptivePortalTabHelper(profile(), web_contents()));
#endif
  constrained_window_tab_helper_.reset(new ConstrainedWindowTabHelper(this));
  core_tab_helper_.reset(new CoreTabHelper(contents));
  extension_tab_helper_.reset(new extensions::TabHelper(this));
  favicon_tab_helper_.reset(new FaviconTabHelper(contents));
  find_tab_helper_.reset(new FindTabHelper(contents));
  history_tab_helper_.reset(new HistoryTabHelper(contents));
  hung_plugin_tab_helper_.reset(new HungPluginTabHelper(contents));
  infobar_tab_helper_.reset(new InfoBarTabHelper(contents));
  metro_pin_tab_helper_.reset(new MetroPinTabHelper(contents));
  password_manager_delegate_.reset(new PasswordManagerDelegateImpl(this));
  password_manager_.reset(
      new PasswordManager(contents, password_manager_delegate_.get()));
  prefs_tab_helper_.reset(new PrefsTabHelper(contents));
  prerender_tab_helper_.reset(new prerender::PrerenderTabHelper(this));
  search_engine_tab_helper_.reset(new SearchEngineTabHelper(contents));
  bool is_search_enabled =
      chrome::search::IsInstantExtendedAPIEnabled(profile());
  search_tab_helper_.reset(
      new chrome::search::SearchTabHelper(this, is_search_enabled));
  snapshot_tab_helper_.reset(new SnapshotTabHelper(contents));
  ssl_helper_.reset(new TabContentsSSLHelper(this));
  synced_tab_delegate_.reset(new TabContentsSyncedTabDelegate(this));
  content_settings_.reset(new TabSpecificContentSettings(contents));
  translate_tab_helper_.reset(new TranslateTabHelper(contents));
  zoom_controller_.reset(new ZoomController(this));

#if !defined(OS_ANDROID)
  web_intent_picker_controller_.reset(new WebIntentPickerController(this));
  sad_tab_helper_.reset(new SadTabHelper(contents));
#endif

  // Create the per-tab observers.
  alternate_error_page_tab_observer_.reset(
      new AlternateErrorPageTabObserver(contents, profile()));
  webnavigation_observer_.reset(
      new extensions::WebNavigationTabObserver(contents));
  external_protocol_observer_.reset(new ExternalProtocolObserver(contents));
  navigation_metrics_recorder_.reset(new NavigationMetricsRecorder(contents));
  pdf_tab_observer_.reset(new PDFTabObserver(this));
  safe_browsing_tab_observer_.reset(
      new safe_browsing::SafeBrowsingTabObserver(this));

  plugin_observer_.reset(new PluginObserver(this));

#if !defined(OS_ANDROID)
  if (OmniboxSearchHint::IsEnabled(profile()))
    omnibox_search_hint_.reset(new OmniboxSearchHint(this));
#endif

#if defined(ENABLE_PRINTING)
  print_view_manager_.reset(new printing::PrintViewManager(this));
  print_preview_.reset(new printing::PrintPreviewMessageHandler(contents));
#endif

  // Start the in-browser thumbnailing if the feature is enabled.
  if (ShouldEnableInBrowserThumbnailing()) {
    thumbnail_generator_.reset(new ThumbnailGenerator);
    thumbnail_generator_->StartThumbnailing(web_contents_.get());
  }

  // If this is not an incognito window, setup to handle one-click login.
  // We don't want to check that the profile is already connected at this time
  // because the connected state may change while this tab is open.  Having a
  // one-click signin helper attached does not cause problems if the profile
  // happens to be already connected.
#if defined(ENABLE_ONE_CLICK_SIGNIN)
  if (OneClickSigninHelper::CanOffer(contents, "", false))
      one_click_signin_helper_.reset(new OneClickSigninHelper(contents));
#endif
}

TabContents::~TabContents() {
  in_destructor_ = true;

  // Need to reset |thumbnail_generator_| here before |web_contents_| is
  // deleted because destructing |web_contents_| can end up causing the
  // thumbnailer to generate a thumbnail. Since TabContents can be
  // destructed during shutdown, trying to generate a thumbnail by sending an
  // IPC message to the GPU process is not safe. Sending
  // chrome::NOTIFICATION_TAB_CONTENTS_DESTROYED can also cause the thumbnailer
  // to generate a thumbnail, so this must be placed before sending the
  // notification.
  thumbnail_generator_.reset();

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_TAB_CONTENTS_DESTROYED,
      content::Source<TabContents>(this),
      content::NotificationService::NoDetails());

  // Need to tear down infobars before the WebContents goes away.
  // TODO(avi): Can we get this handled by the tab helper itself?
  infobar_tab_helper_.reset();
}

base::PropertyAccessor<TabContents*>* TabContents::property_accessor() {
  return g_tab_contents_property_accessor.Pointer();
}

TabContents* TabContents::Clone() {
  WebContents* new_web_contents = web_contents()->Clone();
  TabContents* new_tab_contents = new TabContents(new_web_contents);

  // TODO(avi): Can we generalize this so that knowledge of the functionings of
  // the tab helpers isn't required here?
  new_tab_contents->extension_tab_helper()->CopyStateFrom(
      *extension_tab_helper_.get());
  return new_tab_contents;
}

// static
TabContents* TabContents::FromWebContents(WebContents* contents) {
  TabContents** tab_contents =
      property_accessor()->GetProperty(contents->GetPropertyBag());

  return tab_contents ? *tab_contents : NULL;
}

// static
const TabContents* TabContents::FromWebContents(const WebContents* contents) {
  TabContents* const* tab_contents =
      property_accessor()->GetProperty(contents->GetPropertyBag());

  return tab_contents ? *tab_contents : NULL;
}

WebContents* TabContents::web_contents() const {
  return web_contents_.get();
}

Profile* TabContents::profile() const {
  return Profile::FromBrowserContext(web_contents()->GetBrowserContext());
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsObserver overrides

void TabContents::WebContentsDestroyed(WebContents* tab) {
  // Destruction of the WebContents should only be done by us from our
  // destructor. Otherwise it's very likely we (or one of the helpers we own)
  // will attempt to access the WebContents and we'll crash.
  DCHECK(in_destructor_);
}
