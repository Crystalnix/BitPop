// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/task_manager_handler.h"

#include <algorithm>
#include <functional>
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/task_manager/task_manager.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/render_view_host_delegate.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "webkit/glue/webpreferences.h"

namespace {

Value* CreateColumnValue(const TaskManagerModel* tm,
                         const std::string column_name,
                         const int i) {
  if (column_name == "uniqueId")
    return Value::CreateIntegerValue(tm->GetResourceUniqueId(i));
  if (column_name == "type")
    return Value::CreateStringValue(
        TaskManager::Resource::GetResourceTypeAsString(
        tm->GetResourceType(i)));
  if (column_name == "processId")
    return Value::CreateStringValue(tm->GetResourceProcessId(i));
  if (column_name == "processIdValue")
    return Value::CreateIntegerValue(tm->GetProcessId(i));
  if (column_name == "cpuUsage")
    return Value::CreateStringValue(tm->GetResourceCPUUsage(i));
  if (column_name == "cpuUsageValue")
    return Value::CreateDoubleValue(tm->GetCPUUsage(i));
  if (column_name == "privateMemory")
    return Value::CreateStringValue(tm->GetResourcePrivateMemory(i));
  if (column_name == "privateMemoryValue") {
    size_t private_memory;
    tm->GetPrivateMemory(i, &private_memory);
    return Value::CreateDoubleValue(private_memory);
  }
  if (column_name == "sharedMemory")
    return Value::CreateStringValue(tm->GetResourceSharedMemory(i));
  if (column_name == "sharedMemoryValue") {
    size_t shared_memory;
    tm->GetSharedMemory(i, &shared_memory);
    return Value::CreateDoubleValue(shared_memory);
  }
  if (column_name == "physicalMemory")
    return Value::CreateStringValue(tm->GetResourcePhysicalMemory(i));
  if (column_name == "physicalMemoryValue") {
    size_t physical_memory;
    tm->GetPhysicalMemory(i, &physical_memory);
    return Value::CreateDoubleValue(physical_memory);
  }
  if (column_name == "icon")
    return Value::CreateStringValue(
               web_ui_util::GetImageDataUrl(tm->GetResourceIcon(i)));
  if (column_name == "title")
    return Value::CreateStringValue(tm->GetResourceTitle(i));
  if (column_name == "profileName")
    return Value::CreateStringValue(tm->GetResourceProfileName(i));
  if (column_name == "networkUsage")
    return Value::CreateStringValue(tm->GetResourceNetworkUsage(i));
  if (column_name == "networkUsageValue")
    return Value::CreateDoubleValue(tm->GetNetworkUsage(i));
  if (column_name == "webCoreImageCacheSize")
    return Value::CreateStringValue(tm->GetResourceWebCoreImageCacheSize(i));
  if (column_name == "webCoreImageCacheSizeValue") {
    WebKit::WebCache::ResourceTypeStats resource_stats;
    tm->GetWebCoreCacheStats(i, &resource_stats);
    return Value::CreateDoubleValue(resource_stats.images.size);
  }
  if (column_name == "webCoreScriptsCacheSize")
    return Value::CreateStringValue(tm->GetResourceWebCoreScriptsCacheSize(i));
  if (column_name == "webCoreScriptsCacheSizeValue") {
    WebKit::WebCache::ResourceTypeStats resource_stats;
    tm->GetWebCoreCacheStats(i, &resource_stats);
    return Value::CreateDoubleValue(resource_stats.scripts.size);
  }
  if (column_name == "webCoreCSSCacheSize")
    return Value::CreateStringValue(tm->GetResourceWebCoreCSSCacheSize(i));
  if (column_name == "webCoreCSSCacheSizeValue") {
    WebKit::WebCache::ResourceTypeStats resource_stats;
    tm->GetWebCoreCacheStats(i, &resource_stats);
    return Value::CreateDoubleValue(resource_stats.cssStyleSheets.size);
  }
  if (column_name == "fps")
    return Value::CreateStringValue(tm->GetResourceFPS(i));
  if (column_name == "fpsValue") {
    float fps;
    tm->GetFPS(i, &fps);
    return Value::CreateDoubleValue(fps);
  }
  if (column_name == "sqliteMemoryUsed")
    return Value::CreateStringValue(tm->GetResourceSqliteMemoryUsed(i));
  if (column_name == "sqliteMemoryUsedValue") {
    size_t sqlite_memory;
    tm->GetSqliteMemoryUsedBytes(i, &sqlite_memory);
    return Value::CreateDoubleValue(sqlite_memory);
  }
  if (column_name == "goatsTeleported")
    return Value::CreateStringValue(tm->GetResourceGoatsTeleported(i));
  if (column_name == "goatsTeleportedValue")
    return Value::CreateIntegerValue(tm->GetGoatsTeleported(i));
  if (column_name == "v8MemoryAllocatedSize")
    return Value::CreateStringValue(tm->GetResourceV8MemoryAllocatedSize(i));
  if (column_name == "v8MemoryAllocatedSizeValue") {
    size_t v8_memory;
    tm->GetV8Memory(i, &v8_memory);
    return Value::CreateDoubleValue(v8_memory);
  }
  if (column_name == "canInspect")
    return Value::CreateBooleanValue(tm->CanInspect(i));
  if (column_name == "canActivate")
    return Value::CreateBooleanValue(tm->CanActivate(i));

  NOTREACHED();
  return NULL;
}

void CreateGroupColumnList(const TaskManagerModel* tm,
                           const std::string& column_name,
                           const int index,
                           const int length,
                           DictionaryValue* val) {
  ListValue *list = new ListValue();
  for (int i = index; i < (index + length); ++i) {
    list->Append(CreateColumnValue(tm, column_name, i));
  }
  val->Set(column_name, list);
}

struct ColumnType {
  const char* column_id;
  // Whether the column has the real value separately or not, instead of the
  // formatted value to display.
  const bool has_real_value;
  // Whether the column has single datum or multiple data in each group.
  const bool has_multiple_data;
};

const ColumnType kColumnsList[] = {
  {"type", false, false},
  {"processId", true, false},
  {"cpuUsage", true, false},
  {"physicalMemory", true, false},
  {"sharedMemory", true, false},
  {"privateMemory", true, false},
  {"webCoreImageCacheSize", true, false},
  {"webCoreImageCacheSize", true, false},
  {"webCoreScriptsCacheSize", true, false},
  {"webCoreCSSCacheSize", true, false},
  {"sqliteMemoryUsed", true, false},
  {"v8MemoryAllocatedSize", true, false},
  {"icon", false, true},
  {"title", false, true},
  {"profileName", false, true},
  {"networkUsage", true, true},
  {"fps", true, true},
  {"goatsTeleported", true, true},
  {"canInspect", false, true},
  {"canActivate", false, true}
};

DictionaryValue* CreateTaskGroupValue(
    const TaskManagerModel* tm,
    const int group_index,
    const std::set<std::string>& columns) {
  DictionaryValue* val = new DictionaryValue();

  const int group_count = tm->GroupCount();
  if (group_index >= group_count)
     return val;

  int index = tm->GetResourceIndexForGroup(group_index, 0);
  std::pair<int, int> group_range;
  group_range = tm->GetGroupRangeForResource(index);
  int length = group_range.second;

  // Forces to set following 3 columns regardless of |enable_columns|.
  val->SetInteger("index", index);
  val->SetBoolean("isBackgroundResource",
                  tm->IsBackgroundResource(index));
  CreateGroupColumnList(tm, "uniqueId", index, length, val);
  CreateGroupColumnList(tm, "processId", index, 1, val);

  for (size_t i = 0; i < arraysize(kColumnsList); ++i) {
    const std::string column_id = kColumnsList[i].column_id;

    if (columns.find(column_id) == columns.end())
      continue;

    int column_length = kColumnsList[i].has_multiple_data ? length : 1;
    CreateGroupColumnList(tm, column_id, index, column_length, val);

    if (kColumnsList[i].has_real_value)
      CreateGroupColumnList(tm, column_id + "Value", index, column_length, val);
  }

  return val;
}

}  // namespace

