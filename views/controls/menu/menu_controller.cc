// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/controls/menu/menu_controller.h"

#include "base/i18n/rtl.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/canvas_skia.h"
#include "views/controls/button/menu_button.h"
#include "views/controls/menu/menu_scroll_view_container.h"
#include "views/controls/menu/submenu_view.h"
#include "views/drag_utils.h"
#include "views/screen.h"
#include "views/view_constants.h"
#include "views/views_delegate.h"
#include "views/widget/root_view.h"
#include "views/widget/widget.h"

#if defined(OS_LINUX)
#include "ui/base/keycodes/keyboard_code_conversion_gtk.h"
#endif

#if defined(TOUCH_UI)
#include "views/focus/accelerator_handler.h"
#endif

using base::Time;
using base::TimeDelta;
using ui::OSExchangeData;

// Period of the scroll timer (in milliseconds).
static const int kScrollTimerMS = 30;

// Delay, in ms, between when menus are selected are moused over and the menu
// appears.
static const int kShowDelay = 400;

// Amount of time from when the drop exits the menu and the menu is hidden.
static const int kCloseOnExitTime = 1200;

// Amount to inset submenus.
static const int kSubmenuHorizontalInset = 3;

namespace views {

namespace {

// Returns true if the mnemonic of |menu| matches key.
bool MatchesMnemonic(MenuItemView* menu, wchar_t key) {
  return menu->GetMnemonic() == key;
}

// Returns true if |menu| doesn't have a mnemonic and first character of the its
// title is |key|.
bool TitleMatchesMnemonic(MenuItemView* menu, wchar_t key) {
  if (menu->GetMnemonic())
    return false;

  std::wstring lower_title = UTF16ToWide(
      l10n_util::ToLower(WideToUTF16(menu->GetTitle())));
  return !lower_title.empty() && lower_title[0] == key;
}

}  // namespace

// Convenience for scrolling the view such that the origin is visible.
static void ScrollToVisible(View* view) {
  view->ScrollRectToVisible(view->GetLocalBounds());
}

// Returns the first descendant of |view| that is hot tracked.
static View* GetFirstHotTrackedView(View* view) {
  if (!view)
    return NULL;

  if (view->IsHotTracked())
    return view;

  for (int i = 0; i < view->child_count(); ++i) {
    View* hot_view = GetFirstHotTrackedView(view->GetChildViewAt(i));
    if (hot_view)
      return hot_view;
  }
  return NULL;
}

// Recurses through the child views of |view| returning the first view starting
// at |start| that is focusable. A value of -1 for |start| indicates to start at
// the first view (if |forward| is false, iterating starts at the last view). If
// |forward| is true the children are considered first to last, otherwise last
// to first.
static View* GetFirstFocusableView(View* view, int start, bool forward) {
  if (forward) {
    for (int i = start == -1 ? 0 : start; i < view->child_count(); ++i) {
      View* deepest = GetFirstFocusableView(view->GetChildViewAt(i), -1,
                                            forward);
      if (deepest)
        return deepest;
    }
  } else {
    for (int i = start == -1 ? view->child_count() - 1 : start; i >= 0; --i) {
      View* deepest = GetFirstFocusableView(view->GetChildViewAt(i), -1,
                                            forward);
      if (deepest)
        return deepest;
    }
  }
  return view->IsFocusableInRootView() ? view : NULL;
}

// Returns the first child of |start| that is focusable.
static View* GetInitialFocusableView(View* start, bool forward) {
  return GetFirstFocusableView(start, -1, forward);
}

// Returns the next view after |start_at| that is focusable. Returns NULL if
// there are no focusable children of |ancestor| after |start_at|.
static View* GetNextFocusableView(View* ancestor,
                                  View* start_at,
                                  bool forward) {
  DCHECK(ancestor->Contains(start_at));
  View* parent = start_at;
  do {
    View* new_parent = parent->parent();
    int index = new_parent->GetIndexOf(parent);
    index += forward ? 1 : -1;
    if (forward || index != -1) {
      View* next = GetFirstFocusableView(new_parent, index, forward);
      if (next)
        return next;
    }
    parent = new_parent;
  } while (parent != ancestor);
  return NULL;
}

// MenuScrollTask --------------------------------------------------------------

// MenuScrollTask is used when the SubmenuView does not all fit on screen and
// the mouse is over the scroll up/down buttons. MenuScrollTask schedules
// itself with a RepeatingTimer. When Run is invoked MenuScrollTask scrolls
// appropriately.

class MenuController::MenuScrollTask {
 public:
  MenuScrollTask() : submenu_(NULL) {
    pixels_per_second_ = MenuItemView::pref_menu_height() * 20;
  }

  void Update(const MenuController::MenuPart& part) {
    if (!part.is_scroll()) {
      StopScrolling();
      return;
    }
    DCHECK(part.submenu);
    SubmenuView* new_menu = part.submenu;
    bool new_is_up = (part.type == MenuController::MenuPart::SCROLL_UP);
    if (new_menu == submenu_ && is_scrolling_up_ == new_is_up)
      return;

    start_scroll_time_ = Time::Now();
    start_y_ = part.submenu->GetVisibleBounds().y();
    submenu_ = new_menu;
    is_scrolling_up_ = new_is_up;

    if (!scrolling_timer_.IsRunning()) {
      scrolling_timer_.Start(TimeDelta::FromMilliseconds(kScrollTimerMS), this,
                             &MenuScrollTask::Run);
    }
  }

  void StopScrolling() {
    if (scrolling_timer_.IsRunning()) {
      scrolling_timer_.Stop();
      submenu_ = NULL;
    }
  }

  // The menu being scrolled. Returns null if not scrolling.
  SubmenuView* submenu() const { return submenu_; }

 private:
  void Run() {
    DCHECK(submenu_);
    gfx::Rect vis_rect = submenu_->GetVisibleBounds();
    const int delta_y = static_cast<int>(
        (Time::Now() - start_scroll_time_).InMilliseconds() *
        pixels_per_second_ / 1000);
    vis_rect.set_y(is_scrolling_up_ ?
        std::max(0, start_y_ - delta_y) :
        std::min(submenu_->height() - vis_rect.height(), start_y_ + delta_y));
    submenu_->ScrollRectToVisible(vis_rect);
  }

  // SubmenuView being scrolled.
  SubmenuView* submenu_;

  // Direction scrolling.
  bool is_scrolling_up_;

  // Timer to periodically scroll.
  base::RepeatingTimer<MenuScrollTask> scrolling_timer_;

  // Time we started scrolling at.
  Time start_scroll_time_;

  // How many pixels to scroll per second.
  int pixels_per_second_;

  // Y-coordinate of submenu_view_ when scrolling started.
  int start_y_;

  DISALLOW_COPY_AND_ASSIGN(MenuScrollTask);
};

// MenuController:SelectByCharDetails ----------------------------------------

struct MenuController::SelectByCharDetails {
  SelectByCharDetails()
      : first_match(-1),
        has_multiple(false),
        index_of_item(-1),
        next_match(-1) {
  }

  // Index of the first menu with the specified mnemonic.
  int first_match;

  // If true there are multiple menu items with the same mnemonic.
  bool has_multiple;

  // Index of the selected item; may remain -1.
  int index_of_item;

