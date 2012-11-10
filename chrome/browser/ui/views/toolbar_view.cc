// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/toolbar_view.h"

#include "base/i18n/number_formatting.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/event_disposition.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_content_setting_bubble_model_delegate.h"
#include "chrome/browser/ui/browser_instant_controller.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/global_error/global_error_service.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/browser/ui/search/search.h"
#include "chrome/browser/ui/search/search_model.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/browser/ui/search/search_types.h"
#include "chrome/browser/ui/search/search_ui.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/toolbar/wrench_menu_model.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/browser_actions_container.h"
#include "chrome/browser/ui/views/location_bar/location_bar_container.h"
#include "chrome/browser/ui/views/location_bar/page_action_image_view.h"
#include "chrome/browser/ui/views/wrench_menu.h"
#include "chrome/browser/upgrade_detector.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/theme_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/views/controls/button/button_dropdown.h"
#include "ui/views/controls/menu/menu_listener.h"
#include "ui/views/focus/view_storage.h"
#include "ui/views/widget/tooltip_manager.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

#if defined(OS_WIN)
#include "chrome/browser/enumerate_modules_model_win.h"
#include "chrome/browser/ui/views/critical_notification_bubble_view.h"
#if !defined(USE_AURA)
#include "chrome/browser/ui/views/app_menu_button_win.h"
#endif
#endif

#if defined(USE_AURA)
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#endif

using content::UserMetricsAction;
using content::WebContents;

namespace {

// The edge graphics have some built-in spacing/shadowing, so we have to adjust
// our spacing to make it match.
const int kLeftEdgeSpacing = 3;
const int kRightEdgeSpacing = 2;

// The buttons to the left of the omnibox are close together.
const int kButtonSpacing = 0;

#if defined(USE_ASH)
// Ash doesn't use a rounded content area and its top edge has an extra shadow.
const int kContentShadowHeight = 2;
#else
// Windows uses a rounded content area with no shadow in the assets.
const int kContentShadowHeight = 0;
#endif

const int kPopupTopSpacingNonGlass = 3;
const int kPopupBottomSpacingNonGlass = 2;
const int kPopupBottomSpacingGlass = 1;

// Top margin for the wrench menu badges (badge is placed in the upper right
// corner of the wrench menu).
const int kBadgeTopMargin = 2;

// Added padding for search toolbar.
const int kSearchTopButtonSpacing = 3;
const int kSearchTopLocationBarSpacing = 2;
const int kSearchToolbarSpacing = 5;

gfx::ImageSkia* kPopupBackgroundEdge = NULL;

// The omnibox border has some additional shadow, so we use less vertical
// spacing than ToolbarView::kVertSpacing.
int location_bar_vert_spacing() {
  static int value = -1;
  if (value == -1) {
    switch (ui::GetDisplayLayout()) {
      case ui::LAYOUT_ASH:
      case ui::LAYOUT_DESKTOP:
        value = 4;
        break;
      case ui::LAYOUT_TOUCH:
        value = 6;
        break;
      default:
        NOTREACHED();
    }
  }
  return value;
}

class BadgeImageSource: public gfx::CanvasImageSource {
 public:
  BadgeImageSource(const gfx::ImageSkia& icon, const gfx::ImageSkia& badge)
      : gfx::CanvasImageSource(icon.size(), false),
        icon_(icon),
        badge_(badge) {
  }

  ~BadgeImageSource() {
  }

  // Overridden from gfx::CanvasImageSource:
  void Draw(gfx::Canvas* canvas) OVERRIDE {
    canvas->DrawImageInt(icon_, 0, 0);
    canvas->DrawImageInt(badge_, icon_.width() - badge_.width(),
                         kBadgeTopMargin);
  }

 private:
  const gfx::ImageSkia icon_;
  const gfx::ImageSkia badge_;

  DISALLOW_COPY_AND_ASSIGN(BadgeImageSource);
};

}  // namespace

// static
const char ToolbarView::kViewClassName[] = "browser/ui/views/ToolbarView";
// The space between items is 3 px in general.
const int ToolbarView::kStandardSpacing = 3;
// The top of the toolbar has an edge we have to skip over in addition to the
// above spacing.
const int ToolbarView::kVertSpacing = 5;

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, public:

