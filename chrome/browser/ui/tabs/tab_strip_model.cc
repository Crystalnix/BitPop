// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/tab_strip_model.h"

#include <algorithm>
#include <map>
#include <string>

#include "base/command_line.h"
#include "base/metrics/histogram.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service.h"
#include "chrome/browser/ui/tab_contents/core_tab_helper.h"
#include "chrome/browser/ui/tab_contents/core_tab_helper_delegate.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model_order_controller.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_view.h"

using content::NavigationController;
using content::NavigationEntry;
using content::UserMetricsAction;
using content::WebContents;

namespace {

// Returns true if the specified transition is one of the types that cause the
// opener relationships for the tab in which the transition occured to be
// forgotten. This is generally any navigation that isn't a link click (i.e.
// any navigation that can be considered to be the start of a new task distinct
// from what had previously occurred in that tab).
bool ShouldForgetOpenersForTransition(content::PageTransition transition) {
  return transition == content::PAGE_TRANSITION_TYPED ||
      transition == content::PAGE_TRANSITION_AUTO_BOOKMARK ||
      transition == content::PAGE_TRANSITION_GENERATED ||
      transition == content::PAGE_TRANSITION_KEYWORD ||
      transition == content::PAGE_TRANSITION_START_PAGE;
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// TabStripModel, public:

TabStripModel::TabStripModel(TabStripModelDelegate* delegate, Profile* profile)
    : delegate_(delegate),
      profile_(profile),
      closing_all_(false),
      order_controller_(NULL) {
  DCHECK(delegate_);
  registrar_.Add(this,
                 chrome::NOTIFICATION_TAB_CONTENTS_DESTROYED,
                 content::NotificationService::AllBrowserContextsAndSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(profile_));
  order_controller_ = new TabStripModelOrderController(this);
}

TabStripModel::~TabStripModel() {
  FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                    TabStripModelDeleted());
  STLDeleteElements(&contents_data_);
  delete order_controller_;
}

void TabStripModel::AddObserver(TabStripModelObserver* observer) {
  observers_.AddObserver(observer);
}

void TabStripModel::RemoveObserver(TabStripModelObserver* observer) {
  observers_.RemoveObserver(observer);
}

void TabStripModel::SetInsertionPolicy(InsertionPolicy policy) {
  order_controller_->set_insertion_policy(policy);
}

TabStripModel::InsertionPolicy TabStripModel::insertion_policy() const {
  return order_controller_->insertion_policy();
}

bool TabStripModel::HasObserver(TabStripModelObserver* observer) {
  return observers_.HasObserver(observer);
}

bool TabStripModel::ContainsIndex(int index) const {
  return index >= 0 && index < count();
}

void TabStripModel::AppendTabContents(TabContents* contents,
                                      bool foreground) {
  int index = order_controller_->DetermineInsertionIndexForAppending();
  InsertTabContentsAt(index, contents,
                      foreground ? (ADD_INHERIT_GROUP | ADD_ACTIVE) :
                                   ADD_NONE);
}

void TabStripModel::InsertTabContentsAt(int index,
                                        TabContents* contents,
                                        int add_types) {
  bool active = add_types & ADD_ACTIVE;
  // Force app tabs to be pinned.
  bool pin =
      contents->extension_tab_helper()->is_app() || add_types & ADD_PINNED;
  index = ConstrainInsertionIndex(index, pin);

  // In tab dragging situations, if the last tab in the window was detached
  // then the user aborted the drag, we will have the |closing_all_| member
  // set (see DetachTabContentsAt) which will mess with our mojo here. We need
  // to clear this bit.
  closing_all_ = false;

  // Have to get the active contents before we monkey with |contents_|
  // otherwise we run into problems when we try to change the active contents
  // since the old contents and the new contents will be the same...
  TabContents* selected_contents = GetActiveTabContents();
  TabContentsData* data = new TabContentsData(contents);
  data->pinned = pin;
  if ((add_types & ADD_INHERIT_GROUP) && selected_contents) {
    if (active) {
      // Forget any existing relationships, we don't want to make things too
      // confusing by having multiple groups active at the same time.
      ForgetAllOpeners();
    }
    // Anything opened by a link we deem to have an opener.
    data->SetGroup(&selected_contents->web_contents()->GetController());
  } else if ((add_types & ADD_INHERIT_OPENER) && selected_contents) {
    if (active) {
      // Forget any existing relationships, we don't want to make things too
      // confusing by having multiple groups active at the same time.
      ForgetAllOpeners();
    }
    data->opener = &selected_contents->web_contents()->GetController();
  }

  contents_data_.insert(contents_data_.begin() + index, data);

  selection_model_.IncrementFrom(index);

  FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                    TabInsertedAt(contents, index, active));
  if (active) {
    TabStripSelectionModel new_model;
    new_model.Copy(selection_model_);
    new_model.SetSelectedIndex(index);
    SetSelection(new_model, NOTIFY_DEFAULT);
  }
}

TabContents* TabStripModel::ReplaceTabContentsAt(int index,
                                                 TabContents* new_contents) {
  DCHECK(ContainsIndex(index));
  TabContents* old_contents = GetContentsAt(index);

  ForgetOpenersAndGroupsReferencing(
      &(old_contents->web_contents()->GetController()));

  contents_data_[index]->contents = new_contents;

  FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                    TabReplacedAt(this, old_contents, new_contents, index));

  // When the active tab contents is replaced send out selected notification
  // too. We do this as nearly all observers need to treat a replace of the
  // selected contents as selection changing.
  if (active_index() == index) {
    FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                      ActiveTabChanged(old_contents, new_contents,
                                       active_index(), false));
  }
  return old_contents;
}

