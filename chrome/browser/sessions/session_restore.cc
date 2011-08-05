// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sessions/session_restore.h"

#include <algorithm>
#include <list>
#include <set>
#include <vector>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/histogram.h"
#include "base/stl_util-inl.h"
#include "base/stringprintf.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/session_types.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_window.h"
#include "content/browser/renderer_host/render_widget_host.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/browser/tab_contents/navigation_controller.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/tab_contents_view.h"
#include "content/common/notification_registrar.h"
#include "content/common/notification_service.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/boot_times_loader.h"
#include "chrome/browser/chromeos/network_state_notifier.h"
#endif

// Are we in the process of restoring?
static bool restoring = false;

namespace {

// TabLoader ------------------------------------------------------------------

// Initial delay (see class decription for details).
static const int kInitialDelayTimerMS = 100;

// TabLoader is responsible for loading tabs after session restore creates
// tabs. New tabs are loaded after the current tab finishes loading, or a delay
// is reached (initially kInitialDelayTimerMS). If the delay is reached before
// a tab finishes loading a new tab is loaded and the time of the delay
// doubled. When all tabs are loading TabLoader deletes itself.
//
// This is not part of SessionRestoreImpl so that synchronous destruction
// of SessionRestoreImpl doesn't have timing problems.
class TabLoader : public NotificationObserver {
 public:
  explicit TabLoader(base::TimeTicks restore_started);
  ~TabLoader();

  // Schedules a tab for loading.
  void ScheduleLoad(NavigationController* controller);

  // Notifies the loader that a tab has been scheduled for loading through
  // some other mechanism.
  void TabIsLoading(NavigationController* controller);

  // Invokes |LoadNextTab| to load a tab.
  //
  // This must be invoked once to start loading.
  void StartLoading();

 private:
  typedef std::set<NavigationController*> TabsLoading;
  typedef std::list<NavigationController*> TabsToLoad;
  typedef std::set<RenderWidgetHost*> RenderWidgetHostSet;

  // Loads the next tab. If there are no more tabs to load this deletes itself,
  // otherwise |force_load_timer_| is restarted.
  void LoadNextTab();

  // NotificationObserver method. Removes the specified tab and loads the next
  // tab.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // Removes the listeners from the specified tab and removes the tab from
  // the set of tabs to load and list of tabs we're waiting to get a load
  // from.
  void RemoveTab(NavigationController* tab);

  // Invoked from |force_load_timer_|. Doubles |force_load_delay_| and invokes
  // |LoadNextTab| to load the next tab
  void ForceLoadTimerFired();

  // Returns the RenderWidgetHost associated with a tab if there is one,
  // NULL otherwise.
  static RenderWidgetHost* GetRenderWidgetHost(NavigationController* tab);

  // Register for necessary notificaitons on a tab navigation controller.
  void RegisterForNotifications(NavigationController* controller);

  // Called when a tab goes away or a load completes.
  void HandleTabClosedOrLoaded(NavigationController* controller);

  NotificationRegistrar registrar_;

  // Current delay before a new tab is loaded. See class description for
  // details.
  int64 force_load_delay_;

  // Has Load been invoked?
  bool loading_;

  // Have we recorded the times for a tab paint?
  bool got_first_paint_;

  // The set of tabs we've initiated loading on. This does NOT include the
  // selected tabs.
  TabsLoading tabs_loading_;

  // The tabs we need to load.
  TabsToLoad tabs_to_load_;

  // The renderers we have started loading into.
  RenderWidgetHostSet render_widget_hosts_loading_;

  // The renderers we have loaded and are waiting on to paint.
  RenderWidgetHostSet render_widget_hosts_to_paint_;

  // The number of tabs that have been restored.
  int tab_count_;

  base::OneShotTimer<TabLoader> force_load_timer_;

  // The time the restore process started.
  base::TimeTicks restore_started_;