TaskManagerHandler::TaskManagerHandler(TaskManager* tm)
    : task_manager_(tm),
      model_(tm->model()),
      is_enabled_(false) {
}

TaskManagerHandler::~TaskManagerHandler() {
  DisableTaskManager(NULL);
}

// TaskManagerHandler, public: -----------------------------------------------

void TaskManagerHandler::OnModelChanged() {
  const int count = model_->GroupCount();

  base::FundamentalValue start_value(0);
  base::FundamentalValue length_value(count);
  base::ListValue tasks_value;
  for (int i = 0; i < count; ++i)
    tasks_value.Append(CreateTaskGroupValue(model_, i, enabled_columns_));

  if (is_enabled_) {
    web_ui()->CallJavascriptFunction("taskChanged",
                                     start_value, length_value, tasks_value);
  }
}

void TaskManagerHandler::OnItemsChanged(const int start, const int length) {
  UpdateResourceGroupTable(start, length);

  // Converts from an index of resources to an index of groups.
  int group_start = model_->GetGroupIndexForResource(start);
  int group_end = model_->GetGroupIndexForResource(start + length - 1);

  OnGroupChanged(group_start, group_end - group_start + 1);
}

void TaskManagerHandler::OnItemsAdded(const int start, const int length) {
  UpdateResourceGroupTable(start, length);

  // Converts from an index of resources to an index of groups.
  int group_start = model_->GetGroupIndexForResource(start);
  int group_end = model_->GetGroupIndexForResource(start + length - 1);

  // First group to add does not contain all the items in the group. Because the
  // first item to add and the previous one are in same group.
  if (!model_->IsResourceFirstInGroup(start)) {
    OnGroupChanged(group_start, 1);
    if (group_start == group_end)
      return;
    else
      group_start++;
  }

  // Last group to add does not contain all the items in the group. Because the
  // last item to add and the next one are in same group.
  if (!model_->IsResourceLastInGroup(start + length - 1)) {
    OnGroupChanged(group_end, 1);
    if (group_start == group_end)
      return;
    else
      group_end--;
  }

  OnGroupAdded(group_start, group_end - group_start + 1);
}