void TabStripModel::ReplaceNavigationControllerAt(
    int index, TabContents* contents) {
  // This appears to be OK with no flicker since no redraw event
  // occurs between the call to add an additional tab and one to close
  // the previous tab.
  InsertTabContentsAt(index + 1, contents, ADD_ACTIVE | ADD_INHERIT_GROUP);
  std::vector<int> closing_tabs;
  closing_tabs.push_back(index);
  InternalCloseTabs(closing_tabs, CLOSE_NONE);
}

TabContents* TabStripModel::DiscardTabContentsAt(int index) {
  DCHECK(ContainsIndex(index));
  TabContents* null_contents = new TabContents(
      WebContents::Create(profile(),
                          NULL /* site_instance */,
                          MSG_ROUTING_NONE,
                          NULL /* base_tab_contents */,
                          NULL /* session_storage_namespace */));
  TabContents* old_contents = GetContentsAt(index);
  // Copy over the state from the navigation controller so we preserve the
  // back/forward history and continue to display the correct title/favicon.
  null_contents->web_contents()->GetController().CopyStateFrom(
      old_contents->web_contents()->GetController());
  // Replace the tab we're discarding with the null version.
  ReplaceTabContentsAt(index, null_contents);
  // Mark the tab so it will reload when we click.
  contents_data_[index]->discarded = true;
  // Discard the old tab's renderer.
  // TODO(jamescook): This breaks script connections with other tabs.
  // We need to find a different approach that doesn't do that, perhaps based
  // on navigation to swappedout://.
  delete old_contents;
  return null_contents;
}

TabContents* TabStripModel::DetachTabContentsAt(int index) {
  if (contents_data_.empty())
    return NULL;

  DCHECK(ContainsIndex(index));

  TabContents* removed_contents = GetContentsAt(index);
  bool was_selected = IsTabSelected(index);
  int next_selected_index = order_controller_->DetermineNewSelectedIndex(index);
  delete contents_data_[index];
  contents_data_.erase(contents_data_.begin() + index);
  ForgetOpenersAndGroupsReferencing(
      &(removed_contents->web_contents()->GetController()));
  if (empty())
    closing_all_ = true;
  FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
      TabDetachedAt(removed_contents, index));
  if (empty()) {
    selection_model_.Clear();
    // TabDetachedAt() might unregister observers, so send |TabStripEmtpy()| in
    // a second pass.
    FOR_EACH_OBSERVER(TabStripModelObserver, observers_, TabStripEmpty());
  } else {
    int old_active = active_index();
    selection_model_.DecrementFrom(index);
    TabStripSelectionModel old_model;
    old_model.Copy(selection_model_);
    if (index == old_active) {
      NotifyIfTabDeactivated(removed_contents);
      if (!selection_model_.empty()) {
        // The active tab was removed, but there is still something selected.
        // Move the active and anchor to the first selected index.
        selection_model_.set_active(selection_model_.selected_indices()[0]);
        selection_model_.set_anchor(selection_model_.active());
      } else {
        // The active tab was removed and nothing is selected. Reset the
        // selection and send out notification.
        selection_model_.SetSelectedIndex(next_selected_index);
      }
      NotifyIfActiveTabChanged(removed_contents, NOTIFY_DEFAULT);
    }

    // Sending notification in case the detached tab was selected. Using
    // NotifyIfActiveOrSelectionChanged() here would not guarantee that a
    // notification is sent even though the tab selection has changed because
    // |old_model| is stored after calling DecrementFrom().
    if (was_selected) {
      FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                        TabSelectionChanged(this, old_model));
    }
  }
  return removed_contents;
}

void TabStripModel::ActivateTabAt(int index, bool user_gesture) {
  DCHECK(ContainsIndex(index));
  TabStripSelectionModel new_model;
  new_model.Copy(selection_model_);
  new_model.SetSelectedIndex(index);
  SetSelection(new_model, user_gesture ? NOTIFY_USER_GESTURE : NOTIFY_DEFAULT);
}

void TabStripModel::AddTabAtToSelection(int index) {
  DCHECK(ContainsIndex(index));
  TabStripSelectionModel new_model;
  new_model.Copy(selection_model_);
  new_model.AddIndexToSelection(index);
  SetSelection(new_model, NOTIFY_DEFAULT);
}

void TabStripModel::MoveTabContentsAt(int index,
                                      int to_position,
                                      bool select_after_move) {
  DCHECK(ContainsIndex(index));
  if (index == to_position)
    return;

  int first_non_mini_tab = IndexOfFirstNonMiniTab();
  if ((index < first_non_mini_tab && to_position >= first_non_mini_tab) ||
      (to_position < first_non_mini_tab && index >= first_non_mini_tab)) {
    // This would result in mini tabs mixed with non-mini tabs. We don't allow
    // that.
    return;
  }

  MoveTabContentsAtImpl(index, to_position, select_after_move);
}

void TabStripModel::MoveSelectedTabsTo(int index) {
  int total_mini_count = IndexOfFirstNonMiniTab();
  int selected_mini_count = 0;
  int selected_count =
      static_cast<int>(selection_model_.selected_indices().size());
  for (int i = 0; i < selected_count &&
           IsMiniTab(selection_model_.selected_indices()[i]); ++i) {
    selected_mini_count++;
  }

  // To maintain that all mini-tabs occur before non-mini-tabs we move them
  // first.
  if (selected_mini_count > 0) {
    MoveSelectedTabsToImpl(
        std::min(total_mini_count - selected_mini_count, index), 0u,
        selected_mini_count);
    if (index > total_mini_count - selected_mini_count) {
      // We're being told to drag mini-tabs to an invalid location. Adjust the
      // index such that non-mini-tabs end up at a location as though we could
      // move the mini-tabs to index. See description in header for more
      // details.
      index += selected_mini_count;
    }
  }
  if (selected_mini_count == selected_count)
    return;

  // Then move the non-pinned tabs.
  MoveSelectedTabsToImpl(std::max(index, total_mini_count),
                         selected_mini_count,
                         selected_count - selected_mini_count);
}

