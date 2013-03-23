// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "base/command_line.h"
#include "base/json/json_writer.h"
#include "base/lazy_instance.h"
#include "base/string_number_conversions.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/debugger/devtools_window.h"
#include "chrome/browser/extensions/api/debugger/debugger_api.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/file_select_helper.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_impl.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/devtools_agent_host_registry.h"
#include "content/public/browser/devtools_manager.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/load_notification_details.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/page_transition_types.h"
#include "grit/generated_resources.h"

typedef std::vector<DevToolsWindow*> DevToolsWindowList;
namespace {
base::LazyInstance<DevToolsWindowList>::Leaky
     g_instances = LAZY_INSTANCE_INITIALIZER;
}  // namespace

using content::DevToolsAgentHost;
using content::DevToolsAgentHostRegistry;
using content::DevToolsClientHost;
using content::DevToolsManager;
using content::FileChooserParams;
using content::NativeWebKeyboardEvent;
using content::NavigationController;
using content::NavigationEntry;
using content::OpenURLParams;
using content::RenderViewHost;
using content::WebContents;

const char DevToolsWindow::kDevToolsApp[] = "DevToolsApp";

const char kOldPrefBottom[] = "bottom";
const char kOldPrefRight[] = "right";

const char kPrefBottom[] = "dock_bottom";
const char kPrefRight[] = "dock_right";
const char kPrefUndocked[] = "undocked";

const char kDockSideBottom[] = "bottom";
const char kDockSideRight[] = "right";
const char kDockSideUndocked[] = "undocked";

// Minimal height of devtools pane or content pane when devtools are docked
// to the browser window.
const int kMinDevToolsHeight = 50;
const int kMinDevToolsWidth = 150;
const int kMinContentsSize = 50;

// static
void DevToolsWindow::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterBooleanPref(prefs::kDevToolsOpenDocked,
                             true,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kDevToolsDockSide,
                            kDockSideBottom,
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterDictionaryPref(prefs::kDevToolsEditedFiles,
                                PrefService::UNSYNCABLE_PREF);
}

// static
DevToolsWindow* DevToolsWindow::GetDockedInstanceForInspectedTab(
    WebContents* inspected_web_contents) {
  if (!inspected_web_contents)
    return NULL;

  if (!DevToolsAgentHostRegistry::HasDevToolsAgentHost(
      inspected_web_contents->GetRenderViewHost()))
    return NULL;
  DevToolsAgentHost* agent = DevToolsAgentHostRegistry::GetDevToolsAgentHost(
      inspected_web_contents->GetRenderViewHost());
  DevToolsManager* manager = DevToolsManager::GetInstance();
  DevToolsClientHost* client_host = manager->GetDevToolsClientHostFor(agent);
  DevToolsWindow* window = AsDevToolsWindow(client_host);
  return window && window->IsDocked() ? window : NULL;
}

// static
bool DevToolsWindow::IsDevToolsWindow(RenderViewHost* window_rvh) {
  return AsDevToolsWindow(window_rvh) != NULL;
}

// static
DevToolsWindow* DevToolsWindow::OpenDevToolsWindowForWorker(
    Profile* profile,
    DevToolsAgentHost* worker_agent) {
  DevToolsWindow* window;
  DevToolsClientHost* client = content::DevToolsManager::GetInstance()->
      GetDevToolsClientHostFor(worker_agent);
  if (client) {
    window = AsDevToolsWindow(client);
    if (!window)
      return NULL;
  } else {
    window = DevToolsWindow::CreateDevToolsWindowForWorker(profile);
    DevToolsManager::GetInstance()->RegisterDevToolsClientHostFor(
        worker_agent,
        window->frontend_host_);
  }
  window->Show(DEVTOOLS_TOGGLE_ACTION_SHOW);
  return window;
}

// static
DevToolsWindow* DevToolsWindow::CreateDevToolsWindowForWorker(
    Profile* profile) {
  return Create(profile, NULL, DEVTOOLS_DOCK_SIDE_UNDOCKED, true);
}

