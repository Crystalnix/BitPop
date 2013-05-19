// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHATBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHATBAR_VIEW_H_

#pragma once

#include <vector>

#include "chrome/browser/facebook_chat/facebook_chatbar.h"
#include "ui/base/animation/animation_delegate.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"

class Browser;
class BrowserView;
class ChatItemView;
class FacebookChatItem;

namespace ui {
class SlideAnimation;
}

namespace views {
class ImageButton;
}

class ChatbarView : public views::View,
                    public ui::AnimationDelegate,
                    public FacebookChatbar,
                    public views::ButtonListener {
public:
  ChatbarView(Browser* browser, BrowserView* parent);
  virtual ~ChatbarView();

  virtual void AddChatItem(FacebookChatItem *chat_item);
  virtual void RemoveAll();

  virtual void Show();
  virtual void Hide();

  virtual Browser *browser() const;

  // Implementation of View.
  virtual gfx::Size GetPreferredSize();
  virtual void Layout();

  // Implementation of ui::AnimationDelegate.
  virtual void AnimationProgressed(const ui::Animation* animation);
  virtual void AnimationEnded(const ui::Animation* animation);

  // Implementation of ButtonListener.
  // Invoked when the user clicks the close button. Asks the browser to
  // hide the chatbar.
  virtual void ButtonPressed(views::Button* button, const ui::Event& event);

  bool IsShowing() const;
  bool IsClosing() const;

  void Remove(ChatItemView *item, bool should_animate);

  void PlaceFirstInOrder(ChatItemView* item);

  //void SwitchParentWindow(NSWindow *window);
protected:
  //virtual void OnPaint(gfx::Canvas* canvas);
  //virtual void OnPaintBackground(gfx::Canvas* canvas);
  virtual void OnPaintBorder(gfx::Canvas* canvas);
private:
  // called when the hide bar animation ends
  void Closed();

  // TODO: should be called on theme change
  void UpdateButtonColors();

  void StopPendingAnimations();

  void RemoveItem(ChatItemView* item);

  std::list<ChatItemView*> chat_items_;

  // The show/hide animation for the shelf itself.
  scoped_ptr<ui::SlideAnimation> bar_animation_;

  // Items animation
  scoped_ptr<ui::SlideAnimation> new_item_animation_;
  scoped_ptr<ui::SlideAnimation> remove_item_animation_;
  scoped_ptr<ui::SlideAnimation> place_first_animation_;
  ChatItemView *item_to_add_;
  ChatItemView *item_to_remove_;
  ChatItemView *item_to_place_first_;

  // Button for closing the chats. This is contained as a child, and
  // deleted by View.
  views::ImageButton* close_button_;

  Browser* browser_;
  BrowserView* parent_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHATBAR_VIEW_H_
