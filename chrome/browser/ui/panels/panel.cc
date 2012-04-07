// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/panel.h"

#include "base/logging.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/panels/native_panel.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/window_sizer.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/rect.h"

#if defined(USE_AURA)
#include "chrome/browser/ui/panels/panel_browser_view.h"
#endif

using content::SSLStatus;
using content::WebContents;

// static
const Extension* Panel::GetExtensionFromBrowser(Browser* browser) {
  // Find the extension. When we create a panel from an extension, the extension
  // ID is passed as the app name to the Browser.
  ExtensionService* extension_service =
      browser->GetProfile()->GetExtensionService();
  return extension_service->GetExtensionById(
      web_app::GetExtensionIdFromApplicationName(browser->app_name()), false);
}

Panel::Panel(Browser* browser, const gfx::Size& requested_size)
    : browser_(browser),
      initialized_(false),
      has_temporary_layout_(false),
      restored_size_(requested_size),
      auto_resizable_(false),
      draggable_(true),
      expansion_state_(EXPANDED),
      old_expansion_state_(EXPANDED),
      app_icon_visible_(true) {
}

Panel::~Panel() {
  // Invoked by native panel destructor. Do not access native_panel_ here.
}

void Panel::Initialize(const gfx::Rect& bounds) {
  DCHECK(!initialized_);
  DCHECK(!bounds.IsEmpty());
  initialized_ = true;
  native_panel_ = CreateNativePanel(browser_, this, bounds);
}

void Panel::OnNativePanelClosed() {
  if (auto_resizable_)
    native_panel_->GetPanelBrowser()->tabstrip_model()->RemoveObserver(this);
  manager()->Remove(this);
}

PanelManager* Panel::manager() const {
  return PanelManager::GetInstance();
}

const Extension* Panel::GetExtension() const {
  return GetExtensionFromBrowser(browser());
}

void Panel::SetPanelBounds(const gfx::Rect& bounds) {
  if (expansion_state_ == Panel::EXPANDED)
    restored_size_ = bounds.size();

  native_panel_->SetPanelBounds(bounds);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_CHANGED_BOUNDS,
      content::Source<Panel>(this),
      content::NotificationService::NoDetails());
}

void Panel::SetPanelBoundsInstantly(const gfx::Rect& bounds) {
  if (expansion_state_ == Panel::EXPANDED)
    restored_size_ = bounds.size();

  native_panel_->SetPanelBoundsInstantly(bounds);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_CHANGED_BOUNDS,
      content::Source<Panel>(this),
      content::NotificationService::NoDetails());
}

void Panel::SetAutoResizable(bool resizable) {
  if (auto_resizable_ == resizable)
    return;

  auto_resizable_ = resizable;
  if (auto_resizable_) {
    browser()->tabstrip_model()->AddObserver(this);
    WebContents* web_contents = browser()->GetSelectedWebContents();
    if (web_contents)
      EnableWebContentsAutoResize(web_contents);
  } else {
    browser()->tabstrip_model()->RemoveObserver(this);
    registrar_.RemoveAll();
  }
}

void Panel::SetSizeRange(const gfx::Size& min_size, const gfx::Size& max_size) {
  if (min_size == min_size_ && max_size == max_size_)
    return;

  DCHECK(min_size.width() <= max_size.width());
  DCHECK(min_size.height() <= max_size.height());
  min_size_ = min_size;
  max_size_ = max_size;

  ConfigureAutoResize(browser()->GetSelectedWebContents());
}

void Panel::SetAppIconVisibility(bool visible) {
  if (app_icon_visible_ == visible)
    return;
  app_icon_visible_ = visible;
  native_panel_->SetPanelAppIconVisibility(visible);
}

void Panel::SetExpansionState(ExpansionState new_state) {
  if (expansion_state_ == new_state)
    return;
  old_expansion_state_ = expansion_state_;
  expansion_state_ = new_state;

  manager()->OnPanelExpansionStateChanged(this);

  // The minimized panel should not get the focus.
  if (expansion_state_ == MINIMIZED)
    Deactivate();

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_CHANGED_EXPANSION_STATE,
      content::Source<Panel>(this),
      content::NotificationService::NoDetails());
}

bool Panel::IsDrawingAttention() const {
  return native_panel_->IsDrawingAttention();
}

void Panel::FullScreenModeChanged(bool is_full_screen) {
  native_panel_->FullScreenModeChanged(is_full_screen);
}

void Panel::Show() {
  // Don't show panel as active if it is in overflow state.
  if (expansion_state_ == IN_OVERFLOW)
    ShowInactive();
  else
    native_panel_->ShowPanel();
}

