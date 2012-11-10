// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_BROWSER_TAB_STRIP_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_BROWSER_TAB_STRIP_CONTROLLER_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "chrome/browser/ui/search/search_model_observer.h"
#include "chrome/browser/ui/search/toolbar_search_animator_observer.h"
#include "chrome/browser/ui/tabs/hover_tab_selector.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/tab_strip_controller.h"
#include "content/public/browser/notification_observer.h"

class BaseTab;
class Browser;
class TabContents;
class TabStrip;
class TabStripSelectionModel;
struct TabRendererData;

namespace content {
class WebContents;
}

// An implementation of TabStripController that sources data from the
// TabContentses in a TabStripModel.
class BrowserTabStripController
    : public TabStripController,
      public TabStripModelObserver,
      public content::NotificationObserver,
      public chrome::search::SearchModelObserver,
      public chrome::search::ToolbarSearchAnimatorObserver {
 public:
  BrowserTabStripController(Browser* browser, TabStripModel* model);
  virtual ~BrowserTabStripController();

  void InitFromModel(TabStrip* tabstrip);

  TabStripModel* model() const { return model_; }

  bool IsCommandEnabledForTab(TabStripModel::ContextMenuCommand command_id,
                              BaseTab* tab) const;
  void ExecuteCommandForTab(TabStripModel::ContextMenuCommand command_id,
                            BaseTab* tab);
  bool IsTabPinned(BaseTab* tab) const;

  // TabStripController implementation:
  virtual const TabStripSelectionModel& GetSelectionModel() OVERRIDE;
  virtual int GetCount() const OVERRIDE;
  virtual bool IsValidIndex(int model_index) const OVERRIDE;
  virtual bool IsActiveTab(int model_index) const OVERRIDE;
  virtual int GetActiveIndex() const OVERRIDE;
  virtual bool IsTabSelected(int model_index) const OVERRIDE;
  virtual bool IsTabPinned(int model_index) const OVERRIDE;
  virtual bool IsNewTabPage(int model_index) const OVERRIDE;
  virtual void SelectTab(int model_index) OVERRIDE;
  virtual void ExtendSelectionTo(int model_index) OVERRIDE;
  virtual void ToggleSelected(int model_index) OVERRIDE;
  virtual void AddSelectionFromAnchorTo(int model_index) OVERRIDE;
  virtual void CloseTab(int model_index) OVERRIDE;
  virtual void ShowContextMenuForTab(BaseTab* tab,
                                     const gfx::Point& p) OVERRIDE;
  virtual void UpdateLoadingAnimations() OVERRIDE;
  virtual int HasAvailableDragActions() const OVERRIDE;
  virtual void OnDropIndexUpdate(int index, bool drop_before) OVERRIDE;
  virtual void PerformDrop(bool drop_before,
                           int index,
                           const GURL& url) OVERRIDE;
  virtual bool IsCompatibleWith(TabStrip* other) const OVERRIDE;
  virtual void CreateNewTab() OVERRIDE;
  virtual void ClickActiveTab(int index) OVERRIDE;
  virtual bool IsIncognito() OVERRIDE;
  virtual void LayoutTypeMaybeChanged() OVERRIDE;

  // TabStripModelObserver implementation:
  virtual void TabInsertedAt(TabContents* contents,
                             int model_index,
                             bool is_active) OVERRIDE;
  virtual void TabDetachedAt(TabContents* contents,
                             int model_index) OVERRIDE;
  virtual void TabSelectionChanged(
      TabStripModel* tab_strip_model,
      const TabStripSelectionModel& old_model) OVERRIDE;
  virtual void TabMoved(TabContents* contents,
                        int from_model_index,
                        int to_model_index) OVERRIDE;
  virtual void TabChangedAt(TabContents* contents,
                            int model_index,
                            TabChangeType change_type) OVERRIDE;
  virtual void TabReplacedAt(TabStripModel* tab_strip_model,
                             TabContents* old_contents,
                             TabContents* new_contents,
                             int model_index) OVERRIDE;
  virtual void TabPinnedStateChanged(TabContents* contents,
                                     int model_index) OVERRIDE;
  virtual void TabMiniStateChanged(TabContents* contents,
                                   int model_index) OVERRIDE;
  virtual void TabBlockedStateChanged(TabContents* contents,
                                      int model_index) OVERRIDE;

  // chrome::search::SearchModelObserver implementation:
  virtual void ModeChanged(const chrome::search::Mode& mode) OVERRIDE;

  // chrome::search::ToolbarSearchAnimatorObserver implementation:
  virtual void OnToolbarBackgroundAnimatorProgressed() OVERRIDE;
  virtual void OnToolbarBackgroundAnimatorCanceled(
      TabContents* tab_contents) OVERRIDE;

  // content::NotificationObserver implementation:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 protected:
  // The context in which SetTabRendererDataFromModel is being called.
  enum TabStatus {
    NEW_TAB,
    EXISTING_TAB
  };

  // Sets the TabRendererData from the TabStripModel.
  virtual void SetTabRendererDataFromModel(content::WebContents* contents,
                                           int model_index,
                                           TabRendererData* data,
                                           TabStatus tab_status);

  Profile* profile() const { return model_->profile(); }

  const TabStrip* tabstrip() const { return tabstrip_; }

  const Browser* browser() const { return browser_; }

 private:
  class TabContextMenuContents;

  // Invokes tabstrip_->SetTabData.
  void SetTabDataAt(TabContents* contents, int model_index);

  void StartHighlightTabsForCommand(
      TabStripModel::ContextMenuCommand command_id,
      BaseTab* tab);
  void StopHighlightTabsForCommand(
      TabStripModel::ContextMenuCommand command_id,
      BaseTab* tab);

  // Adds a tab.
  void AddTab(TabContents* contents, int index, bool is_active);

  // Resets the tabstrips layout type from prefs.
  void UpdateLayoutType();

  TabStripModel* model_;

  TabStrip* tabstrip_;

  // Non-owning pointer to the browser which is using this controller.
  Browser* browser_;

  // If non-NULL it means we're showing a menu for the tab.
  scoped_ptr<TabContextMenuContents> context_menu_contents_;

  // Helper for performing tab selection as a result of dragging over a tab.
  HoverTabSelector hover_tab_selector_;

  PrefChangeRegistrar local_pref_registrar_;

  DISALLOW_COPY_AND_ASSIGN(BrowserTabStripController);
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_BROWSER_TAB_STRIP_CONTROLLER_H_