  DISALLOW_COPY_AND_ASSIGN(TabLoader);
};

TabLoader::TabLoader(base::TimeTicks restore_started)
    : force_load_delay_(kInitialDelayTimerMS),
      loading_(false),
      got_first_paint_(false),
      tab_count_(0),
      restore_started_(restore_started) {
}

TabLoader::~TabLoader() {
  DCHECK((got_first_paint_ || render_widget_hosts_to_paint_.empty()) &&
          tabs_loading_.empty() && tabs_to_load_.empty());
}

void TabLoader::ScheduleLoad(NavigationController* controller) {
  DCHECK(controller);
  DCHECK(find(tabs_to_load_.begin(), tabs_to_load_.end(), controller) ==
         tabs_to_load_.end());
  tabs_to_load_.push_back(controller);
  RegisterForNotifications(controller);
}

void TabLoader::TabIsLoading(NavigationController* controller) {
  DCHECK(controller);
  DCHECK(find(tabs_loading_.begin(), tabs_loading_.end(), controller) ==
         tabs_loading_.end());
  tabs_loading_.insert(controller);
  RenderWidgetHost* render_widget_host = GetRenderWidgetHost(controller);
  DCHECK(render_widget_host);
  render_widget_hosts_loading_.insert(render_widget_host);
  RegisterForNotifications(controller);
}

void TabLoader::StartLoading() {
  registrar_.Add(this, NotificationType::RENDER_WIDGET_HOST_DID_PAINT,
                 NotificationService::AllSources());
#if defined(OS_CHROMEOS)
  if (chromeos::NetworkStateNotifier::is_connected()) {
    loading_ = true;
    LoadNextTab();
  } else {
    // Start listening to network state notification now.
    registrar_.Add(this, NotificationType::NETWORK_STATE_CHANGED,
                   NotificationService::AllSources());
  }
#else
  loading_ = true;
  LoadNextTab();
#endif
}

void TabLoader::LoadNextTab() {
  if (!tabs_to_load_.empty()) {
    NavigationController* tab = tabs_to_load_.front();
    DCHECK(tab);
    tabs_loading_.insert(tab);
    tabs_to_load_.pop_front();
    tab->LoadIfNecessary();
    if (tab->tab_contents()) {
      int tab_index;
      Browser* browser = Browser::GetBrowserForController(tab, &tab_index);
      if (browser && browser->active_index() != tab_index) {
        // By default tabs are marked as visible. As only the active tab is
        // visible we need to explicitly tell non-active tabs they are hidden.
        // Without this call non-active tabs are not marked as backgrounded.
        //
        // NOTE: We need to do this here rather than when the tab is added to
        // the Browser as at that time not everything has been created, so that
        // the call would do nothing.
        tab->tab_contents()->WasHidden();
      }
    }
  }

  if (!tabs_to_load_.empty()) {
    force_load_timer_.Stop();
    // Each time we load a tab we also set a timer to force us to start loading
    // the next tab if this one doesn't load quickly enough.
    force_load_timer_.Start(
        base::TimeDelta::FromMilliseconds(force_load_delay_),
        this, &TabLoader::ForceLoadTimerFired);
  }
}

void TabLoader::Observe(NotificationType type,
                        const NotificationSource& source,
                        const NotificationDetails& details) {
  switch (type.value) {
#if defined(OS_CHROMEOS)
    case NotificationType::NETWORK_STATE_CHANGED: {
      chromeos::NetworkStateDetails* state_details =
          Details<chromeos::NetworkStateDetails>(details).ptr();
      switch (state_details->state()) {
        case chromeos::NetworkStateDetails::CONNECTED:
          if (!loading_) {
            loading_ = true;
            LoadNextTab();
          }
          // Start loading
          break;
        case chromeos::NetworkStateDetails::CONNECTING:
        case chromeos::NetworkStateDetails::DISCONNECTED:
          // Disconnected while loading. Set loading_ false so
          // that it stops trying to load next tab.
          loading_ = false;
          break;
        default:
          NOTREACHED() << "Unknown nework state notification:"
                       << state_details->state();
      }
      break;
    }
#endif
    case NotificationType::LOAD_START: {
      // Add this render_widget_host to the set of those we're waiting for
      // paints on. We want to only record stats for paints that occur after
      // a load has finished.
      NavigationController* tab = Source<NavigationController>(source).ptr();
      RenderWidgetHost* render_widget_host = GetRenderWidgetHost(tab);
      DCHECK(render_widget_host);
      render_widget_hosts_loading_.insert(render_widget_host);
      break;
    }
    case NotificationType::TAB_CONTENTS_DESTROYED: {
      TabContents* tab_contents = Source<TabContents>(source).ptr();
      if (!got_first_paint_) {
        RenderWidgetHost* render_widget_host =
            GetRenderWidgetHost(&tab_contents->controller());
        render_widget_hosts_loading_.erase(render_widget_host);
      }
      HandleTabClosedOrLoaded(&tab_contents->controller());
      break;
    }
    case NotificationType::LOAD_STOP: {
      NavigationController* tab = Source<NavigationController>(source).ptr();
      render_widget_hosts_to_paint_.insert(GetRenderWidgetHost(tab));
      HandleTabClosedOrLoaded(tab);
      break;
    }
    case NotificationType::RENDER_WIDGET_HOST_DID_PAINT: {
      if (!got_first_paint_) {
        RenderWidgetHost* render_widget_host =
            Source<RenderWidgetHost>(source).ptr();
        if (render_widget_hosts_to_paint_.find(render_widget_host) !=
            render_widget_hosts_to_paint_.end()) {
          // Got a paint for one of our renderers, so record time.
          got_first_paint_ = true;
          base::TimeDelta time_to_paint =
              base::TimeTicks::Now() - restore_started_;
          UMA_HISTOGRAM_CUSTOM_TIMES(
              "SessionRestore.FirstTabPainted",
              time_to_paint,
              base::TimeDelta::FromMilliseconds(10),
              base::TimeDelta::FromSeconds(100),
              100);
          // Record a time for the number of tabs, to help track down
          // contention.
          std::string time_for_count =
              base::StringPrintf("SessionRestore.FirstTabPainted_%d",
                                 tab_count_);
          base::Histogram* counter_for_count =
              base::Histogram::FactoryTimeGet(
                  time_for_count,
                  base::TimeDelta::FromMilliseconds(10),
                  base::TimeDelta::FromSeconds(100),
                  100,
                  base::Histogram::kUmaTargetedHistogramFlag);
          counter_for_count->AddTime(time_to_paint);
        } else if (render_widget_hosts_loading_.find(render_widget_host) ==
            render_widget_hosts_loading_.end()) {
          // If this is a host for a tab we're not loading some other tab
          // has rendered and there's no point tracking the time. This could
          // happen because the user opened a different tab or restored tabs
          // to an already existing browser and an existing tab painted.
          got_first_paint_ = true;
        }
      }
      break;
    }
    default:
      NOTREACHED() << "Unknown notification received:" << type.value;
  }
  // Delete ourselves when we're not waiting for any more notifications.
  if ((got_first_paint_ || render_widget_hosts_to_paint_.empty()) &&
      tabs_loading_.empty() && tabs_to_load_.empty())
    delete this;
}

void TabLoader::RemoveTab(NavigationController* tab) {
  registrar_.Remove(this, NotificationType::TAB_CONTENTS_DESTROYED,
                    Source<TabContents>(tab->tab_contents()));
  registrar_.Remove(this, NotificationType::LOAD_STOP,
                    Source<NavigationController>(tab));
  registrar_.Remove(this, NotificationType::LOAD_START,
                    Source<NavigationController>(tab));

  TabsLoading::iterator i = tabs_loading_.find(tab);
  if (i != tabs_loading_.end())
    tabs_loading_.erase(i);

  TabsToLoad::iterator j =
      find(tabs_to_load_.begin(), tabs_to_load_.end(), tab);
  if (j != tabs_to_load_.end())
    tabs_to_load_.erase(j);
}

void TabLoader::ForceLoadTimerFired() {
  force_load_delay_ *= 2;
  LoadNextTab();
}

RenderWidgetHost* TabLoader::GetRenderWidgetHost(NavigationController* tab) {
  TabContents* tab_contents = tab->tab_contents();
  if (tab_contents) {
    RenderWidgetHostView* render_widget_host_view =
        tab_contents->GetRenderWidgetHostView();
    if (render_widget_host_view)
      return render_widget_host_view->GetRenderWidgetHost();
  }
  return NULL;
}

void TabLoader::RegisterForNotifications(NavigationController* controller) {
  registrar_.Add(this, NotificationType::TAB_CONTENTS_DESTROYED,
                 Source<TabContents>(controller->tab_contents()));
  registrar_.Add(this, NotificationType::LOAD_STOP,
                 Source<NavigationController>(controller));
  registrar_.Add(this, NotificationType::LOAD_START,
                 Source<NavigationController>(controller));
  ++tab_count_;
}

void TabLoader::HandleTabClosedOrLoaded(NavigationController* tab) {
  RemoveTab(tab);
  if (loading_)
    LoadNextTab();
  if (tabs_loading_.empty() && tabs_to_load_.empty()) {
    base::TimeDelta time_to_load =
        base::TimeTicks::Now() - restore_started_;
    UMA_HISTOGRAM_CUSTOM_TIMES(
        "SessionRestore.AllTabsLoaded",
        time_to_load,
        base::TimeDelta::FromMilliseconds(10),
        base::TimeDelta::FromSeconds(100),
        100);
    // Record a time for the number of tabs, to help track down contention.
    std::string time_for_count =
        base::StringPrintf("SessionRestore.AllTabsLoaded_%d", tab_count_);
    base::Histogram* counter_for_count =
        base::Histogram::FactoryTimeGet(
            time_for_count,
            base::TimeDelta::FromMilliseconds(10),
            base::TimeDelta::FromSeconds(100),
            100,
            base::Histogram::kUmaTargetedHistogramFlag);
    counter_for_count->AddTime(time_to_load);
  }
}

// SessionRestoreImpl ---------------------------------------------------------

// SessionRestoreImpl is responsible for fetching the set of tabs to create
// from SessionService. SessionRestoreImpl deletes itself when done.

class SessionRestoreImpl : public NotificationObserver {
 public:
  SessionRestoreImpl(Profile* profile,
                     Browser* browser,
                     bool synchronous,
                     bool clobber_existing_window,
                     bool always_create_tabbed_browser,
                     const std::vector<GURL>& urls_to_open)
      : profile_(profile),
        browser_(browser),
        synchronous_(synchronous),
        clobber_existing_window_(clobber_existing_window),
        always_create_tabbed_browser_(always_create_tabbed_browser),
        urls_to_open_(urls_to_open),
        restore_started_(base::TimeTicks::Now()) {
  }

