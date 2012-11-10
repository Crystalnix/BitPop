// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/page_action_controller.h"

#include "chrome/browser/extensions/browser_event_router.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/sessions/session_id.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/common/extensions/extension_set.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"

namespace extensions {

PageActionController::PageActionController(TabContents* tab_contents)
    : content::WebContentsObserver(tab_contents->web_contents()),
      tab_contents_(tab_contents) {}

PageActionController::~PageActionController() {}

std::vector<ExtensionAction*> PageActionController::GetCurrentActions() const {
  ExtensionService* service = GetExtensionService();
  if (!service)
    return std::vector<ExtensionAction*>();

  std::vector<ExtensionAction*> current_actions;

  for (ExtensionSet::const_iterator i = service->extensions()->begin();
       i != service->extensions()->end(); ++i) {
    ExtensionAction* action = (*i)->page_action();
    if (action)
      current_actions.push_back(action);
  }
  return current_actions;
}

LocationBarController::Action PageActionController::OnClicked(
    const std::string& extension_id, int mouse_button) {
  ExtensionService* service = GetExtensionService();
  if (!service)
    return ACTION_NONE;

  const Extension* extension = service->extensions()->GetByID(extension_id);
  CHECK(extension);
  ExtensionAction* page_action = extension->page_action();
  CHECK(page_action);
  int tab_id = ExtensionTabUtil::GetTabId(tab_contents_->web_contents());

  tab_contents_->extension_tab_helper()->active_tab_permission_manager()->
      GrantIfRequested(extension);

  switch (mouse_button) {
    case 1:  // left
    case 2:  // middle
      if (page_action->HasPopup(tab_id))
        return ACTION_SHOW_POPUP;

      GetExtensionService()->browser_event_router()->PageActionExecuted(
          tab_contents_->profile(),
          *page_action,
          tab_id,
          tab_contents_->web_contents()->GetURL().spec(),
          mouse_button);
      return ACTION_NONE;

    case 3:  // right
      return extension->ShowConfigureContextMenus() ?
          ACTION_SHOW_CONTEXT_MENU : ACTION_NONE;
  }

  return ACTION_NONE;
}

void PageActionController::NotifyChange() {
  tab_contents_->web_contents()->NotifyNavigationStateChanged(
      content::INVALIDATE_TYPE_PAGE_ACTIONS);
}

void PageActionController::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  if (details.is_in_page)
    return;

  const std::vector<ExtensionAction*> current_actions = GetCurrentActions();

  if (current_actions.empty())
    return;

  for (size_t i = 0; i < current_actions.size(); ++i) {
    current_actions[i]->ClearAllValuesForTab(
        SessionID::IdForTab(tab_contents_));
  }

  NotifyChange();
}

ExtensionService* PageActionController::GetExtensionService() const {
  return ExtensionSystem::Get(tab_contents_->profile())->extension_service();
}

}  // namespace extensions
