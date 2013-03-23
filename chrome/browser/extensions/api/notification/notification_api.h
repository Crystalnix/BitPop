// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_NOTIFICATION_NOTIFICATION_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_NOTIFICATION_NOTIFICATION_API_H_

#include "base/memory/ref_counted.h"
#include "chrome/browser/extensions/api/api_function.h"
#include "chrome/browser/extensions/api/api_resource_manager.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/common/extensions/api/experimental_notification.h"

#include <string>

namespace extensions {

class ApiResourceEventNotifier;

class NotificationShowFunction : public ApiFunction {
 public:
  NotificationShowFunction();

  // UIThreadExtensionFunction:
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~NotificationShowFunction();

 private:
  scoped_ptr<api::experimental_notification::Show::Params> params_;
  int src_id_;
  ApiResourceEventNotifier* event_notifier_;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.notification.show")
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_NOTIFICATION_NOTIFICATION_API_H_
