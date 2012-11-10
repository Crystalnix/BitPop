// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_VIEW_H_

#include <map>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/timer.h"
#include "build/build_config.h"
#include "chrome/browser/infobars/infobar_container.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/search/search_types.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/unhandled_keyboard_event_handler.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/sys_color_change_listener.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/single_split_view_listener.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/client_view.h"

#if defined(OS_WIN)
#include "chrome/browser/hang_monitor/hung_plugin_action.h"
#include "chrome/browser/hang_monitor/hung_window_detector.h"
#endif

// NOTE: For more information about the objects and files in this directory,
// view: http://dev.chromium.org/developers/design-documents/browser-window

class BookmarkBarView;
class Browser;
class BrowserViewLayout;
class ContentsContainer;
class DownloadShelfView;
class FullscreenExitBubbleViews;
class InfoBarContainerView;
class LocationBarView;
class StatusBubbleViews;
class TabStrip;
class TabStripModel;
class ToolbarView;

#if defined(OS_WIN)
class JumpList;
#endif

#if defined(USE_AURA)
class BrowserLauncherItemController;
class SearchViewController;
#endif

namespace autofill {
class PasswordGenerator;
}

namespace content {
class RenderWidgetHost;
}

namespace extensions {
class Extension;
}

namespace views {
class AccessiblePaneView;
class ExternalFocusTracker;
class WebView;
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView
//
//  A ClientView subclass that provides the contents of a browser window,
//  including the TabStrip, toolbars, download shelves, the content area etc.
//
class BrowserView : public BrowserWindow,
                    public BrowserWindowTesting,
                    public TabStripModelObserver,
                    public ui::AcceleratorProvider,
                    public views::WidgetDelegate,
                    public views::WidgetObserver,
                    public views::ClientView,
                    public InfoBarContainer::Delegate,
                    public views::SingleSplitViewListener,
                    public gfx::SysColorChangeListener {
 public:
  // The browser view's class name.
  static const char kViewClassName[];

  explicit BrowserView(Browser* browser);
  virtual ~BrowserView();

  void set_frame(BrowserFrame* frame) { frame_ = frame; }
  BrowserFrame* frame() const { return frame_; }

#if defined(OS_WIN) || defined(USE_AURA)
  // Returns a pointer to the BrowserView* interface implementation (an
  // instance of this object, typically) for a given native window, or NULL if
  // there is no such association.
  //
  // Don't use this unless you only have a NativeWindow. In nearly all
  // situations plumb through browser and use it.
  static BrowserView* GetBrowserViewForNativeWindow(gfx::NativeWindow window);
#endif

  // Returns the BrowserView used for the specified Browser.
  static BrowserView* GetBrowserViewForBrowser(const Browser* browser);

  // Returns a Browser instance of this view.
  Browser* browser() const { return browser_.get(); }

  // Returns the apparent bounds of the toolbar, in BrowserView coordinates.
  // These differ from |toolbar_.bounds()| in that they match where the toolbar
  // background image is drawn -- slightly outside the "true" bounds
  // horizontally. Note that this returns the bounds for the toolbar area.
  virtual gfx::Rect GetToolbarBounds() const;

  // Returns the bounds of the content area, in the coordinates of the
  // BrowserView's parent.
  gfx::Rect GetClientAreaBounds() const;

  // Returns the constraining bounding box that should be used to lay out the
  // FindBar within. This is _not_ the size of the find bar, just the bounding
  // box it should be laid out within. The coordinate system of the returned
  // rect is in the coordinate system of the frame, since the FindBar is a child
  // window.
  gfx::Rect GetFindBarBoundingBox() const;

  // Returns the preferred height of the TabStrip. Used to position the OTR
  // avatar icon.
  virtual int GetTabStripHeight() const;

  // Takes some view's origin (relative to this BrowserView) and offsets it such
  // that it can be used as the source origin for seamlessly tiling the toolbar
  // background image over that view.
  gfx::Point OffsetPointForToolbarBackgroundImage(
      const gfx::Point& point) const;

  // Accessor for the TabStrip.
  TabStrip* tabstrip() const { return tabstrip_; }

  // Accessor for the Toolbar.
  ToolbarView* toolbar() const { return toolbar_; }

  // Returns true if various window components are visible.
  virtual bool IsTabStripVisible() const;

  // Returns true if the profile associated with this Browser window is
  // incognito.
  bool IsOffTheRecord() const;

  // Returns true if the profile associated with this Browser window is
  // a guest session.
  bool IsGuestSession() const;

  // Returns true if the non-client view should render an avatar icon.
  virtual bool ShouldShowAvatar() const;

  // Handle the specified |accelerator| being pressed.
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator) OVERRIDE;