// static
DevToolsWindow* DevToolsWindow::OpenDevToolsWindow(
    RenderViewHost* inspected_rvh) {
  return ToggleDevToolsWindow(inspected_rvh, true,
                              DEVTOOLS_TOGGLE_ACTION_SHOW);
}

// static
DevToolsWindow* DevToolsWindow::ToggleDevToolsWindow(
    Browser* browser,
    DevToolsToggleAction action) {
  if (action == DEVTOOLS_TOGGLE_ACTION_TOGGLE && browser->is_devtools()) {
    browser->tab_strip_model()->CloseAllTabs();
    return NULL;
  }
  RenderViewHost* inspected_rvh =
      browser->tab_strip_model()->GetActiveWebContents()->GetRenderViewHost();

  return ToggleDevToolsWindow(inspected_rvh,
                       action == DEVTOOLS_TOGGLE_ACTION_INSPECT,
                       action);
}

void DevToolsWindow::InspectElement(RenderViewHost* inspected_rvh,
                                    int x,
                                    int y) {
  DevToolsAgentHost* agent = DevToolsAgentHostRegistry::GetDevToolsAgentHost(
      inspected_rvh);
  DevToolsManager::GetInstance()->InspectElement(agent, x, y);
  // TODO(loislo): we should initiate DevTools window opening from within
  // renderer. Otherwise, we still can hit a race condition here.
  OpenDevToolsWindow(inspected_rvh);
}


DevToolsWindow* DevToolsWindow::Create(
    Profile* profile,
    RenderViewHost* inspected_rvh,
    DevToolsDockSide dock_side,
    bool shared_worker_frontend) {
  // Create WebContents with devtools.
  WebContents* web_contents =
      WebContents::Create(WebContents::CreateParams(profile));
  web_contents->GetRenderViewHost()->AllowBindings(
      content::BINDINGS_POLICY_WEB_UI);
  web_contents->GetController().LoadURL(
      GetDevToolsUrl(profile, dock_side, shared_worker_frontend),
      content::Referrer(),
      content::PAGE_TRANSITION_AUTO_TOPLEVEL,
      std::string());
  return new DevToolsWindow(web_contents, profile, inspected_rvh, dock_side);
}

DevToolsWindow::DevToolsWindow(WebContents* web_contents,
                               Profile* profile,
                               RenderViewHost* inspected_rvh,
                               DevToolsDockSide dock_side)
    : profile_(profile),
      inspected_web_contents_(NULL),
      web_contents_(web_contents),
      browser_(NULL),
      dock_side_(dock_side),
      is_loaded_(false),
      action_on_load_(DEVTOOLS_TOGGLE_ACTION_SHOW),
      width_(-1),
      height_(-1) {
  frontend_host_ = DevToolsClientHost::CreateDevToolsFrontendHost(web_contents,
                                                                  this);
  file_helper_.reset(new DevToolsFileHelper(profile, this));

  g_instances.Get().push_back(this);
  // Wipe out page icon so that the default application icon is used.
  NavigationEntry* entry = web_contents->GetController().GetActiveEntry();
  entry->GetFavicon().image = gfx::Image();
  entry->GetFavicon().valid = true;

  // Register on-load actions.
  registrar_.Add(
      this,
      content::NOTIFICATION_LOAD_STOP,
      content::Source<NavigationController>(&web_contents->GetController()));
  registrar_.Add(
      this,
      chrome::NOTIFICATION_TAB_CLOSING,
      content::Source<NavigationController>(&web_contents->GetController()));
  registrar_.Add(
      this,
      chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
      content::Source<ThemeService>(
          ThemeServiceFactory::GetForProfile(profile_)));
  // There is no inspected_rvh in case of shared workers.
  if (inspected_rvh)
    inspected_web_contents_ = WebContents::FromRenderViewHost(inspected_rvh);
}

DevToolsWindow::~DevToolsWindow() {
  DevToolsWindowList& instances = g_instances.Get();
  DevToolsWindowList::iterator it = std::find(instances.begin(),
                                              instances.end(),
                                              this);
  DCHECK(it != instances.end());
  instances.erase(it);
}

