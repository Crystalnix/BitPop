// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_TRAY_SYSTEM_TRAY_BUBBLE_H_
#define ASH_SYSTEM_TRAY_SYSTEM_TRAY_BUBBLE_H_

#include "ash/system/tray/tray_bubble_view.h"
#include "ash/system/user/login_status.h"
#include "base/base_export.h"
#include "base/timer.h"
#include "ui/views/widget/widget_observer.h"

#include <vector>

namespace ash {

class SystemTray;
class SystemTrayItem;

namespace internal {

class SystemTrayBubble : public TrayBubbleView::Host,
                         public views::WidgetObserver {
 public:
  enum BubbleType {
    BUBBLE_TYPE_DEFAULT,
    BUBBLE_TYPE_DETAILED,
    BUBBLE_TYPE_NOTIFICATION
  };

  SystemTrayBubble(ash::SystemTray* tray,
                   const std::vector<ash::SystemTrayItem*>& items,
                   BubbleType bubble_type);
  virtual ~SystemTrayBubble();

  // Change the items displayed in the bubble.
  void UpdateView(const std::vector<ash::SystemTrayItem*>& items,
                  BubbleType bubble_type);

  // Creates |bubble_view_| and a child views for each member of |items_|.
  // Also creates |bubble_widget_| and sets up animations.
  void InitView(views::View* anchor,
                TrayBubbleView::InitParams init_params,
                user::LoginStatus login_status);

  // Overridden from TrayBubbleView::Host.
  virtual void BubbleViewDestroyed() OVERRIDE;
  virtual void OnMouseEnteredView() OVERRIDE;
  virtual void OnMouseExitedView() OVERRIDE;
  virtual void OnClickedOutsideView() OVERRIDE;

  BubbleType bubble_type() const { return bubble_type_; }
  TrayBubbleView* bubble_view() const { return bubble_view_; }

  void DestroyItemViews();
  void StartAutoCloseTimer(int seconds);
  void StopAutoCloseTimer();
  void RestartAutoCloseTimer();
  void Close();
  void SetVisible(bool is_visible);
  bool IsVisible();

 private:
  void CreateItemViews(user::LoginStatus login_status);

  // Overridden from views::WidgetObserver:
  virtual void OnWidgetClosing(views::Widget* widget) OVERRIDE;

  ash::SystemTray* tray_;
  TrayBubbleView* bubble_view_;
  views::Widget* bubble_widget_;
  std::vector<ash::SystemTrayItem*> items_;
  BubbleType bubble_type_;

  int autoclose_delay_;
  base::OneShotTimer<SystemTrayBubble> autoclose_;

  DISALLOW_COPY_AND_ASSIGN(SystemTrayBubble);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_SYSTEM_TRAY_SYSTEM_TRAY_BUBBLE_H_
