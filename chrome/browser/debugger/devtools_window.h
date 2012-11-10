// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEBUGGER_DEVTOOLS_WINDOW_H_
#define CHROME_BROWSER_DEBUGGER_DEVTOOLS_WINDOW_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/debugger/devtools_file_helper.h"
#include "chrome/browser/debugger/devtools_toggle_action.h"
#include "content/public/browser/devtools_client_host.h"
#include "content/public/browser/devtools_frontend_host_delegate.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_contents_delegate.h"

namespace IPC {
class Message;
}

class Browser;
class BrowserWindow;
class PrefService;
class Profile;
class TabContents;

namespace base {
class Value;
}

namespace content {
class DevToolsAgentHost;
class DevToolsClientHost;
struct FileChooserParams;
class RenderViewHost;
class WebContents;
}

class DevToolsWindow : private content::NotificationObserver,
                       private content::WebContentsDelegate,
                       private content::DevToolsFrontendHostDelegate,
                       private DevToolsFileHelper::Delegate {
 public:
  static const char kDevToolsApp[];
  static void RegisterUserPrefs(PrefService* prefs);
  static TabContents* GetDevToolsContents(content::WebContents* inspected_tab);
  static bool IsDevToolsWindow(content::RenderViewHost* window_rvh);

  static DevToolsWindow* OpenDevToolsWindowForWorker(
      Profile* profile,
      content::DevToolsAgentHost* worker_agent);
  static DevToolsWindow* CreateDevToolsWindowForWorker(Profile* profile);
  static DevToolsWindow* OpenDevToolsWindow(
      content::RenderViewHost* inspected_rvh);
  static DevToolsWindow* ToggleDevToolsWindow(
      content::RenderViewHost* inspected_rvh,
      DevToolsToggleAction action);
  static void InspectElement(
      content::RenderViewHost* inspected_rvh, int x, int y);

  virtual ~DevToolsWindow();

  // Overridden from DevToolsClientHost.
  virtual void InspectedContentsClosing() OVERRIDE;
  virtual void ContentsReplaced(content::WebContents* new_contents) OVERRIDE;
  content::RenderViewHost* GetRenderViewHost();

  void Show(DevToolsToggleAction action);

  TabContents* tab_contents() { return tab_contents_; }
  Browser* browser() { return browser_; }  // For tests.
  bool is_docked() { return docked_; }
  content::DevToolsClientHost* devtools_client_host() {
    return frontend_host_;
  }

 private:
  static DevToolsWindow* Create(Profile* profile,
                                content::RenderViewHost* inspected_rvh,
                                bool docked, bool shared_worker_frontend);
  DevToolsWindow(TabContents* tab_contents, Profile* profile,
                 content::RenderViewHost* inspected_rvh, bool docked);

  void CreateDevToolsBrowser();
  bool FindInspectedBrowserAndTabIndex(Browser**, int* tab);
  BrowserWindow* GetInspectedBrowserWindow();
  bool IsInspectedBrowserPopupOrPanel();
  void UpdateFrontendAttachedState();

  // Overridden from content::NotificationObserver.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  void ScheduleAction(DevToolsToggleAction action);
  void DoAction();
  static GURL GetDevToolsUrl(Profile* profile, bool docked,
                             bool shared_worker_frontend);
  void UpdateTheme();
  void AddDevToolsExtensionsToClient();
  void CallClientFunction(const std::string& function_name,
                          const base::Value* arg);
  // Overridden from content::WebContentsDelegate.
  virtual content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params) OVERRIDE;
  virtual void AddNewContents(content::WebContents* source,
                              content::WebContents* new_contents,
                              WindowOpenDisposition disposition,
                              const gfx::Rect& initial_pos,
                              bool user_gesture) OVERRIDE;
  virtual void CloseContents(content::WebContents* source) OVERRIDE {}
  virtual bool PreHandleKeyboardEvent(
      const content::NativeWebKeyboardEvent& event,
      bool* is_keyboard_shortcut) OVERRIDE;
  virtual void HandleKeyboardEvent(
      const content::NativeWebKeyboardEvent& event) OVERRIDE;
  virtual content::JavaScriptDialogCreator*
      GetJavaScriptDialogCreator() OVERRIDE;
  virtual void RunFileChooser(
      content::WebContents* web_contents,
      const content::FileChooserParams& params) OVERRIDE;

  virtual void FrameNavigating(const std::string& url) OVERRIDE {}

  static DevToolsWindow* ToggleDevToolsWindow(
      content::RenderViewHost* inspected_rvh,
      bool force_open,
      DevToolsToggleAction action);
  static DevToolsWindow* AsDevToolsWindow(content::DevToolsClientHost*);

  // content::DevToolsClientHandlerDelegate overrides.
  virtual void ActivateWindow() OVERRIDE;
  virtual void CloseWindow() OVERRIDE;
  virtual void MoveWindow(int x, int y) OVERRIDE;
  virtual void DockWindow() OVERRIDE;
  virtual void UndockWindow() OVERRIDE;
  virtual void SetDockSide(const std::string& side) OVERRIDE;
  virtual void OpenInNewTab(const std::string& url) OVERRIDE;
  virtual void SaveToFile(const std::string& url,
                          const std::string& content,
                          bool save_as) OVERRIDE;
  virtual void AppendToFile(const std::string& url,
                            const std::string& content) OVERRIDE;

  // Overridden from DevToolsFileHelper::Delegate
  virtual void FileSavedAs(const std::string& url)  OVERRIDE;
  virtual void AppendedTo(const std::string& url)  OVERRIDE;

  void RequestSetDocked(bool docked);

  void UpdateBrowserToolbar();

  Profile* profile_;
  TabContents* inspected_tab_;
  TabContents* tab_contents_;
  Browser* browser_;
  bool docked_;
  bool is_loaded_;
  DevToolsToggleAction action_on_load_;
  content::NotificationRegistrar registrar_;
  content::DevToolsClientHost* frontend_host_;
  scoped_ptr<DevToolsFileHelper> file_helper_;
  DISALLOW_COPY_AND_ASSIGN(DevToolsWindow);
};

#endif  // CHROME_BROWSER_DEBUGGER_DEVTOOLS_WINDOW_H_