void DevToolsWindow::InspectedContentsClosing() {
  if (IsDocked()) {
    // Update dev tools to reflect removed dev tools window.
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window)
      inspected_window->UpdateDevTools();
    // In case of docked web_contents_, we own it so delete here.
    delete web_contents_;

    delete this;
  } else {
    // First, initiate self-destruct to free all the registrars.
    // Then close all tabs. Browser will take care of deleting web_contents_
    // for us.
    Browser* browser = browser_;
    delete this;
    browser->tab_strip_model()->CloseAllTabs();
  }
}

void DevToolsWindow::ContentsReplaced(WebContents* new_contents) {
  inspected_web_contents_ = new_contents;
}

void DevToolsWindow::Show(DevToolsToggleAction action) {
  if (IsDocked()) {
    Browser* inspected_browser;
    int inspected_tab_index;
    // Tell inspected browser to update splitter and switch to inspected panel.
    if (!IsInspectedBrowserPopupOrPanel() &&
        FindInspectedBrowserAndTabIndex(&inspected_browser,
                                        &inspected_tab_index)) {
      BrowserWindow* inspected_window = inspected_browser->window();
      web_contents_->SetDelegate(this);
      inspected_window->UpdateDevTools();
      web_contents_->GetView()->SetInitialFocus();
      inspected_window->Show();
      TabStripModel* tab_strip_model = inspected_browser->tab_strip_model();
      tab_strip_model->ActivateTabAt(inspected_tab_index, true);
      ScheduleAction(action);
      return;
    } else {
      // Sometimes we don't know where to dock. Stay undocked.
      dock_side_ = DEVTOOLS_DOCK_SIDE_UNDOCKED;
    }
  }

  // Avoid consecutive window switching if the devtools window has been opened
  // and the Inspect Element shortcut is pressed in the inspected tab.
  bool should_show_window =
      !browser_ || action != DEVTOOLS_TOGGLE_ACTION_INSPECT;

  if (!browser_)
    CreateDevToolsBrowser();

  if (should_show_window) {
    browser_->window()->Show();
    web_contents_->GetView()->SetInitialFocus();
  }

  ScheduleAction(action);
}

int DevToolsWindow::GetWidth(int container_width) {
  if (width_ == -1) {
    width_ = profile_->GetPrefs()->
        GetInteger(prefs::kDevToolsVSplitLocation);
  }

  // By default, size devtools as 1/3 of the browser window.
  if (width_ == -1)
    width_ = container_width / 3;

  // Respect the minimum devtools width preset.
  width_ = std::max(kMinDevToolsWidth, width_);

  // But it should never compromise the content window size unless the entire
  // window is tiny.
  width_ = std::min(container_width - kMinContentsSize, width_);
  if (width_ < (kMinContentsSize / 2))
    width_ = container_width / 3;
  return width_;
}

int DevToolsWindow::GetHeight(int container_height) {
  if (height_ == -1) {
    height_ = profile_->GetPrefs()->
        GetInteger(prefs::kDevToolsHSplitLocation);
  }

  // By default, size devtools as 1/3 of the browser window.
  if (height_ == -1)
    height_ = container_height / 3;

  // Respect the minimum devtools width preset.
  height_ = std::max(kMinDevToolsHeight, height_);

  // But it should never compromise the content window size.
  height_ = std::min(container_height - kMinContentsSize, height_);
  if (height_ < (kMinContentsSize / 2))
    height_ = container_height / 3;
  return height_;
}

void DevToolsWindow::SetWidth(int width) {
  width_ = width;
  profile_->GetPrefs()->SetInteger(prefs::kDevToolsVSplitLocation, width);
}

void DevToolsWindow::SetHeight(int height) {
  height_ = height;
  profile_->GetPrefs()->SetInteger(prefs::kDevToolsHSplitLocation, height);
}

RenderViewHost* DevToolsWindow::GetRenderViewHost() {
  return web_contents_->GetRenderViewHost();
}

