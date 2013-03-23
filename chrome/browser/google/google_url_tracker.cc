// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/google/google_url_tracker.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/string_util.h"
#include "chrome/browser/google/google_url_tracker_factory.h"
#include "chrome/browser/google/google_url_tracker_infobar_delegate.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "net/base/load_flags.h"
#include "net/base/net_util.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_request_status.h"


namespace {

GoogleURLTrackerInfoBarDelegate* CreateInfoBar(
    InfoBarTabHelper* infobar_helper,
    GoogleURLTracker* google_url_tracker,
    const GURL& search_url) {
  GoogleURLTrackerInfoBarDelegate* infobar =
      new GoogleURLTrackerInfoBarDelegate(infobar_helper, google_url_tracker,
                                          search_url);
  // AddInfoBar() takes ownership; it will delete |infobar| if it fails.
  return infobar_helper->AddInfoBar(infobar) ? infobar : NULL;
}

}  // namespace


// GoogleURLTracker -----------------------------------------------------------

const char GoogleURLTracker::kDefaultGoogleHomepage[] =
    "http://www.google.com/";
const char GoogleURLTracker::kSearchDomainCheckURL[] =
    "https://www.google.com/searchdomaincheck?format=url&type=chrome";

GoogleURLTracker::GoogleURLTracker(Profile* profile, Mode mode)
    : profile_(profile),
      infobar_creator_(base::Bind(&CreateInfoBar)),
      google_url_(mode == UNIT_TEST_MODE ? kDefaultGoogleHomepage :
          profile->GetPrefs()->GetString(prefs::kLastKnownGoogleURL)),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)),
      fetcher_id_(0),
      in_startup_sleep_(true),
      already_fetched_(false),
      need_to_fetch_(false),
      need_to_prompt_(false),
      search_committed_(false) {
  net::NetworkChangeNotifier::AddIPAddressObserver(this);

  // Because this function can be called during startup, when kicking off a URL
  // fetch can eat up 20 ms of time, we delay five seconds, which is hopefully
  // long enough to be after startup, but still get results back quickly.
  // Ideally, instead of this timer, we'd do something like "check if the
  // browser is starting up, and if so, come back later", but there is currently
  // no function to do this.
  //
  // In UNIT_TEST mode, where we want to explicitly control when the tracker
  // "wakes up", we do nothing at all.
  if (mode == NORMAL_MODE) {
    static const int kStartFetchDelayMS = 5000;
    MessageLoop::current()->PostDelayedTask(FROM_HERE,
        base::Bind(&GoogleURLTracker::FinishSleep,
                   weak_ptr_factory_.GetWeakPtr()),
        base::TimeDelta::FromMilliseconds(kStartFetchDelayMS));
  }
}

GoogleURLTracker::~GoogleURLTracker() {
  // We should only reach here after any tabs and their infobars have been torn
  // down.
  DCHECK(entry_map_.empty());
}

// static
GURL GoogleURLTracker::GoogleURL(Profile* profile) {
  const GoogleURLTracker* tracker =
      GoogleURLTrackerFactory::GetForProfile(profile);
  return tracker ? tracker->google_url_ : GURL(kDefaultGoogleHomepage);
}

// static
void GoogleURLTracker::RequestServerCheck(Profile* profile) {
  GoogleURLTracker* tracker = GoogleURLTrackerFactory::GetForProfile(profile);
  if (tracker)
    tracker->SetNeedToFetch();
}

// static
void GoogleURLTracker::GoogleURLSearchCommitted(Profile* profile) {
  GoogleURLTracker* tracker = GoogleURLTrackerFactory::GetForProfile(profile);
  if (tracker)
    tracker->SearchCommitted();
}

void GoogleURLTracker::AcceptGoogleURL(bool redo_searches) {
  UpdatedDetails urls(google_url_, fetched_google_url_);
  google_url_ = fetched_google_url_;
  PrefService* prefs = profile_->GetPrefs();
  prefs->SetString(prefs::kLastKnownGoogleURL, google_url_.spec());
  prefs->SetString(prefs::kLastPromptedGoogleURL, google_url_.spec());
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_GOOGLE_URL_UPDATED,
      content::Source<Profile>(profile_),
      content::Details<UpdatedDetails>(&urls));
  need_to_prompt_ = false;
  CloseAllEntries(redo_searches);
}