  // If there are multiple matches this is the index of the item after the
  // currently selected item whose mnemonic matches. This may remain -1 even
  // though there are matches.
  int next_match;
};

// MenuController:State ------------------------------------------------------

MenuController::State::State() : item(NULL), submenu_open(false) {}

MenuController::State::~State() {}

// MenuController ------------------------------------------------------------

// static
MenuController* MenuController::active_instance_ = NULL;

// static
MenuController* MenuController::GetActiveInstance() {
  return active_instance_;
}

MenuItemView* MenuController::Run(gfx::NativeWindow parent,
                                  MenuButton* button,
                                  MenuItemView* root,
                                  const gfx::Rect& bounds,
                                  MenuItemView::AnchorPosition position,
                                  int* result_mouse_event_flags) {
  exit_type_ = EXIT_NONE;
  possible_drag_ = false;
  drag_in_progress_ = false;

  bool nested_menu = showing_;
  if (showing_) {
    // Only support nesting of blocking_run menus, nesting of
    // blocking/non-blocking shouldn't be needed.
    DCHECK(blocking_run_);

    // We're already showing, push the current state.
    menu_stack_.push_back(state_);

    // The context menu should be owned by the same parent.
    DCHECK(owner_ == parent);
  } else {
    showing_ = true;
  }

  // Reset current state.
  pending_state_ = State();
  state_ = State();
  UpdateInitialLocation(bounds, position);

  owner_ = parent;

  // Set the selection, which opens the initial menu.
  SetSelection(root, SELECTION_OPEN_SUBMENU | SELECTION_UPDATE_IMMEDIATELY);

  if (!blocking_run_) {
    // Start the timer to hide the menu. This is needed as we get no
    // notification when the drag has finished.
    StartCancelAllTimer();
    return NULL;
  } else if (button) {
    menu_button_ = button;
  }

  // Make sure Chrome doesn't attempt to shut down while the menu is showing.
  if (ViewsDelegate::views_delegate)
    ViewsDelegate::views_delegate->AddRef();

  // We need to turn on nestable tasks as in some situations (pressing alt-f for
  // one) the menus are run from a task. If we don't do this and are invoked
  // from a task none of the tasks we schedule are processed and the menu
  // appears totally broken.
  MessageLoopForUI* loop = MessageLoopForUI::current();
  bool did_allow_task_nesting = loop->NestableTasksAllowed();
  loop->SetNestableTasksAllowed(true);
  loop->Run(this);
  loop->SetNestableTasksAllowed(did_allow_task_nesting);

  if (ViewsDelegate::views_delegate)
    ViewsDelegate::views_delegate->ReleaseRef();

  // Close any open menus.
  SetSelection(NULL, SELECTION_UPDATE_IMMEDIATELY | SELECTION_EXIT);

  if (nested_menu) {
    DCHECK(!menu_stack_.empty());
    // We're running from within a menu, restore the previous state.
    // The menus are already showing, so we don't have to show them.
    state_ = menu_stack_.back();
    pending_state_ = menu_stack_.back();
    menu_stack_.pop_back();
  } else {
    showing_ = false;
    did_capture_ = false;
  }

  MenuItemView* result = result_;
  // In case we're nested, reset result_.
  result_ = NULL;

  if (result_mouse_event_flags)
    *result_mouse_event_flags = result_mouse_event_flags_;

  if (exit_type_ == EXIT_OUTERMOST) {
    exit_type_ = EXIT_NONE;
  } else {
    if (nested_menu && result) {
      // We're nested and about to return a value. The caller might enter
      // another blocking loop. We need to make sure all menus are hidden
      // before that happens otherwise the menus will stay on screen.
      CloseAllNestedMenus();

      // Set exit_all_, which makes sure all nested loops exit immediately.
      if (exit_type_ != EXIT_DESTROYED)
        exit_type_ = EXIT_ALL;
    }
  }

  // If we stopped running because one of the menus was destroyed chances are
  // the button was also destroyed.
  if (exit_type_ != EXIT_DESTROYED && menu_button_) {
    menu_button_->SetState(CustomButton::BS_NORMAL);
    menu_button_->SchedulePaint();
  }

  return result;
}

void MenuController::Cancel(ExitType type) {
  if (!showing_) {
    // This occurs if we're in the process of notifying the delegate for a drop
    // and the delegate cancels us.
    return;
  }

  MenuItemView* selected = state_.item;
  exit_type_ = type;

  SendMouseCaptureLostToActiveView();

  // Hide windows immediately.
  SetSelection(NULL, SELECTION_UPDATE_IMMEDIATELY | SELECTION_EXIT);

  if (!blocking_run_) {
    // If we didn't block the caller we need to notify the menu, which
    // triggers deleting us.
    DCHECK(selected);
    showing_ = false;
    selected->GetRootMenuItem()->DropMenuClosed(true);
    // WARNING: the call to MenuClosed deletes us.
    return;
  }
}

void MenuController::OnMousePressed(SubmenuView* source,
                                    const MouseEvent& event) {
  if (!blocking_run_)
    return;

  DCHECK(!active_mouse_view_);

  MenuPart part = GetMenuPart(source, event.location());
  if (part.is_scroll())
    return;  // Ignore presses on scroll buttons.

  if (part.type == MenuPart::NONE ||
      (part.type == MenuPart::MENU_ITEM && part.menu &&
       part.menu->GetRootMenuItem() != state_.item->GetRootMenuItem())) {
    // Mouse wasn't pressed over any menu, or the active menu, cancel.

    // We're going to close and we own the mouse capture. We need to repost the
    // mouse down, otherwise the window the user clicked on won't get the
    // event.
#if defined(OS_WIN)
    RepostEvent(source, event);
    // NOTE: not reposting on linux seems fine.
#endif

    // And close.
    ExitType exit_type = EXIT_ALL;
    if (!menu_stack_.empty()) {
      // We're running nested menus. Only exit all if the mouse wasn't over one
      // of the menus from the last run.
      gfx::Point screen_loc(event.location());
      View::ConvertPointToScreen(source->GetScrollViewContainer(), &screen_loc);
      MenuPart last_part = GetMenuPartByScreenCoordinateUsingMenu(
          menu_stack_.back().item, screen_loc);
      if (last_part.type != MenuPart::NONE)
        exit_type = EXIT_OUTERMOST;
    }
    Cancel(exit_type);
    return;
  }

  // On a press we immediately commit the selection, that way a submenu
  // pops up immediately rather than after a delay.
  int selection_types = SELECTION_UPDATE_IMMEDIATELY;
  if (!part.menu) {
    part.menu = part.parent;
    selection_types |= SELECTION_OPEN_SUBMENU;
  } else {
    if (part.menu->GetDelegate()->CanDrag(part.menu)) {
      possible_drag_ = true;
      press_pt_ = event.location();
    }
    if (part.menu->HasSubmenu())
      selection_types |= SELECTION_OPEN_SUBMENU;
  }
  SetSelection(part.menu, selection_types);
}

void MenuController::OnMouseDragged(SubmenuView* source,
                                    const MouseEvent& event) {
  MenuPart part = GetMenuPart(source, event.location());
  UpdateScrolling(part);

  if (!blocking_run_)
    return;

  if (possible_drag_) {
    if (View::ExceededDragThreshold(event.x() - press_pt_.x(),
                                    event.y() - press_pt_.y())) {
      MenuItemView* item = state_.item;
      DCHECK(item);
      // Points are in the coordinates of the submenu, need to map to that of
      // the selected item. Additionally source may not be the parent of
      // the selected item, so need to map to screen first then to item.
      gfx::Point press_loc(press_pt_);
      View::ConvertPointToScreen(source->GetScrollViewContainer(), &press_loc);
      View::ConvertPointToView(NULL, item, &press_loc);
      // TODO(beng): Convert to CanvasSkia
      gfx::CanvasSkia canvas(item->width(), item->height(), false);
      item->PaintButton(&canvas, MenuItemView::PB_FOR_DRAG);

      OSExchangeData data;
      item->GetDelegate()->WriteDragData(item, &data);
      drag_utils::SetDragImageOnDataObject(canvas, item->size(), press_loc,
                                           &data);
      StopScrolling();
      int drag_ops = item->GetDelegate()->GetDragOperations(item);
      drag_in_progress_ = true;
      item->GetWidget()->RunShellDrag(NULL, data, drag_ops);
      drag_in_progress_ = false;

      if (GetActiveInstance() == this) {
        if (showing_) {
          // We're still showing, close all menus.
          CloseAllNestedMenus();
          Cancel(EXIT_ALL);
        }  // else case, drop was on us.
      }  // else case, someone canceled us, don't do anything
    }
    return;
  }
  MenuItemView* mouse_menu = NULL;
  if (part.type == MenuPart::MENU_ITEM) {
    if (!part.menu)
      part.menu = source->GetMenuItem();
    else
      mouse_menu = part.menu;
    SetSelection(part.menu ? part.menu : state_.item, SELECTION_OPEN_SUBMENU);
  } else if (part.type == MenuPart::NONE) {
    ShowSiblingMenu(source, event);
  }
  UpdateActiveMouseView(source, event, mouse_menu);
}

void MenuController::OnMouseReleased(SubmenuView* source,
                                     const MouseEvent& event) {
  if (!blocking_run_)
    return;

  DCHECK(state_.item);
  possible_drag_ = false;
  DCHECK(blocking_run_);
  MenuPart part = GetMenuPart(source, event.location());
  if (event.IsRightMouseButton() && (part.type == MenuPart::MENU_ITEM &&
                                     part.menu)) {
    // Set the selection immediately, making sure the submenu is only open
    // if it already was.
    int selection_types = SELECTION_UPDATE_IMMEDIATELY;
    if (state_.item == pending_state_.item && state_.submenu_open)
      selection_types |= SELECTION_OPEN_SUBMENU;
    SetSelection(pending_state_.item, selection_types);
    gfx::Point loc(event.location());
    View::ConvertPointToScreen(source->GetScrollViewContainer(), &loc);

    // If we open a context menu just return now
    if (part.menu->GetDelegate()->ShowContextMenu(
            part.menu, part.menu->GetCommand(), loc, true)) {
      SendMouseCaptureLostToActiveView();
      return;
    }
  }

  // We can use Ctrl+click or the middle mouse button to recursively open urls
  // for selected folder menu items. If it's only a left click, show the
  // contents of the folder.
  if (!part.is_scroll() && part.menu &&
      !(part.menu->HasSubmenu() &&
        (event.flags() == ui::EF_LEFT_BUTTON_DOWN))) {
    if (active_mouse_view_) {
      SendMouseReleaseToActiveView(source, event);
      return;
    }
    if (part.menu->GetDelegate()->IsTriggerableEvent(event)) {
      Accept(part.menu, event.flags());
      return;
    }
  } else if (part.type == MenuPart::MENU_ITEM) {
    // User either clicked on empty space, or a menu that has children.
    SetSelection(part.menu ? part.menu : state_.item,
                 SELECTION_OPEN_SUBMENU | SELECTION_UPDATE_IMMEDIATELY);
  }
  SendMouseCaptureLostToActiveView();
}

void MenuController::OnMouseMoved(SubmenuView* source,
                                  const MouseEvent& event) {
  if (showing_submenu_)
    return;

  MenuPart part = GetMenuPart(source, event.location());

  UpdateScrolling(part);

  if (!blocking_run_)
    return;

  if (part.type == MenuPart::NONE && ShowSiblingMenu(source, event))
    return;

  if (part.type == MenuPart::MENU_ITEM && part.menu) {
    SetSelection(part.menu, SELECTION_OPEN_SUBMENU);
  } else if (!part.is_scroll() && pending_state_.item &&
             pending_state_.item->GetParentMenuItem() &&
             (!pending_state_.item->HasSubmenu() ||
              !pending_state_.item->GetSubmenu()->IsShowing())) {
    // On exit if the user hasn't selected an item with a submenu, move the
    // selection back to the parent menu item.
    SetSelection(pending_state_.item->GetParentMenuItem(),
                 SELECTION_OPEN_SUBMENU);
  }
}

void MenuController::OnMouseEntered(SubmenuView* source,
                                    const MouseEvent& event) {
  // MouseEntered is always followed by a mouse moved, so don't need to
  // do anything here.
}

#if defined(OS_LINUX)
bool MenuController::OnMouseWheel(SubmenuView* source,
                                  const MouseWheelEvent& event) {
  MenuPart part = GetMenuPart(source, event.location());
  return part.submenu && part.submenu->OnMouseWheel(event);
}
#endif

bool MenuController::GetDropFormats(
      SubmenuView* source,
      int* formats,
      std::set<OSExchangeData::CustomFormat>* custom_formats) {
  return source->GetMenuItem()->GetDelegate()->GetDropFormats(
      source->GetMenuItem(), formats, custom_formats);
}

bool MenuController::AreDropTypesRequired(SubmenuView* source) {
  return source->GetMenuItem()->GetDelegate()->AreDropTypesRequired(
      source->GetMenuItem());
}

bool MenuController::CanDrop(SubmenuView* source, const OSExchangeData& data) {
  return source->GetMenuItem()->GetDelegate()->CanDrop(source->GetMenuItem(),
                                                       data);
}

void MenuController::OnDragEntered(SubmenuView* source,
                                   const DropTargetEvent& event) {
  valid_drop_coordinates_ = false;
}

int MenuController::OnDragUpdated(SubmenuView* source,
                                  const DropTargetEvent& event) {
  StopCancelAllTimer();

  gfx::Point screen_loc(event.location());
  View::ConvertPointToScreen(source, &screen_loc);
  if (valid_drop_coordinates_ && screen_loc == drop_pt_)
    return last_drop_operation_;
  drop_pt_ = screen_loc;
  valid_drop_coordinates_ = true;

  MenuItemView* menu_item = GetMenuItemAt(source, event.x(), event.y());
  bool over_empty_menu = false;
  if (!menu_item) {
    // See if we're over an empty menu.
    menu_item = GetEmptyMenuItemAt(source, event.x(), event.y());
    if (menu_item)
      over_empty_menu = true;
  }
  MenuDelegate::DropPosition drop_position = MenuDelegate::DROP_NONE;
  int drop_operation = ui::DragDropTypes::DRAG_NONE;
  if (menu_item) {
    gfx::Point menu_item_loc(event.location());
    View::ConvertPointToView(source, menu_item, &menu_item_loc);
    MenuItemView* query_menu_item;
    if (!over_empty_menu) {
      int menu_item_height = menu_item->height();
      if (menu_item->HasSubmenu() &&
          (menu_item_loc.y() > kDropBetweenPixels &&
           menu_item_loc.y() < (menu_item_height - kDropBetweenPixels))) {
        drop_position = MenuDelegate::DROP_ON;
      } else {
        drop_position = (menu_item_loc.y() < menu_item_height / 2) ?
            MenuDelegate::DROP_BEFORE : MenuDelegate::DROP_AFTER;
      }
      query_menu_item = menu_item;
    } else {
      query_menu_item = menu_item->GetParentMenuItem();
      drop_position = MenuDelegate::DROP_ON;
    }
    drop_operation = menu_item->GetDelegate()->GetDropOperation(
        query_menu_item, event, &drop_position);

    // If the menu has a submenu, schedule the submenu to open.
    SetSelection(menu_item, menu_item->HasSubmenu() ? SELECTION_OPEN_SUBMENU :
                 SELECTION_DEFAULT);

    if (drop_position == MenuDelegate::DROP_NONE ||
        drop_operation == ui::DragDropTypes::DRAG_NONE)
      menu_item = NULL;
  } else {
    SetSelection(source->GetMenuItem(), SELECTION_OPEN_SUBMENU);
  }
  SetDropMenuItem(menu_item, drop_position);
  last_drop_operation_ = drop_operation;
  return drop_operation;
}

void MenuController::OnDragExited(SubmenuView* source) {
  StartCancelAllTimer();

  if (drop_target_) {
    StopShowTimer();
    SetDropMenuItem(NULL, MenuDelegate::DROP_NONE);
  }
}

int MenuController::OnPerformDrop(SubmenuView* source,
                                  const DropTargetEvent& event) {
  DCHECK(drop_target_);
  // NOTE: the delegate may delete us after invoking OnPerformDrop, as such
  // we don't call cancel here.

  MenuItemView* item = state_.item;
  DCHECK(item);

  MenuItemView* drop_target = drop_target_;
  MenuDelegate::DropPosition drop_position = drop_position_;

  // Close all menus, including any nested menus.
  SetSelection(NULL, SELECTION_UPDATE_IMMEDIATELY | SELECTION_EXIT);
  CloseAllNestedMenus();

  // Set state such that we exit.
  showing_ = false;
  exit_type_ = EXIT_ALL;

  if (!IsBlockingRun())
    item->GetRootMenuItem()->DropMenuClosed(false);

  // WARNING: the call to MenuClosed deletes us.

  // If over an empty menu item, drop occurs on the parent.
  if (drop_target->GetID() == MenuItemView::kEmptyMenuItemViewID)
    drop_target = drop_target->GetParentMenuItem();

  return drop_target->GetDelegate()->OnPerformDrop(
      drop_target, drop_position, event);
}

void MenuController::OnDragEnteredScrollButton(SubmenuView* source,
                                               bool is_up) {
  MenuPart part;
  part.type = is_up ? MenuPart::SCROLL_UP : MenuPart::SCROLL_DOWN;
  part.submenu = source;
  UpdateScrolling(part);

  // Do this to force the selection to hide.
  SetDropMenuItem(source->GetMenuItemAt(0), MenuDelegate::DROP_NONE);

  StopCancelAllTimer();
}

void MenuController::OnDragExitedScrollButton(SubmenuView* source) {
  StartCancelAllTimer();
  SetDropMenuItem(NULL, MenuDelegate::DROP_NONE);
  StopScrolling();
}

void MenuController::SetSelection(MenuItemView* menu_item,
                                  int selection_types) {
  size_t paths_differ_at = 0;
  std::vector<MenuItemView*> current_path;
  std::vector<MenuItemView*> new_path;
  BuildPathsAndCalculateDiff(pending_state_.item, menu_item, &current_path,
                             &new_path, &paths_differ_at);

  size_t current_size = current_path.size();
  size_t new_size = new_path.size();

  if (pending_state_.item != menu_item && pending_state_.item) {
    View* current_hot_view = GetFirstHotTrackedView(pending_state_.item);
    if (current_hot_view)
      current_hot_view->SetHotTracked(false);
  }

  // Notify the old path it isn't selected.
  for (size_t i = paths_differ_at; i < current_size; ++i)
    current_path[i]->SetSelected(false);

  // Notify the new path it is selected.
  for (size_t i = paths_differ_at; i < new_size; ++i)
    new_path[i]->SetSelected(true);

  if (menu_item && menu_item->GetDelegate())
    menu_item->GetDelegate()->SelectionChanged(menu_item);

  DCHECK(menu_item || (selection_types & SELECTION_EXIT) != 0);

  pending_state_.item = menu_item;
  pending_state_.submenu_open = (selection_types & SELECTION_OPEN_SUBMENU) != 0;

  // Stop timers.
  StopShowTimer();
  StopCancelAllTimer();

  if (selection_types & SELECTION_UPDATE_IMMEDIATELY)
    CommitPendingSelection();
  else
    StartShowTimer();

  // Notify an accessibility focus event on all menu items except for the root.
  if (menu_item &&
      (MenuDepth(menu_item) != 1 ||
       menu_item->GetType() != MenuItemView::SUBMENU)) {
    menu_item->GetWidget()->NotifyAccessibilityEvent(
        menu_item, ui::AccessibilityTypes::EVENT_FOCUS, true);
  }
}

// static
void MenuController::SetActiveInstance(MenuController* controller) {
  active_instance_ = controller;
}

#if defined(OS_WIN)
bool MenuController::Dispatch(const MSG& msg) {
  DCHECK(blocking_run_);

  if (exit_type_ == EXIT_ALL || exit_type_ == EXIT_DESTROYED) {
    // We must translate/dispatch the message here, otherwise we would drop
    // the message on the floor.
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    return false;
  }

  // NOTE: we don't get WM_ACTIVATE or anything else interesting in here.
  switch (msg.message) {
    case WM_CONTEXTMENU: {
      MenuItemView* item = pending_state_.item;
      if (item && item->GetRootMenuItem() != item) {
        gfx::Point screen_loc(0, item->height());
        View::ConvertPointToScreen(item, &screen_loc);
        item->GetDelegate()->ShowContextMenu(item, item->GetCommand(),
                                             screen_loc, false);
      }
      return true;
    }

    // NOTE: focus wasn't changed when the menu was shown. As such, don't
    // dispatch key events otherwise the focused window will get the events.
    case WM_KEYDOWN:
      return OnKeyDown(msg.wParam, msg);

    case WM_CHAR:
      return !SelectByChar(static_cast<wchar_t>(msg.wParam));

    case WM_KEYUP:
      return true;

    case WM_SYSKEYUP:
      // We may have been shown on a system key, as such don't do anything
      // here. If another system key is pushed we'll get a WM_SYSKEYDOWN and
      // close the menu.
      return true;

    case WM_CANCELMODE:
    case WM_SYSKEYDOWN:
      // Exit immediately on system keys.
      Cancel(EXIT_ALL);
      return false;

    default:
      break;
  }
  TranslateMessage(&msg);
  DispatchMessage(&msg);
  return exit_type_ == EXIT_NONE;
}

#else
bool MenuController::Dispatch(GdkEvent* event) {
  if (exit_type_ == EXIT_ALL || exit_type_ == EXIT_DESTROYED) {
    gtk_main_do_event(event);
    return false;
  }

  switch (event->type) {
    case GDK_KEY_PRESS: {
      ui::KeyboardCode win_keycode =
          ui::WindowsKeyCodeForGdkKeyCode(event->key.keyval);

      if (!OnKeyDown(win_keycode))
        return false;

      // OnKeyDown may have set exit_type_.
      if (exit_type_ != EXIT_NONE)
        return false;

      guint32 keycode = gdk_keyval_to_unicode(event->key.keyval);
      if (keycode)
        return !SelectByChar(keycode);
      return true;
    }

    case GDK_KEY_RELEASE:
      return true;

    default:
      break;
  }

  // We don't want Gtk to handle keyboard events, otherwise if they get
  // handled by Gtk, unexpected behavior may occur. For example Tab key
  // may cause unexpected focus traversing.
  gtk_main_do_event(event);
  return exit_type_ == EXIT_NONE;
}

#if defined(TOUCH_UI)
base::MessagePumpGlibXDispatcher::DispatchStatus
    MenuController::DispatchX(XEvent* xev) {
  if (!DispatchXEvent(xev))
    return EVENT_IGNORED;

  return exit_type_ != EXIT_NONE ?
      base::MessagePumpGlibXDispatcher::EVENT_QUIT :
      base::MessagePumpGlibXDispatcher::EVENT_PROCESSED;
}
#endif

#endif

bool MenuController::OnKeyDown(int key_code
#if defined(OS_WIN)
                               , const MSG& msg
#else
#endif
                               ) {
  DCHECK(blocking_run_);

  switch (key_code) {
    case ui::VKEY_UP:
      IncrementSelection(-1);
      break;

    case ui::VKEY_DOWN:
      IncrementSelection(1);
      break;

    // Handling of VK_RIGHT and VK_LEFT is different depending on the UI
    // layout.
    case ui::VKEY_RIGHT:
      if (base::i18n::IsRTL())
        CloseSubmenu();
      else
        OpenSubmenuChangeSelectionIfCan();
      break;

    case ui::VKEY_LEFT:
      if (base::i18n::IsRTL())
        OpenSubmenuChangeSelectionIfCan();
      else
        CloseSubmenu();
      break;

    case ui::VKEY_SPACE:
      SendAcceleratorToHotTrackedView();
      break;

    case ui::VKEY_RETURN:
      if (pending_state_.item) {
        if (pending_state_.item->HasSubmenu()) {
          OpenSubmenuChangeSelectionIfCan();
        } else if (!SendAcceleratorToHotTrackedView() &&
                   pending_state_.item->IsEnabled()) {
          Accept(pending_state_.item, 0);
          return false;
        }
      }
      break;

    case ui::VKEY_ESCAPE:
      if (!state_.item->GetParentMenuItem() ||
          (!state_.item->GetParentMenuItem()->GetParentMenuItem() &&
           (!state_.item->HasSubmenu() ||
            !state_.item->GetSubmenu()->IsShowing()))) {
        // User pressed escape and only one menu is shown, cancel it.
        Cancel(EXIT_OUTERMOST);
        return false;
      } else {
        CloseSubmenu();
      }
      break;

#if defined(OS_WIN)
    case VK_APPS:
      break;
#endif

    default:
#if defined(OS_WIN)
      TranslateMessage(&msg);
#endif
      break;
  }
  return true;
}

MenuController::MenuController(bool blocking)
    : blocking_run_(blocking),
      showing_(false),
      exit_type_(EXIT_NONE),
      did_capture_(false),
      result_(NULL),
      result_mouse_event_flags_(0),
      drop_target_(NULL),
      owner_(NULL),
      possible_drag_(false),
      drag_in_progress_(false),
      valid_drop_coordinates_(false),
      showing_submenu_(false),
      menu_button_(NULL),
      active_mouse_view_(NULL) {
}

MenuController::~MenuController() {
  DCHECK(!showing_);
  StopShowTimer();
  StopCancelAllTimer();
}

bool MenuController::SendAcceleratorToHotTrackedView() {
  View* hot_view = GetFirstHotTrackedView(pending_state_.item);
  if (!hot_view)
    return false;

  Accelerator accelerator(ui::VKEY_RETURN, false, false, false);
  hot_view->AcceleratorPressed(accelerator);
  hot_view->SetHotTracked(true);
  return true;
}

void MenuController::UpdateInitialLocation(
    const gfx::Rect& bounds,
    MenuItemView::AnchorPosition position) {
  pending_state_.initial_bounds = bounds;
  if (bounds.height() > 1) {
    // Inset the bounds slightly, otherwise drag coordinates don't line up
    // nicely and menus close prematurely.
    pending_state_.initial_bounds.Inset(0, 1);
  }
  pending_state_.anchor = position;

  // Calculate the bounds of the monitor we'll show menus on. Do this once to
  // avoid repeated system queries for the info.
  pending_state_.monitor_bounds = Screen::GetMonitorWorkAreaNearestPoint(
      bounds.origin());
}

void MenuController::Accept(MenuItemView* item, int mouse_event_flags) {
  DCHECK(IsBlockingRun());
  result_ = item;
  if (item && !menu_stack_.empty() &&
      !item->GetDelegate()->ShouldCloseAllMenusOnExecute(item->GetCommand())) {
    exit_type_ = EXIT_OUTERMOST;
  } else {
    exit_type_ = EXIT_ALL;
  }
  result_mouse_event_flags_ = mouse_event_flags;
}

bool MenuController::ShowSiblingMenu(SubmenuView* source,
                                     const MouseEvent& event) {
  if (!menu_stack_.empty() || !menu_button_)
    return false;

  View* source_view = source->GetScrollViewContainer();
  if (event.x() >= 0 && event.x() < source_view->width() && event.y() >= 0 &&
      event.y() < source_view->height()) {
    // The mouse is over the menu, no need to continue.
    return false;
  }

  gfx::NativeWindow window_under_mouse = Screen::GetWindowAtCursorScreenPoint();
  if (window_under_mouse != owner_)
    return false;

  // The user moved the mouse outside the menu and over the owning window. See
  // if there is a sibling menu we should show.
  gfx::Point screen_point(event.location());
  View::ConvertPointToScreen(source_view, &screen_point);
  MenuItemView::AnchorPosition anchor;
  bool has_mnemonics;
  MenuButton* button = NULL;
  MenuItemView* alt_menu = source->GetMenuItem()->GetDelegate()->
      GetSiblingMenu(source->GetMenuItem()->GetRootMenuItem(),
                     screen_point, &anchor, &has_mnemonics, &button);
  if (!alt_menu || alt_menu == state_.item)
    return false;

  if (!button) {
    // If the delegate returns a menu, they must also return a button.
    NOTREACHED();
    return false;
  }

  // There is a sibling menu, update the button state, hide the current menu
  // and show the new one.
  menu_button_->SetState(CustomButton::BS_NORMAL);
  menu_button_->SchedulePaint();
  menu_button_ = button;
  menu_button_->SetState(CustomButton::BS_PUSHED);
  menu_button_->SchedulePaint();

  // Need to reset capture when we show the menu again, otherwise we aren't
  // going to get any events.
  did_capture_ = false;
  gfx::Point screen_menu_loc;
  View::ConvertPointToScreen(button, &screen_menu_loc);
  // Subtract 1 from the height to make the popup flush with the button border.
  UpdateInitialLocation(gfx::Rect(screen_menu_loc.x(), screen_menu_loc.y(),
                                  button->width(), button->height() - 1),
                        anchor);
  alt_menu->PrepareForRun(
      has_mnemonics,
      source->GetMenuItem()->GetRootMenuItem()->show_mnemonics_);
  alt_menu->controller_ = this;
  SetSelection(alt_menu, SELECTION_OPEN_SUBMENU | SELECTION_UPDATE_IMMEDIATELY);
  return true;
}

void MenuController::CloseAllNestedMenus() {
  for (std::list<State>::iterator i = menu_stack_.begin();
       i != menu_stack_.end(); ++i) {
    MenuItemView* item = i->item;
    MenuItemView* last_item = item;
    while (item) {
      CloseMenu(item);
      last_item = item;
      item = item->GetParentMenuItem();
    }
    i->submenu_open = false;
    i->item = last_item;
  }
}

MenuItemView* MenuController::GetMenuItemAt(View* source, int x, int y) {
  // Walk the view hierarchy until we find a menu item (or the root).
  View* child_under_mouse = source->GetEventHandlerForPoint(gfx::Point(x, y));
  while (child_under_mouse &&
         child_under_mouse->GetID() != MenuItemView::kMenuItemViewID) {
    child_under_mouse = child_under_mouse->parent();
  }
  if (child_under_mouse && child_under_mouse->IsEnabled() &&
      child_under_mouse->GetID() == MenuItemView::kMenuItemViewID) {
    return static_cast<MenuItemView*>(child_under_mouse);
  }
  return NULL;
}

MenuItemView* MenuController::GetEmptyMenuItemAt(View* source, int x, int y) {
  View* child_under_mouse = source->GetEventHandlerForPoint(gfx::Point(x, y));
  if (child_under_mouse &&
      child_under_mouse->GetID() == MenuItemView::kEmptyMenuItemViewID) {
    return static_cast<MenuItemView*>(child_under_mouse);
  }
  return NULL;
}

bool MenuController::IsScrollButtonAt(SubmenuView* source,
                                      int x,
                                      int y,
                                      MenuPart::Type* part) {
  MenuScrollViewContainer* scroll_view = source->GetScrollViewContainer();
  View* child_under_mouse =
      scroll_view->GetEventHandlerForPoint(gfx::Point(x, y));
  if (child_under_mouse && child_under_mouse->IsEnabled()) {
    if (child_under_mouse == scroll_view->scroll_up_button()) {
      *part = MenuPart::SCROLL_UP;
      return true;
    }
    if (child_under_mouse == scroll_view->scroll_down_button()) {
      *part = MenuPart::SCROLL_DOWN;
      return true;
    }
  }
  return false;
}

MenuController::MenuPart MenuController::GetMenuPart(
    SubmenuView* source,
    const gfx::Point& source_loc) {
  gfx::Point screen_loc(source_loc);
  View::ConvertPointToScreen(source->GetScrollViewContainer(), &screen_loc);
  return GetMenuPartByScreenCoordinateUsingMenu(state_.item, screen_loc);
}

MenuController::MenuPart MenuController::GetMenuPartByScreenCoordinateUsingMenu(
    MenuItemView* item,
    const gfx::Point& screen_loc) {
  MenuPart part;
  while (item) {
    if (item->HasSubmenu() && item->GetSubmenu()->IsShowing() &&
        GetMenuPartByScreenCoordinateImpl(
            item->GetSubmenu(), screen_loc, &part)) {
      return part;
    }
    item = item->GetParentMenuItem();
  }
  return part;
}

bool MenuController::GetMenuPartByScreenCoordinateImpl(
    SubmenuView* menu,
    const gfx::Point& screen_loc,
    MenuPart* part) {
  // Is the mouse over the scroll buttons?
  gfx::Point scroll_view_loc = screen_loc;
  View* scroll_view_container = menu->GetScrollViewContainer();
  View::ConvertPointToView(NULL, scroll_view_container, &scroll_view_loc);
  if (scroll_view_loc.x() < 0 ||
      scroll_view_loc.x() >= scroll_view_container->width() ||
      scroll_view_loc.y() < 0 ||
      scroll_view_loc.y() >= scroll_view_container->height()) {
    // Point isn't contained in menu.
    return false;
  }
  if (IsScrollButtonAt(menu, scroll_view_loc.x(), scroll_view_loc.y(),
                       &(part->type))) {
    part->submenu = menu;
    return true;
  }

  // Not over the scroll button. Check the actual menu.
  if (DoesSubmenuContainLocation(menu, screen_loc)) {
    gfx::Point menu_loc = screen_loc;
    View::ConvertPointToView(NULL, menu, &menu_loc);
    part->menu = GetMenuItemAt(menu, menu_loc.x(), menu_loc.y());
    part->type = MenuPart::MENU_ITEM;
    part->submenu = menu;
    if (!part->menu)
      part->parent = menu->GetMenuItem();
    return true;
  }

  // While the mouse isn't over a menu item or the scroll buttons of menu, it
  // is contained by menu and so we return true. If we didn't return true other
  // menus would be searched, even though they are likely obscured by us.
  return true;
}

bool MenuController::DoesSubmenuContainLocation(SubmenuView* submenu,
                                                const gfx::Point& screen_loc) {
  gfx::Point view_loc = screen_loc;
  View::ConvertPointToView(NULL, submenu, &view_loc);
  gfx::Rect vis_rect = submenu->GetVisibleBounds();
  return vis_rect.Contains(view_loc.x(), view_loc.y());
}

void MenuController::CommitPendingSelection() {
  StopShowTimer();

  size_t paths_differ_at = 0;
  std::vector<MenuItemView*> current_path;
  std::vector<MenuItemView*> new_path;
  BuildPathsAndCalculateDiff(state_.item, pending_state_.item, &current_path,
                             &new_path, &paths_differ_at);

  // Hide the old menu.
  for (size_t i = paths_differ_at; i < current_path.size(); ++i) {
    if (current_path[i]->HasSubmenu()) {
      current_path[i]->GetSubmenu()->Hide();
    }
  }

  // Copy pending to state_, making sure to preserve the direction menus were
  // opened.
  std::list<bool> pending_open_direction;
  state_.open_leading.swap(pending_open_direction);
  state_ = pending_state_;
  state_.open_leading.swap(pending_open_direction);

  int menu_depth = MenuDepth(state_.item);
  if (menu_depth == 0) {
    state_.open_leading.clear();
  } else {
    int cached_size = static_cast<int>(state_.open_leading.size());
    DCHECK_GE(menu_depth, 0);
    while (cached_size-- >= menu_depth)
      state_.open_leading.pop_back();
  }

  if (!state_.item) {
    // Nothing to select.
    StopScrolling();
    return;
  }

  // Open all the submenus preceeding the last menu item (last menu item is
  // handled next).
  if (new_path.size() > 1) {
    for (std::vector<MenuItemView*>::iterator i = new_path.begin();
         i != new_path.end() - 1; ++i) {
      OpenMenu(*i);
    }
  }

  if (state_.submenu_open) {
    // The submenu should be open, open the submenu if the item has a submenu.
    if (state_.item->HasSubmenu()) {
      OpenMenu(state_.item);
    } else {
      state_.submenu_open = false;
    }
  } else if (state_.item->HasSubmenu() &&
             state_.item->GetSubmenu()->IsShowing()) {
    state_.item->GetSubmenu()->Hide();
  }

  if (scroll_task_.get() && scroll_task_->submenu()) {
    // Stop the scrolling if none of the elements of the selection contain
    // the menu being scrolled.
    bool found = false;
    MenuItemView* item = state_.item;
    while (item && !found) {
      found = (item->HasSubmenu() && item->GetSubmenu()->IsShowing() &&
               item->GetSubmenu() == scroll_task_->submenu());
      item = item->GetParentMenuItem();
    }
    if (!found)
      StopScrolling();
  }
}

void MenuController::CloseMenu(MenuItemView* item) {
  DCHECK(item);
  if (!item->HasSubmenu())
    return;
  item->GetSubmenu()->Hide();
}

void MenuController::OpenMenu(MenuItemView* item) {
  DCHECK(item);
  if (item->GetSubmenu()->IsShowing()) {
    return;
  }

  OpenMenuImpl(item, true);
  did_capture_ = true;
}

void MenuController::OpenMenuImpl(MenuItemView* item, bool show) {
  bool prefer_leading =
      state_.open_leading.empty() ? true : state_.open_leading.back();
  bool resulting_direction;
  gfx::Rect bounds =
      CalculateMenuBounds(item, prefer_leading, &resulting_direction);
  state_.open_leading.push_back(resulting_direction);
  bool do_capture = (!did_capture_ && blocking_run_);
  showing_submenu_ = true;
  if (show)
    item->GetSubmenu()->ShowAt(owner_, bounds, do_capture);
  else
    item->GetSubmenu()->Reposition(bounds);
  showing_submenu_ = false;
}

void MenuController::MenuChildrenChanged(MenuItemView* item) {
  DCHECK(item);
  DCHECK(item->GetSubmenu()->IsShowing());

  // Currently this only supports adjusting the bounds of the last menu.
  DCHECK(item == state_.item->GetParentMenuItem());

  // Make sure the submenu isn't showing for the current item (the position may
  // have changed or the menu removed). This also moves the selection back to
  // the parent, which handles the case where the selected item was removed.
  SetSelection(state_.item->GetParentMenuItem(),
               SELECTION_OPEN_SUBMENU | SELECTION_UPDATE_IMMEDIATELY);

  OpenMenuImpl(item, false);
}

void MenuController::BuildPathsAndCalculateDiff(
    MenuItemView* old_item,
    MenuItemView* new_item,
    std::vector<MenuItemView*>* old_path,
    std::vector<MenuItemView*>* new_path,
    size_t* first_diff_at) {
  DCHECK(old_path && new_path && first_diff_at);
  BuildMenuItemPath(old_item, old_path);
  BuildMenuItemPath(new_item, new_path);

  size_t common_size = std::min(old_path->size(), new_path->size());

  // Find the first difference between the two paths, when the loop
  // returns, diff_i is the first index where the two paths differ.
  for (size_t i = 0; i < common_size; ++i) {
    if ((*old_path)[i] != (*new_path)[i]) {
      *first_diff_at = i;
      return;
    }
  }

  *first_diff_at = common_size;
}

void MenuController::BuildMenuItemPath(MenuItemView* item,
                                       std::vector<MenuItemView*>* path) {
  if (!item)
    return;
  BuildMenuItemPath(item->GetParentMenuItem(), path);
  path->push_back(item);
}

void MenuController::StartShowTimer() {
  show_timer_.Start(TimeDelta::FromMilliseconds(kShowDelay), this,
                    &MenuController::CommitPendingSelection);
}

void MenuController::StopShowTimer() {
  show_timer_.Stop();
}

void MenuController::StartCancelAllTimer() {
  cancel_all_timer_.Start(TimeDelta::FromMilliseconds(kCloseOnExitTime),
                          this, &MenuController::CancelAll);
}

void MenuController::StopCancelAllTimer() {
  cancel_all_timer_.Stop();
}

gfx::Rect MenuController::CalculateMenuBounds(MenuItemView* item,
                                              bool prefer_leading,
                                              bool* is_leading) {
  DCHECK(item);

  SubmenuView* submenu = item->GetSubmenu();
  DCHECK(submenu);

  gfx::Size pref = submenu->GetScrollViewContainer()->GetPreferredSize();

  // Don't let the menu go to wide.
  pref.set_width(std::min(pref.width(),
                          item->GetDelegate()->GetMaxWidthForMenu()));
  if (!state_.monitor_bounds.IsEmpty())
    pref.set_width(std::min(pref.width(), state_.monitor_bounds.width()));

  // Assume we can honor prefer_leading.
  *is_leading = prefer_leading;

  int x, y;

  if (!item->GetParentMenuItem()) {
    // First item, position relative to initial location.
    x = state_.initial_bounds.x();
    y = state_.initial_bounds.bottom();
    if (state_.anchor == MenuItemView::TOPRIGHT)
      x = x + state_.initial_bounds.width() - pref.width();
    if (!state_.monitor_bounds.IsEmpty() &&
        y + pref.height() > state_.monitor_bounds.bottom()) {
      // The menu doesn't fit on screen. If the first location is above the
      // half way point, show from the mouse location to bottom of screen.
      // Otherwise show from the top of the screen to the location of the mouse.
      // While odd, this behavior matches IE.
      if (y < (state_.monitor_bounds.y() +
               state_.monitor_bounds.height() / 2)) {
        pref.set_height(std::min(pref.height(),
                                 state_.monitor_bounds.bottom() - y));
      } else {
        pref.set_height(std::min(pref.height(),
            state_.initial_bounds.y() - state_.monitor_bounds.y()));
        y = state_.initial_bounds.y() - pref.height();
      }
    }
  } else {
    // Not the first menu; position it relative to the bounds of the menu
    // item.
    gfx::Point item_loc;
    View::ConvertPointToScreen(item, &item_loc);

    // We must make sure we take into account the UI layout. If the layout is
    // RTL, then a 'leading' menu is positioned to the left of the parent menu
    // item and not to the right.
    bool layout_is_rtl = base::i18n::IsRTL();
    bool create_on_the_right = (prefer_leading && !layout_is_rtl) ||
                               (!prefer_leading && layout_is_rtl);

    if (create_on_the_right) {
      x = item_loc.x() + item->width() - kSubmenuHorizontalInset;
      if (state_.monitor_bounds.width() != 0 &&
          x + pref.width() > state_.monitor_bounds.right()) {
        if (layout_is_rtl)
          *is_leading = true;
        else
          *is_leading = false;
        x = item_loc.x() - pref.width() + kSubmenuHorizontalInset;
      }
    } else {
      x = item_loc.x() - pref.width() + kSubmenuHorizontalInset;
      if (state_.monitor_bounds.width() != 0 && x < state_.monitor_bounds.x()) {
        if (layout_is_rtl)
          *is_leading = false;
        else
          *is_leading = true;
        x = item_loc.x() + item->width() - kSubmenuHorizontalInset;
      }
    }
    y = item_loc.y() - SubmenuView::kSubmenuBorderSize;
    if (state_.monitor_bounds.width() != 0) {
      pref.set_height(std::min(pref.height(), state_.monitor_bounds.height()));
      if (y + pref.height() > state_.monitor_bounds.bottom())
        y = state_.monitor_bounds.bottom() - pref.height();
      if (y < state_.monitor_bounds.y())
        y = state_.monitor_bounds.y();
    }
  }

  if (state_.monitor_bounds.width() != 0) {
    if (x + pref.width() > state_.monitor_bounds.right())
      x = state_.monitor_bounds.right() - pref.width();
    if (x < state_.monitor_bounds.x())
      x = state_.monitor_bounds.x();
  }
  return gfx::Rect(x, y, pref.width(), pref.height());
}

// static
int MenuController::MenuDepth(MenuItemView* item) {
  if (!item)
    return 0;
  return MenuDepth(item->GetParentMenuItem()) + 1;
}

void MenuController::IncrementSelection(int delta) {
  MenuItemView* item = pending_state_.item;
  DCHECK(item);
  if (pending_state_.submenu_open && item->HasSubmenu() &&
      item->GetSubmenu()->IsShowing()) {
    // A menu is selected and open, but none of its children are selected,
    // select the first menu item.
    if (item->GetSubmenu()->GetMenuItemCount()) {
      SetSelection(item->GetSubmenu()->GetMenuItemAt(0), SELECTION_DEFAULT);
      ScrollToVisible(item->GetSubmenu()->GetMenuItemAt(0));
      return;
    }
  }

  if (item->has_children()) {
    View* hot_view = GetFirstHotTrackedView(item);
    if (hot_view) {
      hot_view->SetHotTracked(false);
      View* to_make_hot = GetNextFocusableView(item, hot_view, delta == 1);
      if (to_make_hot) {
        to_make_hot->SetHotTracked(true);
        return;
      }
    } else {
      View* to_make_hot = GetInitialFocusableView(item, delta == 1);
      if (to_make_hot) {
        to_make_hot->SetHotTracked(true);
        return;
      }
    }
  }

  if (item->GetParentMenuItem()) {
    MenuItemView* parent = item->GetParentMenuItem();
    int parent_count = parent->GetSubmenu()->GetMenuItemCount();
    if (parent_count > 1) {
      for (int i = 0; i < parent_count; ++i) {
        if (parent->GetSubmenu()->GetMenuItemAt(i) == item) {
          MenuItemView* to_select =
              FindNextSelectableMenuItem(parent, i, delta);
          if (!to_select)
            break;
          ScrollToVisible(to_select);
          SetSelection(to_select, SELECTION_DEFAULT);
          View* to_make_hot = GetInitialFocusableView(to_select, delta == 1);
          if (to_make_hot)
            to_make_hot->SetHotTracked(true);
          break;
        }
      }
    }
  }
}

MenuItemView* MenuController::FindNextSelectableMenuItem(MenuItemView* parent,
                                                         int index,
                                                         int delta) {
  int start_index = index;
  int parent_count = parent->GetSubmenu()->GetMenuItemCount();
  // Loop through the menu items skipping any invisible menus. The loop stops
  // when we wrap or find a visible child.
  do {
    index = (index + delta + parent_count) % parent_count;
    if (index == start_index)
      return NULL;
    MenuItemView* child = parent->GetSubmenu()->GetMenuItemAt(index);
    if (child->IsVisible())
      return child;
  } while (index != start_index);
  return NULL;
}

void MenuController::OpenSubmenuChangeSelectionIfCan() {
  MenuItemView* item = pending_state_.item;
  if (item->HasSubmenu()) {
    if (item->GetSubmenu()->GetMenuItemCount() > 0) {
      SetSelection(item->GetSubmenu()->GetMenuItemAt(0),
                   SELECTION_UPDATE_IMMEDIATELY);
    } else {
      // No menu items, just show the sub-menu.
      SetSelection(item, SELECTION_OPEN_SUBMENU | SELECTION_UPDATE_IMMEDIATELY);
    }
  }
}

void MenuController::CloseSubmenu() {
  MenuItemView* item = state_.item;
  DCHECK(item);
  if (!item->GetParentMenuItem())
    return;
  if (item->HasSubmenu() && item->GetSubmenu()->IsShowing()) {
    SetSelection(item, SELECTION_UPDATE_IMMEDIATELY);
  } else if (item->GetParentMenuItem()->GetParentMenuItem()) {
    SetSelection(item->GetParentMenuItem(), SELECTION_UPDATE_IMMEDIATELY);
  }
}

MenuController::SelectByCharDetails MenuController::FindChildForMnemonic(
    MenuItemView* parent,
    wchar_t key,
    bool (*match_function)(MenuItemView* menu, wchar_t mnemonic)) {
  SubmenuView* submenu = parent->GetSubmenu();
  DCHECK(submenu);
  SelectByCharDetails details;

  for (int i = 0, menu_item_count = submenu->GetMenuItemCount();
       i < menu_item_count; ++i) {
    MenuItemView* child = submenu->GetMenuItemAt(i);
    if (child->IsEnabled() && child->IsVisible()) {
      if (child == pending_state_.item)
        details.index_of_item = i;
      if (match_function(child, key)) {
        if (details.first_match == -1)
          details.first_match = i;
        else
          details.has_multiple = true;
        if (details.next_match == -1 && details.index_of_item != -1 &&
            i > details.index_of_item)
          details.next_match = i;
      }
    }
  }
  return details;
}

bool MenuController::AcceptOrSelect(MenuItemView* parent,
                                    const SelectByCharDetails& details) {
  // This should only be invoked if there is a match.
  DCHECK(details.first_match != -1);
  DCHECK(parent->HasSubmenu());
  SubmenuView* submenu = parent->GetSubmenu();
  DCHECK(submenu);
  if (!details.has_multiple) {
    // There's only one match, activate it (or open if it has a submenu).
    if (submenu->GetMenuItemAt(details.first_match)->HasSubmenu()) {
      SetSelection(submenu->GetMenuItemAt(details.first_match),
                   SELECTION_OPEN_SUBMENU);
    } else {
      Accept(submenu->GetMenuItemAt(details.first_match), 0);
      return true;
    }
  } else if (details.index_of_item == -1 || details.next_match == -1) {
    SetSelection(submenu->GetMenuItemAt(details.first_match),
                 SELECTION_DEFAULT);
  } else {
    SetSelection(submenu->GetMenuItemAt(details.next_match),
                 SELECTION_DEFAULT);
  }
  return false;
}

bool MenuController::SelectByChar(wchar_t character) {
  wchar_t char_array[1] = { character };
  wchar_t key = UTF16ToWide(l10n_util::ToLower(WideToUTF16(char_array)))[0];
  MenuItemView* item = pending_state_.item;
  if (!item->HasSubmenu() || !item->GetSubmenu()->IsShowing())
    item = item->GetParentMenuItem();
  DCHECK(item);
  DCHECK(item->HasSubmenu());
  DCHECK(item->GetSubmenu());
  if (item->GetSubmenu()->GetMenuItemCount() == 0)
    return false;

  // Look for matches based on mnemonic first.
  SelectByCharDetails details =
      FindChildForMnemonic(item, key, &MatchesMnemonic);
  if (details.first_match != -1)
    return AcceptOrSelect(item, details);

  if (item->GetRootMenuItem()->has_mnemonics()) {
    // Don't guess at mnemonics if the menu explicitly has them.
    return false;
  }

  // If no mnemonics found, look at first character of titles.
  details = FindChildForMnemonic(item, key, &TitleMatchesMnemonic);
  if (details.first_match != -1)
    return AcceptOrSelect(item, details);

  return false;
}

#if defined(OS_WIN)
void MenuController::RepostEvent(SubmenuView* source,
                                 const MouseEvent& event) {
  if (!state_.item) {
    // We some times get an event after closing all the menus. Ignore it.
    // Make sure the menu is in fact not visible. If the menu is visible, then
    // we're in a bad state where we think the menu isn't visibile but it is.
    DCHECK(!source->GetWidget()->IsVisible());
    return;
  }

  gfx::Point screen_loc(event.location());
  View::ConvertPointToScreen(source->GetScrollViewContainer(), &screen_loc);
  HWND window = WindowFromPoint(screen_loc.ToPOINT());
  if (window) {
    // Release the capture.
    SubmenuView* submenu = state_.item->GetRootMenuItem()->GetSubmenu();
    submenu->ReleaseCapture();

    if (submenu->GetWidget()->GetNativeView() &&
        GetWindowThreadProcessId(submenu->GetWidget()->GetNativeView(), NULL) !=
        GetWindowThreadProcessId(window, NULL)) {
      // Even though we have mouse capture, windows generates a mouse event
      // if the other window is in a separate thread. Don't generate an event in
      // this case else the target window can get double events leading to bad
      // behavior.
      return;
    }

    // Convert the coordinates to the target window.
    RECT window_bounds;
    GetWindowRect(window, &window_bounds);
    int window_x = screen_loc.x() - window_bounds.left;
    int window_y = screen_loc.y() - window_bounds.top;

    // Determine whether the click was in the client area or not.
    // NOTE: WM_NCHITTEST coordinates are relative to the screen.
    LRESULT nc_hit_result = SendMessage(window, WM_NCHITTEST, 0,
                                        MAKELPARAM(screen_loc.x(),
                                                   screen_loc.y()));
    const bool in_client_area = (nc_hit_result == HTCLIENT);

    // TODO(sky): this isn't right. The event to generate should correspond
    // with the event we just got. MouseEvent only tells us what is down,
    // which may differ. Need to add ability to get changed button from
    // MouseEvent.
    int event_type;
    if (event.IsLeftMouseButton())
      event_type = in_client_area ? WM_LBUTTONDOWN : WM_NCLBUTTONDOWN;
    else if (event.IsMiddleMouseButton())
      event_type = in_client_area ? WM_MBUTTONDOWN : WM_NCMBUTTONDOWN;
    else if (event.IsRightMouseButton())
      event_type = in_client_area ? WM_RBUTTONDOWN : WM_NCRBUTTONDOWN;
    else
      event_type = 0;  // Unknown mouse press.

    if (event_type) {
      if (in_client_area) {
        PostMessage(window, event_type, event.GetWindowsFlags(),
                    MAKELPARAM(window_x, window_y));
      } else {
        PostMessage(window, event_type, nc_hit_result,
                    MAKELPARAM(screen_loc.x(), screen_loc.y()));
      }
    }
  }
}
#endif

void MenuController::SetDropMenuItem(
    MenuItemView* new_target,
    MenuDelegate::DropPosition new_position) {
  if (new_target == drop_target_ && new_position == drop_position_)
    return;

  if (drop_target_) {
    drop_target_->GetParentMenuItem()->GetSubmenu()->SetDropMenuItem(
        NULL, MenuDelegate::DROP_NONE);
  }
  drop_target_ = new_target;
  drop_position_ = new_position;
  if (drop_target_) {
    drop_target_->GetParentMenuItem()->GetSubmenu()->SetDropMenuItem(
        drop_target_, drop_position_);
  }
}

void MenuController::UpdateScrolling(const MenuPart& part) {
  if (!part.is_scroll() && !scroll_task_.get())
    return;

  if (!scroll_task_.get())
    scroll_task_.reset(new MenuScrollTask());
  scroll_task_->Update(part);
}

void MenuController::StopScrolling() {
  scroll_task_.reset(NULL);
}

void MenuController::UpdateActiveMouseView(SubmenuView* event_source,
                                           const MouseEvent& event,
                                           View* target_menu) {
  View* target = NULL;
  gfx::Point target_menu_loc(event.location());
  if (target_menu && target_menu->has_children()) {
    // Locate the deepest child view to send events to.  This code assumes we
    // don't have to walk up the tree to find a view interested in events. This
    // is currently true for the cases we are embedding views, but if we embed
    // more complex hierarchies it'll need to change.
    View::ConvertPointToScreen(event_source->GetScrollViewContainer(),
                               &target_menu_loc);
    View::ConvertPointToView(NULL, target_menu, &target_menu_loc);
    target = target_menu->GetEventHandlerForPoint(target_menu_loc);
    if (target == target_menu || !target->IsEnabled())
      target = NULL;
  }
  if (target != active_mouse_view_) {
    SendMouseCaptureLostToActiveView();
    active_mouse_view_ = target;
    if (active_mouse_view_) {
      gfx::Point target_point(target_menu_loc);
      View::ConvertPointToView(target_menu, active_mouse_view_, &target_point);
      MouseEvent mouse_entered_event(ui::ET_MOUSE_ENTERED,
                                     target_point.x(), target_point.y(), 0);
      active_mouse_view_->OnMouseEntered(mouse_entered_event);

      MouseEvent mouse_pressed_event(ui::ET_MOUSE_PRESSED,
                                     target_point.x(), target_point.y(),
                                     event.flags());
      active_mouse_view_->OnMousePressed(mouse_pressed_event);
    }
  }

  if (active_mouse_view_) {
    gfx::Point target_point(target_menu_loc);
    View::ConvertPointToView(target_menu, active_mouse_view_, &target_point);
    MouseEvent mouse_dragged_event(ui::ET_MOUSE_DRAGGED,
                                   target_point.x(), target_point.y(),
                                   event.flags());
    active_mouse_view_->OnMouseDragged(mouse_dragged_event);
  }
}

void MenuController::SendMouseReleaseToActiveView(SubmenuView* event_source,
                                                  const MouseEvent& event) {
  if (!active_mouse_view_)
    return;

  gfx::Point target_loc(event.location());
  View::ConvertPointToScreen(event_source->GetScrollViewContainer(),
                             &target_loc);
  View::ConvertPointToView(NULL, active_mouse_view_, &target_loc);
  MouseEvent release_event(ui::ET_MOUSE_RELEASED, target_loc.x(),
                           target_loc.y(), event.flags());
  // Reset the active_mouse_view_ before sending mouse released. That way if it
  // calls back to us, we aren't in a weird state.
  View* active_view = active_mouse_view_;
  active_mouse_view_ = NULL;
  active_view->OnMouseReleased(release_event);
}

void MenuController::SendMouseCaptureLostToActiveView() {
  if (!active_mouse_view_)
    return;

  // Reset the active_mouse_view_ before sending mouse capture lost. That way if
  // it calls back to us, we aren't in a weird state.
  View* active_view = active_mouse_view_;
  active_mouse_view_ = NULL;
  active_view->OnMouseCaptureLost();
}

}  // namespace views