void DevToolsWindow::CreateDevToolsBrowser() {
  // TODO(pfeldman): Make browser's getter for this key static.
  std::string wp_key;
  wp_key.append(prefs::kBrowserWindowPlacement);
  wp_key.append("_");
  wp_key.append(kDevToolsApp);

  PrefService* prefs = profile_->GetPrefs();
  if (!prefs->FindPreference(wp_key.c_str())) {
    prefs->RegisterDictionaryPref(wp_key.c_str(), PrefService::UNSYNCABLE_PREF);
  }

  const DictionaryValue* wp_pref = prefs->GetDictionary(wp_key.c_str());
  if (!wp_pref || wp_pref->empty()) {
    DictionaryPrefUpdate update(prefs, wp_key.c_str());
    DictionaryValue* defaults = update.Get();
    defaults->SetInteger("left", 100);
    defaults->SetInteger("top", 100);
    defaults->SetInteger("right", 740);
    defaults->SetInteger("bottom", 740);
    defaults->SetBoolean("maximized", false);
    defaults->SetBoolean("always_on_top", false);
  }

  browser_ = new Browser(Browser::CreateParams::CreateForDevTools(profile_));
  browser_->tab_strip_model()->AddWebContents(
      web_contents_, -1, content::PAGE_TRANSITION_AUTO_TOPLEVEL,
      TabStripModel::ADD_ACTIVE);
}

bool DevToolsWindow::FindInspectedBrowserAndTabIndex(Browser** browser,
                                                     int* tab) {
  if (!inspected_web_contents_)
    return false;

  bool found = FindInspectedBrowserAndTabIndexFromBrowserList(
      chrome::BrowserListImpl::GetInstance(chrome::HOST_DESKTOP_TYPE_NATIVE),
      browser,
      tab);
  // On Windows 8 we can have the desktop environment and the ASH environment
  // active concurrently. If we fail to find the inspected web contents in the
  // native browser list, then we should look in the ASH browser list.
#if defined(OS_WIN) && defined(USE_AURA)
  if (!found) {
    found = FindInspectedBrowserAndTabIndexFromBrowserList(
        chrome::BrowserListImpl::GetInstance(chrome::HOST_DESKTOP_TYPE_ASH),
        browser,
        tab);
  }
#endif
  return found;
}

bool DevToolsWindow::FindInspectedBrowserAndTabIndexFromBrowserList(
    chrome::BrowserListImpl* browser_list,
    Browser** browser,
    int* tab) {
  if (!inspected_web_contents_)
    return false;

  for (chrome::BrowserListImpl::const_iterator it = browser_list->begin();
       it != browser_list->end(); ++it) {
    int tab_index = (*it)->tab_strip_model()->GetIndexOfWebContents(
        inspected_web_contents_);
    if (tab_index != TabStripModel::kNoTab) {
      *browser = *it;
      *tab = tab_index;
      return true;
    }
  }
  return false;
}

BrowserWindow* DevToolsWindow::GetInspectedBrowserWindow() {
  Browser* browser = NULL;
  int tab;
  return FindInspectedBrowserAndTabIndex(&browser, &tab) ?
      browser->window() : NULL;
}

bool DevToolsWindow::IsInspectedBrowserPopupOrPanel() {
  Browser* browser = NULL;
  int tab;
  if (!FindInspectedBrowserAndTabIndex(&browser, &tab))
    return false;

  return browser->is_type_popup() || browser->is_type_panel();
}

void DevToolsWindow::UpdateFrontendDockSide() {
  base::StringValue dock_side(SideToString(dock_side_));
  CallClientFunction("InspectorFrontendAPI.setDockSide", &dock_side);
  base::FundamentalValue docked(IsDocked());
  CallClientFunction("InspectorFrontendAPI.setAttachedWindow", &docked);
}