  // Provides the containing frame with the accelerator for the specified
  // command id. This can be used to provide menu item shortcut hints etc.
  // Returns true if an accelerator was found for the specified |cmd_id|, false
  // otherwise.
  bool GetAccelerator(int cmd_id, ui::Accelerator* accelerator);

  // Shows the next app-modal dialog box, if there is one to be shown, or moves
  // an existing showing one to the front. Returns true if one was shown or
  // activated, false if none was shown.
  bool ActivateAppModalDialog() const;

  // Returns the selected WebContents/TabContents. Used by our
  // NonClientView's TabIconView::TabContentsProvider implementations.
  // TODO(beng): exposing this here is a bit bogus, since it's only used to
  // determine loading state. It'd be nicer if we could change this to be
  // bool IsSelectedTabLoading() const; or something like that. We could even
  // move it to a WindowDelegate subclass.
  content::WebContents* GetActiveWebContents() const;
  TabContents* GetActiveTabContents() const;

  // Retrieves the icon to use in the frame to indicate an OTR window.
  gfx::ImageSkia GetOTRAvatarIcon() const;

  // Returns true if the Browser object associated with this BrowserView is a
  // tabbed-type window (i.e. a browser window, not an app or popup).
  bool IsBrowserTypeNormal() const {
    return browser_->is_type_tabbed();
  }

  // Returns true if the specified point(BrowserView coordinates) is in
  // in the window caption area of the browser window.
  bool IsPositionInWindowCaption(const gfx::Point& point);

  // Returns whether the fullscreen bubble is visible or not.
  virtual bool IsFullscreenBubbleVisible() const OVERRIDE;

  // Invoked from the frame when the full screen state changes. This is only
  // used on Linux.
  void FullScreenStateChanged();

  // Restores the focused view. This is also used to set the initial focus
  // when a new browser window is created.
  void RestoreFocus();

  void SetWindowSwitcherButton(views::Button* button);

  views::Button* window_switcher_button() {
    return window_switcher_button_;
  }

  // Called from BookmarkBarView/DownloadShelfView during their show/hide
  // animations.
  void ToolbarSizeChanged(bool is_animating);

  // Returns the toolbar background color.
  SkColor GetToolbarBackgroundColor(chrome::search::Mode::Type mode);

  // Returns the toolbar background image.
  gfx::ImageSkia* GetToolbarBackgroundImage(chrome::search::Mode::Type mode);

#if defined(USE_ASH)
  BrowserLauncherItemController* launcher_item_controller() const {
    return launcher_item_controller_.get();
  }
#endif