TabContents* TabStripModel::GetActiveTabContents() const {
  return GetTabContentsAt(active_index());
}

TabContents* TabStripModel::GetTabContentsAt(int index) const {
  if (ContainsIndex(index))
    return GetContentsAt(index);
  return NULL;
}

int TabStripModel::GetIndexOfTabContents(const TabContents* contents) const {
  int index = 0;
  TabContentsDataVector::const_iterator iter = contents_data_.begin();
  for (; iter != contents_data_.end(); ++iter, ++index) {
    if ((*iter)->contents == contents)
      return index;
  }
  return kNoTab;
}

int TabStripModel::GetIndexOfWebContents(const WebContents* contents) const {
  int index = 0;
  TabContentsDataVector::const_iterator iter = contents_data_.begin();
  for (; iter != contents_data_.end(); ++iter, ++index) {
    if ((*iter)->contents->web_contents() == contents)
      return index;
  }
  return kNoTab;
}

void TabStripModel::UpdateTabContentsStateAt(int index,
    TabStripModelObserver::TabChangeType change_type) {
  DCHECK(ContainsIndex(index));

  FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
      TabChangedAt(GetContentsAt(index), index, change_type));
}

void TabStripModel::CloseAllTabs() {
  // Set state so that observers can adjust their behavior to suit this
  // specific condition when CloseTabContentsAt causes a flurry of
  // Close/Detach/Select notifications to be sent.
  closing_all_ = true;
  std::vector<int> closing_tabs;
  for (int i = count() - 1; i >= 0; --i)
    closing_tabs.push_back(i);
  InternalCloseTabs(closing_tabs, CLOSE_CREATE_HISTORICAL_TAB);
}

bool TabStripModel::CloseTabContentsAt(int index, uint32 close_types) {
  std::vector<int> closing_tabs;
  closing_tabs.push_back(index);
  return InternalCloseTabs(closing_tabs, close_types);
}

bool TabStripModel::TabsAreLoading() const {
  TabContentsDataVector::const_iterator iter = contents_data_.begin();
  for (; iter != contents_data_.end(); ++iter) {
    if ((*iter)->contents->web_contents()->IsLoading())
      return true;
  }
  return false;
}

NavigationController* TabStripModel::GetOpenerOfTabContentsAt(int index) {
  DCHECK(ContainsIndex(index));
  return contents_data_[index]->opener;
}

void TabStripModel::SetOpenerOfTabContentsAt(
    int index,
    content::NavigationController* opener) {
  DCHECK(ContainsIndex(index));
  DCHECK(opener);
  contents_data_[index]->opener = opener;
}

int TabStripModel::GetIndexOfNextTabContentsOpenedBy(
    const NavigationController* opener,
    int start_index,
    bool use_group) const {
  DCHECK(opener);
  DCHECK(ContainsIndex(start_index));

  // Check tabs after start_index first.
  for (int i = start_index + 1; i < count(); ++i) {
    if (OpenerMatches(contents_data_[i], opener, use_group))
      return i;
  }
  // Then check tabs before start_index, iterating backwards.
  for (int i = start_index - 1; i >= 0; --i) {
    if (OpenerMatches(contents_data_[i], opener, use_group))
      return i;
  }
  return kNoTab;
}

int TabStripModel::GetIndexOfFirstTabContentsOpenedBy(
    const NavigationController* opener, int start_index) const {
  DCHECK(opener);
  DCHECK(ContainsIndex(start_index));

  for (int i = 0; i < start_index; ++i) {
    if (contents_data_[i]->opener == opener)
      return i;
  }
  return kNoTab;
}

int TabStripModel::GetIndexOfLastTabContentsOpenedBy(
    const NavigationController* opener, int start_index) const {
  DCHECK(opener);
  DCHECK(ContainsIndex(start_index));

  TabContentsDataVector::const_iterator end =
      contents_data_.begin() + start_index;
  TabContentsDataVector::const_iterator iter = contents_data_.end();
  TabContentsDataVector::const_iterator next;
  for (; iter != end; --iter) {
    next = iter - 1;
    if (next == end)
      break;
    if ((*next)->opener == opener)
      return static_cast<int>(next - contents_data_.begin());
  }
  return kNoTab;
}

void TabStripModel::TabNavigating(TabContents* contents,
                                  content::PageTransition transition) {
  if (ShouldForgetOpenersForTransition(transition)) {
    // Don't forget the openers if this tab is a New Tab page opened at the
    // end of the TabStrip (e.g. by pressing Ctrl+T). Give the user one
    // navigation of one of these transition types before resetting the
    // opener relationships (this allows for the use case of opening a new
    // tab to do a quick look-up of something while viewing a tab earlier in
    // the strip). We can make this heuristic more permissive if need be.
    if (!IsNewTabAtEndOfTabStrip(contents)) {
      // If the user navigates the current tab to another page in any way
      // other than by clicking a link, we want to pro-actively forget all
      // TabStrip opener relationships since we assume they're beginning a
      // different task by reusing the current tab.
      ForgetAllOpeners();
      // In this specific case we also want to reset the group relationship,
      // since it is now technically invalid.
      ForgetGroup(contents);
    }
  }
}

void TabStripModel::ForgetAllOpeners() {
  // Forget all opener memories so we don't do anything weird with tab
  // re-selection ordering.
  TabContentsDataVector::const_iterator iter = contents_data_.begin();
  for (; iter != contents_data_.end(); ++iter)
    (*iter)->ForgetOpener();
}

void TabStripModel::ForgetGroup(TabContents* contents) {
  int index = GetIndexOfTabContents(contents);
  DCHECK(ContainsIndex(index));
  contents_data_[index]->SetGroup(NULL);
  contents_data_[index]->ForgetOpener();
}