void DevToolsWindow::AddDevToolsExtensionsToClient() {
  if (inspected_web_contents_) {
    SessionTabHelper* session_tab_helper =
        SessionTabHelper::FromWebContents(inspected_web_contents_);
    if (session_tab_helper) {
      base::FundamentalValue tabId(session_tab_helper->session_id().id());
      CallClientFunction("WebInspector.setInspectedTabId", &tabId);
    }
  }
  ListValue results;
  Profile* profile =
      Profile::FromBrowserContext(web_contents_->GetBrowserContext());
  const ExtensionService* extension_service = extensions::ExtensionSystem::Get(
      profile->GetOriginalProfile())->extension_service();
  if (!extension_service)
    return;

  const ExtensionSet* extensions = extension_service->extensions();

  for (ExtensionSet::const_iterator extension = extensions->begin();
       extension != extensions->end(); ++extension) {
    if ((*extension)->devtools_url().is_empty())
      continue;
    DictionaryValue* extension_info = new DictionaryValue();
    extension_info->Set("startPage",
        new StringValue((*extension)->devtools_url().spec()));
    extension_info->Set("name", new StringValue((*extension)->name()));
    bool allow_experimental = (*extension)->HasAPIPermission(
        extensions::APIPermission::kExperimental);
    extension_info->Set("exposeExperimentalAPIs",
        new base::FundamentalValue(allow_experimental));
    results.Append(extension_info);
  }
  CallClientFunction("WebInspector.addExtensions", &results);
}

WebContents* DevToolsWindow::OpenURLFromTab(WebContents* source,
                                            const OpenURLParams& params) {
  if (inspected_web_contents_)
    return inspected_web_contents_->OpenURL(params);
  return NULL;
}

void DevToolsWindow::CallClientFunction(const std::string& function_name,
                                        const Value* arg) {
  std::string json;
  if (arg)
    base::JSONWriter::Write(arg, &json);

  string16 javascript =
      ASCIIToUTF16(function_name + "(" + json + ");");
  web_contents_->GetRenderViewHost()->
      ExecuteJavascriptInWebFrame(string16(), javascript);
}

void DevToolsWindow::Observe(int type,
                             const content::NotificationSource& source,
                             const content::NotificationDetails& details) {
  if (type == content::NOTIFICATION_LOAD_STOP && !is_loaded_) {
    is_loaded_ = true;
    UpdateTheme();
    DoAction();
    AddDevToolsExtensionsToClient();
  } else if (type == chrome::NOTIFICATION_TAB_CLOSING) {
    if (content::Source<NavigationController>(source).ptr() ==
            &web_contents_->GetController()) {
      // This happens when browser closes all of its tabs as a result
      // of window.Close event.
      // Notify manager that this DevToolsClientHost no longer exists and
      // initiate self-destuct here.
      DevToolsManager::GetInstance()->ClientHostClosing(frontend_host_);
      UpdateBrowserToolbar();
      delete this;
    }
  } else if (type == chrome::NOTIFICATION_BROWSER_THEME_CHANGED) {
    UpdateTheme();
  }
}

void DevToolsWindow::ScheduleAction(DevToolsToggleAction action) {
  action_on_load_ = action;
  if (is_loaded_)
    DoAction();
}

void DevToolsWindow::DoAction() {
  UpdateFrontendDockSide();
  switch (action_on_load_) {
    case DEVTOOLS_TOGGLE_ACTION_SHOW_CONSOLE:
      CallClientFunction("InspectorFrontendAPI.showConsole", NULL);
      break;
    case DEVTOOLS_TOGGLE_ACTION_INSPECT:
      CallClientFunction("InspectorFrontendAPI.enterInspectElementMode", NULL);
    case DEVTOOLS_TOGGLE_ACTION_SHOW:
    case DEVTOOLS_TOGGLE_ACTION_TOGGLE:
      // Do nothing.
      break;
    default:
      NOTREACHED();
  }
  action_on_load_ = DEVTOOLS_TOGGLE_ACTION_SHOW;
}

std::string SkColorToRGBAString(SkColor color) {
  // We convert the alpha using DoubleToString because StringPrintf will use
  // locale specific formatters (e.g., use , instead of . in German).
  return StringPrintf("rgba(%d,%d,%d,%s)", SkColorGetR(color),
      SkColorGetG(color), SkColorGetB(color),
      base::DoubleToString(SkColorGetA(color) / 255.0).c_str());
}

