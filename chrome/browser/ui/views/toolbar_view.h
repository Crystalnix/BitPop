// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TOOLBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_TOOLBAR_VIEW_H_
#pragma once

#include <set>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/prefs/pref_member.h"
#include "chrome/browser/ui/toolbar/back_forward_menu_model.h"
#include "chrome/browser/ui/views/accessible_pane_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/reload_button.h"
#include "ui/base/animation/slide_animation.h"
#include "ui/base/models/accelerator.h"
#include "views/controls/button/menu_button.h"
#include "views/controls/menu/menu.h"
#include "views/controls/menu/menu_wrapper.h"
#include "views/controls/menu/view_menu_delegate.h"
#include "views/view.h"

class BrowserActionsContainer;
class Browser;
class Profile;
class WrenchMenu;

// The Browser Window's toolbar.
class ToolbarView : public AccessiblePaneView,
                    public views::ViewMenuDelegate,
                    public ui::AcceleratorProvider,
                    public LocationBarView::Delegate,
                    public NotificationObserver,
                    public CommandUpdater::CommandObserver,
                    public views::ButtonListener {
 public:
  explicit ToolbarView(Browser* browser);
  virtual ~ToolbarView();

  // Create the contents of the Browser Toolbar
  void Init(Profile* profile);

  // Sets the profile which is active on the currently-active tab.
  void SetProfile(Profile* profile);
  Profile* profile() { return profile_; }

  // Updates the toolbar (and transitively the location bar) with the states of
  // the specified |tab|.  If |should_restore_state| is true, we're switching
  // (back?) to this tab and should restore any previous location bar state
  // (such as user editing) as well.
  void Update(TabContents* tab, bool should_restore_state);

  // Set focus to the toolbar with complete keyboard access, with the
  // focus initially set to the location bar. Focus will be restored
  // to the ViewStorage with id |view_storage_id| if the user escapes.
  void SetPaneFocusAndFocusLocationBar(int view_storage_id);

  // Set focus to the toolbar with complete keyboard access, with the
  // focus initially set to the app menu. Focus will be restored
  // to the ViewStorage with id |view_storage_id| if the user escapes.
  void SetPaneFocusAndFocusAppMenu(int view_storage_id);

  // Returns true if the app menu is focused.
  bool IsAppMenuFocused();

  // Add a listener to receive a callback when the menu opens.
  void AddMenuListener(views::MenuListener* listener);

  // Remove a menu listener.
  void RemoveMenuListener(views::MenuListener* listener);

  // Gets a bitmap with the icon for the app menu and any overlaid notification
  // badge.
  SkBitmap GetAppMenuIcon(views::CustomButton::ButtonState state);

  virtual bool GetAcceleratorInfo(int id, ui::Accelerator* accel);

  // Accessors...
  Browser* browser() const { return browser_; }
  BrowserActionsContainer* browser_actions() const { return browser_actions_; }
  ReloadButton* reload_button() const { return reload_; }
  LocationBarView* location_bar() const { return location_bar_; }
  views::MenuButton* app_menu() const { return app_menu_; }

  // Overridden from AccessiblePaneView
  virtual bool SetPaneFocus(int view_storage_id, View* initial_focus) OVERRIDE;
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;

  // Overridden from views::MenuDelegate:
  virtual void RunMenu(views::View* source, const gfx::Point& pt) OVERRIDE;

  // Overridden from LocationBarView::Delegate:
  virtual TabContentsWrapper* GetTabContentsWrapper() const OVERRIDE;
  virtual InstantController* GetInstant() OVERRIDE;
  virtual void OnInputInProgress(bool in_progress) OVERRIDE;

  // Overridden from CommandUpdater::CommandObserver:
  virtual void EnabledStateChangedForCommand(int id, bool enabled) OVERRIDE;

  // Overridden from views::BaseButton::ButtonListener:
  virtual void ButtonPressed(views::Button* sender, const views::Event& event)
      OVERRIDE;

  // Overridden from NotificationObserver:
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) OVERRIDE;

  // Overridden from ui::AcceleratorProvider:
  virtual bool GetAcceleratorForCommandId(
      int command_id, ui::Accelerator* accelerator) OVERRIDE;

  // Overridden from views::View:
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  virtual bool GetDropFormats(
      int* formats,
      std::set<OSExchangeData::CustomFormat>* custom_formats) OVERRIDE;
  virtual bool CanDrop(const ui::OSExchangeData& data) OVERRIDE;
  virtual int OnDragUpdated(const views::DropTargetEvent& event) OVERRIDE;
  virtual int OnPerformDrop(const views::DropTargetEvent& event) OVERRIDE;
  virtual void OnThemeChanged() OVERRIDE;

  // The apparent horizontal space between most items, and the vertical padding
  // above and below them.
  static const int kStandardSpacing;
  // The top of the toolbar has an edge we have to skip over in addition to the
  // standard spacing.
  static const int kVertSpacing;

 protected:

  // Overridden from AccessiblePaneView
  virtual views::View* GetDefaultFocusableChild() OVERRIDE;
  virtual void RemovePaneFocus() OVERRIDE;

 private:
  // Returns true if we should show the upgrade recommended dot.
  bool IsUpgradeRecommended();

  // Returns true if we should show the background page badge.
  bool ShouldShowBackgroundPageBadge();

  // Returns true if we should show the warning for incompatible software.
  bool ShouldShowIncompatibilityWarning();

  // Returns the number of pixels above the location bar in non-normal display.
  int PopupTopSpacing() const;

  // Loads the images for all the child views.
  void LoadImages();

  // Types of display mode this toolbar can have.
  enum DisplayMode {
    DISPLAYMODE_NORMAL,       // Normal toolbar with buttons, etc.
    DISPLAYMODE_LOCATION      // Slimline toolbar showing only compact location
                              // bar, used for popups.
  };
  bool IsDisplayModeNormal() const {
    return display_mode_ == DISPLAYMODE_NORMAL;
  }

  // Updates the badge on the app menu (Wrench).
  void UpdateAppMenuBadge();

  // Gets a badge for the wrench icon corresponding to the number of
  // unacknowledged background pages in the system.
  SkBitmap GetBackgroundPageBadge();

  scoped_ptr<BackForwardMenuModel> back_menu_model_;
  scoped_ptr<BackForwardMenuModel> forward_menu_model_;

  // The model that contains the security level, text, icon to display...
  ToolbarModel* model_;

  // Controls
  views::ImageButton* back_;
  views::ImageButton* forward_;
  ReloadButton* reload_;
  views::ImageButton* home_;
  LocationBarView* location_bar_;
  BrowserActionsContainer* browser_actions_;
  views::MenuButton* app_menu_;
  Profile* profile_;
  Browser* browser_;

  // Contents of the profiles menu to populate with profile names.
  scoped_ptr<ui::SimpleMenuModel> profiles_menu_contents_;

  // Controls whether or not a home button should be shown on the toolbar.
  BooleanPrefMember show_home_button_;

  // The display mode used when laying out the toolbar.
  DisplayMode display_mode_;

  // The contents of the wrench menu.
  scoped_ptr<ui::SimpleMenuModel> wrench_menu_model_;

  // Wrench menu.
  scoped_refptr<WrenchMenu> wrench_menu_;

  // Vector of listeners to receive callbacks when the menu opens.
  std::vector<views::MenuListener*> menu_listeners_;

  NotificationRegistrar registrar_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(ToolbarView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_TOOLBAR_VIEW_H_