  Browser* Restore() {
    SessionService* session_service =
        SessionServiceFactory::GetForProfile(profile_);
    DCHECK(session_service);
    SessionService::SessionCallback* callback =
        NewCallback(this, &SessionRestoreImpl::OnGotSession);
    session_service->GetLastSession(&request_consumer_, callback);

    if (synchronous_) {
      bool old_state = MessageLoop::current()->NestableTasksAllowed();
      MessageLoop::current()->SetNestableTasksAllowed(true);
      MessageLoop::current()->Run();
      MessageLoop::current()->SetNestableTasksAllowed(old_state);
      Browser* browser = ProcessSessionWindows(&windows_);
      delete this;
      return browser;
    }

    if (browser_) {
      registrar_.Add(this, NotificationType::BROWSER_CLOSED,
                     Source<Browser>(browser_));
    }

    return browser_;
  }

  // Restore window(s) from a foreign session.
  void RestoreForeignSession(
      std::vector<SessionWindow*>::const_iterator begin,
      std::vector<SessionWindow*>::const_iterator end) {
    StartTabCreation();
    // Create a browser instance to put the restored tabs in.
    for (std::vector<SessionWindow*>::const_iterator i = begin;
        i != end; ++i) {
      Browser* browser = CreateRestoredBrowser(
          static_cast<Browser::Type>((*i)->type),
          (*i)->bounds,
          (*i)->is_maximized);

      // Restore and show the browser.
      const int initial_tab_count = browser->tab_count();
      int selected_tab_index = (*i)->selected_tab_index;
      RestoreTabsToBrowser(*(*i), browser, selected_tab_index);
      ShowBrowser(browser, initial_tab_count, selected_tab_index);
      tab_loader_->TabIsLoading(
          &browser->GetSelectedTabContents()->controller());
      NotifySessionServiceOfRestoredTabs(browser, initial_tab_count);
    }

    // Always create in a new window
    FinishedTabCreation(true, true);
  }