// static
GURL DevToolsWindow::GetDevToolsUrl(Profile* profile,
                                    DevToolsDockSide dock_side,
                                    bool shared_worker_frontend) {
  ThemeService* tp = ThemeServiceFactory::GetForProfile(profile);
  CHECK(tp);

  SkColor color_toolbar =
      tp->GetColor(ThemeService::COLOR_TOOLBAR);
  SkColor color_tab_text =
      tp->GetColor(ThemeService::COLOR_BOOKMARK_TEXT);

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  bool experiments_enabled =
      command_line.HasSwitch(switches::kEnableDevToolsExperiments);

  std::string url_string = StringPrintf("%sdevtools.html?"
      "dockSide=%s&toolbarColor=%s&textColor=%s%s%s",
      chrome::kChromeUIDevToolsURL,
      SideToString(dock_side).c_str(),
      SkColorToRGBAString(color_toolbar).c_str(),
      SkColorToRGBAString(color_tab_text).c_str(),
      shared_worker_frontend ? "&isSharedWorker=true" : "",
      experiments_enabled ? "&experiments=true" : "");
  return GURL(url_string);
}

void DevToolsWindow::UpdateTheme() {
  ThemeService* tp = ThemeServiceFactory::GetForProfile(profile_);
  CHECK(tp);

  SkColor color_toolbar =
      tp->GetColor(ThemeService::COLOR_TOOLBAR);
  SkColor color_tab_text =
      tp->GetColor(ThemeService::COLOR_BOOKMARK_TEXT);
  std::string command = StringPrintf(
      "InspectorFrontendAPI.setToolbarColors(\"%s\", \"%s\")",
      SkColorToRGBAString(color_toolbar).c_str(),
      SkColorToRGBAString(color_tab_text).c_str());
  web_contents_->GetRenderViewHost()->
      ExecuteJavascriptInWebFrame(string16(), UTF8ToUTF16(command));
}

void DevToolsWindow::AddNewContents(WebContents* source,
                                    WebContents* new_contents,
                                    WindowOpenDisposition disposition,
                                    const gfx::Rect& initial_pos,
                                    bool user_gesture,
                                    bool* was_blocked) {
  if (inspected_web_contents_) {
    inspected_web_contents_->GetDelegate()->AddNewContents(
        source, new_contents, disposition, initial_pos, user_gesture,
        was_blocked);
  }
}

bool DevToolsWindow::PreHandleKeyboardEvent(
    WebContents* source,
    const NativeWebKeyboardEvent& event, bool* is_keyboard_shortcut) {
  if (IsDocked()) {
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window)
      return inspected_window->PreHandleKeyboardEvent(
          event, is_keyboard_shortcut);
  }
  return false;
}

void DevToolsWindow::HandleKeyboardEvent(WebContents* source,
                                         const NativeWebKeyboardEvent& event) {
  if (IsDocked()) {
    if (event.windowsKeyCode == 0x08) {
      // Do not navigate back in history on Windows (http://crbug.com/74156).
      return;
    }
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window)
      inspected_window->HandleKeyboardEvent(event);
  }
}

// static
DevToolsWindow* DevToolsWindow::ToggleDevToolsWindow(
    RenderViewHost* inspected_rvh,
    bool force_open,
    DevToolsToggleAction action) {
  DevToolsAgentHost* agent = DevToolsAgentHostRegistry::GetDevToolsAgentHost(
      inspected_rvh);
  DevToolsManager* manager = DevToolsManager::GetInstance();
  DevToolsClientHost* host = manager->GetDevToolsClientHostFor(agent);
  DevToolsWindow* window = AsDevToolsWindow(host);
  if (host && !window) {
    // Break remote debugging / extension debugging session.
    host->ReplacedWithAnotherClient();
    manager->UnregisterDevToolsClientHostFor(agent);
  }

  bool do_open = force_open;
  if (!window) {
    Profile* profile = Profile::FromBrowserContext(
        inspected_rvh->GetProcess()->GetBrowserContext());
    DevToolsDockSide dock_side = GetDockSideFromPrefs(profile);
    window = Create(profile, inspected_rvh, dock_side, false);
    manager->RegisterDevToolsClientHostFor(agent, window->frontend_host_);
    do_open = true;
  }

  // Update toolbar to reflect DevTools changes.
  window->UpdateBrowserToolbar();

  // If window is docked and visible, we hide it on toggle. If window is
  // undocked, we show (activate) it.
  if (!window->IsDocked() || do_open)
    window->Show(action);
  else
    manager->UnregisterDevToolsClientHostFor(agent);

  return window;
}