void GoogleURLTracker::CancelGoogleURL() {
  profile_->GetPrefs()->SetString(prefs::kLastPromptedGoogleURL,
                                  fetched_google_url_.spec());
  need_to_prompt_ = false;
  CloseAllEntries(false);
}

void GoogleURLTracker::OnURLFetchComplete(const net::URLFetcher* source) {
  // Delete the fetcher on this function's exit.
  scoped_ptr<net::URLFetcher> clean_up_fetcher(fetcher_.release());

  // Don't update the URL if the request didn't succeed.
  if (!source->GetStatus().is_success() || (source->GetResponseCode() != 200)) {
    already_fetched_ = false;
    return;
  }

  // See if the response data was valid.  It should be
  // "<scheme>://[www.]google.<TLD>/".
  std::string url_str;
  source->GetResponseAsString(&url_str);
  TrimWhitespace(url_str, TRIM_ALL, &url_str);
  GURL url(url_str);
  if (!url.is_valid() || (url.path().length() > 1) || url.has_query() ||
      url.has_ref() ||
      !google_util::IsGoogleDomainUrl(url.spec(),
                                      google_util::DISALLOW_SUBDOMAIN,
                                      google_util::DISALLOW_NON_STANDARD_PORTS))
    return;

  std::swap(url, fetched_google_url_);
  GURL last_prompted_url(
      profile_->GetPrefs()->GetString(prefs::kLastPromptedGoogleURL));

  if (last_prompted_url.is_empty()) {
    // On the very first run of Chrome, when we've never looked up the URL at
    // all, we should just silently switch over to whatever we get immediately.
    AcceptGoogleURL(true);  // Arg is irrelevant.
    return;
  }

  string16 fetched_host(net::StripWWWFromHost(fetched_google_url_));
  if (fetched_google_url_ == google_url_) {
    // Either the user has continually been on this URL, or we prompted for a
    // different URL but have now changed back before they responded to any of
    // the prompts.  In this latter case we want to close any infobars and stop
    // prompting.
    CancelGoogleURL();
  } else if (fetched_host == net::StripWWWFromHost(google_url_)) {
    // Similar to the above case, but this time the new URL differs from the
    // existing one, probably due to switching between HTTP and HTTPS searching.
    // Like before we want to close any infobars and stop prompting; we also
    // want to silently accept the change in scheme.  We don't redo open
    // searches so as to avoid suddenly changing a page the user might be
    // interacting with; it's enough to simply get future searches right.
    AcceptGoogleURL(false);
  } else if (fetched_host == net::StripWWWFromHost(last_prompted_url)) {
    // We've re-fetched a TLD the user previously turned down.  Although the new
    // URL might have a different scheme than the old, we want to preserve the
    // user's decision.  Note that it's possible that, like in the above two
    // cases, we fetched yet another different URL in the meantime, which we
    // have infobars prompting about; in this case, as in those above, we want
    // to go ahead and close the infobars and stop prompting, since we've
    // switched back away from that URL.
    CancelGoogleURL();
  } else {
    // We've fetched a URL with a different TLD than the user is currently using
    // or was previously prompted about.  This means we need to prompt again.
    need_to_prompt_ = true;

    // As in all the above cases, there could be infobars prompting about some
    // URL.  If these URLs have the same TLD (e.g. for scheme changes), we can
    // simply leave the existing infobars open as their messages will still be
    // accurate.  Otherwise we go ahead and close them because we need to
    // display a new message.
    // Note: |url| is the previous |fetched_google_url_|.
    if (url.is_valid() && (fetched_host != net::StripWWWFromHost(url)))
      CloseAllEntries(false);
  }
}