bool TabStripModel::ShouldResetGroupOnSelect(TabContents* contents) const {
  int index = GetIndexOfTabContents(contents);
  DCHECK(ContainsIndex(index));
  return contents_data_[index]->reset_group_on_select;
}

void TabStripModel::SetTabBlocked(int index, bool blocked) {
  DCHECK(ContainsIndex(index));
  if (contents_data_[index]->blocked == blocked)
    return;
  contents_data_[index]->blocked = blocked;
  FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
      TabBlockedStateChanged(contents_data_[index]->contents,
      index));
}

void TabStripModel::SetTabPinned(int index, bool pinned) {
  DCHECK(ContainsIndex(index));
  if (contents_data_[index]->pinned == pinned)
    return;

  if (IsAppTab(index)) {
    if (!pinned) {
      // App tabs should always be pinned.
      NOTREACHED();
      return;
    }
    // Changing the pinned state of an app tab doesn't affect its mini-tab
    // status.
    contents_data_[index]->pinned = pinned;
  } else {
    // The tab is not an app tab, it's position may have to change as the
    // mini-tab state is changing.
    int non_mini_tab_index = IndexOfFirstNonMiniTab();
    contents_data_[index]->pinned = pinned;
    if (pinned && index != non_mini_tab_index) {
      MoveTabContentsAtImpl(index, non_mini_tab_index, false);
      index = non_mini_tab_index;
    } else if (!pinned && index + 1 != non_mini_tab_index) {
      MoveTabContentsAtImpl(index, non_mini_tab_index - 1, false);
      index = non_mini_tab_index - 1;
    }

    FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                      TabMiniStateChanged(contents_data_[index]->contents,
                                          index));
  }

  // else: the tab was at the boundary and it's position doesn't need to
  // change.
  FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                    TabPinnedStateChanged(contents_data_[index]->contents,
                                          index));
}

bool TabStripModel::IsTabPinned(int index) const {
  DCHECK(ContainsIndex(index));
  return contents_data_[index]->pinned;
}

bool TabStripModel::IsMiniTab(int index) const {
  return IsTabPinned(index) || IsAppTab(index);
}

bool TabStripModel::IsAppTab(int index) const {
  TabContents* contents = GetTabContentsAt(index);
  return contents && contents->extension_tab_helper()->is_app();
}

bool TabStripModel::IsTabBlocked(int index) const {
  return contents_data_[index]->blocked;
}

bool TabStripModel::IsTabDiscarded(int index) const {
  return contents_data_[index]->discarded;
}

int TabStripModel::IndexOfFirstNonMiniTab() const {
  for (size_t i = 0; i < contents_data_.size(); ++i) {
    if (!IsMiniTab(static_cast<int>(i)))
      return static_cast<int>(i);
  }
  // No mini-tabs.
  return count();
}

int TabStripModel::ConstrainInsertionIndex(int index, bool mini_tab) {
  return mini_tab ? std::min(std::max(0, index), IndexOfFirstNonMiniTab()) :
      std::min(count(), std::max(index, IndexOfFirstNonMiniTab()));
}

void TabStripModel::ExtendSelectionTo(int index) {
  DCHECK(ContainsIndex(index));
  TabStripSelectionModel new_model;
  new_model.Copy(selection_model_);
  new_model.SetSelectionFromAnchorTo(index);
  SetSelection(new_model, NOTIFY_DEFAULT);
}

void TabStripModel::ToggleSelectionAt(int index) {
  DCHECK(ContainsIndex(index));
  TabStripSelectionModel new_model;
  new_model.Copy(selection_model());
  if (selection_model_.IsSelected(index)) {
    if (selection_model_.size() == 1) {
      // One tab must be selected and this tab is currently selected so we can't
      // unselect it.
      return;
    }
    new_model.RemoveIndexFromSelection(index);
    new_model.set_anchor(index);
    if (new_model.active() == index ||
        new_model.active() == TabStripSelectionModel::kUnselectedIndex)
      new_model.set_active(new_model.selected_indices()[0]);
  } else {
    new_model.AddIndexToSelection(index);
    new_model.set_anchor(index);
    new_model.set_active(index);
  }
  SetSelection(new_model, NOTIFY_DEFAULT);
}

void TabStripModel::AddSelectionFromAnchorTo(int index) {
  TabStripSelectionModel new_model;
  new_model.Copy(selection_model_);
  new_model.AddSelectionFromAnchorTo(index);
  SetSelection(new_model, NOTIFY_DEFAULT);
}

bool TabStripModel::IsTabSelected(int index) const {
  DCHECK(ContainsIndex(index));
  return selection_model_.IsSelected(index);
}

void TabStripModel::SetSelectionFromModel(
    const TabStripSelectionModel& source) {
  DCHECK_NE(TabStripSelectionModel::kUnselectedIndex, source.active());
  SetSelection(source, NOTIFY_DEFAULT);
}

