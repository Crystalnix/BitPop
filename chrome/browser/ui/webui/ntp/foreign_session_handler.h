// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_NTP_FOREIGN_SESSION_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_NTP_FOREIGN_SESSION_HANDLER_H_
#pragma once

#include <vector>

#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sync/glue/session_model_associator.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace browser_sync {

class ForeignSessionHandler : public content::WebUIMessageHandler,
                              public content::NotificationObserver {
 public:
  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  ForeignSessionHandler();
  virtual ~ForeignSessionHandler() {}

 private:
  // Used to register ForeignSessionHandler for notifications.
  void Init();

  // Determines how ForeignSessionHandler will interact with the new tab page.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Returns a pointer to the current session model associator or NULL.
  SessionModelAssociator* GetModelAssociator();

  // Determines which session is to be opened, and then calls
  // OpenForeignSession, to begin the process of opening a new browser window.
  // This is a javascript callback handler.
  void HandleOpenForeignSession(const ListValue* args);

  // Determines whether foreign sessions should be obtained from the sync model.
  // This is a javascript callback handler, and it is also called when the sync
  // model has changed and the new tab page needs to reflect the changes.
  void HandleGetForeignSessions(const ListValue* args);

  // Helper methods to create JSON compatible objects from Session objects.
  bool SessionTabToValue(const SessionTab& tab, DictionaryValue* dictionary);
  bool SessionWindowToValue(const SessionWindow& window,
                            DictionaryValue* dictionary);

  // The Registrar used to register ForeignSessionHandler for notifications.
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(ForeignSessionHandler);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_UI_WEBUI_NTP_FOREIGN_SESSION_HANDLER_H_
