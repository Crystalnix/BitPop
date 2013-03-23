// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/notification/notification_api.h"

#include "base/callback.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/api/api_resource_event_notifier.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_ui_manager.h"
#include "chrome/common/extensions/extension.h"
#include "googleurl/src/gurl.h"
#include "ui/notifications/notification_types.h"

const char kResultKey[] = "result";

namespace {

const char kNotificationPrefix[] = "extension.api.";

class NotificationApiDelegate : public NotificationDelegate {
 public:
  NotificationApiDelegate(extensions::ApiFunction* api_function,
                          extensions::ApiResourceEventNotifier* event_notifier)
      : api_function_(api_function),
        id_(kNotificationPrefix + base::Uint64ToString(next_id_++)) {
    DCHECK(api_function_);
  }

  virtual void Display() OVERRIDE {
    // TODO(miket): propagate to JS
  }

  virtual void Error() OVERRIDE {
    // TODO(miket): propagate to JS
  }

  virtual void Close(bool by_user) OVERRIDE {
    // TODO(miket): propagate to JS
  }

  virtual void Click() OVERRIDE {
    // TODO(miket): propagate to JS
  }

  virtual void ButtonClick(int index) OVERRIDE {
    // TODO(miket): propagate to JS
  }

  virtual std::string id() const OVERRIDE {
    return id_;
  }

  virtual content::RenderViewHost* GetRenderViewHost() const OVERRIDE {
    // We're holding a reference to api_function_, so we know it'll be valid as
    // long as we are, and api_function_ (as a UIThreadExtensionFunction)
    // listens to content::NOTIFICATION_RENDER_VIEW_HOST_DELETED and will
    // properly zero out its copy of render_view_host when the RVH goes away.
    return api_function_->render_view_host();
  }

 private:
  virtual ~NotificationApiDelegate() {}

  scoped_refptr<extensions::ApiFunction> api_function_;
  std::string id_;

  static uint64 next_id_;

  DISALLOW_COPY_AND_ASSIGN(NotificationApiDelegate);
};

uint64 NotificationApiDelegate::next_id_ = 0;

}  // namespace

namespace extensions {

NotificationShowFunction::NotificationShowFunction() {
}

NotificationShowFunction::~NotificationShowFunction() {
}

bool NotificationShowFunction::RunImpl() {
  params_ = api::experimental_notification::Show::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params_.get());

  api::experimental_notification::ShowOptions* options = &params_->options;
  scoped_ptr<DictionaryValue> options_dict(options->ToValue());
  src_id_ = ExtractSrcId(options_dict.get());
  event_notifier_ = CreateEventNotifier(src_id_);

  ui::notifications::NotificationType type =
      ui::notifications::StringToNotificationType(options->type);
  GURL icon_url(UTF8ToUTF16(options->icon_url));
  string16 title(UTF8ToUTF16(options->title));
  string16 message(UTF8ToUTF16(options->message));

  scoped_ptr<DictionaryValue> optional_fields(new DictionaryValue());

  // For all notification types.
  if (options->priority.get())
    optional_fields->SetInteger(ui::notifications::kPriorityKey,
                                *options->priority);
  if (options->timestamp.get())
    optional_fields->SetString(ui::notifications::kTimestampKey,
                               *options->timestamp);
  if (options->second_icon_url.get())
    optional_fields->SetString(ui::notifications::kSecondIconUrlKey,
                               UTF8ToUTF16(*options->second_icon_url));
  if (options->unread_count.get())
    optional_fields->SetInteger(ui::notifications::kUnreadCountKey,
                                *options->unread_count);
  if (options->button_one_title.get())
    optional_fields->SetString(ui::notifications::kButtonOneTitleKey,
                               UTF8ToUTF16(*options->button_one_title));
  if (options->button_two_title.get())
    optional_fields->SetString(ui::notifications::kButtonTwoTitleKey,
                               UTF8ToUTF16(*options->button_two_title));
  if (options->expanded_message.get())
    optional_fields->SetString(ui::notifications::kExpandedMessageKey,
                               UTF8ToUTF16(*options->expanded_message));

  // For image notifications (type == 'image').
  // TODO(dharcourt): Fail if (type == 'image' && !options->image_url.get())
  // TODO(dharcourt): Fail if (type != 'image' && options->image_url.get())
  if (options->image_url.get())
    optional_fields->SetString(ui::notifications::kImageUrlKey,
                               UTF8ToUTF16(*options->image_url));

  // For multiple-item notifications (type == 'multiple').
  // TODO(dharcourt): Fail if (type == 'multiple' && !options->items.get())
  // TODO(dharcourt): Fail if (type != 'multiple' && options->items.get())
  if (options->items.get()) {
    base::ListValue* items = new base::ListValue();
    std::vector<
        linked_ptr<
            api::experimental_notification::NotificationItem> >::iterator i;
    for (i = options->items->begin(); i != options->items->end(); ++i) {
      base::DictionaryValue* item = new base::DictionaryValue();
      item->SetString(ui::notifications::kItemTitleKey,
                      UTF8ToUTF16(i->get()->title));
      item->SetString(ui::notifications::kItemMessageKey,
                      UTF8ToUTF16(i->get()->message));
      items->Append(item);
    }
    optional_fields->Set(ui::notifications::kItemsKey, items);
  }

  string16 replace_id(UTF8ToUTF16(options->replace_id));

  Notification notification(type, icon_url, title, message,
                            WebKit::WebTextDirectionDefault,
                            string16(), replace_id,
                            optional_fields.get(),
                            new NotificationApiDelegate(this,
                                                        event_notifier_));
  g_browser_process->notification_ui_manager()->Add(notification, profile());

  // TODO(miket): why return a result if it's always true?
  DictionaryValue* result = new DictionaryValue();
  result->SetBoolean(kResultKey, true);
  SetResult(result);
  SendResponse(true);

  return true;
}

}  // namespace extensions