  // Restore a single tab from a foreign session.
  // Note: we currently restore the tab to the last active browser.
  void RestoreForeignTab(const SessionTab& tab) {
    StartTabCreation();
    Browser* current_browser =
        browser_ ? browser_ : BrowserList::GetLastActive();
    RestoreTab(tab, current_browser->tab_count(), current_browser, true);
    NotifySessionServiceOfRestoredTabs(current_browser,
                                       current_browser->tab_count());
    FinishedTabCreation(true, true);
  }

  ~SessionRestoreImpl() {
    STLDeleteElements(&windows_);
    restoring = false;
  }

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) {
    switch (type.value) {
      case NotificationType::BROWSER_CLOSED:
        delete this;
        return;

      default:
        NOTREACHED();
        break;
    }
  }

 private:
  // Invoked when beginning to create new tabs. Resets the tab_loader_.
  void StartTabCreation() {
    tab_loader_.reset(new TabLoader(restore_started_));
  }

  // Invoked when done with creating all the tabs/browsers.
  //
  // |created_tabbed_browser| indicates whether a tabbed browser was created,
  // or we used an existing tabbed browser.
  //
  // If successful, this begins loading tabs and deletes itself when all tabs
  // have been loaded.
  //
  // Returns the Browser that was created, if any.
  Browser* FinishedTabCreation(bool succeeded, bool created_tabbed_browser) {
    Browser* browser = NULL;
    if (!created_tabbed_browser && always_create_tabbed_browser_) {
      browser = Browser::Create(profile_);
      if (urls_to_open_.empty()) {
        // No tab browsers were created and no URLs were supplied on the command
        // line. Add an empty URL, which is treated as opening the users home
        // page.
        urls_to_open_.push_back(GURL());
      }
      AppendURLsToBrowser(browser, urls_to_open_);
      browser->window()->Show();
    }

    if (succeeded) {
      DCHECK(tab_loader_.get());
      // TabLoader delets itself when done loading.
      tab_loader_.release()->StartLoading();
    }

    if (!synchronous_) {
      // If we're not synchronous we need to delete ourself.
      // NOTE: we must use DeleteLater here as most likely we're in a callback
      // from the history service which doesn't deal well with deleting the
      // object it is notifying.
      MessageLoop::current()->DeleteSoon(FROM_HERE, this);
    }

    return browser;
  }