  // Overridden from BrowserWindow:
  virtual void Show() OVERRIDE;
  virtual void ShowInactive() OVERRIDE;
  virtual void SetBounds(const gfx::Rect& bounds) OVERRIDE;
  virtual void Close() OVERRIDE;
  virtual void Activate() OVERRIDE;
  virtual void Deactivate() OVERRIDE;
  virtual bool IsActive() const OVERRIDE;
  virtual void FlashFrame(bool flash) OVERRIDE;
  virtual bool IsAlwaysOnTop() const OVERRIDE;
  virtual gfx::NativeWindow GetNativeWindow() OVERRIDE;
  virtual BrowserWindowTesting* GetBrowserWindowTesting() OVERRIDE;
  virtual StatusBubble* GetStatusBubble() OVERRIDE;
  virtual void UpdateTitleBar() OVERRIDE;
  virtual void BookmarkBarStateChanged(
      BookmarkBar::AnimateChangeType change_type) OVERRIDE;
  virtual void UpdateDevTools() OVERRIDE;
  virtual void SetDevToolsDockSide(DevToolsDockSide side) OVERRIDE;
  virtual void UpdateLoadingAnimations(bool should_animate) OVERRIDE;
  virtual void SetStarredState(bool is_starred) OVERRIDE;
  virtual void SetZoomIconState(
      ZoomController::ZoomIconState zoom_icon_state) OVERRIDE;
  virtual void SetZoomIconTooltipPercent(int zoom_percent) OVERRIDE;
  virtual void ShowZoomBubble(int zoom_percent) OVERRIDE;
  virtual gfx::Rect GetRestoredBounds() const OVERRIDE;
  virtual gfx::Rect GetBounds() const OVERRIDE;
  virtual bool IsMaximized() const OVERRIDE;
  virtual bool IsMinimized() const OVERRIDE;
  virtual void Maximize() OVERRIDE;
  virtual void Minimize() OVERRIDE;
  virtual void Restore() OVERRIDE;
  virtual void EnterFullscreen(
      const GURL& url, FullscreenExitBubbleType bubble_type) OVERRIDE;
  virtual void ExitFullscreen() OVERRIDE;
  virtual void UpdateFullscreenExitBubbleContent(
      const GURL& url,
      FullscreenExitBubbleType bubble_type) OVERRIDE;
  virtual bool IsFullscreen() const OVERRIDE;
#if defined(OS_WIN)
  virtual void SetMetroSnapMode(bool enable) OVERRIDE;
  virtual bool IsInMetroSnapMode() const OVERRIDE;
#endif
  virtual LocationBar* GetLocationBar() const OVERRIDE;
  virtual void SetFocusToLocationBar(bool select_all) OVERRIDE;
  virtual void UpdateReloadStopState(bool is_loading, bool force) OVERRIDE;
  virtual void UpdateToolbar(TabContents* contents,
                             bool should_restore_state) OVERRIDE;
  virtual void FocusToolbar() OVERRIDE;
  virtual void FocusAppMenu() OVERRIDE;
  virtual void FocusBookmarksToolbar() OVERRIDE;
  virtual void RotatePaneFocus(bool forwards) OVERRIDE;
  virtual void DestroyBrowser() OVERRIDE;
  virtual bool IsBookmarkBarVisible() const OVERRIDE;
  virtual bool IsBookmarkBarAnimating() const OVERRIDE;
  virtual bool IsTabStripEditable() const OVERRIDE;
  virtual bool IsToolbarVisible() const OVERRIDE;
  virtual bool IsPanel() const OVERRIDE;
  virtual gfx::Rect GetRootWindowResizerRect() const OVERRIDE;
  virtual void DisableInactiveFrame() OVERRIDE;
  virtual void ConfirmAddSearchProvider(TemplateURL* template_url,
                                        Profile* profile) OVERRIDE;
  virtual void ToggleBookmarkBar() OVERRIDE;
  virtual void ShowUpdateChromeDialog() OVERRIDE;
  virtual void ShowTaskManager() OVERRIDE;
  virtual void ShowBackgroundPages() OVERRIDE;
  virtual void ShowBookmarkBubble(const GURL& url,
                                  bool already_bookmarked) OVERRIDE;
  virtual void ShowChromeToMobileBubble() OVERRIDE;
#if defined(ENABLE_ONE_CLICK_SIGNIN)
  virtual void ShowOneClickSigninBubble(
      const StartSyncCallback& start_sync_callback) OVERRIDE;
#endif
  // TODO(beng): Not an override, move somewhere else.
  void SetDownloadShelfVisible(bool visible);
  virtual bool IsDownloadShelfVisible() const OVERRIDE;
  virtual DownloadShelf* GetDownloadShelf() OVERRIDE;
  virtual void ConfirmBrowserCloseWithPendingDownloads() OVERRIDE;
  virtual void UserChangedTheme() OVERRIDE;
  virtual int GetExtraRenderViewHeight() const OVERRIDE;
  virtual void WebContentsFocused(content::WebContents* contents) OVERRIDE;
  virtual void ShowPageInfo(content::WebContents* web_contents,
                            const GURL& url,
                            const content::SSLStatus& ssl,
                            bool show_history) OVERRIDE;
  virtual void ShowWebsiteSettings(Profile* profile,
                                   TabContents* tab_contents,
                                   const GURL& url,
                                   const content::SSLStatus& ssl,
                                   bool show_history) OVERRIDE;
  virtual void ShowAppMenu() OVERRIDE;
  virtual bool PreHandleKeyboardEvent(
      const content::NativeWebKeyboardEvent& event,
      bool* is_keyboard_shortcut) OVERRIDE;
  virtual void HandleKeyboardEvent(
      const content::NativeWebKeyboardEvent& event) OVERRIDE;
  virtual void ShowCreateChromeAppShortcutsDialog(
      Profile*, const extensions::Extension* app) OVERRIDE;
  virtual void Cut() OVERRIDE;
  virtual void Copy() OVERRIDE;
  virtual void Paste() OVERRIDE;
  virtual void ShowInstant(TabContents* preview) OVERRIDE;
  virtual void HideInstant() OVERRIDE;
  virtual gfx::Rect GetInstantBounds() OVERRIDE;
  virtual WindowOpenDisposition GetDispositionForPopupBounds(
      const gfx::Rect& bounds) OVERRIDE;
  virtual FindBar* CreateFindBar() OVERRIDE;
  virtual void ShowAvatarBubble(content::WebContents* web_contents,
                                const gfx::Rect& rect) OVERRIDE;
  virtual void ShowAvatarBubbleFromAvatarButton() OVERRIDE;
  virtual void ShowPasswordGenerationBubble(
      const gfx::Rect& rect,
      autofill::PasswordGenerator* password_generator,
      const webkit::forms::PasswordForm& form) OVERRIDE;