ToolbarView::ToolbarView(Browser* browser)
    : model_(browser->toolbar_model()),
      back_(NULL),
      forward_(NULL),
      reload_(NULL),
      home_(NULL),
      location_bar_(NULL),
      location_bar_container_(NULL),
      browser_actions_(NULL),
      app_menu_(NULL),
      browser_(browser),
      profiles_menu_contents_(NULL) {
  set_id(VIEW_ID_TOOLBAR);

  chrome::AddCommandObserver(browser_, IDC_BACK, this);
  chrome::AddCommandObserver(browser_, IDC_FORWARD, this);
  chrome::AddCommandObserver(browser_, IDC_RELOAD, this);
  chrome::AddCommandObserver(browser_, IDC_HOME, this);
  chrome::AddCommandObserver(browser_, IDC_LOAD_NEW_TAB_PAGE, this);

  display_mode_ = browser->SupportsWindowFeature(Browser::FEATURE_TABSTRIP) ?
      DISPLAYMODE_NORMAL : DISPLAYMODE_LOCATION;

  if (!kPopupBackgroundEdge) {
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    kPopupBackgroundEdge = rb.GetImageSkiaNamed(IDR_LOCATIONBG_POPUPMODE_EDGE);
  }

  registrar_.Add(this, chrome::NOTIFICATION_UPGRADE_RECOMMENDED,
                 content::NotificationService::AllSources());
#if defined(OS_WIN)
  registrar_.Add(this, chrome::NOTIFICATION_CRITICAL_UPGRADE_INSTALLED,
                 content::NotificationService::AllSources());
#endif
  registrar_.Add(this,
                 chrome::NOTIFICATION_MODULE_INCOMPATIBILITY_BADGE_CHANGE,
                 content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_GLOBAL_ERRORS_CHANGED,
                 content::Source<Profile>(browser_->profile()));
  browser_->search_model()->AddObserver(this);
}

ToolbarView::~ToolbarView() {
  // NOTE: Don't remove the command observers here.  This object gets destroyed
  // after the Browser (which owns the CommandUpdater), so the CommandUpdater is
  // already gone.

  // TODO(kuan): Reset the search model observer in ~BrowserView before we lose
  // browser.
}

void ToolbarView::Init(views::View* location_bar_parent,
                       views::View* popup_parent_view) {
  back_menu_model_.reset(new BackForwardMenuModel(
      browser_, BackForwardMenuModel::BACKWARD_MENU));
  forward_menu_model_.reset(new BackForwardMenuModel(
      browser_, BackForwardMenuModel::FORWARD_MENU));

  back_ = new views::ButtonDropDown(this, back_menu_model_.get());
  back_->set_triggerable_event_flags(ui::EF_LEFT_MOUSE_BUTTON |
                                     ui::EF_MIDDLE_MOUSE_BUTTON);
  back_->set_tag(IDC_BACK);
  back_->SetImageAlignment(views::ImageButton::ALIGN_RIGHT,
                           views::ImageButton::ALIGN_TOP);
  back_->SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_BACK));
  back_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_BACK));
  back_->set_id(VIEW_ID_BACK_BUTTON);

  forward_ = new views::ButtonDropDown(this, forward_menu_model_.get());
  forward_->set_triggerable_event_flags(ui::EF_LEFT_MOUSE_BUTTON |
                                        ui::EF_MIDDLE_MOUSE_BUTTON);
  forward_->set_tag(IDC_FORWARD);
  forward_->SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_FORWARD));
  forward_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_FORWARD));
  forward_->set_id(VIEW_ID_FORWARD_BUTTON);

  // Have to create this before |reload_| as |reload_|'s constructor needs it.
  location_bar_container_ = new LocationBarContainer(
      location_bar_parent,
      chrome::search::IsInstantExtendedAPIEnabled(browser_->profile()));
  location_bar_ = new LocationBarView(
      browser_->profile(),
      browser_->command_controller()->command_updater(),
      model_,
      this,
      browser_->search_model(),
      (display_mode_ == DISPLAYMODE_LOCATION) ?
          LocationBarView::POPUP : LocationBarView::NORMAL);
  // TODO(sky): if we want this to work on windows we need to make sure the
  // LocationBarContainer gets focus. This will involve tweaking view_ids.
  // location_bar_->set_view_to_focus(location_bar_container_);
  location_bar_container_->SetLocationBarView(location_bar_);

  reload_ = new ReloadButton(location_bar_,
                             browser_->command_controller()->command_updater());
  reload_->set_triggerable_event_flags(ui::EF_LEFT_MOUSE_BUTTON |
                                       ui::EF_MIDDLE_MOUSE_BUTTON);
  reload_->set_tag(IDC_RELOAD);
  reload_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_RELOAD));
  reload_->set_id(VIEW_ID_RELOAD_BUTTON);

  home_ = new views::ImageButton(this);
  home_->set_triggerable_event_flags(ui::EF_LEFT_MOUSE_BUTTON |
                                     ui::EF_MIDDLE_MOUSE_BUTTON);
  home_->set_tag(IDC_HOME);
  home_->SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_HOME));
  home_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_HOME));
  home_->set_id(VIEW_ID_HOME_BUTTON);

  browser_actions_ = new BrowserActionsContainer(browser_, this);

#if defined(OS_WIN) && !defined(USE_AURA)
  app_menu_ = new AppMenuButtonWin(this);
#else
  app_menu_ = new views::MenuButton(NULL, string16(), this, false);
