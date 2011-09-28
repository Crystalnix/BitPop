// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FRIENDS_SIDEBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FRIENDS_SIDEBAR_VIEW_H_

#include "chrome/browser/ui/views/tab_contents/tab_contents_container.h"
#include "content/browser/tab_contents/tab_contents_delegate.h"

class Browser;
class BrowserView;
class TabContents;

class FriendsSidebarView : public TabContentsContainer,
						   public TabContentsDelegate {
public:
  FriendsSidebarView(Browser* browser, BrowserView* parent);
  virtual ~FriendsSidebarView();

  // Implementation of View.
  virtual gfx::Size GetPreferredSize();
  // virtual void Layout();
  virtual void OnPaint(gfx::Canvas* canvas);

  // Overriden from TabContentsDelegate
  virtual void OpenURLFromTab(TabContents* source,
                              const GURL& url,
                              const GURL& referrer,
                              WindowOpenDisposition disposition,
                              PageTransition::Type transition);
  virtual void NavigationStateChanged(const TabContents* source,
                                      unsigned changed_flags);
  virtual void AddNewContents(TabContents* source,
                              TabContents* new_contents,
                              WindowOpenDisposition disposition,
                              const gfx::Rect& initial_pos,
                              bool user_gesture);
  virtual void ActivateContents(TabContents* contents);
  virtual void DeactivateContents(TabContents* contents);
  virtual void LoadingStateChanged(TabContents* source);
  virtual void CloseContents(TabContents* source);
  virtual void MoveContents(TabContents* source, const gfx::Rect& pos);
  virtual void DetachContents(TabContents* source);
  virtual bool IsPopupOrPanel(const TabContents* source) const;
  virtual bool CanReloadContents(TabContents* source) const;
  virtual void UpdateTargetURL(TabContents* source, const GURL& url);
  virtual void ContentsMouseEvent(
      TabContents* source, const gfx::Point& location, bool motion);
  virtual void ContentsZoomChange(bool zoom_in);
  virtual void SetTabContentBlocked(TabContents* contents, bool blocked);
  virtual void TabContentsFocused(TabContents* tab_content);
  virtual bool TakeFocus(bool reverse);
  virtual bool IsApplication() const;
  virtual void ConvertContentsToApplication(TabContents* source);
  virtual bool ShouldDisplayURLField();
  virtual void BeforeUnloadFired(TabContents* source,
                                 bool proceed,
                                 bool* proceed_to_fire_unload);
  virtual void SetFocusToLocationBar(bool select_all);
  virtual void RenderWidgetShowing();
  virtual int GetExtraRenderViewHeight() const;
  virtual void ShowPageInfo(Profile* profile,
                            const GURL& url,
                            const NavigationEntry::SSLStatus& ssl,
                            bool show_history);
  virtual void ViewSourceForTab(TabContents* source, const GURL& page_url);
  virtual void ViewSourceForFrame(TabContents* source,
                                  const GURL& frame_url,
                                  const std::string& frame_content_state);
  virtual bool PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                        bool* is_keyboard_shortcut);
  virtual void HandleKeyboardEvent(const NativeWebKeyboardEvent& event);
  virtual void ShowRepostFormWarningDialog(TabContents* tab_contents);
  virtual bool ShouldAddNavigationToHistory(
      const history::HistoryAddPageArgs& add_page_args,
      NavigationType::Type navigation_type);
  virtual void ContentRestrictionsChanged(TabContents* source);
  virtual void WorkerCrashed();

private:
  void Init();
  
  /* data */
  Browser* browser_;
  BrowserView* parent_;
  //static scoped_ptr<TabContents> extension_page_contents_;
};
#endif // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FRIENDS_SIDEBAR_VIEW_H_