  // Overridden from BrowserWindowTesting:
  virtual BookmarkBarView* GetBookmarkBarView() const OVERRIDE;
  virtual LocationBarView* GetLocationBarView() const OVERRIDE;
  virtual views::View* GetTabContentsContainerView() const OVERRIDE;
  virtual ToolbarView* GetToolbarView() const OVERRIDE;

  // Overridden from TabStripModelObserver:
  virtual void TabDetachedAt(TabContents* contents, int index) OVERRIDE;
  virtual void TabDeactivated(TabContents* contents) OVERRIDE;
  virtual void ActiveTabChanged(TabContents* old_contents,
                                TabContents* new_contents,
                                int index,
                                bool user_gesture) OVERRIDE;
  virtual void TabReplacedAt(TabStripModel* tab_strip_model,
                             TabContents* old_contents,
                             TabContents* new_contents,
                             int index) OVERRIDE;
  virtual void TabStripEmpty() OVERRIDE;

  // Overridden from ui::AcceleratorProvider:
  virtual bool GetAcceleratorForCommandId(int command_id,
      ui::Accelerator* accelerator) OVERRIDE;

  // Overridden from views::WidgetDelegate:
  virtual bool CanResize() const OVERRIDE;
  virtual bool CanMaximize() const OVERRIDE;
  virtual bool CanActivate() const OVERRIDE;
  virtual string16 GetWindowTitle() const OVERRIDE;
  virtual string16 GetAccessibleWindowTitle() const OVERRIDE;
  virtual views::View* GetInitiallyFocusedView() OVERRIDE;
  virtual bool ShouldShowWindowTitle() const OVERRIDE;
  virtual gfx::ImageSkia GetWindowAppIcon() OVERRIDE;
  virtual gfx::ImageSkia GetWindowIcon() OVERRIDE;
  virtual bool ShouldShowWindowIcon() const OVERRIDE;
  virtual bool ExecuteWindowsCommand(int command_id) OVERRIDE;
  virtual std::string GetWindowName() const OVERRIDE;
  virtual void SaveWindowPlacement(const gfx::Rect& bounds,
                                   ui::WindowShowState show_state) OVERRIDE;
  virtual bool GetSavedWindowPlacement(
      gfx::Rect* bounds,
      ui::WindowShowState* show_state) const OVERRIDE;
  virtual views::View* GetContentsView() OVERRIDE;
  virtual views::ClientView* CreateClientView(views::Widget* widget) OVERRIDE;
  virtual void OnWindowBeginUserBoundsChange() OVERRIDE;
  virtual void OnWidgetMove() OVERRIDE;
  virtual views::Widget* GetWidget() OVERRIDE;
  virtual const views::Widget* GetWidget() const OVERRIDE;