void GoogleURLTracker::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_NAV_ENTRY_PENDING: {
      content::NavigationController* controller =
          content::Source<content::NavigationController>(source).ptr();
      content::WebContents* web_contents = controller->GetWebContents();
      InfoBarTabHelper* infobar_tab_helper =
          InfoBarTabHelper::FromWebContents(web_contents);
      // Because we're listening to all sources, there may be no
      // InfoBarTabHelper for some notifications, e.g. navigations in
      // bubbles/balloons etc.
      if (infobar_tab_helper) {
        OnNavigationPending(
            source, content::Source<content::WebContents>(web_contents),
            infobar_tab_helper, controller->GetPendingEntry()->GetUniqueID());
      }
      break;
    }

    case content::NOTIFICATION_NAV_ENTRY_COMMITTED: {
      content::NavigationController* controller =
          content::Source<content::NavigationController>(source).ptr();
      // Here we're only listening to notifications where we already know
      // there's an associated InfoBarTabHelper.
      content::WebContents* web_contents = controller->GetWebContents();
      InfoBarTabHelper* infobar_tab_helper =
          InfoBarTabHelper::FromWebContents(web_contents);
      DCHECK(infobar_tab_helper);
      const GURL& search_url = controller->GetActiveEntry()->GetURL();
      if (!search_url.is_valid())  // Not clear if this can happen.
        OnTabClosed(content::Source<content::WebContents>(web_contents));
      OnNavigationCommitted(infobar_tab_helper, search_url);
      break;
    }

    case content::NOTIFICATION_WEB_CONTENTS_DESTROYED:
      OnTabClosed(source);
      break;

    case chrome::NOTIFICATION_INSTANT_COMMITTED: {
      content::WebContents* web_contents =
          content::Source<content::WebContents>(source).ptr();
      const GURL& search_url = web_contents->GetURL();
      if (!search_url.is_valid())  // Not clear if this can happen.
        OnTabClosed(source);
      OnInstantCommitted(
          content::Source<content::NavigationController>(
              &web_contents->GetController()),
          source, InfoBarTabHelper::FromWebContents(web_contents), search_url);
      break;
    }

    default:
      NOTREACHED() << "Unknown notification received:" << type;
  }
}

void GoogleURLTracker::OnIPAddressChanged() {
  already_fetched_ = false;
  StartFetchIfDesirable();
}

void GoogleURLTracker::Shutdown() {
  registrar_.RemoveAll();
  weak_ptr_factory_.InvalidateWeakPtrs();
  fetcher_.reset();
  net::NetworkChangeNotifier::RemoveIPAddressObserver(this);
}

void GoogleURLTracker::DeleteMapEntryForHelper(
    const InfoBarTabHelper* infobar_helper) {
  // WARNING: |infobar_helper| may point to a deleted object.  Do not
  // dereference it!  See OnTabClosed().
  EntryMap::iterator i(entry_map_.find(infobar_helper));
  DCHECK(i != entry_map_.end());
  GoogleURLTrackerMapEntry* map_entry = i->second;

  UnregisterForEntrySpecificNotifications(*map_entry, false);
  entry_map_.erase(i);
  delete map_entry;
}

void GoogleURLTracker::SetNeedToFetch() {
  need_to_fetch_ = true;
  StartFetchIfDesirable();
}

void GoogleURLTracker::FinishSleep() {
  in_startup_sleep_ = false;
  StartFetchIfDesirable();
}

void GoogleURLTracker::StartFetchIfDesirable() {
  // Bail if a fetch isn't appropriate right now.  This function will be called
  // again each time one of the preconditions changes, so we'll fetch
  // immediately once all of them are met.
  //
  // See comments in header on the class, on RequestServerCheck(), and on the
  // various members here for more detail on exactly what the conditions are.
  if (in_startup_sleep_ || already_fetched_ || !need_to_fetch_)
    return;

  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableBackgroundNetworking))
    return;

  std::string fetch_url = CommandLine::ForCurrentProcess()->
      GetSwitchValueASCII(switches::kGoogleSearchDomainCheckURL);
  if (fetch_url.empty())
    fetch_url = kSearchDomainCheckURL;

  already_fetched_ = true;
  fetcher_.reset(net::URLFetcher::Create(fetcher_id_, GURL(fetch_url),
                                         net::URLFetcher::GET, this));
  ++fetcher_id_;
  // We don't want this fetch to set new entries in the cache or cookies, lest
  // we alarm the user.
  fetcher_->SetLoadFlags(net::LOAD_DISABLE_CACHE |
                         net::LOAD_DO_NOT_SAVE_COOKIES);
  fetcher_->SetRequestContext(profile_->GetRequestContext());

  // Configure to max_retries at most kMaxRetries times for 5xx errors.
  static const int kMaxRetries = 5;
  fetcher_->SetMaxRetriesOn5xx(kMaxRetries);

  fetcher_->Start();
}

