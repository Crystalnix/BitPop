// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/intents/web_intents_dispatcher_impl.h"

#include "content/browser/intents/intent_injector.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/intents_messages.h"
#include "webkit/glue/web_intent_data.h"
#include "webkit/glue/web_intent_reply_data.h"

using content::WebContents;

WebIntentsDispatcherImpl::WebIntentsDispatcherImpl(
    TabContents* source_tab,
    const webkit_glue::WebIntentData& intent,
    int intent_id)
    : content::WebContentsObserver(source_tab),
      intent_(intent),
      intent_id_(intent_id),
      intent_injector_(NULL) {}

WebIntentsDispatcherImpl::~WebIntentsDispatcherImpl() {}

const webkit_glue::WebIntentData& WebIntentsDispatcherImpl::GetIntent() {
  return intent_;
}

void WebIntentsDispatcherImpl::DispatchIntent(WebContents* destination_tab) {
  DCHECK(!intent_injector_);
  intent_injector_ = new IntentInjector(destination_tab);
  intent_injector_->SetIntent(this, intent_);
}

void WebIntentsDispatcherImpl::SendReplyMessage(
    webkit_glue::WebIntentReplyType reply_type,
    const string16& data) {
  intent_injector_ = NULL;

  if (web_contents()) {
    Send(new IntentsMsg_WebIntentReply(
        routing_id(), reply_type, data, intent_id_));
  }

  for (size_t i = 0; i < reply_notifiers_.size(); ++i) {
    if (!reply_notifiers_[i].is_null())
      reply_notifiers_[i].Run(reply_type);
  }

  delete this;
}

void WebIntentsDispatcherImpl::RegisterReplyNotification(
    const base::Callback<void(webkit_glue::WebIntentReplyType)>& closure) {
  reply_notifiers_.push_back(closure);
}

void WebIntentsDispatcherImpl::WebContentsDestroyed(WebContents* tab) {
  if (intent_injector_)
    intent_injector_->SourceWebContentsDestroyed(tab);

  intent_injector_ = NULL;
}