  // Overridden from views::WidgetObserver:
  virtual void OnWidgetActivationChanged(views::Widget* widget,
                                         bool active) OVERRIDE;

  // Overridden from views::ClientView:
  virtual bool CanClose() OVERRIDE;
  virtual int NonClientHitTest(const gfx::Point& point) OVERRIDE;
  virtual gfx::Size GetMinimumSize() OVERRIDE;

  // InfoBarContainer::Delegate overrides
  virtual SkColor GetInfoBarSeparatorColor() const OVERRIDE;
  virtual void InfoBarContainerStateChanged(bool is_animating) OVERRIDE;
  virtual bool DrawInfoBarArrows(int* x) const OVERRIDE;

  // views::SingleSplitViewListener overrides:
  virtual bool SplitHandleMoved(views::SingleSplitView* sender) OVERRIDE;

  // gfx::ScopedSysColorChangeListener overrides:
  virtual void OnSysColorChange() OVERRIDE;

  // Returns the resource ID to use for the OTR icon, which depends on
  // which layout is being shown and whether we are full-screen.
  int GetOTRIconResourceID() const;

 protected:
  // Appends to |toolbars| a pointer to each AccessiblePaneView that
  // can be traversed using F6, in the order they should be traversed.
  // Abstracted here so that it can be extended for Chrome OS.
  virtual void GetAccessiblePanes(
      std::vector<views::AccessiblePaneView*>* panes);

  int last_focused_view_storage_id() const {
    return last_focused_view_storage_id_;
  }

  // Overridden from views::View:
  virtual std::string GetClassName() const OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual void PaintChildren(gfx::Canvas* canvas) OVERRIDE;
  virtual void ViewHierarchyChanged(bool is_add,
                                    views::View* parent,
                                    views::View* child) OVERRIDE;
  virtual void ChildPreferredSizeChanged(View* child) OVERRIDE;
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;

  // Factory Method.
  // Returns a new LayoutManager for this browser view. A subclass may
  // override to implement different layout policy.
  virtual views::LayoutManager* CreateLayoutManager() const;

  // Browser window related initializations.
  virtual void Init();

  // Callback for the loading animation(s) associated with this view.
  virtual void LoadingAnimationCallback();

 private:
  friend class BrowserViewLayout;
  FRIEND_TEST_ALL_PREFIXES(BrowserViewsAccessibilityTest,
                           TestAboutChromeViewAccObj);

  enum FullscreenType {
    FOR_DESKTOP,
    FOR_METRO
  };

  // We store this on linux because we must call ProcessFullscreen()
  // asynchronously from FullScreenStateChanged() instead of directly from
  // EnterFullscreen().
  struct PendingFullscreenRequest {
    PendingFullscreenRequest()
        : pending(false),
          bubble_type(FEB_TYPE_NONE) {}

    bool pending;
    GURL url;
    FullscreenExitBubbleType bubble_type;
  };

  // Returns the BrowserViewLayout.
  BrowserViewLayout* GetBrowserViewLayout() const;

  // Layout the Status Bubble.
  void LayoutStatusBubble();

  // Prepare to show the Bookmark Bar for the specified TabContents.
  // Returns true if the Bookmark Bar can be shown (i.e. it's supported for this
  // Browser type) and there should be a subsequent re-layout to show it.
  // |contents| can be NULL.
  bool MaybeShowBookmarkBar(TabContents* contents);

  // Prepare to show an Info Bar for the specified TabContents. Returns
  // true if there is an Info Bar to show and one is supported for this Browser
  // type, and there should be a subsequent re-layout to show it.
  // |contents| can be NULL.
  bool MaybeShowInfoBar(TabContents* contents);

  // Shows docked devtools.
  void ShowDevToolsContainer();

  // Hides docked devtools.
  void HideDevToolsContainer();

