// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/app_runtime/app_runtime_api.h"

#include "base/json/json_writer.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/web_intent_callbacks.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_intents_dispatcher.h"
#include "googleurl/src/gurl.h"
#include "webkit/glue/web_intent_data.h"

namespace extensions {

namespace {

const char kIntentIdKey[] = "intentId";
const char kIntentSuccessKey[] = "success";
const char kIntentDataKey[] = "data";
const char kOnLaunchedEvent[] = "app.runtime.onLaunched";
const char kOnRestartedEvent[] = "app.runtime.onRestarted";

const char kCallbackNotFoundError[] =
    "WebIntent callback not found; perhaps already responded to";

void DispatchOnLaunchedEventImpl(const std::string& extension_id,
                                 scoped_ptr<base::ListValue> args,
                                 Profile* profile) {
  extensions::ExtensionSystem* system =
      extensions::ExtensionSystem::Get(profile);
  // Special case: normally, extensions add their own lazy event listeners.
  // However, since the extension might have just been enabled, it hasn't had a
  // chance to register for events. So we register on its behalf. If the
  // extension does not actually have a listener, the event will just be
  // ignored (but an app that doesn't listen for the onLaunched event doesn't
  // make sense anyway).
  system->event_router()->AddLazyEventListener(kOnLaunchedEvent, extension_id);
  scoped_ptr<Event> event(new Event(kOnLaunchedEvent, args.Pass()));
  event->restrict_to_profile = profile;
  system->event_router()->DispatchEventToExtension(extension_id, event.Pass());
  system->event_router()->RemoveLazyEventListener(kOnLaunchedEvent,
                                                  extension_id);
}

}  // anonymous namespace

// static.
void AppEventRouter::DispatchOnLaunchedEvent(
    Profile* profile, const Extension* extension) {
  scoped_ptr<ListValue> arguments(new ListValue());
  DispatchOnLaunchedEventImpl(extension->id(), arguments.Pass(), profile);
}

// static.
void AppEventRouter::DispatchOnRestartedEvent(
    Profile* profile, const Extension* extension) {
  scoped_ptr<ListValue> arguments(new ListValue());
  scoped_ptr<Event> event(new Event(kOnRestartedEvent, arguments.Pass()));
  event->restrict_to_profile = profile;
  extensions::ExtensionSystem::Get(profile)->event_router()->
      DispatchEventToExtension(extension->id(), event.Pass());
}

// static.
void AppEventRouter::DispatchOnLaunchedEventWithFileEntry(
    Profile* profile, const Extension* extension, const string16& action,
    const std::string& handler_id, const std::string& mime_type,
    const std::string& file_system_id, const std::string& base_name) {
  scoped_ptr<ListValue> args(new ListValue());
  DictionaryValue* launch_data = new DictionaryValue();
  launch_data->SetString("id", handler_id);
  DictionaryValue* launch_item = new DictionaryValue;
  launch_item->SetString("fileSystemId", file_system_id);
  launch_item->SetString("baseName", base_name);
  launch_item->SetString("mimeType", mime_type);
  ListValue* items = new ListValue;
  items->Append(launch_item);
  launch_data->Set("items", items);
  args->Append(launch_data);
  DispatchOnLaunchedEventImpl(extension->id(), args.Pass(), profile);
}

// static.
void AppEventRouter::DispatchOnLaunchedEventWithWebIntent(
    Profile* profile, const Extension* extension,
    content::WebIntentsDispatcher* intents_dispatcher,
    content::WebContents* source) {
  webkit_glue::WebIntentData web_intent_data = intents_dispatcher->GetIntent();
  scoped_ptr<ListValue> args(new ListValue());
  DictionaryValue* launch_data = new DictionaryValue();
  DictionaryValue* intent = new DictionaryValue();
  intent->SetString("action", UTF16ToUTF8(web_intent_data.action));
  intent->SetString("type", UTF16ToUTF8(web_intent_data.type));
  launch_data->Set("intent", intent);
  args->Append(launch_data);
  DictionaryValue* intent_data;
  switch (web_intent_data.data_type) {
    case webkit_glue::WebIntentData::SERIALIZED:
      intent_data = new DictionaryValue();
      intent_data->SetString("format", "serialized");
      intent_data->SetString("data", UTF16ToUTF8(web_intent_data.data));
      // NOTE: This second argument is dropped before being dispatched to the
      // client code.
      args->Append(intent_data);
      break;
    case webkit_glue::WebIntentData::UNSERIALIZED:
      args->Append(Value::CreateNullValue());
      intent->SetString("data", UTF16ToUTF8(web_intent_data.unserialized_data));
      break;
    case webkit_glue::WebIntentData::BLOB:
      intent_data = new DictionaryValue();
      intent_data->SetString("format", "blob");
      intent_data->SetString("blobFileName", web_intent_data.blob_file.value());
      intent_data->SetString("blobLength",
                             base::Int64ToString(web_intent_data.blob_length));
      // NOTE: This second argument is dropped before being dispatched to the
      // client code.
      args->Append(intent_data);
      break;
    case webkit_glue::WebIntentData::FILESYSTEM:
      intent_data = new DictionaryValue();
      intent_data->SetString("format", "filesystem");
      intent_data->SetString("fileSystemId", web_intent_data.filesystem_id);
      intent_data->SetString("baseName", web_intent_data.root_name);
      args->Append(intent_data);
      break;
    default:
      NOTREACHED();
      break;
  }
  DCHECK(args->GetSize() == 2);  // intent_id must be our third argument.
  WebIntentCallbacks* callbacks = WebIntentCallbacks::Get(profile);
  int intent_id =
      callbacks->RegisterCallback(extension, intents_dispatcher, source);
  args->Append(base::Value::CreateIntegerValue(intent_id));
  DispatchOnLaunchedEventImpl(extension->id(), args.Pass(), profile);
}

bool AppRuntimePostIntentResponseFunction::RunImpl() {
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &details));

  int intent_id = 0;
  EXTENSION_FUNCTION_VALIDATE(details->GetInteger(kIntentIdKey, &intent_id));

  WebIntentCallbacks* callbacks = WebIntentCallbacks::Get(profile());
  content::WebIntentsDispatcher* intents_dispatcher =
      callbacks->RetrieveCallback(GetExtension(), intent_id);
  if (!intents_dispatcher) {
    error_ = kCallbackNotFoundError;
    return false;
  }

  webkit_glue::WebIntentReplyType reply_type =
      webkit_glue::WEB_INTENT_REPLY_FAILURE;
  bool success;
  EXTENSION_FUNCTION_VALIDATE(details->GetBoolean(kIntentSuccessKey, &success));
  if (success)
    reply_type = webkit_glue::WEB_INTENT_REPLY_SUCCESS;

  std::string data;
  EXTENSION_FUNCTION_VALIDATE(details->GetString(kIntentDataKey, &data));

  intents_dispatcher->SendReply(webkit_glue::WebIntentReply(
      reply_type, UTF8ToUTF16(data)));

  return true;
}

}  // namespace extensions
