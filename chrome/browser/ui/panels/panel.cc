// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/panel.h"

#include "base/logging.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "ui/gfx/rect.h"

Panel::Panel(Browser* browser, const gfx::Rect& bounds)
    : bounds_(bounds),
#ifndef NDEBUG
      closing_(false),
#endif
      minimized_(false) {
  browser_window_.reset(CreateNativePanel(browser, this));
}

Panel::~Panel() {
  Close();
}

PanelManager* Panel::manager() const {
  return PanelManager::GetInstance();
}

void Panel::SetPanelBounds(const gfx::Rect& bounds) {
  if (bounds_ == bounds)
    return;
  bounds_ = bounds;
  browser_window_->SetBounds(bounds);
}

void Panel::Minimize() {
  if (minimized_)
    return;
  minimized_ = true;

  NOTIMPLEMENTED();
}

void Panel::Restore() {
  if (!minimized_)
    return;
  minimized_ = false;

  NOTIMPLEMENTED();
}

void Panel::Show() {
  browser_window_->Show();
}

void Panel::ShowInactive() {
  NOTIMPLEMENTED();
}

void Panel::SetBounds(const gfx::Rect& bounds) {
  // Ignore any SetBounds requests since the bounds are completely controlled
  // by panel manager.
}

void Panel::Close() {
  if (!browser_window_.get())
    return;

  // Mark that we're starting the closing process. This is used by the platform
  // specific BrowserWindow implementation to ensure Panel::Close() should be
  // called to close a panel.
#ifndef NDEBUG
  closing_ = true;
#endif

  browser_window_->Close();
  manager()->Remove(this);
}

void Panel::Activate() {
  browser_window_->Activate();
}

void Panel::Deactivate() {
  browser_window_->Deactivate();
}

bool Panel::IsActive() const {
  return browser_window_->IsActive();
}

void Panel::FlashFrame() {
  NOTIMPLEMENTED();
}

gfx::NativeWindow Panel::GetNativeHandle() {
  return browser_window_->GetNativeHandle();
}

BrowserWindowTesting* Panel::GetBrowserWindowTesting() {
  NOTIMPLEMENTED();
  return NULL;
}

StatusBubble* Panel::GetStatusBubble() {
  NOTIMPLEMENTED();
  return NULL;
}

void Panel::ToolbarSizeChanged(bool is_animating){
  NOTIMPLEMENTED();
}

void Panel::UpdateTitleBar() {
  browser_window_->UpdateTitleBar();
}

void Panel::ShelfVisibilityChanged() {
  NOTIMPLEMENTED();
}

void Panel::UpdateDevTools() {
  NOTIMPLEMENTED();
}

void Panel::UpdateLoadingAnimations(bool should_animate) {
  NOTIMPLEMENTED();
}

void Panel::SetStarredState(bool is_starred) {
  NOTIMPLEMENTED();
}

gfx::Rect Panel::GetRestoredBounds() const {
  return bounds_;
}

gfx::Rect Panel::GetBounds() const {
  return minimized_ ? minimized_bounds_ : bounds_;
}

bool Panel::IsMaximized() const {
  NOTIMPLEMENTED();
  return false;
}

void Panel::SetFullscreen(bool fullscreen) {
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
  NOTIMPLEMENTED();
  return NULL;
}

void Panel::SetFocusToLocationBar(bool select_all) {
  NOTIMPLEMENTED();
}

void Panel::UpdateReloadStopState(bool is_loading, bool force) {
  NOTIMPLEMENTED();
}

void Panel::UpdateToolbar(TabContentsWrapper* contents,
                          bool should_restore_state) {
  NOTIMPLEMENTED();
}