void GoogleURLTracker::SearchCommitted() {
  if (need_to_prompt_) {
    search_committed_ = true;
    // These notifications will fire a bit later in the same call chain we're
    // currently in.
    if (!registrar_.IsRegistered(this, content::NOTIFICATION_NAV_ENTRY_PENDING,
        content::NotificationService::AllBrowserContextsAndSources())) {
      registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_PENDING,
          content::NotificationService::AllBrowserContextsAndSources());
      registrar_.Add(this, chrome::NOTIFICATION_INSTANT_COMMITTED,
          content::NotificationService::AllBrowserContextsAndSources());
    }
  }
}

void GoogleURLTracker::OnNavigationPending(
    const content::NotificationSource& navigation_controller_source,
    const content::NotificationSource& web_contents_source,
    InfoBarTabHelper* infobar_helper,
    int pending_id) {
  EntryMap::iterator i(entry_map_.find(infobar_helper));

  if (search_committed_) {
    search_committed_ = false;
    // Whether there's an existing infobar or not, we need to listen for the
    // load to commit, so we can show and/or update the infobar when it does.
    // (We may already be registered for this if there is an existing infobar
    // that had a previous pending search that hasn't yet committed.)
    if (!registrar_.IsRegistered(this,
                                 content::NOTIFICATION_NAV_ENTRY_COMMITTED,
                                 navigation_controller_source)) {
      registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
                      navigation_controller_source);
    }
    if (i == entry_map_.end()) {
      // This is a search on a tab that doesn't have one of our infobars, so
      // prepare to add one.  Note that we only listen for the tab's destruction
      // on this path; if there was already a map entry, then either it doesn't
      // yet have an infobar and we're already registered for this, or it has an
      // infobar and the infobar's owner will handle tearing it down when the
      // tab is destroyed.
      registrar_.Add(this, content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                     web_contents_source);
      entry_map_.insert(std::make_pair(
          infobar_helper,
          new GoogleURLTrackerMapEntry(this, infobar_helper,
                                       navigation_controller_source,
                                       web_contents_source)));
    } else if (i->second->has_infobar()) {
      // This is a new search on a tab where we already have an infobar.
      i->second->infobar()->set_pending_id(pending_id);
    }
  } else if (i != entry_map_.end()){
    if (i->second->has_infobar()) {
      // This is a non-search navigation on a tab with an infobar.  If there was
      // a previous pending search on this tab, this means it won't commit, so
      // undo anything we did in response to seeing that.  Note that if there
      // was no pending search on this tab, these statements are effectively a
      // no-op.
      //
      // If this navigation actually commits, that will trigger the infobar's
      // owner to expire the infobar if need be.  If it doesn't commit, then
      // simply leaving the infobar as-is will have been the right thing.
      UnregisterForEntrySpecificNotifications(*i->second, false);
      i->second->infobar()->set_pending_id(0);
    } else {
      // Non-search navigation on a tab with an entry that has not yet created
      // an infobar.  This means the original search won't commit, so delete the
      // entry.
      i->second->Close(false);
    }
  } else {
    // Non-search navigation on a tab without an infobars.  This is irrelevant
    // to us.
  }
}

void GoogleURLTracker::OnNavigationCommitted(InfoBarTabHelper* infobar_helper,
                                             const GURL& search_url) {
  EntryMap::iterator i(entry_map_.find(infobar_helper));
  DCHECK(i != entry_map_.end());
  GoogleURLTrackerMapEntry* map_entry = i->second;
  DCHECK(search_url.is_valid());

  UnregisterForEntrySpecificNotifications(*map_entry, true);
  if (map_entry->has_infobar()) {
    map_entry->infobar()->Update(search_url);
  } else {
    GoogleURLTrackerInfoBarDelegate* infobar_delegate =
        infobar_creator_.Run(infobar_helper, this, search_url);
    if (infobar_delegate)
      map_entry->SetInfoBar(infobar_delegate);
    else
      map_entry->Close(false);
  }
}

void GoogleURLTracker::OnTabClosed(
    const content::NotificationSource& web_contents_source) {
  // Because InfoBarTabHelper tears itself down in response to
  // NOTIFICATION_WEB_CONTENTS_DESTROYED, it may or may not be possible to
  // get a non-NULL pointer back from InfoBarTabHelper::FromWebContents() here,
  // depending on which order notifications fired in.  Likewise, the pointer in
  // |entry_map_| (and in its associated MapEntry) may point to deleted memory.
  // Therefore, if we were to access to the InfoBarTabHelper* we have for this
  // tab, we'd need to ensure we just looked at the raw pointer value, and never
  // dereferenced it.  This function doesn't need to do even that, but others in
  // the call chain from here might (and have comments pointing back here).
  for (EntryMap::iterator i(entry_map_.begin()); i != entry_map_.end(); ++i) {
    if (i->second->web_contents_source() == web_contents_source) {
      i->second->Close(false);
      return;
    }
  }
  NOTREACHED();
}