void TabStripModel::AddTabContents(TabContents* contents,
                                   int index,
                                   content::PageTransition transition,
                                   int add_types) {
  // If the newly-opened tab is part of the same task as the parent tab, we want
  // to inherit the parent's "group" attribute, so that if this tab is then
  // closed we'll jump back to the parent tab.
  bool inherit_group = (add_types & ADD_INHERIT_GROUP) == ADD_INHERIT_GROUP;

  if (transition == content::PAGE_TRANSITION_LINK &&
      (add_types & ADD_FORCE_INDEX) == 0) {
    // We assume tabs opened via link clicks are part of the same task as their
    // parent.  Note that when |force_index| is true (e.g. when the user
    // drag-and-drops a link to the tab strip), callers aren't really handling
    // link clicks, they just want to score the navigation like a link click in
    // the history backend, so we don't inherit the group in this case.
    index = order_controller_->DetermineInsertionIndex(
        contents, transition, add_types & ADD_ACTIVE);
    inherit_group = true;
  } else {
    // For all other types, respect what was passed to us, normalizing -1s and
    // values that are too large.
    if (index < 0 || index > count())
      index = order_controller_->DetermineInsertionIndexForAppending();
  }

  if (transition == content::PAGE_TRANSITION_TYPED && index == count()) {
    // Also, any tab opened at the end of the TabStrip with a "TYPED"
    // transition inherit group as well. This covers the cases where the user
    // creates a New Tab (e.g. Ctrl+T, or clicks the New Tab button), or types
    // in the address bar and presses Alt+Enter. This allows for opening a new
    // Tab to quickly look up something. When this Tab is closed, the old one
    // is re-selected, not the next-adjacent.
    inherit_group = true;
  }
  InsertTabContentsAt(
      index, contents,
      add_types | (inherit_group ? ADD_INHERIT_GROUP : 0));
  // Reset the index, just in case insert ended up moving it on us.
  index = GetIndexOfTabContents(contents);

  if (inherit_group && transition == content::PAGE_TRANSITION_TYPED)
    contents_data_[index]->reset_group_on_select = true;

  // TODO(sky): figure out why this is here and not in InsertTabContentsAt. When
  // here we seem to get failures in startup perf tests.
  // Ensure that the new WebContentsView begins at the same size as the
  // previous WebContentsView if it existed.  Otherwise, the initial WebKit
  // layout will be performed based on a width of 0 pixels, causing a
  // very long, narrow, inaccurate layout.  Because some scripts on pages (as
  // well as WebKit's anchor link location calculation) are run on the
  // initial layout and not recalculated later, we need to ensure the first
  // layout is performed with sane view dimensions even when we're opening a
  // new background tab.
  if (TabContents* old_contents = GetActiveTabContents()) {
    if ((add_types & ADD_ACTIVE) == 0) {
      contents->web_contents()->GetView()->
          SizeContents(old_contents->web_contents()->
              GetView()->GetContainerSize());
      // We need to hide the contents or else we get and execute paints for
      // background tabs. With enough background tabs they will steal the
      // backing store of the visible tab causing flashing. See bug 20831.
      contents->web_contents()->WasHidden();
    }
  }
}

void TabStripModel::CloseSelectedTabs() {
  InternalCloseTabs(selection_model_.selected_indices(),
                    CLOSE_CREATE_HISTORICAL_TAB | CLOSE_USER_GESTURE);
}

void TabStripModel::SelectNextTab() {
  SelectRelativeTab(true);
}

void TabStripModel::SelectPreviousTab() {
  SelectRelativeTab(false);
}

void TabStripModel::SelectLastTab() {
  ActivateTabAt(count() - 1, true);
}

void TabStripModel::MoveTabNext() {
  // TODO: this likely needs to be updated for multi-selection.
  int new_index = std::min(active_index() + 1, count() - 1);
  MoveTabContentsAt(active_index(), new_index, true);
}

void TabStripModel::MoveTabPrevious() {
  // TODO: this likely needs to be updated for multi-selection.
  int new_index = std::max(active_index() - 1, 0);
  MoveTabContentsAt(active_index(), new_index, true);
}

void TabStripModel::ActiveTabClicked(int index) {
  FOR_EACH_OBSERVER(TabStripModelObserver, observers_, ActiveTabClicked(index));
}

// Context menu functions.
bool TabStripModel::IsContextMenuCommandEnabled(
    int context_index, ContextMenuCommand command_id) const {
  DCHECK(command_id > CommandFirst && command_id < CommandLast);
  switch (command_id) {
    case CommandNewTab:
    case CommandCloseTab:
      return true;

    case CommandReload: {
      std::vector<int> indices = GetIndicesForCommand(context_index);
      for (size_t i = 0; i < indices.size(); ++i) {
        TabContents* tab = GetTabContentsAt(indices[i]);
        if (tab) {
          CoreTabHelperDelegate* core_delegate =
              tab->core_tab_helper()->delegate();
          if (!core_delegate || core_delegate->CanReloadContents(tab))
            return true;
        }
      }
      return false;
    }

    case CommandCloseOtherTabs:
    case CommandCloseTabsToRight:
      return !GetIndicesClosedByCommand(context_index, command_id).empty();

    case CommandDuplicate: {
      std::vector<int> indices = GetIndicesForCommand(context_index);
      for (size_t i = 0; i < indices.size(); ++i) {
        if (delegate_->CanDuplicateContentsAt(indices[i]))
          return true;
      }
      return false;
    }

    case CommandRestoreTab:
      return delegate_->CanRestoreTab();

    case CommandTogglePinned: {
      std::vector<int> indices = GetIndicesForCommand(context_index);
      for (size_t i = 0; i < indices.size(); ++i) {
        if (!IsAppTab(indices[i]))
          return true;
      }
      return false;
    }

    case CommandBookmarkAllTabs:
      return browser_defaults::bookmarks_enabled &&
          delegate_->CanBookmarkAllTabs();

    case CommandSelectByDomain:
    case CommandSelectByOpener:
      return true;

    default:
      NOTREACHED();
  }
  return false;
}

