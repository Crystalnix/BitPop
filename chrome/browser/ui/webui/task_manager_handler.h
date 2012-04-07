// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_TASK_MANAGER_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_TASK_MANAGER_HANDLER_H_
#pragma once

#include <set>
#include <string>
#include <vector>

#include "content/public/browser/web_ui_message_handler.h"
#include "chrome/browser/task_manager/task_manager.h"

namespace base {
class ListValue;
}

class TaskManagerHandler : public content::WebUIMessageHandler,
                           public TaskManagerModelObserver {
 public:
  explicit TaskManagerHandler(TaskManager* tm);
  virtual ~TaskManagerHandler();

  // TaskManagerModelObserver implementation.
  // Invoked when the model has been completely changed.
  virtual void OnModelChanged() OVERRIDE;
  // Invoked when a range of items has changed.
  virtual void OnItemsChanged(int start, int length) OVERRIDE;
  // Invoked when new items are added.
  virtual void OnItemsAdded(int start, int length) OVERRIDE;
  // Invoked when a range of items has been removed.
  virtual void OnItemsRemoved(int start, int length) OVERRIDE;

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // Callback for the "killProcesses" message.
  void HandleKillProcesses(const base::ListValue* indexes);

  // Callback for the "activatePage" message.
  void HandleActivatePage(const base::ListValue* resource_index);

  // Callback for the "inspect" message.
  void HandleInspect(const base::ListValue* resource_index);

  void EnableTaskManager(const base::ListValue* indexes);
  void DisableTaskManager(const base::ListValue* indexes);
  void OpenAboutMemory(const base::ListValue* indexes);

  // Callback for the "setUpdateColumn" message.
  void HandleSetUpdateColumn(const base::ListValue* args);

 private:
  bool is_alive();

  // Models
  TaskManager* task_manager_;
  TaskManagerModel* model_;

  bool is_enabled_;

  // Table to cache the group index of the resource index.
  std::vector<int> resource_to_group_table_;

  // Set to store the enabled columns.
  std::set<std::string> enabled_columns_;

  // Invoked when group(s) are added/changed/removed.
  // These method are called from OnItemAdded/-Changed/-Removed internally.
  void OnGroupAdded(int start, int length);
  void OnGroupChanged(int start, int length);
  void OnGroupRemoved(int start, int length);

  // Updates |resource_to_group_table_|.
  void UpdateResourceGroupTable(int start, int length);

  DISALLOW_COPY_AND_ASSIGN(TaskManagerHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_TASK_MANAGER_HANDLER_H_