  void OnGotSession(SessionService::Handle handle,
                    std::vector<SessionWindow*>* windows) {
    if (synchronous_) {
      // See comment above windows_ as to why we don't process immediately.
      windows_.swap(*windows);
      MessageLoop::current()->Quit();
      return;
    }

    ProcessSessionWindows(windows);
  }

  Browser* ProcessSessionWindows(std::vector<SessionWindow*>* windows) {
    if (windows->empty()) {
      // Restore was unsuccessful.
      return FinishedTabCreation(false, false);
    }

    StartTabCreation();

    Browser* current_browser =
        browser_ ? browser_ : BrowserList::GetLastActiveWithProfile(profile_);
    // After the for loop this contains the last TABBED_BROWSER. Is null if no
    // tabbed browsers exist.
    Browser* last_browser = NULL;
    bool has_tabbed_browser = false;
    for (std::vector<SessionWindow*>::iterator i = windows->begin();
         i != windows->end(); ++i) {
      Browser* browser = NULL;
      if (!has_tabbed_browser && (*i)->type == Browser::TYPE_TABBED)
        has_tabbed_browser = true;
      if (i == windows->begin() && (*i)->type == Browser::TYPE_TABBED &&
          !clobber_existing_window_) {
        // If there is an open tabbed browser window, use it. Otherwise fall
        // through and create a new one.
        browser = current_browser;
        if (browser && (!browser->is_type_tabbed() ||
                        browser->profile()->IsOffTheRecord())) {
          browser = NULL;
        }
      }
      if (!browser) {
        browser = CreateRestoredBrowser(
            static_cast<Browser::Type>((*i)->type),
            (*i)->bounds,
            (*i)->is_maximized);
      }
      if ((*i)->type == Browser::TYPE_TABBED)
        last_browser = browser;
      const int initial_tab_count = browser->tab_count();
      int selected_tab_index = (*i)->selected_tab_index;
      RestoreTabsToBrowser(*(*i), browser, selected_tab_index);
      ShowBrowser(browser, initial_tab_count, selected_tab_index);
      tab_loader_->TabIsLoading(
          &browser->GetSelectedTabContents()->controller());
      NotifySessionServiceOfRestoredTabs(browser, initial_tab_count);
    }

    // If we're restoring a session as the result of a crash and the session
    // included at least one tabbed browser, then close the browser window
    // that was opened when the user clicked to restore the session.
    if (clobber_existing_window_ && current_browser && has_tabbed_browser &&
        current_browser->is_type_tabbed()) {
      current_browser->CloseAllTabs();
    }
    if (last_browser && !urls_to_open_.empty())
      AppendURLsToBrowser(last_browser, urls_to_open_);
    // If last_browser is NULL and urls_to_open_ is non-empty,
    // FinishedTabCreation will create a new TabbedBrowser and add the urls to
    // it.
    Browser* finished_browser = FinishedTabCreation(true, has_tabbed_browser);
    if (finished_browser)
      last_browser = finished_browser;
    return last_browser;
  }