// static
DevToolsWindow* DevToolsWindow::AsDevToolsWindow(
    DevToolsClientHost* client_host) {
  if (!client_host || g_instances == NULL)
    return NULL;
  DevToolsWindowList& instances = g_instances.Get();
  for (DevToolsWindowList::iterator it = instances.begin();
       it != instances.end(); ++it) {
    if ((*it)->frontend_host_ == client_host)
      return *it;
  }
  return NULL;
}

// static
DevToolsWindow* DevToolsWindow::AsDevToolsWindow(RenderViewHost* window_rvh) {
  if (g_instances == NULL)
    return NULL;
  DevToolsWindowList& instances = g_instances.Get();
  for (DevToolsWindowList::iterator it = instances.begin();
       it != instances.end(); ++it) {
    if ((*it)->web_contents_->GetRenderViewHost() == window_rvh)
      return *it;
  }
  return NULL;
}

void DevToolsWindow::ActivateWindow() {
  if (!IsDocked()) {
    if (!browser_->window()->IsActive()) {
      browser_->window()->Activate();
    }
  } else {
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window)
      web_contents_->GetView()->Focus();
  }
}

void DevToolsWindow::CloseWindow() {
  DCHECK(IsDocked());
  DevToolsManager::GetInstance()->ClientHostClosing(frontend_host_);
  InspectedContentsClosing();
}

void DevToolsWindow::MoveWindow(int x, int y) {
  if (!IsDocked()) {
    gfx::Rect bounds = browser_->window()->GetBounds();
    bounds.Offset(x, y);
    browser_->window()->SetBounds(bounds);
  }
}

void DevToolsWindow::SetDockSide(const std::string& side) {
  DevToolsDockSide requested_side = SideFromString(side);
  bool dock_requested = requested_side != DEVTOOLS_DOCK_SIDE_UNDOCKED;
  bool is_docked = IsDocked();

  if (dock_requested && (!inspected_web_contents_ ||
      !GetInspectedBrowserWindow() || IsInspectedBrowserPopupOrPanel())) {
      // Cannot dock, avoid window flashing due to close-reopen cycle.
    return;
  }

  dock_side_ = requested_side;
  if (dock_requested) {
    if (!is_docked) {
      // Detach window from the external devtools browser. It will lead to
      // the browser object's close and delete. Remove observer first.
      TabStripModel* tab_strip_model = browser_->tab_strip_model();
      tab_strip_model->DetachWebContentsAt(
          tab_strip_model->GetIndexOfWebContents(web_contents_));
      browser_ = NULL;
    }
  } else if (is_docked) {
    // Update inspected window to hide split and reset it.
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window)
      inspected_window->UpdateDevTools();
  }

  std::string pref_value = kPrefBottom;
  switch (dock_side_) {
    case DEVTOOLS_DOCK_SIDE_UNDOCKED:
        pref_value = kPrefUndocked;
        break;
    case DEVTOOLS_DOCK_SIDE_RIGHT:
        pref_value = kPrefRight;
        break;
    case DEVTOOLS_DOCK_SIDE_BOTTOM:
        pref_value = kPrefBottom;
        break;
  }
  profile_->GetPrefs()->SetString(prefs::kDevToolsDockSide, pref_value);

  Show(DEVTOOLS_TOGGLE_ACTION_SHOW);
}

void DevToolsWindow::OpenInNewTab(const std::string& url) {
  OpenURLParams params(GURL(url),
                       content::Referrer(),
                       NEW_FOREGROUND_TAB,
                       content::PAGE_TRANSITION_LINK,
                       false /* is_renderer_initiated */);
  if (inspected_web_contents_) {
    inspected_web_contents_->OpenURL(params);
  } else {
    for (BrowserList::const_iterator it = BrowserList::begin();
         it != BrowserList::end(); ++it) {
      if ((*it)->type() == Browser::TYPE_TABBED) {
        (*it)->OpenURL(params);
        break;
      }
    }
  }
}