#endif
  app_menu_->set_border(NULL);
  app_menu_->EnableCanvasFlippingForRTLUI(true);
  app_menu_->SetAccessibleName(l10n_util::GetStringUTF16(IDS_ACCNAME_APP));
  app_menu_->SetTooltipText(l10n_util::GetStringFUTF16(
      IDS_APPMENU_TOOLTIP,
      l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  app_menu_->set_id(VIEW_ID_APP_MENU);

  // Add any necessary badges to the menu item based on the system state.
  if (ShouldShowUpgradeRecommended() || ShouldShowIncompatibilityWarning()) {
    UpdateAppMenuState();
  }
  LoadImages();

  // Always add children in order from left to right, for accessibility.
  AddChildView(back_);
  AddChildView(forward_);
  AddChildView(reload_);
  AddChildView(home_);
  AddChildView(browser_actions_);
  AddChildView(app_menu_);

  location_bar_->Init(popup_parent_view);
  show_home_button_.Init(prefs::kShowHomeButton,
                         browser_->profile()->GetPrefs(), this);
  browser_actions_->Init();

  // Accessibility specific tooltip text.
  if (BrowserAccessibilityState::GetInstance()->IsAccessibleBrowser()) {
    back_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_ACCNAME_TOOLTIP_BACK));
    forward_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_ACCNAME_TOOLTIP_FORWARD));
  }
}

void ToolbarView::Update(WebContents* tab, bool should_restore_state) {
  if (location_bar_)
    location_bar_->Update(should_restore_state ? tab : NULL);

  if (browser_actions_)
    browser_actions_->RefreshBrowserActionViews();

  if (reload_)
    reload_->set_menu_enabled(chrome::IsDebuggerAttachedToCurrentTab(browser_));
}

void ToolbarView::SetPaneFocusAndFocusAppMenu() {
  SetPaneFocus(app_menu_);
}

bool ToolbarView::IsAppMenuFocused() {
  return app_menu_->HasFocus();
}

void ToolbarView::AddMenuListener(views::MenuListener* listener) {
  menu_listeners_.AddObserver(listener);
}

void ToolbarView::RemoveMenuListener(views::MenuListener* listener) {
  menu_listeners_.RemoveObserver(listener);
}

gfx::ImageSkia ToolbarView::GetAppMenuIcon(
    views::CustomButton::ButtonState state) {
  ui::ThemeProvider* tp = GetThemeProvider();

  int id = 0;
  switch (state) {
    case views::CustomButton::BS_NORMAL: id = IDR_TOOLS;   break;
    case views::CustomButton::BS_HOT:    id = IDR_TOOLS_H; break;
    case views::CustomButton::BS_PUSHED: id = IDR_TOOLS_P; break;
    default:                             NOTREACHED();     break;
  }
  gfx::ImageSkia icon = *tp->GetImageSkiaNamed(id);

#if defined(OS_WIN)
  // Keep track of whether we were showing the badge before, so we don't send
  // multiple UMA events for example when multiple Chrome windows are open.
  static bool incompatibility_badge_showing = false;
  // Save the old value before resetting it.
  bool was_showing = incompatibility_badge_showing;
  incompatibility_badge_showing = false;
#endif

  int error_badge_id = GlobalErrorServiceFactory::GetForProfile(
      browser_->profile())->GetFirstBadgeResourceID();

  bool add_badge = ShouldShowUpgradeRecommended() ||
                   ShouldShowIncompatibilityWarning() || error_badge_id;
  if (!add_badge)
    return icon;

  gfx::ImageSkia badge;
  // Only one badge can be active at any given time. The Upgrade notification
  // is deemed most important, then the DLL conflict badge.
  if (ShouldShowUpgradeRecommended()) {
    badge = *tp->GetImageSkiaNamed(
        UpgradeDetector::GetInstance()->GetIconResourceID(
            UpgradeDetector::UPGRADE_ICON_TYPE_BADGE));
  } else if (ShouldShowIncompatibilityWarning()) {
#if defined(OS_WIN)
    if (!was_showing)
      content::RecordAction(UserMetricsAction("ConflictBadge"));
    badge = *tp->GetImageSkiaNamed(IDR_CONFLICT_BADGE);
    incompatibility_badge_showing = true;
#else
    NOTREACHED();
#endif
  } else if (error_badge_id) {
    badge = *tp->GetImageSkiaNamed(error_badge_id);
  } else {
    NOTREACHED();
  }

  gfx::CanvasImageSource* source = new BadgeImageSource(icon, badge);
  // ImageSkia takes ownership of |source|.
  return gfx::ImageSkia(source, source->size());
}

