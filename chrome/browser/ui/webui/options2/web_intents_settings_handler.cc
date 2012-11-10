// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/web_intents_settings_handler.h"

#include "base/bind.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browsing_data/browsing_data_appcache_helper.h"
#include "chrome/browser/browsing_data/browsing_data_database_helper.h"
#include "chrome/browser/browsing_data/browsing_data_file_system_helper.h"
#include "chrome/browser/browsing_data/browsing_data_indexed_db_helper.h"
#include "chrome/browser/browsing_data/browsing_data_local_storage_helper.h"
#include "chrome/browser/intents/web_intents_registry.h"
#include "chrome/browser/intents/web_intents_registry_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "net/url_request/url_request_context_getter.h"
#include "ui/base/l10n/l10n_util.h"

namespace options2 {

WebIntentsSettingsHandler::WebIntentsSettingsHandler()
    : web_intents_registry_(NULL),
      batch_update_(false) {
}

WebIntentsSettingsHandler::~WebIntentsSettingsHandler() {
}

void WebIntentsSettingsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "intentsDomain", IDS_INTENTS_DOMAIN_COLUMN_HEADER },
    { "intentsServiceData", IDS_INTENTS_SERVICE_DATA_COLUMN_HEADER },
    { "manageIntents", IDS_INTENTS_MANAGE_BUTTON },
    { "removeIntent", IDS_INTENTS_REMOVE_INTENT_BUTTON },
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "intentsViewPage",
                IDS_INTENTS_MANAGER_WINDOW_TITLE);
}

void WebIntentsSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("removeIntent",
      base::Bind(&WebIntentsSettingsHandler::RemoveIntent,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("loadIntents",
      base::Bind(&WebIntentsSettingsHandler::LoadChildren,
                 base::Unretained(this)));
}

void WebIntentsSettingsHandler::TreeNodesAdded(ui::TreeModel* model,
                                               ui::TreeModelNode* parent,
                                               int start,
                                               int count) {
  SendChildren(intents_tree_model_->GetRoot());
}

void WebIntentsSettingsHandler::TreeNodesRemoved(ui::TreeModel* model,
                                                 ui::TreeModelNode* parent,
                                                 int start,
                                                 int count) {
  SendChildren(intents_tree_model_->GetRoot());
}

void WebIntentsSettingsHandler::TreeModelBeginBatch(WebIntentsModel* model) {
  batch_update_ = true;
}

void WebIntentsSettingsHandler::TreeModelEndBatch(WebIntentsModel* model) {
  batch_update_ = false;

  SendChildren(intents_tree_model_->GetRoot());
}

void WebIntentsSettingsHandler::EnsureWebIntentsModelCreated() {
  if (intents_tree_model_.get()) return;

  Profile* profile = Profile::FromWebUI(web_ui());
  web_intents_registry_ = WebIntentsRegistryFactory::GetForProfile(profile);
  intents_tree_model_.reset(new WebIntentsModel(web_intents_registry_));
  intents_tree_model_->AddWebIntentsTreeObserver(this);
}

void WebIntentsSettingsHandler::RemoveIntent(const base::ListValue* args) {
  std::string node_path;
  if (!args->GetString(0, &node_path)) {
    return;
  }

  EnsureWebIntentsModelCreated();

  WebIntentsTreeNode* node = intents_tree_model_->GetTreeNode(node_path);
  if (node->Type() == WebIntentsTreeNode::TYPE_ORIGIN) {
    RemoveOrigin(node);
  } else if (node->Type() == WebIntentsTreeNode::TYPE_SERVICE) {
    ServiceTreeNode* snode = static_cast<ServiceTreeNode*>(node);
    RemoveService(snode);
  }
}

void WebIntentsSettingsHandler::RemoveOrigin(WebIntentsTreeNode* node) {
  // TODO(gbillock): This is a known batch update. Worth optimizing?
  while (!node->empty()) {
    WebIntentsTreeNode* cnode = node->GetChild(0);
    CHECK(cnode->Type() == WebIntentsTreeNode::TYPE_SERVICE);
    ServiceTreeNode* snode = static_cast<ServiceTreeNode*>(cnode);
    RemoveService(snode);
  }
  delete intents_tree_model_->Remove(node->parent(), node);
}

void WebIntentsSettingsHandler::RemoveService(ServiceTreeNode* snode) {
  webkit_glue::WebIntentServiceData service;
  service.service_url = GURL(snode->ServiceUrl());
  service.action = snode->Action();
  string16 stype;
  if (snode->Types().GetString(0, &stype)) {
    service.type = stype;  // Really need to iterate here.
  }
  service.title = snode->ServiceName();
  web_intents_registry_->UnregisterIntentService(service);
  delete intents_tree_model_->Remove(snode->parent(), snode);
}

void WebIntentsSettingsHandler::LoadChildren(const base::ListValue* args) {
  EnsureWebIntentsModelCreated();

  std::string node_path;
  if (!args->GetString(0, &node_path)) {
    SendChildren(intents_tree_model_->GetRoot());
    return;
  }

  WebIntentsTreeNode* node = intents_tree_model_->GetTreeNode(node_path);
  SendChildren(node);
}

void WebIntentsSettingsHandler::SendChildren(WebIntentsTreeNode* parent) {
  // Early bailout during batch updates. We'll get one after the batch concludes
  // with batch_update_ set false.
  if (batch_update_) return;

  ListValue* children = new ListValue;
  intents_tree_model_->GetChildNodeList(parent, 0, parent->child_count(),
                                        children);

  ListValue args;
  args.Append(parent == intents_tree_model_->GetRoot() ?
      Value::CreateNullValue() :
      Value::CreateStringValue(intents_tree_model_->GetTreeNodeId(parent)));
  args.Append(children);

  web_ui()->CallJavascriptFunction("IntentsView.loadChildren", args);
}

}  // namespace options2