void DevToolsWindow::SaveToFile(const std::string& url,
                                const std::string& content,
                                bool save_as) {
  file_helper_->Save(url, content, save_as);
}

void DevToolsWindow::AppendToFile(const std::string& url,
                                  const std::string& content) {
  file_helper_->Append(url, content);
}

void DevToolsWindow::FileSavedAs(const std::string& url) {
  StringValue url_value(url);
  CallClientFunction("InspectorFrontendAPI.savedURL", &url_value);
}

void DevToolsWindow::AppendedTo(const std::string& url) {
  StringValue url_value(url);
  CallClientFunction("InspectorFrontendAPI.appendedToURL", &url_value);
}

content::JavaScriptDialogCreator* DevToolsWindow::GetJavaScriptDialogCreator() {
  if (inspected_web_contents_ && inspected_web_contents_->GetDelegate()) {
    return inspected_web_contents_->GetDelegate()->
        GetJavaScriptDialogCreator();
  }
  return content::WebContentsDelegate::GetJavaScriptDialogCreator();
}

void DevToolsWindow::RunFileChooser(WebContents* web_contents,
                                    const FileChooserParams& params) {
  FileSelectHelper::RunFileChooser(web_contents, params);
}

void DevToolsWindow::WebContentsFocused(WebContents* contents) {
  Browser* inspected_browser = NULL;
  int inspected_tab_index = -1;

  if (IsDocked() && FindInspectedBrowserAndTabIndex(&inspected_browser,
                                                    &inspected_tab_index)) {
    inspected_browser->window()->WebContentsFocused(contents);
  }
}

void DevToolsWindow::UpdateBrowserToolbar() {
  if (!inspected_web_contents_)
    return;
  BrowserWindow* inspected_window = GetInspectedBrowserWindow();
  if (inspected_window)
    inspected_window->UpdateToolbar(inspected_web_contents_, false);
}

bool DevToolsWindow::IsDocked() {
  return dock_side_ != DEVTOOLS_DOCK_SIDE_UNDOCKED;
}

// static
DevToolsDockSide DevToolsWindow::GetDockSideFromPrefs(Profile* profile) {
  std::string dock_side =
      profile->GetPrefs()->GetString(prefs::kDevToolsDockSide);

  // Migrate prefs
  if (dock_side == kOldPrefBottom || dock_side == kOldPrefRight) {
    bool docked = profile->GetPrefs()->GetBoolean(prefs::kDevToolsOpenDocked);
    if (dock_side == kOldPrefBottom)
      return docked ? DEVTOOLS_DOCK_SIDE_BOTTOM : DEVTOOLS_DOCK_SIDE_UNDOCKED;
    else
      return docked ? DEVTOOLS_DOCK_SIDE_RIGHT : DEVTOOLS_DOCK_SIDE_UNDOCKED;
  }

  if (dock_side == kPrefUndocked)
    return DEVTOOLS_DOCK_SIDE_UNDOCKED;
  else if (dock_side == kPrefRight)
    return DEVTOOLS_DOCK_SIDE_RIGHT;
  // Default to docked to bottom
  return DEVTOOLS_DOCK_SIDE_BOTTOM;
}

// static
std::string DevToolsWindow::SideToString(DevToolsDockSide dock_side) {
  std::string dock_side_string;
  switch (dock_side) {
    case DEVTOOLS_DOCK_SIDE_UNDOCKED: return kDockSideUndocked;
    case DEVTOOLS_DOCK_SIDE_RIGHT: return kDockSideRight;
    case DEVTOOLS_DOCK_SIDE_BOTTOM: return kDockSideBottom;
  }
  return kDockSideUndocked;
}

// static
DevToolsDockSide DevToolsWindow::SideFromString(
    const std::string& dock_side) {
  if (dock_side == kDockSideRight)
    return DEVTOOLS_DOCK_SIDE_RIGHT;
  if (dock_side == kDockSideBottom)
    return DEVTOOLS_DOCK_SIDE_BOTTOM;
  return DEVTOOLS_DOCK_SIDE_UNDOCKED;
}
