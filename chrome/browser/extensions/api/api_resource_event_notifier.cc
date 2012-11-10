// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/api_resource_event_notifier.h"

#include "base/bind.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace events {
const char kExperimentalUsbOnEvent[] = "experimental.usb.onEvent";
};

namespace extensions {

const char kEventTypeKey[] = "type";

const char kEventTypeConnectComplete[] = "connectComplete";
const char kEventTypeDataRead[] = "dataRead";
const char kEventTypeWriteComplete[] = "writeComplete";

const char kEventTypeTransferComplete[] = "transferComplete";

const char kSrcIdKey[] = "srcId";
const char kIsFinalEventKey[] = "isFinalEvent";

const char kResultCodeKey[] = "resultCode";
const char kDataKey[] = "data";
const char kAddressKey[] = "address";
const char kPortKey[] = "port";
const char kErrorKey[] = "error";

ApiResourceEventNotifier::ApiResourceEventNotifier(
    EventRouter* router,
    Profile* profile,
    const std::string& src_extension_id,
    int src_id,
    const GURL& src_url)
    : router_(router),
      profile_(profile),
      src_extension_id_(src_extension_id),
      src_id_(src_id),
      src_url_(src_url) {
}

void ApiResourceEventNotifier::OnTransferComplete(UsbTransferStatus status,
                                                  const std::string& error,
                                                  base::BinaryValue* data) {
  if (src_id_ < 0) {
    delete data;
    return;
  }

  DictionaryValue* event = CreateApiResourceEvent(
      API_RESOURCE_EVENT_TRANSFER_COMPLETE);
  event->SetInteger(kResultCodeKey, status);
  event->Set(kDataKey, data);
  if (!error.empty()) {
    event->SetString(kErrorKey, error);
  }

  DispatchEvent(events::kExperimentalUsbOnEvent, event);
}

// static
std::string ApiResourceEventNotifier::ApiResourceEventTypeToString(
    ApiResourceEventType event_type) {
  switch (event_type) {
    case API_RESOURCE_EVENT_TRANSFER_COMPLETE:
      return kEventTypeTransferComplete;
  }

  NOTREACHED();
  return std::string();
}

ApiResourceEventNotifier::~ApiResourceEventNotifier() {}

void ApiResourceEventNotifier::DispatchEvent(const std::string &extension,
                                             DictionaryValue* event) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(
          &ApiResourceEventNotifier::DispatchEventOnUIThread, this, extension,
          event));
}

void ApiResourceEventNotifier::DispatchEventOnUIThread(
    const std::string &extension, DictionaryValue* event) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  ListValue args;
  args.Set(0, event);
  router_->DispatchEventToExtension(src_extension_id_, extension, args,
                                    profile_, src_url_);
}

DictionaryValue* ApiResourceEventNotifier::CreateApiResourceEvent(
    ApiResourceEventType event_type) {
  DictionaryValue* event = new DictionaryValue();
  event->SetString(kEventTypeKey, ApiResourceEventTypeToString(event_type));
  event->SetInteger(kSrcIdKey, src_id_);

  // TODO(miket): Signal that it's OK to clean up onEvent listeners. This is
  // the framework we'll use, but we need to start using it.
  event->SetBoolean(kIsFinalEventKey, false);

  // The caller owns the created event, which typically is then given to a
  // ListValue to dispose of.
  return event;
}

void ApiResourceEventNotifier::SendEventWithResultCode(
    const std::string &extension,
    ApiResourceEventType event_type,
    int result_code) {
  if (src_id_ < 0)
    return;

  DictionaryValue* event = CreateApiResourceEvent(event_type);
  event->SetInteger(kResultCodeKey, result_code);
  DispatchEvent(extension, event);
}

}  // namespace extensions