  void RestoreTabsToBrowser(const SessionWindow& window,
                            Browser* browser,
                            int selected_tab_index) {
    DCHECK(!window.tabs.empty());
    for (std::vector<SessionTab*>::const_iterator i = window.tabs.begin();
         i != window.tabs.end(); ++i) {
      const SessionTab& tab = *(*i);
      const int tab_index = static_cast<int>(i - window.tabs.begin());
      // Don't schedule a load for the selected tab, as ShowBrowser() will
      // already have done that.
      RestoreTab(tab, tab_index, browser, tab_index != selected_tab_index);
    }
  }

  void RestoreTab(const SessionTab& tab,
                  const int tab_index,
                  Browser* browser,
                  bool schedule_load) {
    DCHECK(!tab.navigations.empty());
    int selected_index = tab.current_navigation_index;
    selected_index = std::max(
        0,
        std::min(selected_index,
                 static_cast<int>(tab.navigations.size() - 1)));

    // Record an app launch, if applicable.
    GURL url = tab.navigations.at(tab.current_navigation_index).virtual_url();
    if (
#if defined(OS_CHROMEOS)
        browser->profile()->GetExtensionService() &&
#endif
        browser->profile()->GetExtensionService()->IsInstalledApp(url)) {
      UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppLaunchHistogram,
                                extension_misc::APP_LAUNCH_SESSION_RESTORE,
                                extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);
    }

    TabContents* tab_contents =
        browser->AddRestoredTab(tab.navigations,
                                tab_index,
                                selected_index,
                                tab.extension_app_id,
                                false,
                                tab.pinned,
                                true,
                                NULL);
    if (schedule_load)
      tab_loader_->ScheduleLoad(&tab_contents->controller());
  }

  Browser* CreateRestoredBrowser(Browser::Type type,
                                 gfx::Rect bounds,
                                 bool is_maximized) {
    Browser* browser = new Browser(type, profile_);
    browser->set_override_bounds(bounds);
    browser->set_maximized_state(is_maximized ?
        Browser::MAXIMIZED_STATE_MAXIMIZED :
        Browser::MAXIMIZED_STATE_UNMAXIMIZED);
    browser->InitBrowserWindow();
    return browser;
  }

  void ShowBrowser(Browser* browser,
                   int initial_tab_count,
                   int selected_session_index) {
    if (browser_ == browser) {
      browser->ActivateTabAt(browser->tab_count() - 1, true);
      return;
    }

    DCHECK(browser);
    DCHECK(browser->tab_count());
    browser->ActivateTabAt(
        std::min(initial_tab_count + std::max(0, selected_session_index),
                 browser->tab_count() - 1), true);
    browser->window()->Show();
    // TODO(jcampan): http://crbug.com/8123 we should not need to set the
    //                initial focus explicitly.
    browser->GetSelectedTabContents()->view()->SetInitialFocus();
  }

  // Appends the urls in |urls| to |browser|.
  void AppendURLsToBrowser(Browser* browser,
                           const std::vector<GURL>& urls) {
    for (size_t i = 0; i < urls.size(); ++i) {
      int add_types = TabStripModel::ADD_FORCE_INDEX;
      if (i == 0)
        add_types |= TabStripModel::ADD_ACTIVE;
      int index = browser->GetIndexForInsertionDuringRestore(i);
      browser::NavigateParams params(browser, urls[i],
                                     PageTransition::START_PAGE);
      params.disposition = i == 0 ? NEW_FOREGROUND_TAB : NEW_BACKGROUND_TAB;
      params.tabstrip_index = index;
      params.tabstrip_add_types = add_types;
      browser::Navigate(&params);
    }
  }

  // Invokes TabRestored on the SessionService for all tabs in browser after
  // initial_count.
  void NotifySessionServiceOfRestoredTabs(Browser* browser, int initial_count) {
    SessionService* session_service =
        SessionServiceFactory::GetForProfile(profile_);
    for (int i = initial_count; i < browser->tab_count(); ++i)
      session_service->TabRestored(&browser->GetTabContentsAt(i)->controller(),
                                   browser->tabstrip_model()->IsTabPinned(i));
  }

  // The profile to create the sessions for.
  Profile* profile_;

  // The first browser to restore to, may be null.
  Browser* browser_;

  // Whether or not restore is synchronous.
  const bool synchronous_;

  // See description in RestoreSession (in .h).
  const bool clobber_existing_window_;

  // If true and there is an error or there are no windows to restore, we
  // create a tabbed browser anyway. This is used on startup to make sure at
  // at least one window is created.
  const bool always_create_tabbed_browser_;

  // Set of URLs to open in addition to those restored from the session.
  std::vector<GURL> urls_to_open_;

  // Used to get the session.
  CancelableRequestConsumer request_consumer_;

  // Responsible for loading the tabs.
  scoped_ptr<TabLoader> tab_loader_;

  // When synchronous we run a nested message loop. To avoid creating windows
  // from the nested message loop (which can make exiting the nested message
  // loop take a while) we cache the SessionWindows here and create the actual
  // windows when the nested message loop exits.
  std::vector<SessionWindow*> windows_;

  NotificationRegistrar registrar_;

  // The time we started the restore.
  base::TimeTicks restore_started_;
};

}  // namespace