void TabStripModel::ExecuteContextMenuCommand(
    int context_index, ContextMenuCommand command_id) {
  DCHECK(command_id > CommandFirst && command_id < CommandLast);
  switch (command_id) {
    case CommandNewTab:
      content::RecordAction(UserMetricsAction("TabContextMenu_NewTab"));
      UMA_HISTOGRAM_ENUMERATION("Tab.NewTab",
                                TabStripModel::NEW_TAB_CONTEXT_MENU,
                                TabStripModel::NEW_TAB_ENUM_COUNT);
      delegate()->AddBlankTabAt(context_index + 1, true);
      break;

    case CommandReload: {
      content::RecordAction(UserMetricsAction("TabContextMenu_Reload"));
      std::vector<int> indices = GetIndicesForCommand(context_index);
      for (size_t i = 0; i < indices.size(); ++i) {
        TabContents* tab = GetTabContentsAt(indices[i]);
        if (tab) {
          CoreTabHelperDelegate* core_delegate =
              tab->core_tab_helper()->delegate();
          if (!core_delegate || core_delegate->CanReloadContents(tab))
            tab->web_contents()->GetController().Reload(true);
        }
      }
      break;
    }

    case CommandDuplicate: {
      content::RecordAction(UserMetricsAction("TabContextMenu_Duplicate"));
      std::vector<int> indices = GetIndicesForCommand(context_index);
      // Copy the TabContents off as the indices will change as tabs are
      // duplicated.
      std::vector<TabContents*> tabs;
      for (size_t i = 0; i < indices.size(); ++i)
        tabs.push_back(GetTabContentsAt(indices[i]));
      for (size_t i = 0; i < tabs.size(); ++i) {
        int index = GetIndexOfTabContents(tabs[i]);
        if (index != -1 && delegate_->CanDuplicateContentsAt(index))
          delegate_->DuplicateContentsAt(index);
      }
      break;
    }

    case CommandCloseTab: {
      content::RecordAction(UserMetricsAction("TabContextMenu_CloseTab"));
      std::vector<int> indices = GetIndicesForCommand(context_index);
      // Copy the TabContents off as the indices will change as we remove
      // things.
      std::vector<TabContents*> tabs;
      for (size_t i = 0; i < indices.size(); ++i)
        tabs.push_back(GetTabContentsAt(indices[i]));
      for (size_t i = 0; i < tabs.size(); ++i) {
        int index = GetIndexOfTabContents(tabs[i]);
        if (index != -1) {
          CloseTabContentsAt(index,
                             CLOSE_CREATE_HISTORICAL_TAB | CLOSE_USER_GESTURE);
        }
      }
      break;
    }

    case CommandCloseOtherTabs: {
      content::RecordAction(
          UserMetricsAction("TabContextMenu_CloseOtherTabs"));
      InternalCloseTabs(GetIndicesClosedByCommand(context_index, command_id),
                        CLOSE_CREATE_HISTORICAL_TAB);
      break;
    }

    case CommandCloseTabsToRight: {
      content::RecordAction(
          UserMetricsAction("TabContextMenu_CloseTabsToRight"));
      InternalCloseTabs(GetIndicesClosedByCommand(context_index, command_id),
                        CLOSE_CREATE_HISTORICAL_TAB);
      break;
    }

    case CommandRestoreTab: {
      content::RecordAction(UserMetricsAction("TabContextMenu_RestoreTab"));
      delegate_->RestoreTab();
      break;
    }

    case CommandTogglePinned: {
      content::RecordAction(
          UserMetricsAction("TabContextMenu_TogglePinned"));
      std::vector<int> indices = GetIndicesForCommand(context_index);
      bool pin = WillContextMenuPin(context_index);
      if (pin) {
        for (size_t i = 0; i < indices.size(); ++i) {
          if (!IsAppTab(indices[i]))
            SetTabPinned(indices[i], true);
        }
      } else {
        // Unpin from the back so that the order is maintained (unpinning can
        // trigger moving a tab).
        for (size_t i = indices.size(); i > 0; --i) {
          if (!IsAppTab(indices[i - 1]))
            SetTabPinned(indices[i - 1], false);
        }
      }
      break;
    }

    case CommandBookmarkAllTabs: {
      content::RecordAction(
          UserMetricsAction("TabContextMenu_BookmarkAllTabs"));

      delegate_->BookmarkAllTabs();
      break;
    }

    case CommandSelectByDomain:
    case CommandSelectByOpener: {
      std::vector<int> indices;
      if (command_id == CommandSelectByDomain)
        GetIndicesWithSameDomain(context_index, &indices);
      else
        GetIndicesWithSameOpener(context_index, &indices);
      TabStripSelectionModel selection_model;
      selection_model.SetSelectedIndex(context_index);
      for (size_t i = 0; i < indices.size(); ++i)
        selection_model.AddIndexToSelection(indices[i]);
      SetSelectionFromModel(selection_model);
      break;
    }

    default:
      NOTREACHED();
  }
}

std::vector<int> TabStripModel::GetIndicesClosedByCommand(
    int index,
    ContextMenuCommand id) const {
  DCHECK(ContainsIndex(index));
  DCHECK(id == CommandCloseTabsToRight || id == CommandCloseOtherTabs);
  bool is_selected = IsTabSelected(index);
  int start;
  if (id == CommandCloseTabsToRight) {
    if (is_selected) {
      start = selection_model_.selected_indices()[
          selection_model_.selected_indices().size() - 1] + 1;
    } else {
      start = index + 1;
    }
  } else {
    start = 0;
  }
  // NOTE: callers expect the vector to be sorted in descending order.
  std::vector<int> indices;
  for (int i = count() - 1; i >= start; --i) {
    if (i != index && !IsMiniTab(i) && (!is_selected || !IsTabSelected(i)))
      indices.push_back(i);
  }
  return indices;
}

bool TabStripModel::WillContextMenuPin(int index) {
  std::vector<int> indices = GetIndicesForCommand(index);
  // If all tabs are pinned, then we unpin, otherwise we pin.
  bool all_pinned = true;
  for (size_t i = 0; i < indices.size() && all_pinned; ++i) {
    if (!IsAppTab(index))  // We never change app tabs.
      all_pinned = IsTabPinned(indices[i]);
  }
  return !all_pinned;
}