void GoogleURLTracker::OnInstantCommitted(
    const content::NotificationSource& navigation_controller_source,
    const content::NotificationSource& web_contents_source,
    InfoBarTabHelper* infobar_helper,
    const GURL& search_url) {
  // If this was the search we were listening for, OnNavigationPending() should
  // ensure we're registered for NAV_ENTRY_COMMITTED, and we should call
  // OnNavigationCommitted() to simulate that firing.  Otherwise, this is some
  // sort of non-search navigation, so while we should still call
  // OnNavigationPending(), that function should then ensure that we're not
  // listening for NAV_ENTRY_COMMITTED on this tab, and we should not call
  // OnNavigationCommitted() afterwards.  Note that we need to save off
  // |search_committed_| here because OnNavigationPending() will reset it.
  bool was_search_committed = search_committed_;
  OnNavigationPending(navigation_controller_source, web_contents_source,
                      infobar_helper, 0);
  EntryMap::iterator i(entry_map_.find(infobar_helper));
  DCHECK_EQ(was_search_committed, (i != entry_map_.end()) &&
      registrar_.IsRegistered(this,
          content::NOTIFICATION_NAV_ENTRY_COMMITTED,
          i->second->navigation_controller_source()));
  if (was_search_committed)
    OnNavigationCommitted(infobar_helper, search_url);
}

void GoogleURLTracker::CloseAllEntries(bool redo_searches) {
  // Delete all entries, whether they have infobars or not.
  while (!entry_map_.empty())
    entry_map_.begin()->second->Close(redo_searches);
}

void GoogleURLTracker::UnregisterForEntrySpecificNotifications(
    const GoogleURLTrackerMapEntry& map_entry,
    bool must_be_listening_for_commit) {
  // For tabs with map entries but no infobars, we should always be listening
  // for both these notifications.  For tabs with infobars, we may be listening
  // for NOTIFICATION_NAV_ENTRY_COMMITTED if the user has performed a new search
  // on this tab.
  if (registrar_.IsRegistered(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
                              map_entry.navigation_controller_source())) {
    registrar_.Remove(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
                      map_entry.navigation_controller_source());
  } else {
    DCHECK(!must_be_listening_for_commit);
    DCHECK(map_entry.has_infobar());
  }
  const bool registered_for_web_contents_destroyed = registrar_.IsRegistered(
      this, content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
      map_entry.web_contents_source());
  DCHECK_NE(registered_for_web_contents_destroyed, map_entry.has_infobar());
  if (registered_for_web_contents_destroyed) {
    registrar_.Remove(this, content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                      map_entry.web_contents_source());
  }

  // Our global listeners for these other notifications should be in place iff
  // we have any tabs still listening for commits.  These tabs either have no
  // infobars or have received new pending searches atop existing infobars; in
  // either case we want to catch subsequent pending non-search navigations.
  // See the various cases inside OnNavigationPending().
  for (EntryMap::const_iterator i(entry_map_.begin()); i != entry_map_.end();
       ++i) {
    if (registrar_.IsRegistered(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
                                i->second->navigation_controller_source())) {
      DCHECK(registrar_.IsRegistered(this,
          content::NOTIFICATION_NAV_ENTRY_PENDING,
          content::NotificationService::AllBrowserContextsAndSources()));
      return;
    }
  }
  if (registrar_.IsRegistered(this, content::NOTIFICATION_NAV_ENTRY_PENDING,
      content::NotificationService::AllBrowserContextsAndSources())) {
    DCHECK(!search_committed_);
    registrar_.Remove(this, content::NOTIFICATION_NAV_ENTRY_PENDING,
        content::NotificationService::AllBrowserContextsAndSources());
    registrar_.Remove(this, chrome::NOTIFICATION_INSTANT_COMMITTED,
        content::NotificationService::AllBrowserContextsAndSources());
  }
}