  // Updated devtools window for given contents.
  void UpdateDevToolsForContents(TabContents* tab_contents);

  // Updates various optional child Views, e.g. Bookmarks Bar, Info Bar or the
  // Download Shelf in response to a change notification from the specified
  // |contents|. |contents| can be NULL. In this case, all optional UI will be
  // removed.
  void UpdateUIForContents(TabContents* contents);

  // Updates an optional child View, e.g. Bookmarks Bar, Info Bar, Download
  // Shelf. If |*old_view| differs from new_view, the old_view is removed and
  // the new_view is added. This is intended to be used when swapping in/out
  // child views that are referenced via a field.
  // Returns true if anything was changed, and a re-Layout is now required.
  bool UpdateChildViewAndLayout(views::View* new_view, views::View** old_view);

  // Invoked to update the necessary things when our fullscreen state changes
  // to |fullscreen|. On Windows this is invoked immediately when we toggle the
  // full screen state. On Linux changing the fullscreen state is async, so we
  // ask the window to change its fullscreen state, then when we get
  // notification that it succeeded this method is invoked.
  // If |url| is not empty, it is the URL of the page that requested fullscreen
  // (via the fullscreen JS API).
  // |bubble_type| determines what should be shown in the fullscreen exit
  // bubble.
  void ProcessFullscreen(bool fullscreen,
                         FullscreenType fullscreen_type,
                         const GURL& url,
                         FullscreenExitBubbleType bubble_type);

  // Copy the accelerator table from the app resources into something we can
  // use.
  void LoadAccelerators();

  // Retrieves the command id for the specified Windows app command.
  int GetCommandIDForAppCommandID(int app_command_id) const;

  // Initialize the hung plugin detector.
  void InitHangMonitor();

  // Possibly records a user metrics action corresponding to the passed-in
  // accelerator.  Only implemented for Chrome OS, where we're interested in
  // learning about how frequently the top-row keys are used.
  void UpdateAcceleratorMetrics(const ui::Accelerator& accelerator,
                                int command_id);

  // Invoked from ActiveTabChanged or when instant is made active.
  // |new_contents| must not be NULL.
  void ProcessTabSelected(TabContents* new_contents);

  // Exposes resize corner size to BrowserViewLayout.
  gfx::Size GetResizeCornerSize() const;

  // Create an icon for this window in the launcher (currently only for Ash).
  void CreateLauncherIcon();

  // Forces the LocationBarContainer to the top of the native window stacking
  // order. This is needed for the Instant extended API when the location bar
  // can be placed over web contents.
  void RestackLocationBarContainer();

  // Calls |method| which is either RenderWidgetHost::Cut, ::Copy, or ::Paste
  // and returns true if the focus is currently on a WebContent.
  bool DoCutCopyPaste(void (content::RenderWidgetHost::*method)());

  // Last focused view that issued a tab traversal.
  int last_focused_view_storage_id_;

  // The BrowserFrame that hosts this view.
  BrowserFrame* frame_;

  // The Browser object we are associated with.
  scoped_ptr<Browser> browser_;

  // BrowserView layout (LTR one is pictured here).
  //
  // ----------------------------------------------------------------
  // | Tabs (1)                                                     |
  // |--------------------------------------------------------------|
  // | Navigation buttons, menus and the address bar (toolbar_)     |
  // |--------------------------------------------------------------|
  // | All infobars (infobar_container_) *                          |
  // |--------------------------------------------------------------|
  // | Bookmarks (bookmark_bar_view_) *                             |
  // |--------------------------------------------------------------|
  // |Page content (contents_)                                     ||
  // |-------------------------------------------------------------||
  // || contents_container_ and/or                                |||
  // || preview_container_                                        |||
  // ||                                                           |||
  // ||                                                           |||
  // ||                                                           |||
  // ||                                                           |||
  // ||                                                           |||
  // |-------------------------------------------------------------||
  // |==(2)=========================================================|
  // |                                                              |
  // |                                                              |
  // | Debugger (devtools_container_)                               |
  // |                                                              |
  // |                                                              |
  // |--------------------------------------------------------------|
  // | Active downloads (download_shelf_)                           |
  // ----------------------------------------------------------------
  //
  // (1) - tabstrip_, default position
  // (2) - contents_split_
  //
  // * - The bookmark bar and info bar are swapped when on the new tab page.
  //     Additionally contents_ is positioned on top of the bookmark bar when
  //     the bookmark bar is detached. This is done to allow the
  //     preview_container_ to appear over the bookmark bar.