///////////////////////////////////////////////////////////////////////////////
// TabStripModel, content::NotificationObserver implementation:

void TabStripModel::Observe(int type,
                            const content::NotificationSource& source,
                            const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_TAB_CONTENTS_DESTROYED: {
      // Sometimes, on qemu, it seems like a WebContents object can be destroyed
      // while we still have a reference to it. We need to break this reference
      // here so we don't crash later.
      int index = GetIndexOfTabContents(
          content::Source<TabContents>(source).ptr());
      if (index != TabStripModel::kNoTab) {
        // Note that we only detach the contents here, not close it - it's
        // already been closed. We just want to undo our bookkeeping.
        DetachTabContentsAt(index);
      }
      break;
    }

    case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
      const extensions::Extension* extension =
          content::Details<extensions::UnloadedExtensionInfo>(
              details)->extension;
      // Iterate backwards as we may remove items while iterating.
      for (int i = count() - 1; i >= 0; i--) {
        TabContents* contents = GetTabContentsAt(i);
        if (contents->extension_tab_helper()->extension_app() == extension) {
          // The extension an app tab was created from has been nuked. Delete
          // the WebContents. Deleting a WebContents results in a notification
          // of type NOTIFICATION_WEB_CONTENTS_DESTROYED; we do the necessary
          // cleanup in handling that notification.

          InternalCloseTab(contents, i, false);
        }
      }
      break;
    }

    default:
      NOTREACHED();
  }
}

// static
bool TabStripModel::ContextMenuCommandToBrowserCommand(int cmd_id,
    int* browser_cmd) {
  switch (cmd_id) {
    case CommandNewTab:
      *browser_cmd = IDC_NEW_TAB;
      break;
    case CommandReload:
      *browser_cmd = IDC_RELOAD;
      break;
    case CommandDuplicate:
      *browser_cmd = IDC_DUPLICATE_TAB;
      break;
    case CommandCloseTab:
      *browser_cmd = IDC_CLOSE_TAB;
      break;
    case CommandRestoreTab:
      *browser_cmd = IDC_RESTORE_TAB;
      break;
    case CommandBookmarkAllTabs:
      *browser_cmd = IDC_BOOKMARK_ALL_TABS;
      break;
    default:
      *browser_cmd = 0;
      return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// TabStripModel, private:

void TabStripModel::GetIndicesWithSameDomain(int index,
                                             std::vector<int>* indices) {
  TabContents* tab = GetTabContentsAt(index);
  std::string domain = tab->web_contents()->GetURL().host();
  if (domain.empty())
    return;
  for (int i = 0; i < count(); ++i) {
    if (i == index)
      continue;
    if (GetTabContentsAt(i)->web_contents()->GetURL().host() == domain)
      indices->push_back(i);
  }
}

void TabStripModel::GetIndicesWithSameOpener(int index,
                                             std::vector<int>* indices) {
  NavigationController* opener = contents_data_[index]->group;
  if (!opener) {
    // If there is no group, find all tabs with the selected tab as the opener.
    opener = &(GetTabContentsAt(index)->web_contents()->GetController());
    if (!opener)
      return;
  }
  for (int i = 0; i < count(); ++i) {
    if (i == index)
      continue;
    if (contents_data_[i]->group == opener ||
        &(GetTabContentsAt(i)->web_contents()->GetController()) == opener) {
      indices->push_back(i);
    }
  }
}

std::vector<int> TabStripModel::GetIndicesForCommand(int index) const {
  if (!IsTabSelected(index)) {
    std::vector<int> indices;
    indices.push_back(index);
    return indices;
  }
  return selection_model_.selected_indices();
}

bool TabStripModel::IsNewTabAtEndOfTabStrip(TabContents* contents) const {
  const GURL& url = contents->web_contents()->GetURL();
  return url.SchemeIs(chrome::kChromeUIScheme) &&
         url.host() == chrome::kChromeUINewTabHost &&
         contents == GetContentsAt(count() - 1) &&
         contents->web_contents()->GetController().GetEntryCount() == 1;
}

bool TabStripModel::InternalCloseTabs(const std::vector<int>& indices,
                                      uint32 close_types) {
  if (indices.empty())
    return true;

  // Map the indices to TabContents, that way if deleting a tab deletes
  // other tabs we're ok. Crashes seem to indicate during tab deletion other
  // tabs are getting removed.
  std::vector<TabContents*> tabs;
  for (size_t i = 0; i < indices.size(); ++i)
    tabs.push_back(GetContentsAt(indices[i]));

  // We only try the fast shutdown path if the whole browser process is *not*
  // shutting down. Fast shutdown during browser termination is handled in
  // BrowserShutdown.
  if (browser_shutdown::GetShutdownType() == browser_shutdown::NOT_VALID) {
    // Construct a map of processes to the number of associated tabs that are
    // closing.
    std::map<content::RenderProcessHost*, size_t> processes;
    for (size_t i = 0; i < indices.size(); ++i) {
      TabContents* detached_contents = GetContentsAt(indices[i]);
      content::RenderProcessHost* process =
          detached_contents->web_contents()->GetRenderProcessHost();
      std::map<content::RenderProcessHost*, size_t>::iterator iter =
          processes.find(process);
      if (iter == processes.end()) {
        processes[process] = 1;
      } else {
        iter->second++;
      }
    }

    // Try to fast shutdown the tabs that can close.
    for (std::map<content::RenderProcessHost*, size_t>::iterator iter =
            processes.begin();
        iter != processes.end(); ++iter) {
      iter->first->FastShutdownForPageCount(iter->second);
    }
  }

  // We now return to our regularly scheduled shutdown procedure.
  bool retval = true;
  for (size_t i = 0; i < tabs.size(); ++i) {
    TabContents* detached_contents = tabs[i];
    int index = GetIndexOfTabContents(detached_contents);
    // Make sure we still contain the tab.
    if (index == kNoTab)
      continue;

    detached_contents->web_contents()->OnCloseStarted();

    // Update the explicitly closed state. If the unload handlers cancel the
    // close the state is reset in Browser. We don't update the explicitly
    // closed state if already marked as explicitly closed as unload handlers
    // call back to this if the close is allowed.
    if (!detached_contents->web_contents()->GetClosedByUserGesture()) {
      detached_contents->web_contents()->SetClosedByUserGesture(
          close_types & CLOSE_USER_GESTURE);
    }

    if (delegate_->RunUnloadListenerBeforeClosing(detached_contents)) {
      retval = false;
      continue;
    }

    InternalCloseTab(detached_contents, index,
                     (close_types & CLOSE_CREATE_HISTORICAL_TAB) != 0);
  }

  return retval;
}

void TabStripModel::InternalCloseTab(TabContents* contents,
                                     int index,
                                     bool create_historical_tabs) {
  FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                    TabClosingAt(this, contents, index));

  // Ask the delegate to save an entry for this tab in the historical tab
  // database if applicable.
  if (create_historical_tabs)
    delegate_->CreateHistoricalTab(contents);

  // Deleting the TabContents will call back to us via
  // NotificationObserver and detach it.
  delete contents;
}

TabContents* TabStripModel::GetContentsAt(int index) const {
  CHECK(ContainsIndex(index)) <<
      "Failed to find: " << index << " in: " << count() << " entries.";
  return contents_data_[index]->contents;
}

void TabStripModel::NotifyIfTabDeactivated(TabContents* contents) {
  if (contents) {
    FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                      TabDeactivated(contents));
  }
}

