// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_USER_CONTROLLER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_USER_CONTROLLER_H_
#pragma once

#include <string>

#include "base/compiler_specific.h"
#include "base/string16.h"
#include "base/task.h"
#include "chrome/browser/chromeos/login/new_user_view.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/user_view.h"
#include "chrome/browser/chromeos/wm_ipc.h"
#include "testing/gtest/include/gtest/gtest_prod.h"
#include "views/widget/widget_delegate.h"

namespace views {
class Widget;
}

namespace chromeos {

class ThrobberManager;

// UserController manages the set of windows needed to login a single existing
// user or first time login for a new user. ExistingUserController creates
// the nececessary set of UserControllers.
class UserController : public views::WidgetDelegate,
                       public NewUserView::Delegate,
                       public UserView::Delegate {
 public:
  class Delegate {
   public:
    virtual void CreateAccount() = 0;
    virtual void Login(UserController* source,
                       const string16& password) = 0;
    virtual void LoginAsGuest() = 0;
    virtual void ClearErrors() = 0;
    virtual void OnUserSelected(UserController* source) = 0;
    virtual void RemoveUser(UserController* source) = 0;

    // Selects user entry with specified |index|.
    // Does nothing if current user is already selected.
    virtual void SelectUser(int index) = 0;

    // Switch to the enterprise enrollment screen (if applicable).
    virtual void StartEnterpriseEnrollment() = 0;

   protected:
    virtual ~Delegate() {}
  };

  // Creates a UserController representing new user or guest login.
  UserController(Delegate* delegate, bool is_guest);

  // Creates a UserController for the specified user.
  UserController(Delegate* delegate, const UserManager::User& user);

  virtual ~UserController();

  // Initializes the UserController, creating the set of windows/controls.
  // |index| is the index of this user, and |total_user_count| the total
  // number of users.
  void Init(int index, int total_user_count, bool need_browse_without_signin);

  int user_index() const { return user_index_; }
  bool is_new_user() const { return is_new_user_; }
  bool is_guest() const { return is_guest_; }
  bool is_owner() const { return is_owner_; }

  const UserManager::User& user() const { return user_; }

  // Get widget that contains all controls.
  views::Widget* controls_widget() { return controls_widget_; }

  // Called when user view is activated (OnUserSelected).
  void ClearAndEnableFields();

  // Called when user view is activated (OnUserSelected).
  void ClearAndEnablePassword();

  // Enables or disables tooltip with user's email.
  void EnableNameTooltip(bool enable);

  // Called when user image has been changed.
  void OnUserImageChanged(UserManager::User* user);

  // Returns bounds of the main input field in the screen coordinates (e.g.
  // these bounds could be used to choose positions for the error bubble).
  gfx::Rect GetMainInputScreenBounds() const;

  // Selects user relative to the current user.
  void SelectUserRelative(int shift);

  // Starts/Stops throbber.
  void StartThrobber();
  void StopThrobber();

  // Update border window parameters to notify window manager about new numbers.
  // |index| of this user and |total_user_count| of users.
  void UpdateUserCount(int index, int total_user_count);

  // Returns the label for the user which should be spoken when accessibility is
  // enabled.
  std::string GetAccessibleUserLabel();

  // views::WidgetDelegate implementation:
  virtual void OnWidgetActivated(bool active) OVERRIDE;

  // NewUserView::Delegate implementation:
  virtual void OnLogin(const std::string& username,
                       const std::string& password) OVERRIDE;
  virtual void OnLoginAsGuest() OVERRIDE;
  virtual void OnCreateAccount() OVERRIDE;
  virtual void OnStartEnterpriseEnrollment() OVERRIDE;
  virtual void ClearErrors() OVERRIDE;
  virtual void NavigateAway() OVERRIDE;

  // UserView::Delegate implementation:
  virtual void OnRemoveUser() OVERRIDE;
  virtual bool IsUserSelected() const OVERRIDE;

  // UsernameView::Delegate implementation:
  virtual void OnLocaleChanged() OVERRIDE;

  // Padding between the user windows.
  static const int kPadding;

  // Max size needed when an entry is not selected.
  static const int kUnselectedSize;
  static const int kNewUserUnselectedSize;

 private:
  FRIEND_TEST(UserControllerTest, GetNameTooltip);

  class ControlsWidgetDelegate;

  // Performs common setup for login windows.
  void ConfigureAndShow(views::Widget* widget,
                        int index,
                        chromeos::WmIpcWindowType type,
                        views::View* contents_view);
  void SetupControlsWidget(int index,
                           int* width,
                           int* height,
                           bool need_guest_link);
  views::Widget* CreateImageWidget(int index);
  views::Widget* CreateLabelWidget(int index, WmIpcWindowType type);
  gfx::Font GetLabelFont();
  gfx::Font GetUnselectedLabelFont();
  void CreateBorderWindow(int index,
                          int total_user_count,
                          int controls_width, int controls_height);

  // Returns tooltip text for user name.
  std::wstring GetNameTooltip() const;

  // Creates the widget that holds the controls. Caller owns the returned
  // widget.
  static views::Widget* CreateControlsWidget(const gfx::Rect& bounds);

  // Creates a Widget that selects the user any time the widget is
  // clicked. Caller owns the returned widget.
  static views::Widget* CreateClickNotifyingWidget(UserController* controller,
                                                   const gfx::Rect& bounds);

  // User index within all the users.
  int user_index_;

  // Is this user selected now?
  bool is_user_selected_;

  // Is this the new user pod?
  const bool is_new_user_;

  // Is this the guest pod?
  const bool is_guest_;

  // Is this user the owner?
  const bool is_owner_;

  // Should we show tooltips above user image and label to help distinguish
  // users with the same display name.
  bool show_name_tooltip_;

  // If is_new_user_ and is_guest_ are false, this is the user being shown.
  UserManager::User user_;

  Delegate* delegate_;

  // A window is used to represent the individual chunks.
  views::Widget* controls_widget_;
  views::Widget* image_widget_;
  views::Widget* border_window_;
  views::Widget* label_widget_;
  views::Widget* unselected_label_widget_;

  scoped_ptr<ControlsWidgetDelegate> controls_widget_delegate_;

  // View that shows user image on image window.
  UserView* user_view_;

  // Views that show display name of the user.
  views::Label* label_view_;
  views::Label* unselected_label_view_;

  // Input controls which are used for username and password.
  UserInput* user_input_;

  // Throbber host that can show a throbber.
  ThrobberHostView* throbber_host_;

  // Whether name tooltip is enabled.
  bool name_tooltip_enabled_;

  DISALLOW_COPY_AND_ASSIGN(UserController);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_USER_CONTROLLER_H_