  // Tool/Info bars that we are currently showing. Used for layout.
  // active_bookmark_bar_ is either NULL, if the bookmark bar isn't showing,
  // or is bookmark_bar_view_ if the bookmark bar is showing.
  views::View* active_bookmark_bar_;

  // The TabStrip.
  TabStrip* tabstrip_;

  // The Toolbar containing the navigation buttons, menus and the address bar.
  ToolbarView* toolbar_;

  // This button sits next to the tabs on the right hand side and it is used
  // only in windows metro metro mode to allow the user to flip among browser
  // windows.
  views::Button* window_switcher_button_;

  // The Bookmark Bar View for this window. Lazily created.
  scoped_ptr<BookmarkBarView> bookmark_bar_view_;

  // The download shelf view (view at the bottom of the page).
  scoped_ptr<DownloadShelfView> download_shelf_;

  // The InfoBarContainerView that contains InfoBars for the current tab.
  InfoBarContainerView* infobar_container_;

  // The view that contains the selected WebContents.
  views::WebView* contents_container_;

  // The view that contains devtools window for the selected WebContents.
  views::WebView* devtools_container_;

  // The view that contains instant's WebContents.
  views::WebView* preview_container_;

  // The view managing both the contents_container_ and preview_container_.
  ContentsContainer* contents_;

  // Split view containing the contents container and devtools container.
  views::SingleSplitView* contents_split_;

  // Side to dock devtools to
  DevToolsDockSide devtools_dock_side_;

  // Tracks and stores the last focused view which is not the
  // devtools_container_ or any of its children. Used to restore focus once
  // the devtools_container_ is hidden.
  scoped_ptr<views::ExternalFocusTracker> devtools_focus_tracker_;

  // The Status information bubble that appears at the bottom of the window.
  scoped_ptr<StatusBubbleViews> status_bubble_;

  // A mapping between accelerators and commands.
  std::map<ui::Accelerator, int> accelerator_table_;

  // True if we have already been initialized.
  bool initialized_;

  // True if we should ignore requests to layout.  This is set while toggling
  // fullscreen mode on and off to reduce jankiness.
  bool ignore_layout_;

  scoped_ptr<FullscreenExitBubbleViews> fullscreen_bubble_;

#if defined(OS_WIN) && !defined(USE_AURA)
  // This object is used to perform periodic actions in a worker
  // thread. It is currently used to monitor hung plugin windows.
  WorkerThreadTicker ticker_;

  // This object is initialized with the frame window HWND. This
  // object is also passed as a tick handler with the ticker_ object.
  // It is used to periodically monitor for hung plugin windows
  HungWindowDetector hung_window_detector_;

  // This object is invoked by hung_window_detector_ when it detects a hung
  // plugin window.
  HungPluginAction hung_plugin_action_;

  // The custom JumpList for Windows 7.
  scoped_refptr<JumpList> jumplist_;
#endif

#if defined(USE_ASH)
  scoped_ptr<BrowserLauncherItemController> launcher_item_controller_;
#endif

  // The timer used to update frames for the Loading Animation.
  base::RepeatingTimer<BrowserView> loading_animation_timer_;

  UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;

  // Used to measure the loading spinner animation rate.
  base::TimeTicks last_animation_time_;

  // If this flag is set then SetFocusToLocationBar() will set focus to the
  // location bar even if the browser window is not active.
  bool force_location_bar_focus_;

  PendingFullscreenRequest fullscreen_request_;

  gfx::ScopedSysColorChangeListener color_change_listener_;

#if defined(USE_AURA)
  scoped_ptr<SearchViewController> search_view_controller_;
#endif

  DISALLOW_COPY_AND_ASSIGN(BrowserView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_VIEW_H_
