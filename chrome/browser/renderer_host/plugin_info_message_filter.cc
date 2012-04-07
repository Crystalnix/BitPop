// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/plugin_info_message_filter.h"

#include "base/bind.h"
#include "base/metrics/histogram.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/content_settings/content_settings_utils.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/content_settings.h"
#include "chrome/common/chrome_content_client.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "content/browser/plugin_service_filter.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/plugin_service.h"
#include "googleurl/src/gurl.h"
#include "webkit/plugins/npapi/plugin_group.h"
#include "webkit/plugins/npapi/plugin_list.h"

#if defined(OS_WIN)
// These includes are only necessary for the PluginInfobarExperiment.
#include "chrome/common/attrition_experiments.h"
#include "chrome/installer/util/google_update_settings.h"
#endif

using content::PluginService;

namespace {

// Override the behavior of the security infobars for plugins. Only
// operational on windows and only for a small slice of the of the
// UMA opted-in population.
void PluginInfobarExperiment(bool* allow_outdated,
                             bool* always_authorize) {
#if !defined(OS_WIN)
  return;
#else
 std::wstring client_value;
 if (!GoogleUpdateSettings::GetClient(&client_value))
   return;
 if (client_value == attrition_experiments::kPluginNoBlockNoOOD) {
   *always_authorize = true;
   *allow_outdated = true;
 } else if (client_value == attrition_experiments::kPluginNoBlockDoOOD) {
   *always_authorize = true;
   *allow_outdated = false;
 } else if (client_value == attrition_experiments::kPluginDoBlockNoOOD) {
   *always_authorize = false;
   *allow_outdated = true;
 } else if (client_value == attrition_experiments::kPluginDoBlockDoOOD) {
   *always_authorize = false;
   *allow_outdated = false;
 }
#endif
}

}  // namespace

PluginInfoMessageFilter::Context::Context(int render_process_id,
                                          Profile* profile)
    : render_process_id_(render_process_id),
      resource_context_(&profile->GetResourceContext()),
      host_content_settings_map_(profile->GetHostContentSettingsMap()) {
  allow_outdated_plugins_.Init(prefs::kPluginsAllowOutdated,
                               profile->GetPrefs(), NULL);
  allow_outdated_plugins_.MoveToThread(content::BrowserThread::IO);
  always_authorize_plugins_.Init(prefs::kPluginsAlwaysAuthorize,
                                 profile->GetPrefs(), NULL);
  always_authorize_plugins_.MoveToThread(content::BrowserThread::IO);
}

PluginInfoMessageFilter::Context::Context()
    : render_process_id_(0),
      resource_context_(NULL),
      host_content_settings_map_(NULL) {
}

PluginInfoMessageFilter::Context::~Context() {
}

PluginInfoMessageFilter::PluginInfoMessageFilter(
    int render_process_id,
    Profile* profile)
    : context_(render_process_id, profile),
      weak_ptr_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
}

PluginInfoMessageFilter::~PluginInfoMessageFilter() {}

bool PluginInfoMessageFilter::OnMessageReceived(const IPC::Message& message,
                                                bool* message_was_ok) {
  IPC_BEGIN_MESSAGE_MAP_EX(PluginInfoMessageFilter, message, *message_was_ok)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(ChromeViewHostMsg_GetPluginInfo,
                                    OnGetPluginInfo)
    IPC_MESSAGE_UNHANDLED(return false)
  IPC_END_MESSAGE_MAP()
  return true;
}

void PluginInfoMessageFilter::OnDestruct() const {
  const_cast<PluginInfoMessageFilter*>(this)->
      weak_ptr_factory_.DetachFromThread();
  const_cast<PluginInfoMessageFilter*>(this)->
      weak_ptr_factory_.InvalidateWeakPtrs();

  // Destroy on the UI thread because we contain a |PrefMember|.
  content::BrowserThread::DeleteOnUIThread::Destruct(this);
}

struct PluginInfoMessageFilter::GetPluginInfo_Params {
  int render_view_id;
  GURL url;
  GURL top_origin_url;
  std::string mime_type;
};

void PluginInfoMessageFilter::OnGetPluginInfo(
    int render_view_id,
    const GURL& url,
    const GURL& top_origin_url,
    const std::string& mime_type,
    IPC::Message* reply_msg) {
  GetPluginInfo_Params params = {
    render_view_id,
    url,
    top_origin_url,
    mime_type
  };
  PluginService::GetInstance()->GetPlugins(
      base::Bind(&PluginInfoMessageFilter::PluginsLoaded,
                 weak_ptr_factory_.GetWeakPtr(),
                 params, reply_msg));
}

void PluginInfoMessageFilter::PluginsLoaded(
    const GetPluginInfo_Params& params,
    IPC::Message* reply_msg,
    const std::vector<webkit::WebPluginInfo>& plugins) {
  ChromeViewHostMsg_GetPluginInfo_Status status;
  webkit::WebPluginInfo plugin;
  std::string actual_mime_type;
  context_.DecidePluginStatus(params, &status, &plugin, &actual_mime_type);
  ChromeViewHostMsg_GetPluginInfo::WriteReplyParams(
      reply_msg, status, plugin, actual_mime_type);
  Send(reply_msg);
}