void Panel::ShowInactive() {
  native_panel_->ShowPanelInactive();
}

void Panel::SetBounds(const gfx::Rect& bounds) {
  // Ignore bounds position as the panel manager controls all positioning.
  manager()->ResizePanel(this, bounds.size());
}

// Close() may be called multiple times if the browser window is not ready to
// close on the first attempt.
void Panel::Close() {
  native_panel_->ClosePanel();
}

void Panel::Activate() {
  // Make sure the panel is expanded when activated programmatically,
  // so the user input does not go into collapsed window.
  SetExpansionState(Panel::EXPANDED);
  native_panel_->ActivatePanel();
}

void Panel::Deactivate() {
  native_panel_->DeactivatePanel();
}

bool Panel::IsActive() const {
  return native_panel_->IsPanelActive();
}

void Panel::FlashFrame(bool flash) {
  if (IsDrawingAttention() == flash)
    return;

  // Don't draw attention for an active panel.
  if (flash && IsActive())
    return;

  native_panel_->DrawAttention(flash);
  manager()->OnPanelAttentionStateChanged(this);
}

gfx::NativeWindow Panel::GetNativeHandle() {
  return native_panel_->GetNativePanelHandle();
}

BrowserWindowTesting* Panel::GetBrowserWindowTesting() {
  NOTIMPLEMENTED();
  return NULL;
}

StatusBubble* Panel::GetStatusBubble() {
  // TODO(jennb): Implement. http://crbug.com/102723
  return NULL;
}

void Panel::ToolbarSizeChanged(bool is_animating){
  NOTIMPLEMENTED();
}

void Panel::UpdateTitleBar() {
  native_panel_->UpdatePanelTitleBar();
}

void Panel::BookmarkBarStateChanged(
    BookmarkBar::AnimateChangeType change_type) {
  NOTIMPLEMENTED();
}

void Panel::UpdateDevTools() {
  NOTIMPLEMENTED();
}

void Panel::SetDevToolsDockSide(DevToolsDockSide side) {
  NOTIMPLEMENTED();
}

void Panel::UpdateLoadingAnimations(bool should_animate) {
  native_panel_->UpdatePanelLoadingAnimations(should_animate);
}

void Panel::SetStarredState(bool is_starred) {
  // Since panels are typically not bookmarked extension UI windows, they don't
  // have starred state.
}

gfx::Rect Panel::GetRestoredBounds() const {
  gfx::Rect bounds = native_panel_->GetPanelBounds();
  bounds.set_y(bounds.bottom() - restored_size_.height());
  bounds.set_x(bounds.right() - restored_size_.width());
  bounds.set_size(restored_size_);
  return bounds;
}

gfx::Rect Panel::GetBounds() const {
  return native_panel_->GetPanelBounds();
}

int Panel::TitleOnlyHeight() const {
  return native_panel_->TitleOnlyHeight();
}

gfx::Size Panel::IconOnlySize() const {
  return native_panel_->IconOnlySize();
}

void Panel::EnsureFullyVisible() {
  native_panel_->EnsurePanelFullyVisible();
}

bool Panel::IsMaximized() const {
  // Size of panels is managed by PanelManager, they are never 'zoomed'.
  return false;
}

bool Panel::IsMinimized() const {
  return expansion_state_ != EXPANDED;
}

void Panel::Maximize() {
  Restore();
}

void Panel::Minimize() {
  if (expansion_state_ != EXPANDED)
    return;

  if (IsDrawingAttention())
    SetExpansionState(TITLE_ONLY);
  else
    SetExpansionState(MINIMIZED);
}

void Panel::Restore() {
  SetExpansionState(EXPANDED);
}

void Panel::EnterFullscreen(
      const GURL& url, FullscreenExitBubbleType type) {
  NOTIMPLEMENTED();
}

void Panel::ExitFullscreen() {
  NOTIMPLEMENTED();
}

void Panel::UpdateFullscreenExitBubbleContent(
      const GURL& url,
      FullscreenExitBubbleType bubble_type) {
  NOTIMPLEMENTED();
}

bool Panel::IsFullscreen() const {
  return false;
}

bool Panel::IsFullscreenBubbleVisible() const {
  NOTIMPLEMENTED();
  return false;
}

LocationBar* Panel::GetLocationBar() const {
#if defined(USE_AURA)
  // TODO(stevenjb): Remove this when Aura panels are implemented post R18.
  PanelBrowserView* panel_view = static_cast<PanelBrowserView*>(native_panel_);
  return panel_view->GetLocationBar();
#else
  // Panels do not have a location bar.
  return NULL;
#endif
}

