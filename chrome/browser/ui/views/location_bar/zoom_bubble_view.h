// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_ZOOM_BUBBLE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_ZOOM_BUBBLE_VIEW_H_

#include "base/basictypes.h"
#include "base/timer.h"
#include "ui/views/bubble/bubble_delegate.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/text_button.h"
#include "ui/views/controls/label.h"

namespace content {
class WebContents;
}

// View used to display the zoom percentage when it has changed.
class ZoomBubbleView : public views::BubbleDelegateView,
                       public views::ButtonListener {
 public:
  // Shows the bubble and automatically closes it after a short time period if
  // |auto_close| is true.
  static void ShowBubble(views::View* anchor_view,
                         content::WebContents* web_contents,
                         bool auto_close);

  // Closes the showing bubble (if one exists).
  static void CloseBubble();

  // Whether the zoom bubble is currently showing.
  static bool IsShowing();

 private:
  ZoomBubbleView(views::View* anchor_view,
                 content::WebContents* web_contents,
                 bool auto_close);
  virtual ~ZoomBubbleView();

  // Refreshes the bubble by changing the zoom percentage appropriately and
  // resetting the timer if necessary.
  void Refresh();

  void Close();

  // Starts a timer which will close the bubble if |auto_close_| is true.
  void StartTimerIfNecessary();

  // Stops the auto-close timer.
  void StopTimer();

  // views::View method.
  virtual void OnMouseEntered(const ui::MouseEvent& event) OVERRIDE;
  virtual void OnMouseExited(const ui::MouseEvent& event) OVERRIDE;

  // ui::EventHandler method.
  virtual void OnGestureEvent(ui::GestureEvent* event) OVERRIDE;

  // views::ButtonListener method.
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  // views::BubbleDelegateView method.
  virtual void Init() OVERRIDE;
  virtual void WindowClosing() OVERRIDE;

  // Singleton instance of the zoom bubble. The zoom bubble can only be shown on
  // the active browser window, so there is no case in which it will be shown
  // twice at the same time.
  static ZoomBubbleView* zoom_bubble_;

  // Timer used to close the bubble when |auto_close_| is true.
  base::OneShotTimer<ZoomBubbleView> timer_;

  // Label displaying the zoom percentage.
  views::Label* label_;

  // The WebContents for the page whose zoom has changed.
  content::WebContents* web_contents_;

  // Whether the currently displayed bubble will automatically close.
  bool auto_close_;

  DISALLOW_COPY_AND_ASSIGN(ZoomBubbleView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_ZOOM_BUBBLE_VIEW_H_