// SessionRestore -------------------------------------------------------------

static Browser* Restore(Profile* profile,
                        Browser* browser,
                        bool synchronous,
                        bool clobber_existing_window,
                        bool always_create_tabbed_browser,
                        const std::vector<GURL>& urls_to_open) {
#if defined(OS_CHROMEOS)
  chromeos::BootTimesLoader::Get()->AddLoginTimeMarker(
      "SessionRestoreStarted", false);
#endif
  DCHECK(profile);
  // Always restore from the original profile (incognito profiles have no
  // session service).
  profile = profile->GetOriginalProfile();
  if (!SessionServiceFactory::GetForProfile(profile)) {
    NOTREACHED();
    return NULL;
  }
  restoring = true;
  profile->set_restored_last_session(true);
  // SessionRestoreImpl takes care of deleting itself when done.
  SessionRestoreImpl* restorer =
      new SessionRestoreImpl(profile, browser, synchronous,
                             clobber_existing_window,
                             always_create_tabbed_browser, urls_to_open);
  return restorer->Restore();
}

// static
void SessionRestore::RestoreSession(Profile* profile,
                                    Browser* browser,
                                    bool clobber_existing_window,
                                    bool always_create_tabbed_browser,
                                    const std::vector<GURL>& urls_to_open) {
  Restore(profile, browser, false, clobber_existing_window,
          always_create_tabbed_browser, urls_to_open);
}

// static
void SessionRestore::RestoreForeignSessionWindows(
    Profile* profile,
    std::vector<SessionWindow*>::const_iterator begin,
    std::vector<SessionWindow*>::const_iterator end) {
  // Create a SessionRestore object to eventually restore the tabs.
  std::vector<GURL> gurls;
  SessionRestoreImpl restorer(profile,
      static_cast<Browser*>(NULL), true, false, true, gurls);
  restorer.RestoreForeignSession(begin, end);
}

// static
void SessionRestore::RestoreForeignSessionTab(Profile* profile,
    const SessionTab& tab) {
  // Create a SessionRestore object to eventually restore the tabs.
  std::vector<GURL> gurls;
  SessionRestoreImpl restorer(profile,
      static_cast<Browser*>(NULL), true, false, true, gurls);
  restorer.RestoreForeignTab(tab);
}

// static
Browser* SessionRestore::RestoreSessionSynchronously(
    Profile* profile,
    const std::vector<GURL>& urls_to_open) {
  return Restore(profile, NULL, true, false, true, urls_to_open);
}

// static
bool SessionRestore::IsRestoring() {
  return restoring;
}
