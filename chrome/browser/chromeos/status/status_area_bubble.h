// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_BUBBLE_H_
#define CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_BUBBLE_H_
#pragma once

#include "base/timer.h"
#include "ui/views/widget/widget.h"

namespace views {
class Label;
}

namespace chromeos {

// StatusAreaBubbleContentView is used as the content view of
// StatusAreaBubbleController.
// It can be also used to show a bubble-like menu under the status area.
class StatusAreaBubbleContentView : public views::View {
 public:
  // |icon_view| is used to show icon, |this| will take its ownership.
  StatusAreaBubbleContentView(views::View* icon_view, const string16& message);
  virtual ~StatusAreaBubbleContentView();

  string16 GetMessage() const;
  void SetMessage(const string16& message);

  views::View* icon_view() const { return icon_view_; }

  // views::View override
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;

 private:
  views::View* icon_view_;
  views::Label* message_view_;

  DISALLOW_COPY_AND_ASSIGN(StatusAreaBubbleContentView);
};

// StatusAreaBubbleController is used to show a bubble under the status area
class StatusAreaBubbleController : public views::Widget::Observer {
 public:
  virtual ~StatusAreaBubbleController();

  // Show bubble under |view| for a while.
  static StatusAreaBubbleController* ShowBubbleUnderViewForAWhile(
      views::View* view,
      StatusAreaBubbleContentView* content);

  // views::Widget::Observer override
  virtual void OnWidgetClosing(views::Widget* widget) OVERRIDE;

  bool IsBubbleShown() const;
  void HideBubble();

 private:
  class StatusAreaBubbleDelegateView;

  StatusAreaBubbleController();

  StatusAreaBubbleDelegateView* bubble_;
  // A timer to hide this bubble.
  base::OneShotTimer<StatusAreaBubbleController> timer_;

  DISALLOW_COPY_AND_ASSIGN(StatusAreaBubbleController);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_BUBBLE_H_