void Panel::SetFocusToLocationBar(bool select_all) {
#if defined(USE_AURA)
  // TODO(stevenjb): Remove this when Aura panels are implemented post R18.
  PanelBrowserView* panel_view = static_cast<PanelBrowserView*>(native_panel_);
  panel_view->SetFocusToLocationBar(select_all);
#else
  // Panels do not have a location bar.
#endif
}

void Panel::UpdateReloadStopState(bool is_loading, bool force) {
  // Panels don't have stop/reload indicator.
}

void Panel::UpdateToolbar(TabContentsWrapper* contents,
                          bool should_restore_state) {
  // Panels do not have a toolbar.
}

void Panel::FocusToolbar() {
  // Panels do not have a toolbar.
}

void Panel::FocusAppMenu() {
  NOTIMPLEMENTED();
}

void Panel::FocusBookmarksToolbar() {
  NOTIMPLEMENTED();
}

void Panel::FocusChromeOSStatus() {
  NOTIMPLEMENTED();
}

void Panel::RotatePaneFocus(bool forwards) {
  NOTIMPLEMENTED();
}

bool Panel::IsBookmarkBarVisible() const {
  return false;
}

bool Panel::IsBookmarkBarAnimating() const {
  return false;
}

// This is used by extensions to decide if a window can be closed.
// Always return true as panels do not have tabs that can be dragged,
// during which extensions will avoid closing a window.
bool Panel::IsTabStripEditable() const {
  return true;
}

bool Panel::IsToolbarVisible() const {
  NOTIMPLEMENTED();
  return false;
}

gfx::Rect Panel::GetRootWindowResizerRect() const {
  return gfx::Rect();
}

bool Panel::IsPanel() const {
  return true;
}

void Panel::DisableInactiveFrame() {
  NOTIMPLEMENTED();
}

void Panel::ConfirmSetDefaultSearchProvider(WebContents* web_contents,
                                            TemplateURL* template_url,
                                            Profile* profile) {
  NOTIMPLEMENTED();
}

void Panel::ConfirmAddSearchProvider(const TemplateURL* template_url,
                                     Profile* profile) {
  NOTIMPLEMENTED();
}

void Panel::ToggleBookmarkBar() {
  NOTIMPLEMENTED();
}

void Panel::ShowAboutChromeDialog() {
  NOTIMPLEMENTED();
}

void Panel::ShowUpdateChromeDialog() {
  NOTIMPLEMENTED();
}

void Panel::ShowTaskManager() {
  native_panel_->ShowTaskManagerForPanel();
}

void Panel::ShowBackgroundPages() {
  NOTIMPLEMENTED();
}

void Panel::ShowBookmarkBubble(const GURL& url, bool already_bookmarked) {
  NOTIMPLEMENTED();
}

bool Panel::IsDownloadShelfVisible() const {
  return false;
}

DownloadShelf* Panel::GetDownloadShelf() {
  Browser* panel_browser = native_panel_->GetPanelBrowser();
  Profile* profile = panel_browser->GetProfile();
  Browser* tabbed_browser = Browser::GetTabbedBrowser(profile, true);

  if (!tabbed_browser) {
    // Set initial bounds so window will not be positioned at an offset
    // to this panel as panels are at the bottom of the screen.
    gfx::Rect window_bounds;
    WindowSizer::GetBrowserWindowBounds(std::string(), gfx::Rect(),
                                        panel_browser, &window_bounds);
    Browser::CreateParams params(Browser::TYPE_TABBED, profile);
    params.initial_bounds = window_bounds;
    tabbed_browser = Browser::CreateWithParams(params);
    tabbed_browser->NewTab();
  }

  tabbed_browser->window()->Show();  // Ensure download shelf is visible.
  return tabbed_browser->window()->GetDownloadShelf();
}

void Panel::ShowCollectedCookiesDialog(TabContentsWrapper* wrapper) {
  NOTIMPLEMENTED();
}

void Panel::ConfirmBrowserCloseWithPendingDownloads() {
  NOTIMPLEMENTED();
}

void Panel::UserChangedTheme() {
  native_panel_->NotifyPanelOnUserChangedTheme();
}

int Panel::GetExtraRenderViewHeight() const {
  // This is currently used only on Linux and that returns the height for
  // optional elements like bookmark bar, download bar etc.  Not applicable to
  // panels.
  return 0;
}

void Panel::WebContentsFocused(WebContents* contents) {
  native_panel_->PanelWebContentsFocused(contents);
}

void Panel::ShowPageInfo(Profile* profile,
                         const GURL& url,
                         const SSLStatus& ssl,
                         bool show_history) {
  NOTIMPLEMENTED();
}

void Panel::ShowAppMenu() {
  NOTIMPLEMENTED();
}