void PluginInfoMessageFilter::Context::DecidePluginStatus(
    const GetPluginInfo_Params& params,
    ChromeViewHostMsg_GetPluginInfo_Status* status,
    webkit::WebPluginInfo* plugin,
    std::string* actual_mime_type) const {
  status->value = ChromeViewHostMsg_GetPluginInfo_Status::kAllowed;
  // This also fills in |actual_mime_type|.
  if (FindEnabledPlugin(params.render_view_id, params.url,
                        params.top_origin_url, params.mime_type,
                        status, plugin, actual_mime_type)) {
    return;
  }

  ContentSetting plugin_setting = CONTENT_SETTING_DEFAULT;
  bool uses_default_content_setting = true;
  // Check plug-in content settings. The primary URL is the top origin URL and
  // the secondary URL is the plug-in URL.
  scoped_ptr<webkit::npapi::PluginGroup> group(
      webkit::npapi::PluginList::Singleton()->GetPluginGroup(*plugin));

  GetPluginContentSetting(plugin, params.top_origin_url, params.url,
                          group->identifier(), &plugin_setting,
                          &uses_default_content_setting);
  DCHECK(plugin_setting != CONTENT_SETTING_DEFAULT);

  bool allow_outdated = allow_outdated_plugins_.GetValue();
  bool always_authorize = always_authorize_plugins_.GetValue();

  PluginInfobarExperiment(&allow_outdated, &always_authorize);

  // Check if the plug-in is outdated.
  if (group->IsVulnerable(*plugin) && !allow_outdated) {
    if (allow_outdated_plugins_.IsManaged()) {
      status->value =
          ChromeViewHostMsg_GetPluginInfo_Status::kOutdatedDisallowed;
    } else {
      status->value =
          ChromeViewHostMsg_GetPluginInfo_Status::kOutdatedBlocked;
    }
    return;
  }

  // Check if the plug-in requires authorization.
  if ((group->RequiresAuthorization(*plugin) ||
       PluginService::GetInstance()->IsPluginUnstable(plugin->path)) &&
      !always_authorize &&
      plugin_setting != CONTENT_SETTING_BLOCK &&
      uses_default_content_setting) {
    status->value =
       ChromeViewHostMsg_GetPluginInfo_Status::kUnauthorized;
    return;
  }

  if (plugin_setting == CONTENT_SETTING_ASK)
    status->value = ChromeViewHostMsg_GetPluginInfo_Status::kClickToPlay;
  else if (plugin_setting == CONTENT_SETTING_BLOCK)
    status->value = ChromeViewHostMsg_GetPluginInfo_Status::kBlocked;
}

bool PluginInfoMessageFilter::Context::FindEnabledPlugin(
    int render_view_id,
    const GURL& url,
    const GURL& top_origin_url,
    const std::string& mime_type,
    ChromeViewHostMsg_GetPluginInfo_Status* status,
    webkit::WebPluginInfo* plugin,
    std::string* actual_mime_type) const {
  bool allow_wildcard = true;
  std::vector<webkit::WebPluginInfo> matching_plugins;
  std::vector<std::string> mime_types;
  PluginService::GetInstance()->GetPluginInfoArray(
      url, mime_type, allow_wildcard, &matching_plugins, &mime_types);
  content::PluginServiceFilter* filter =
      PluginService::GetInstance()->GetFilter();
  bool found = false;
  for (size_t i = 0; i < matching_plugins.size(); ++i) {
    bool enabled = !filter || filter->ShouldUsePlugin(render_process_id_,
                                                      render_view_id,
                                                      resource_context_,
                                                      url,
                                                      top_origin_url,
                                                      &matching_plugins[i]);
    if (!found || enabled) {
      *plugin = matching_plugins[i];
      *actual_mime_type = mime_types[i];
      if (enabled) {
        // We have found an enabled plug-in. Return immediately.
        return false;
      }
      // We have found a plug-in, but it's disabled. Keep looking for an
      // enabled one.
      found = true;
    }
  }

  // If we're here and have previously found a plug-in, it must have been
  // disabled.
  if (found)
    status->value = ChromeViewHostMsg_GetPluginInfo_Status::kDisabled;
  else
    status->value = ChromeViewHostMsg_GetPluginInfo_Status::kNotFound;
  return true;
}

void PluginInfoMessageFilter::Context::GetPluginContentSetting(
    const webkit::WebPluginInfo* plugin,
    const GURL& policy_url,
    const GURL& plugin_url,
    const std::string& resource,
    ContentSetting* setting,
    bool* uses_default_content_setting) const {
  // Treat Native Client invocations like Javascript.
  bool is_nacl_plugin = (plugin->name == ASCIIToUTF16(
      chrome::ChromeContentClient::kNaClPluginName));

  scoped_ptr<base::Value> value;
  content_settings::SettingInfo info;
  if (is_nacl_plugin) {
    value.reset(
        host_content_settings_map_->GetWebsiteSetting(
            policy_url, policy_url, CONTENT_SETTINGS_TYPE_JAVASCRIPT,
            std::string(), &info));
  } else {
    value.reset(
        host_content_settings_map_->GetWebsiteSetting(
            policy_url, plugin_url, CONTENT_SETTINGS_TYPE_PLUGINS, resource,
            &info));
    if (!value.get()) {
      value.reset(host_content_settings_map_->GetWebsiteSetting(
          policy_url, plugin_url, CONTENT_SETTINGS_TYPE_PLUGINS, std::string(),
          &info));
    }
  }
  *setting = content_settings::ValueToContentSetting(value.get());
  *uses_default_content_setting =
      (info.primary_pattern == ContentSettingsPattern::Wildcard() &&
       info.secondary_pattern == ContentSettingsPattern::Wildcard());
}