void TaskManagerHandler::OnItemsRemoved(const int start, const int length) {
  // Returns if this is called before updating |resource_to_group_table_|.
  if (resource_to_group_table_.size() <= static_cast<size_t>(start + length))
    return;

  // Converts from an index of resources to an index of groups.
  int group_start = resource_to_group_table_[start];
  int group_end = resource_to_group_table_[start + length - 1];

  // First group to remove does not contain all the items in the group. Because
  // the first item to remove and the previous one are in same group.
  if (start != 0 && group_start == resource_to_group_table_[start - 1]) {
    OnGroupChanged(group_start, 1);
    if (group_start == group_end)
      return;
    else
      group_start++;
  }

  // Last group to remove does not contain all the items in the group. Because
  // the last item to remove and the next one are in same group.
  if (start + length != model_->ResourceCount() &&
      group_end == resource_to_group_table_[start + length]) {
    OnGroupChanged(group_end, 1);
    if (group_start == group_end)
      return;
    else
      group_end--;
  }

  std::vector<int>::iterator it_first =
      resource_to_group_table_.begin() + start;
  std::vector<int>::iterator it_last = it_first + length - 1;
  resource_to_group_table_.erase(it_first, it_last);

  OnGroupRemoved(group_start, group_end - group_start + 1);
}

void TaskManagerHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("killProcesses",
      base::Bind(&TaskManagerHandler::HandleKillProcesses,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("inspect",
      base::Bind(&TaskManagerHandler::HandleInspect,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("activatePage",
      base::Bind(&TaskManagerHandler::HandleActivatePage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("openAboutMemory",
      base::Bind(&TaskManagerHandler::OpenAboutMemory,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("enableTaskManager",
      base::Bind(&TaskManagerHandler::EnableTaskManager,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("disableTaskManager",
      base::Bind(&TaskManagerHandler::DisableTaskManager,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("setUpdateColumn",
      base::Bind(&TaskManagerHandler::HandleSetUpdateColumn,
                 base::Unretained(this)));
}

static int parseIndex(const Value* value) {
  int index = -1;
  string16 string16_index;
  double double_index;
  if (value->GetAsString(&string16_index)) {
    bool converted = base::StringToInt(string16_index, &index);
    DCHECK(converted);
  } else if (value->GetAsDouble(&double_index)) {
    index = static_cast<int>(double_index);
  } else {
    value->GetAsInteger(&index);
  }
  return index;
}

void TaskManagerHandler::HandleKillProcesses(const ListValue* unique_ids) {
  for (ListValue::const_iterator i = unique_ids->begin();
       i != unique_ids->end(); ++i) {
    int unique_id = parseIndex(*i);
    int resource_index = model_->GetResourceIndexByUniqueId(unique_id);
    if (resource_index == -1)
      continue;

    task_manager_->KillProcess(resource_index);
  }
}

void TaskManagerHandler::HandleActivatePage(const ListValue* unique_ids) {
  for (ListValue::const_iterator i = unique_ids->begin();
       i != unique_ids->end(); ++i) {
    int unique_id = parseIndex(*i);
    int resource_index = model_->GetResourceIndexByUniqueId(unique_id);
    if (resource_index == -1)
      continue;

    task_manager_->ActivateProcess(resource_index);
    break;
  }
}

void TaskManagerHandler::HandleInspect(const ListValue* unique_ids) {
  for (ListValue::const_iterator i = unique_ids->begin();
       i != unique_ids->end(); ++i) {
    int unique_id = parseIndex(*i);
    int resource_index = model_->GetResourceIndexByUniqueId(unique_id);
    if (resource_index == -1)
      continue;

    if (model_->CanInspect(resource_index))
      model_->Inspect(resource_index);
    break;
  }
}

void TaskManagerHandler::DisableTaskManager(const ListValue* indexes) {
  if (!is_enabled_)
    return;

  is_enabled_ = false;
  model_->StopUpdating();
  model_->RemoveObserver(this);
}

void TaskManagerHandler::EnableTaskManager(const ListValue* indexes) {
  if (is_enabled_)
    return;

  is_enabled_ = true;
  model_->AddObserver(this);
  model_->StartUpdating();

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_TASK_MANAGER_WINDOW_READY,
      content::Source<TaskManagerModel>(model_),
      content::NotificationService::NoDetails());
}

void TaskManagerHandler::OpenAboutMemory(const ListValue* indexes) {
  RenderViewHost* rvh = web_ui()->GetWebContents()->GetRenderViewHost();
  if (rvh && rvh->delegate()) {
    WebPreferences webkit_prefs = rvh->delegate()->GetWebkitPrefs();
    webkit_prefs.allow_scripts_to_close_windows = true;
    rvh->UpdateWebkitPreferences(webkit_prefs);
  } else {
    DCHECK(false);
  }

  task_manager_->OpenAboutMemory();
}

void TaskManagerHandler::HandleSetUpdateColumn(const ListValue* args) {
  DCHECK_EQ(2U, args->GetSize());

  bool ret = true;
  std::string column_id;
  ret &= args->GetString(0, &column_id);
  bool is_enabled;
  ret &= args->GetBoolean(1, &is_enabled);
  DCHECK(ret);

  if (is_enabled)
    enabled_columns_.insert(column_id);
  else
    enabled_columns_.erase(column_id);
}

// TaskManagerHandler, private: -----------------------------------------------

bool TaskManagerHandler::is_alive() {
  return web_ui()->GetWebContents()->GetRenderViewHost() != NULL;
}

void TaskManagerHandler::UpdateResourceGroupTable(int start, int length) {
  if (resource_to_group_table_.size() < static_cast<size_t>(start)) {
    length += start - resource_to_group_table_.size();
    start = resource_to_group_table_.size();
  }

  // Makes room to fill group_index at first since inserting vector is too slow.
  std::vector<int>::iterator it = resource_to_group_table_.begin() + start;
  resource_to_group_table_.insert(it, static_cast<size_t>(length), -1);

  for (int i = start; i < start + length; ++i) {
    const int group_index = model_->GetGroupIndexForResource(i);
    resource_to_group_table_[i] = group_index;
  }
}

void TaskManagerHandler::OnGroupChanged(const int group_start,
                                        const int group_length) {
  base::FundamentalValue start_value(group_start);
  base::FundamentalValue length_value(group_length);
  base::ListValue tasks_value;

  for (int i = 0; i < group_length; ++i)
    tasks_value.Append(
        CreateTaskGroupValue(model_, group_start + i, enabled_columns_));

  if (is_enabled_ && is_alive()) {
    web_ui()->CallJavascriptFunction("taskChanged",
                                     start_value, length_value, tasks_value);
  }
}

void TaskManagerHandler::OnGroupAdded(const int group_start,
                                      const int group_length) {
  base::FundamentalValue start_value(group_start);
  base::FundamentalValue length_value(group_length);
  base::ListValue tasks_value;
  for (int i = 0; i < group_length; ++i)
    tasks_value.Append(
        CreateTaskGroupValue(model_, group_start + i, enabled_columns_));

  if (is_enabled_ && is_alive()) {
    web_ui()->CallJavascriptFunction("taskAdded",
                                     start_value, length_value, tasks_value);
  }
}

void TaskManagerHandler::OnGroupRemoved(const int group_start,
                                        const int group_length) {
  base::FundamentalValue start_value(group_start);
  base::FundamentalValue length_value(group_length);
  if (is_enabled_ && is_alive())
    web_ui()->CallJavascriptFunction("taskRemoved", start_value, length_value);
}