bool Panel::PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                   bool* is_keyboard_shortcut) {
  return native_panel_->PreHandlePanelKeyboardEvent(event,
                                                    is_keyboard_shortcut);
}

void Panel::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
  native_panel_->HandlePanelKeyboardEvent(event);
}

void Panel::ShowCreateWebAppShortcutsDialog(TabContentsWrapper* tab_contents) {
  NOTIMPLEMENTED();
}

void Panel::ShowCreateChromeAppShortcutsDialog(Profile* profile,
                                               const Extension* app) {
  NOTIMPLEMENTED();
}

void Panel::Cut() {
  native_panel_->PanelCut();
}

void Panel::Copy() {
  native_panel_->PanelCopy();
}

void Panel::Paste() {
  native_panel_->PanelPaste();
}

#if defined(OS_MACOSX)
void Panel::OpenTabpose() {
  NOTIMPLEMENTED();
}

void Panel::EnterPresentationMode(
      const GURL& url,
      FullscreenExitBubbleType content_type) {
  NOTIMPLEMENTED();
}

void Panel::ExitPresentationMode() {
  NOTIMPLEMENTED();
}

bool Panel::InPresentationMode() {
  NOTIMPLEMENTED();
  return false;
}
#endif

void Panel::ShowInstant(TabContentsWrapper* preview) {
  NOTIMPLEMENTED();
}

void Panel::HideInstant() {
  NOTIMPLEMENTED();
}

gfx::Rect Panel::GetInstantBounds() {
  NOTIMPLEMENTED();
  return gfx::Rect();
}

WindowOpenDisposition Panel::GetDispositionForPopupBounds(
    const gfx::Rect& bounds) {
#if defined(USE_AURA)
  // TODO(stevenjb): Remove this when Aura panels are implemented post R18.
  PanelBrowserView* panel_view = static_cast<PanelBrowserView*>(native_panel_);
  return panel_view->GetDispositionForPopupBounds(bounds);
#else
  return NEW_POPUP;
#endif
}

FindBar* Panel::CreateFindBar() {
  return native_panel_->CreatePanelFindBar();
}

#if defined(OS_CHROMEOS)
void Panel::ShowMobileSetup() {
  NOTIMPLEMENTED();
}

void Panel::ShowKeyboardOverlay(gfx::NativeWindow owning_window) {
  NOTIMPLEMENTED();
}
#endif

void Panel::UpdatePreferredSize(WebContents* web_contents,
                                const gfx::Size& pref_size) {
  if (auto_resizable_) {
    return manager()->OnPreferredWindowSizeChanged(this,
        native_panel_->WindowSizeFromContentSize(pref_size));
  }
}

void Panel::ShowAvatarBubble(WebContents* web_contents, const gfx::Rect& rect) {
  // Panels will never show a new tab page so this should never be called.
  NOTREACHED();
}

void Panel::ShowAvatarBubbleFromAvatarButton() {
  // Panels will never show an avatar button so this should never be called.
  NOTREACHED();
}

void Panel::TabInsertedAt(TabContentsWrapper* contents,
                          int index,
                          bool foreground) {
  if (auto_resizable_) {
    DCHECK_EQ(0, index);
    EnableWebContentsAutoResize(contents->web_contents());
  }
}

void Panel::EnableWebContentsAutoResize(WebContents* web_contents) {
  DCHECK(web_contents);
  ConfigureAutoResize(web_contents);

  // We also need to know when the render view host changes in order
  // to turn on auto-resize notifications in the new render view host.
  registrar_.RemoveAll();  // Stop notifications for previous contents, if any.
  registrar_.Add(
      this,
      content::NOTIFICATION_WEB_CONTENTS_SWAPPED,
      content::Source<WebContents>(web_contents));
}

void Panel::Observe(int type,
                    const content::NotificationSource& source,
                    const content::NotificationDetails& details) {
  DCHECK_EQ(type, content::NOTIFICATION_WEB_CONTENTS_SWAPPED);
  ConfigureAutoResize(content::Source<WebContents>(source).ptr());
}

void Panel::ConfigureAutoResize(WebContents* web_contents) {
  if (!auto_resizable_ || !web_contents)
    return;

  // NULL might be returned if the tab has not been added.
  RenderViewHost* render_view_host = web_contents->GetRenderViewHost();
  if (!render_view_host)
    return;

  render_view_host->EnableAutoResize(
      min_size_,
      native_panel_->ContentSizeFromWindowSize(max_size_));
}

void Panel::OnWindowSizeAvailable() {
  ConfigureAutoResize(browser()->GetSelectedWebContents());
}

void Panel::DestroyBrowser() {
  native_panel_->DestroyPanelBrowser();
}