void ToolbarView::LayoutForSearch() {
  if (chrome::search::IsInstantExtendedAPIEnabled(browser_->profile()) &&
      browser_->search_model()->mode().is_ntp())
    LayoutLocationBarNTP();
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, AccessiblePaneView overrides:

bool ToolbarView::SetPaneFocus(views::View* initial_focus) {
  if (!AccessiblePaneView::SetPaneFocus(initial_focus))
    return false;

  location_bar_->SetShowFocusRect(true);
  return true;
}

void ToolbarView::GetAccessibleState(ui::AccessibleViewState* state) {
  state->role = ui::AccessibilityTypes::ROLE_TOOLBAR;
  state->name = l10n_util::GetStringUTF16(IDS_ACCNAME_TOOLBAR);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, Menu::Delegate overrides:

bool ToolbarView::GetAcceleratorInfo(int id, ui::Accelerator* accel) {
  return GetWidget()->GetAccelerator(id, accel);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, views::MenuButtonListener implementation:

void ToolbarView::OnMenuButtonClicked(views::View* source,
                                      const gfx::Point& point) {
  DCHECK_EQ(VIEW_ID_APP_MENU, source->id());

  wrench_menu_.reset(new WrenchMenu(browser_));
  WrenchMenuModel model(this, browser_);
  wrench_menu_->Init(&model);

  FOR_EACH_OBSERVER(views::MenuListener, menu_listeners_, OnMenuOpened());

  wrench_menu_->RunMenu(app_menu_);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, LocationBarView::Delegate implementation:

TabContents* ToolbarView::GetTabContents() const {
  return chrome::GetActiveTabContents(browser_);
}

InstantController* ToolbarView::GetInstant() {
  return browser_->instant_controller()->instant();
}

ContentSettingBubbleModelDelegate*
ToolbarView::GetContentSettingBubbleModelDelegate() {
  return browser_->content_setting_bubble_model_delegate();
}

void ToolbarView::ShowPageInfo(content::WebContents* web_contents,
                          const GURL& url,
                          const content::SSLStatus& ssl,
                          bool show_history) {
  chrome::ShowPageInfo(browser_, web_contents, url, ssl, show_history);
}

views::Widget* ToolbarView::CreateViewsBubble(
    views::BubbleDelegateView* bubble_delegate) {
  return views::BubbleDelegateView::CreateBubble(bubble_delegate);
}

PageActionImageView* ToolbarView::CreatePageActionImageView(
    LocationBarView* owner, ExtensionAction* action) {
  return new PageActionImageView(owner, action, browser_);
}

void ToolbarView::OnInputInProgress(bool in_progress) {
  // The edit should make sure we're only notified when something changes.
  DCHECK(model_->input_in_progress() != in_progress);

  model_->set_input_in_progress(in_progress);
  location_bar_->Update(NULL);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, chrome::search::SearchModelObserver implementation:
void ToolbarView::ModeChanged(const chrome::search::Mode& mode) {
  // Layout location bar to determine the visibility of each of its child
  // view based on toolbar mode change.
  if (mode.is_ntp())
    location_bar_->Layout();

  Layout();
  LayoutForSearch();
  SchedulePaint();
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, CommandObserver implementation:

void ToolbarView::EnabledStateChangedForCommand(int id, bool enabled) {
  views::Button* button = NULL;
  switch (id) {
    case IDC_BACK:
      button = back_;
      break;
    case IDC_FORWARD:
      button = forward_;
      break;
    case IDC_RELOAD:
      button = reload_;
      break;
    case IDC_HOME:
      button = home_;
      break;
  }
  if (button)
    button->SetEnabled(enabled);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, views::Button::ButtonListener implementation:

void ToolbarView::ButtonPressed(views::Button* sender,
                                const views::Event& event) {
  int command = sender->tag();
  WindowOpenDisposition disposition =
      chrome::DispositionFromEventFlags(sender->mouse_event_flags());
  if ((disposition == CURRENT_TAB) &&
      ((command == IDC_BACK) || (command == IDC_FORWARD))) {
    // Forcibly reset the location bar, since otherwise it won't discard any
    // ongoing user edits, since it doesn't realize this is a user-initiated
    // action.
    location_bar_->Revert();
  }
  chrome::ExecuteCommandWithDisposition(browser_, command, disposition);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, content::NotificationObserver implementation:

void ToolbarView::Observe(int type,
                          const content::NotificationSource& source,
                          const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_PREF_CHANGED: {
      std::string* pref_name = content::Details<std::string>(details).ptr();
      if (*pref_name == prefs::kShowHomeButton) {
        Layout();
        SchedulePaint();
      }
      break;
    }
    case chrome::NOTIFICATION_UPGRADE_RECOMMENDED:
    case chrome::NOTIFICATION_MODULE_INCOMPATIBILITY_BADGE_CHANGE:
    case chrome::NOTIFICATION_GLOBAL_ERRORS_CHANGED:
      UpdateAppMenuState();
      break;
#if defined(OS_WIN)
    case chrome::NOTIFICATION_CRITICAL_UPGRADE_INSTALLED:
      ShowCriticalNotification();
      break;
#endif
    default:
      NOTREACHED();
  }
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, ui::AcceleratorProvider implementation:

bool ToolbarView::GetAcceleratorForCommandId(int command_id,
    ui::Accelerator* accelerator) {
  // The standard Ctrl-X, Ctrl-V and Ctrl-C are not defined as accelerators
  // anywhere so we need to check for them explicitly here.
  // TODO(cpu) Bug 1109102. Query WebKit land for the actual bindings.
  switch (command_id) {
    case IDC_CUT:
      *accelerator = ui::Accelerator(ui::VKEY_X, ui::EF_CONTROL_DOWN);
      return true;
    case IDC_COPY:
      *accelerator = ui::Accelerator(ui::VKEY_C, ui::EF_CONTROL_DOWN);
      return true;
    case IDC_PASTE:
      *accelerator = ui::Accelerator(ui::VKEY_V, ui::EF_CONTROL_DOWN);
      return true;
#if defined(USE_ASH)
    // When USE_ASH is defined, the commands listed here are handled outside
    // Chrome, in ash/accelerators/accelerator_table.cc (crbug.com/120196).
    case IDC_CLEAR_BROWSING_DATA:
      *accelerator = ui::Accelerator(ui::VKEY_BACK,
                                     ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
      return true;
    case IDC_NEW_TAB:
      *accelerator = ui::Accelerator(ui::VKEY_T, ui::EF_CONTROL_DOWN);
      return true;
    case IDC_NEW_WINDOW:
      *accelerator = ui::Accelerator(ui::VKEY_N, ui::EF_CONTROL_DOWN);
      return true;
    case IDC_NEW_INCOGNITO_WINDOW:
      *accelerator = ui::Accelerator(ui::VKEY_N,
                                     ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN);
      return true;
    case IDC_TASK_MANAGER:
      *accelerator = ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_SHIFT_DOWN);
      return true;
#endif
  }
  // Else, we retrieve the accelerator information from the frame.
  return GetWidget()->GetAccelerator(command_id, accelerator);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, views::View overrides:

gfx::Size ToolbarView::GetPreferredSize() {
  if (is_display_mode_normal()) {
    int min_width = kLeftEdgeSpacing +
        back_->GetPreferredSize().width() + kButtonSpacing +
        forward_->GetPreferredSize().width() + kButtonSpacing +
        reload_->GetPreferredSize().width() + kStandardSpacing +
        (show_home_button_.GetValue() ?
            (home_->GetPreferredSize().width() + kButtonSpacing) : 0) +
        location_bar_container_->GetPreferredSize().width() +
        browser_actions_->GetPreferredSize().width() +
        app_menu_->GetPreferredSize().width() + kRightEdgeSpacing;

    CR_DEFINE_STATIC_LOCAL(gfx::ImageSkia, normal_background, ());
    if (normal_background.isNull()) {
      ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
      normal_background = *rb.GetImageSkiaNamed(IDR_CONTENT_TOP_CENTER);
    }

    int delta = !chrome::search::IsInstantExtendedAPIEnabled(
        browser_->profile()) ? 0 : kSearchToolbarSpacing;
    return gfx::Size(min_width,
                     normal_background.height() - kContentShadowHeight + delta);
  }

  int vertical_spacing = PopupTopSpacing() +
      (GetWidget()->ShouldUseNativeFrame() ?
          kPopupBottomSpacingGlass : kPopupBottomSpacingNonGlass);
  return gfx::Size(0, location_bar_container_->GetPreferredSize().height() +
      vertical_spacing);
}

void ToolbarView::Layout() {
  // If we have not been initialized yet just do nothing.
  if (back_ == NULL)
    return;

  bool maximized = browser_->window() && browser_->window()->IsMaximized();
  if (!is_display_mode_normal()) {
    int edge_width = maximized ?
        0 : kPopupBackgroundEdge->width();  // See OnPaint().
    SetLocationBarContainerBounds(gfx::Rect(edge_width, PopupTopSpacing(),
        std::max(0, width() - (edge_width * 2)),
        location_bar_container_->GetPreferredSize().height()));
    return;
  }

  int delta = !chrome::search::IsInstantExtendedAPIEnabled(
      browser_->profile()) ? 0 : kSearchTopButtonSpacing;

  int child_y = std::min(kVertSpacing, height()) + delta;
  // We assume all child elements are the same height.
  int child_height =
      std::min(back_->GetPreferredSize().height(), height() - child_y);

  // If the window is maximized, we extend the back button to the left so that
  // clicking on the left-most pixel will activate the back button.
  // TODO(abarth):  If the window becomes maximized but is not resized,
  //                then Layout() might not be called and the back button
  //                will be slightly the wrong size.  We should force a
  //                Layout() in this case.
  //                http://crbug.com/5540
  int back_width = back_->GetPreferredSize().width();
  if (maximized)
    back_->SetBounds(0, child_y, back_width + kLeftEdgeSpacing, child_height);
  else
    back_->SetBounds(kLeftEdgeSpacing, child_y, back_width, child_height);

  forward_->SetBounds(back_->x() + back_->width() + kButtonSpacing,
      child_y, forward_->GetPreferredSize().width(), child_height);

  reload_->SetBounds(forward_->x() + forward_->width() + kButtonSpacing,
      child_y, reload_->GetPreferredSize().width(), child_height);

  if (show_home_button_.GetValue()) {
    home_->SetVisible(true);
    home_->SetBounds(reload_->x() + reload_->width() + kButtonSpacing, child_y,
                     home_->GetPreferredSize().width(), child_height);
  } else {
    home_->SetVisible(false);
    home_->SetBounds(reload_->x() + reload_->width(), child_y, 0, child_height);
  }

  int top_delta = !chrome::search::IsInstantExtendedAPIEnabled(
      browser_->profile()) ? 0 : kSearchTopLocationBarSpacing;

  int browser_actions_width = browser_actions_->GetPreferredSize().width();
  int app_menu_width = app_menu_->GetPreferredSize().width();
  int location_x = home_->x() + home_->width() + kStandardSpacing;
  int available_width = std::max(0, width() - kRightEdgeSpacing -
      app_menu_width - browser_actions_width - location_x);
  int location_y = std::min(location_bar_vert_spacing() + top_delta,
                            height());
  int available_height = location_bar_->GetPreferredSize().height();
  const gfx::Rect location_bar_bounds(location_x, location_y,
                                      available_width, available_height);

  // In NTP mode, the location bar needs content area's bounds to layout within
  // it, so we skip doing that here. When the browser view finished setting the
  // tab content bounds, we then layout the NTP location bar over it.
  const chrome::search::Mode& si_mode(browser_->search_model()->mode());
  if (si_mode.is_ntp()) {
    // Force the reload button to go into disabled mode to display the grey
    // circle and not the grey cross. The disabled reload state only exists for
    // ntp pages.
    chrome::UpdateCommandEnabled(browser_, IDC_RELOAD, false);
    // Disable zooming for NTP mode.
    chrome::UpdateCommandEnabled(browser_, IDC_ZOOM_MINUS, false);
    chrome::UpdateCommandEnabled(browser_, IDC_ZOOM_PLUS, false);
  } else {
    // Start the location bar animation.
    if (si_mode.animate && si_mode.is_search() &&
        !location_bar_container_->IsAnimating()) {
      gfx::Point location_bar_origin(location_bar_bounds.origin());
      views::View::ConvertPointToView(this, location_bar_container_->parent(),
                                      &location_bar_origin);
      location_bar_container_->AnimateTo(
          gfx::Rect(location_bar_origin, location_bar_bounds.size()));
    } else {
      SetLocationBarContainerBounds(location_bar_bounds);
    }
    // Enable reload and zooming for non-NTP modes.
    chrome::UpdateCommandEnabled(browser_, IDC_RELOAD, true);
    chrome::UpdateCommandEnabled(browser_, IDC_ZOOM_MINUS, true);
    chrome::UpdateCommandEnabled(browser_, IDC_ZOOM_PLUS, true);
  }

  browser_actions_->SetBounds(location_bar_bounds.right(), 0,
                              browser_actions_width, height());
  // The browser actions need to do a layout explicitly, because when an
  // extension is loaded/unloaded/changed, BrowserActionContainer removes and
  // re-adds everything, regardless of whether it has a page action. For a
  // page action, browser action bounds do not change, as a result of which
  // SetBounds does not do a layout at all.
  // TODO(sidchat): Rework the above behavior so that explicit layout is not
  //                required.
  browser_actions_->Layout();

  // Extend the app menu to the screen's right edge in maximized mode just like
  // we extend the back button to the left edge.
  if (maximized)
    app_menu_width += kRightEdgeSpacing;
  app_menu_->SetBounds(browser_actions_->x() + browser_actions_width, child_y,
                       app_menu_width, child_height);
}

bool ToolbarView::HitTest(const gfx::Point& point) const {
  // Don't take hits in our top shadow edge.  Let them fall through to the
  // tab strip above us.
  if (point.y() < kContentShadowHeight)
    return false;
  // Otherwise let our superclass take care of it.
  return AccessiblePaneView::HitTest(point);
}

void ToolbarView::OnPaint(gfx::Canvas* canvas) {
  View::OnPaint(canvas);

  if (is_display_mode_normal())
    return;

  // In maximized mode, we don't draw the endcaps on the location bar, because
  // when they're flush against the edge of the screen they just look glitchy.
  if (!browser_->window() || !browser_->window()->IsMaximized()) {
    int top_spacing = PopupTopSpacing();
    canvas->DrawImageInt(*kPopupBackgroundEdge, 0, top_spacing);
    canvas->DrawImageInt(*kPopupBackgroundEdge,
                         width() - kPopupBackgroundEdge->width(), top_spacing);
  }

  // For glass, we need to draw a black line below the location bar to separate
  // it from the content area.  For non-glass, the NonClientView draws the
  // toolbar background below the location bar for us.
  // NOTE: Keep this in sync with BrowserView::GetInfoBarSeparatorColor()!
  if (GetWidget()->ShouldUseNativeFrame())
    canvas->FillRect(gfx::Rect(0, height() - 1, width(), 1), SK_ColorBLACK);
}

// Note this method is ignored on Windows, but needs to be implemented for
// linux, where it is called before CanDrop().
bool ToolbarView::GetDropFormats(
    int* formats,
    std::set<OSExchangeData::CustomFormat>* custom_formats) {
  *formats = ui::OSExchangeData::URL | ui::OSExchangeData::STRING;
  return true;
}

bool ToolbarView::CanDrop(const ui::OSExchangeData& data) {
  // To support loading URLs by dropping into the toolbar, we need to support
  // dropping URLs and/or text.
  return data.HasURL() || data.HasString();
}

int ToolbarView::OnDragUpdated(const views::DropTargetEvent& event) {
  if (event.source_operations() & ui::DragDropTypes::DRAG_COPY) {
    return ui::DragDropTypes::DRAG_COPY;
  } else if (event.source_operations() & ui::DragDropTypes::DRAG_LINK) {
    return ui::DragDropTypes::DRAG_LINK;
  }
  return ui::DragDropTypes::DRAG_NONE;
}

int ToolbarView::OnPerformDrop(const views::DropTargetEvent& event) {
  return location_bar_->GetLocationEntry()->OnPerformDrop(event);
}

void ToolbarView::OnThemeChanged() {
  LoadImages();
}

std::string ToolbarView::GetClassName() const {
  return kViewClassName;
}

bool ToolbarView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  const views::View* focused_view = focus_manager_->GetFocusedView();
  if (focused_view == location_bar_)
    return false;  // Let location bar handle all accelerator events.
  return AccessiblePaneView::AcceleratorPressed(accelerator);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, protected:

// Override this so that when the user presses F6 to rotate toolbar panes,
// the location bar gets focus, not the first control in the toolbar - and
// also so that it selects all content in the location bar.
bool ToolbarView::SetPaneFocusAndFocusDefault() {
  if (!location_bar_->HasFocus()) {
    location_bar_->FocusLocation(true);
    return true;
  }

  if (!AccessiblePaneView::SetPaneFocusAndFocusDefault())
    return false;
  browser_->window()->RotatePaneFocus(true);
  return true;
}

void ToolbarView::RemovePaneFocus() {
  AccessiblePaneView::RemovePaneFocus();
  location_bar_->SetShowFocusRect(false);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, private:

bool ToolbarView::ShouldShowUpgradeRecommended() {
#if defined(OS_CHROMEOS)
  // In chromeos, the update recommendation is shown in the system tray. So it
  // should not be displayed in the wrench menu.
  return false;
#else
  return (UpgradeDetector::GetInstance()->notify_upgrade());
#endif
}

bool ToolbarView::ShouldShowIncompatibilityWarning() {
#if defined(OS_WIN)
  EnumerateModulesModel* loaded_modules = EnumerateModulesModel::GetInstance();
  return loaded_modules->ShouldShowConflictWarning();
#else
  return false;
#endif
}

int ToolbarView::PopupTopSpacing() const {
  return GetWidget()->ShouldUseNativeFrame() ? 0 : kPopupTopSpacingNonGlass;
}

void ToolbarView::LoadImages() {
  ui::ThemeProvider* tp = GetThemeProvider();

  back_->SetImage(views::CustomButton::BS_NORMAL,
      tp->GetImageSkiaNamed(IDR_BACK));
  back_->SetImage(views::CustomButton::BS_HOT,
      tp->GetImageSkiaNamed(IDR_BACK_H));
  back_->SetImage(views::CustomButton::BS_PUSHED,
      tp->GetImageSkiaNamed(IDR_BACK_P));
  back_->SetImage(views::CustomButton::BS_DISABLED,
      tp->GetImageSkiaNamed(IDR_BACK_D));

  forward_->SetImage(views::CustomButton::BS_NORMAL,
      tp->GetImageSkiaNamed(IDR_FORWARD));
  forward_->SetImage(views::CustomButton::BS_HOT,
      tp->GetImageSkiaNamed(IDR_FORWARD_H));
  forward_->SetImage(views::CustomButton::BS_PUSHED,
      tp->GetImageSkiaNamed(IDR_FORWARD_P));
  forward_->SetImage(views::CustomButton::BS_DISABLED,
      tp->GetImageSkiaNamed(IDR_FORWARD_D));

  reload_->LoadImages(tp);

  home_->SetImage(views::CustomButton::BS_NORMAL,
      tp->GetImageSkiaNamed(IDR_HOME));
  home_->SetImage(views::CustomButton::BS_HOT,
      tp->GetImageSkiaNamed(IDR_HOME_H));
  home_->SetImage(views::CustomButton::BS_PUSHED,
      tp->GetImageSkiaNamed(IDR_HOME_P));

  app_menu_->SetIcon(GetAppMenuIcon(views::CustomButton::BS_NORMAL));
  app_menu_->SetHoverIcon(GetAppMenuIcon(views::CustomButton::BS_HOT));
  app_menu_->SetPushedIcon(GetAppMenuIcon(views::CustomButton::BS_PUSHED));
}

void ToolbarView::ShowCriticalNotification() {
#if defined(OS_WIN)
  CriticalNotificationBubbleView* bubble_delegate =
      new CriticalNotificationBubbleView(app_menu_);
  views::BubbleDelegateView::CreateBubble(bubble_delegate);
  bubble_delegate->StartFade(true);
#endif
}

void ToolbarView::UpdateAppMenuState() {
  string16 accname_app = l10n_util::GetStringUTF16(IDS_ACCNAME_APP);
  if (ShouldShowUpgradeRecommended()) {
    accname_app = l10n_util::GetStringFUTF16(
        IDS_ACCNAME_APP_UPGRADE_RECOMMENDED, accname_app);
  }
  app_menu_->SetAccessibleName(accname_app);

  app_menu_->SetIcon(GetAppMenuIcon(views::CustomButton::BS_NORMAL));
  app_menu_->SetHoverIcon(GetAppMenuIcon(views::CustomButton::BS_HOT));
  app_menu_->SetPushedIcon(GetAppMenuIcon(views::CustomButton::BS_PUSHED));
  SchedulePaint();
}

void ToolbarView::LayoutLocationBarNTP() {
  // TODO(kuan): this likely needs to cancel animations.

  WebContents* contents = chrome::GetActiveWebContents(browser_);
#if defined(USE_AURA)
  // Under aura we can't use WebContentsView::GetContainerBounds since it is
  // affected by any animations that scale the window (such as during startup).
  // Instead we convert coordinates using aura::Window.
  aura::Window* contents_view = contents && contents->GetView() ?
      contents->GetView()->GetNativeView() : NULL;
  if (!contents_view)
    return;

  aura::Window* browser_window = GetWidget()->GetNativeView();
  // BrowserWindow may not contain contents during startup on the lock screen.
  if (!browser_window || !browser_window->Contains(contents_view))
    return;

  gfx::Size contents_size(contents_view->bounds().size());
  gfx::Rect location_rect = chrome::search::GetNTPOmniboxBounds(contents_size);
  if (location_rect.width() == 0)
    return;

  gfx::Point location_container_origin;
  aura::Window::ConvertPointToWindow(
      contents_view, browser_window, &location_container_origin);
  views::View::ConvertPointFromWidget(location_bar_container_->parent(),
                                      &location_container_origin);
  location_container_origin =
      location_container_origin.Add(location_rect.origin());
#else
  // Get screen bounds of web contents page.
  gfx::Rect web_rect_in_screen;
  if (contents && contents->GetView())
    contents->GetView()->GetContainerBounds(&web_rect_in_screen);
  // No need to layout NTP location bar if there's no web contents page yet.
  if (web_rect_in_screen.IsEmpty())
    return;

  gfx::Rect location_rect = chrome::search::GetNTPOmniboxBounds(
      web_rect_in_screen.size());
  if (location_rect.width() == 0)
    return;

  gfx::Point location_container_origin(
      web_rect_in_screen.x() + location_rect.x(),
      web_rect_in_screen.y() + location_rect.y());
  views::View::ConvertPointFromScreen(location_bar_container_->parent(),
                                      &location_container_origin);
#endif

  location_bar_container_->SetInToolbar(false);
  location_bar_container_->SetBounds(
      location_container_origin.x(),
      location_container_origin.y(),
      location_rect.width(),
      location_bar_container_->GetPreferredSize().height());
}

void ToolbarView::SetLocationBarContainerBounds(
    const gfx::Rect& bounds) {
  if (location_bar_container_->IsAnimating())
    return;

  // LocationBarContainer is not a child of the ToolbarView.
  gfx::Point origin(bounds.origin());
  views::View::ConvertPointToView(this, location_bar_container_->parent(),
                                  &origin);
  gfx::Rect target_bounds(origin, bounds.size());
  if (location_bar_container_->GetTargetBounds() != target_bounds) {
    location_bar_container_->SetInToolbar(true);
    location_bar_container_->SetBoundsRect(target_bounds);
  }
}
