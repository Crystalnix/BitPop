// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/touch/frame/touch_browser_frame_view.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_host/render_widget_host_view_views.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/touch/frame/keyboard_container_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tab_contents/tab_contents_view_touch.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/navigation_controller.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/tab_contents_view.h"
#include "content/common/notification_service.h"
#include "content/common/notification_type.h"
#include "ui/base/animation/slide_animation.h"
#include "ui/gfx/rect.h"
#include "views/controls/button/image_button.h"
#include "views/controls/textfield/textfield.h"
#include "views/focus/focus_manager.h"

namespace {

const int kKeyboardHeight = 300;
const int kKeyboardSlideDuration = 500;  // In milliseconds

PropertyAccessor<bool>* GetFocusedStateAccessor() {
  static PropertyAccessor<bool> state;
  return &state;
}

bool TabContentsHasFocus(const TabContents* contents) {
  views::View* view = static_cast<TabContentsViewTouch*>(contents->view());
  return view->Contains(view->GetFocusManager()->GetFocusedView());
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// TouchBrowserFrameView, public:

TouchBrowserFrameView::TouchBrowserFrameView(BrowserFrame* frame,
                                             BrowserView* browser_view)
    : OpaqueBrowserFrameView(frame, browser_view),
      keyboard_showing_(false),
      focus_listener_added_(false),
      keyboard_(NULL) {
  registrar_.Add(this,
                 NotificationType::NAV_ENTRY_COMMITTED,
                 NotificationService::AllSources());
  registrar_.Add(this,
                 NotificationType::FOCUS_CHANGED_IN_PAGE,
                 NotificationService::AllSources());
  registrar_.Add(this,
                 NotificationType::TAB_CONTENTS_DESTROYED,
                 NotificationService::AllSources());

  browser_view->browser()->tabstrip_model()->AddObserver(this);

  animation_.reset(new ui::SlideAnimation(this));
  animation_->SetTweenType(ui::Tween::LINEAR);
  animation_->SetSlideDuration(kKeyboardSlideDuration);
}

TouchBrowserFrameView::~TouchBrowserFrameView() {
  browser_view()->browser()->tabstrip_model()->RemoveObserver(this);
}

void TouchBrowserFrameView::Layout() {
  OpaqueBrowserFrameView::Layout();

  if (!keyboard_)
    return;

  keyboard_->SetVisible(keyboard_showing_ || animation_->is_animating());
  gfx::Rect bounds = GetBoundsForReservedArea();
  if (animation_->is_animating() && !keyboard_showing_) {
    // The keyboard is in the process of hiding. So pretend it still has the
    // same bounds as when the keyboard is visible. But
    // |GetBoundsForReservedArea| should not take this into account so that the
    // render view gets the entire area to relayout itself.
    bounds.set_y(bounds.y() - kKeyboardHeight);
    bounds.set_height(kKeyboardHeight);
  }
  keyboard_->SetBoundsRect(bounds);
}

void TouchBrowserFrameView::FocusWillChange(views::View* focused_before,
                                            views::View* focused_now) {
  VirtualKeyboardType before = DecideKeyboardStateForView(focused_before);
  VirtualKeyboardType now = DecideKeyboardStateForView(focused_now);
  if (before != now) {
    // TODO(varunjain): support other types of keyboard.
    UpdateKeyboardAndLayout(now == GENERIC);
  }
}

///////////////////////////////////////////////////////////////////////////////
// TouchBrowserFrameView, protected:
int TouchBrowserFrameView::GetReservedHeight() const {
  return keyboard_showing_ ? kKeyboardHeight : 0;
}

void TouchBrowserFrameView::ViewHierarchyChanged(bool is_add,
                                                 View* parent,
                                                 View* child) {
  OpaqueBrowserFrameView::ViewHierarchyChanged(is_add, parent, child);
  if (!GetFocusManager())
    return;

  if (is_add && !focus_listener_added_) {
    // Add focus listener when this view is added to the hierarchy.
    GetFocusManager()->AddFocusChangeListener(this);
    focus_listener_added_ = true;
  } else if (!is_add && focus_listener_added_) {
    // Remove focus listener when this view is removed from the hierarchy.
    GetFocusManager()->RemoveFocusChangeListener(this);
    focus_listener_added_ = false;
  }
}

///////////////////////////////////////////////////////////////////////////////
// TouchBrowserFrameView, private:

void TouchBrowserFrameView::InitVirtualKeyboard() {
  if (keyboard_)
    return;

  Profile* keyboard_profile = browser_view()->browser()->profile();
  DCHECK(keyboard_profile) << "Profile required for virtual keyboard.";

  keyboard_ = new KeyboardContainerView(keyboard_profile);
  keyboard_->SetVisible(false);
  AddChildView(keyboard_);
}

void TouchBrowserFrameView::UpdateKeyboardAndLayout(bool should_show_keyboard) {
  if (should_show_keyboard)
    InitVirtualKeyboard();

  if (should_show_keyboard == keyboard_showing_)
    return;

  DCHECK(keyboard_);

  keyboard_showing_ = should_show_keyboard;
  if (keyboard_showing_) {
    animation_->Show();

    // We don't re-layout the client view until the animation ends (see
    // AnimationEnded below) because we want the client view to occupy the
    // entire height during the animation.
    Layout();
  } else {
    animation_->Hide();

    browser_view()->set_clip_y(ui::Tween::ValueBetween(
          animation_->GetCurrentValue(), 0, kKeyboardHeight));
    parent()->Layout();
  }
}

TouchBrowserFrameView::VirtualKeyboardType
    TouchBrowserFrameView::DecideKeyboardStateForView(views::View* view) {
  if (!view)
    return NONE;

  std::string cname = view->GetClassName();
  if (cname == views::Textfield::kViewClassName) {
    return GENERIC;
  } else if (cname == RenderWidgetHostViewViews::kViewClassName) {
    TabContents* contents = browser_view()->browser()->GetSelectedTabContents();
    bool* editable = contents ? GetFocusedStateAccessor()->GetProperty(
        contents->property_bag()) : NULL;
    if (editable && *editable)
      return GENERIC;
  }
  return NONE;
}

bool TouchBrowserFrameView::HitTest(const gfx::Point& point) const {
  if (OpaqueBrowserFrameView::HitTest(point))
    return true;

  if (close_button()->IsVisible() &&
      close_button()->GetMirroredBounds().Contains(point))
    return true;
  if (restore_button()->IsVisible() &&
      restore_button()->GetMirroredBounds().Contains(point))
    return true;
  if (maximize_button()->IsVisible() &&
      maximize_button()->GetMirroredBounds().Contains(point))
    return true;
  if (minimize_button()->IsVisible() &&
      minimize_button()->GetMirroredBounds().Contains(point))
    return true;

  return false;
}

void TouchBrowserFrameView::TabSelectedAt(TabContentsWrapper* old_contents,
                                          TabContentsWrapper* new_contents,
                                          int index,
                                          bool user_gesture) {
  if (new_contents == old_contents)
    return;

  TabContents* contents = new_contents->tab_contents();
  if (!TabContentsHasFocus(contents))
    return;

  bool* editable = GetFocusedStateAccessor()->GetProperty(
      contents->property_bag());
  UpdateKeyboardAndLayout(editable ? *editable : false);
}


void TouchBrowserFrameView::Observe(NotificationType type,
                                    const NotificationSource& source,
                                    const NotificationDetails& details) {
  Browser* browser = browser_view()->browser();
  if (type == NotificationType::FOCUS_CHANGED_IN_PAGE) {
    // Only modify the keyboard state if the currently active tab sent the
    // notification.
    const TabContents* current_tab = browser->GetSelectedTabContents();
    TabContents* source_tab = Source<TabContents>(source).ptr();
    const bool editable = *Details<const bool>(details).ptr();

    if (current_tab == source_tab && TabContentsHasFocus(source_tab))
      UpdateKeyboardAndLayout(editable);

    // Save the state of the focused field so that the keyboard visibility
    // can be determined after tab switching.
    GetFocusedStateAccessor()->SetProperty(
        source_tab->property_bag(), editable);
  } else if (type == NotificationType::NAV_ENTRY_COMMITTED) {
    Browser* source_browser = Browser::GetBrowserForController(
        Source<NavigationController>(source).ptr(), NULL);
    // If the Browser for the keyboard has navigated, re-evaluate the visibility
    // of the keyboard.
    if (source_browser == browser)
      UpdateKeyboardAndLayout(DecideKeyboardStateForView(
          GetFocusManager()->GetFocusedView()) == GENERIC);
  } else if (type == NotificationType::TAB_CONTENTS_DESTROYED) {
    GetFocusedStateAccessor()->DeleteProperty(
        Source<TabContents>(source).ptr()->property_bag());
  }
}

///////////////////////////////////////////////////////////////////////////////
// ui::AnimationDelegate implementation
void TouchBrowserFrameView::AnimationProgressed(const ui::Animation* anim) {
  keyboard_->SetTranslateY(
      ui::Tween::ValueBetween(anim->GetCurrentValue(), kKeyboardHeight, 0));
  browser_view()->set_clip_y(
      ui::Tween::ValueBetween(anim->GetCurrentValue(), 0, kKeyboardHeight));
  SchedulePaint();
}

void TouchBrowserFrameView::AnimationEnded(const ui::Animation* animation) {
  browser_view()->set_clip_y(0);
  if (keyboard_showing_) {
    // Because the NonClientFrameView is a sibling of the ClientView, we rely on
    // the parent to resize the ClientView instead of resizing it directly.
    parent()->Layout();

    // The keyboard that pops up may end up hiding the text entry. So make sure
    // the renderer scrolls when necessary to keep the textfield visible.
    RenderViewHost* host =
        browser_view()->browser()->GetSelectedTabContents()->render_view_host();
    host->ScrollFocusedEditableNodeIntoView();
  }
  SchedulePaint();
}