void TabStripModel::NotifyIfActiveTabChanged(TabContents* old_contents,
                                             NotifyTypes notify_types) {
  TabContents* new_contents = GetContentsAt(active_index());
  if (old_contents != new_contents) {
    FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                      ActiveTabChanged(old_contents, new_contents,
                                       active_index(),
                                       notify_types == NOTIFY_USER_GESTURE));
    // Activating a discarded tab reloads it, so it is no longer discarded.
    contents_data_[active_index()]->discarded = false;
  }
}

void TabStripModel::NotifyIfActiveOrSelectionChanged(
    TabContents* old_contents,
    NotifyTypes notify_types,
    const TabStripSelectionModel& old_model) {
  NotifyIfActiveTabChanged(old_contents, notify_types);

  if (!selection_model().Equals(old_model)) {
    FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                      TabSelectionChanged(this, old_model));
  }
}

void TabStripModel::SetSelection(
    const TabStripSelectionModel& new_model,
    NotifyTypes notify_types) {
  TabContents* old_contents = GetActiveTabContents();
  TabStripSelectionModel old_model;
  old_model.Copy(selection_model_);
  if (new_model.active() != selection_model_.active())
    NotifyIfTabDeactivated(old_contents);
  selection_model_.Copy(new_model);
  NotifyIfActiveOrSelectionChanged(old_contents, notify_types, old_model);
}

void TabStripModel::SelectRelativeTab(bool next) {
  // This may happen during automated testing or if a user somehow buffers
  // many key accelerators.
  if (contents_data_.empty())
    return;

  int index = active_index();
  int delta = next ? 1 : -1;
  index = (index + count() + delta) % count();
  ActivateTabAt(index, true);
}

void TabStripModel::MoveTabContentsAtImpl(int index,
                                          int to_position,
                                          bool select_after_move) {
  TabContentsData* moved_data = contents_data_[index];
  contents_data_.erase(contents_data_.begin() + index);
  contents_data_.insert(contents_data_.begin() + to_position, moved_data);

  selection_model_.Move(index, to_position);
  if (!selection_model_.IsSelected(select_after_move) && select_after_move) {
    // TODO(sky): why doesn't this code notify observers?
    selection_model_.SetSelectedIndex(to_position);
  }

  FOR_EACH_OBSERVER(TabStripModelObserver, observers_,
                    TabMoved(moved_data->contents, index, to_position));
}

void TabStripModel::MoveSelectedTabsToImpl(int index,
                                           size_t start,
                                           size_t length) {
  DCHECK(start < selection_model_.selected_indices().size() &&
         start + length <= selection_model_.selected_indices().size());
  size_t end = start + length;
  int count_before_index = 0;
  for (size_t i = start; i < end &&
       selection_model_.selected_indices()[i] < index + count_before_index;
       ++i) {
    count_before_index++;
  }

  // First move those before index. Any tabs before index end up moving in the
  // selection model so we use start each time through.
  int target_index = index + count_before_index;
  size_t tab_index = start;
  while (tab_index < end &&
         selection_model_.selected_indices()[start] < index) {
    MoveTabContentsAt(selection_model_.selected_indices()[start],
                      target_index - 1, false);
    tab_index++;
  }

  // Then move those after the index. These don't result in reordering the
  // selection.
  while (tab_index < end) {
    if (selection_model_.selected_indices()[tab_index] != target_index) {
      MoveTabContentsAt(selection_model_.selected_indices()[tab_index],
                        target_index, false);
    }
    tab_index++;
    target_index++;
  }
}

// static
bool TabStripModel::OpenerMatches(const TabContentsData* data,
                                  const NavigationController* opener,
                                  bool use_group) {
  return data->opener == opener || (use_group && data->group == opener);
}

void TabStripModel::ForgetOpenersAndGroupsReferencing(
    const NavigationController* tab) {
  for (TabContentsDataVector::const_iterator i = contents_data_.begin();
       i != contents_data_.end(); ++i) {
    if ((*i)->group == tab)
      (*i)->group = NULL;
    if ((*i)->opener == tab)
      (*i)->opener = NULL;
  }
}