void Panel::FocusToolbar() {
  NOTIMPLEMENTED();
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

bool Panel::IsTabStripEditable() const {
  return false;
}

bool Panel::IsToolbarVisible() const {
  NOTIMPLEMENTED();
  return false;
}

void Panel::DisableInactiveFrame() {
  NOTIMPLEMENTED();
}

void Panel::ConfirmSetDefaultSearchProvider(
    TabContents* tab_contents,
    TemplateURL* template_url,
    TemplateURLModel* template_url_model) {
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
  NOTIMPLEMENTED();
}

void Panel::ShowBackgroundPages() {
  NOTIMPLEMENTED();
}

void Panel::ShowBookmarkBubble(const GURL& url, bool already_bookmarked) {
  NOTIMPLEMENTED();
}

bool Panel::IsDownloadShelfVisible() const {
  NOTIMPLEMENTED();
  return false;
}

DownloadShelf* Panel::GetDownloadShelf() {
  NOTIMPLEMENTED();
  return NULL;
}

void Panel::ShowRepostFormWarningDialog(TabContents* tab_contents) {
  NOTIMPLEMENTED();
}

void Panel::ShowCollectedCookiesDialog(TabContents* tab_contents) {
  NOTIMPLEMENTED();
}

void Panel::ShowThemeInstallBubble() {
  NOTIMPLEMENTED();
}

void Panel::ConfirmBrowserCloseWithPendingDownloads() {
  NOTIMPLEMENTED();
}

void Panel::ShowHTMLDialog(HtmlDialogUIDelegate* delegate,
                           gfx::NativeWindow parent_window) {
  NOTIMPLEMENTED();
}

void Panel::UserChangedTheme() {
  browser_window_->UserChangedTheme();
}

int Panel::GetExtraRenderViewHeight() const {
  NOTIMPLEMENTED();
  return -1;
}

void Panel::TabContentsFocused(TabContents* tab_contents) {
  NOTIMPLEMENTED();
}

void Panel::ShowPageInfo(Profile* profile,
                         const GURL& url,
                         const NavigationEntry::SSLStatus& ssl,
                         bool show_history) {
  NOTIMPLEMENTED();
}

void Panel::ShowAppMenu() {
  NOTIMPLEMENTED();
}

bool Panel::PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                   bool* is_keyboard_shortcut) {
  NOTIMPLEMENTED();
  return false;
}

void Panel::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
  NOTIMPLEMENTED();
}

void Panel::ShowCreateWebAppShortcutsDialog(TabContentsWrapper* tab_contents) {
  NOTIMPLEMENTED();
}

void Panel::ShowCreateChromeAppShortcutsDialog(Profile* profile,
                                               const Extension* app) {
  NOTIMPLEMENTED();
}

void Panel::ToggleUseCompactNavigationBar() {
  NOTIMPLEMENTED();
}

void Panel::Cut() {
  NOTIMPLEMENTED();
}

void Panel::Copy() {
  NOTIMPLEMENTED();
}

void Panel::Paste() {
  NOTIMPLEMENTED();
}

void Panel::ToggleTabStripMode() {
  NOTIMPLEMENTED();
}

#if defined(OS_MACOSX)
void Panel::OpenTabpose() {
  NOTIMPLEMENTED();
}
#endif

void Panel::PrepareForInstant() {
  NOTIMPLEMENTED();
}

void Panel::ShowInstant(TabContentsWrapper* preview) {
  NOTIMPLEMENTED();
}

void Panel::HideInstant(bool instant_is_active) {
  NOTIMPLEMENTED();
}

gfx::Rect Panel::GetInstantBounds() {
  NOTIMPLEMENTED();
  return gfx::Rect();
}

WindowOpenDisposition Panel::GetDispositionForPopupBounds(
    const gfx::Rect& bounds) {
  NOTIMPLEMENTED();
  return NEW_POPUP;
}

#if defined(OS_CHROMEOS)
void Panel::ShowKeyboardOverlay(gfx::NativeWindow owning_window) {
  NOTIMPLEMENTED();
}
#endif

void Panel::DestroyBrowser() {
  NOTIMPLEMENTED();
}
