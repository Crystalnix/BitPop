// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS2_PASSWORD_MANAGER_HANDLER2_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS2_PASSWORD_MANAGER_HANDLER2_H_

#include <string>
#include <vector>

#include "base/memory/scoped_vector.h"
#include "chrome/browser/password_manager/password_store.h"
#include "chrome/browser/password_manager/password_store_consumer.h"
#include "chrome/browser/prefs/pref_member.h"
#include "chrome/browser/ui/webui/options2/options_ui2.h"

namespace webkit {
namespace forms {
struct PasswordForm;
}
}

namespace options2 {

class PasswordManagerHandler : public OptionsPageUIHandler,
                               public PasswordStore::Observer {
 public:
  PasswordManagerHandler();
  virtual ~PasswordManagerHandler();

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(DictionaryValue* localized_strings) OVERRIDE;
  virtual void Initialize() OVERRIDE;
  virtual void RegisterMessages() OVERRIDE;

  // PasswordStore::Observer implementation.
  virtual void OnLoginsChanged() OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  // The password store associated with the currently active profile.
  PasswordStore* GetPasswordStore();

  // Called when the JS PasswordManager object is initialized.
  void UpdatePasswordLists(const ListValue* args);

  // Remove an entry.
  // @param value the entry index to be removed.
  void RemoveSavedPassword(const ListValue* args);

  // Remove an password exception.
  // @param value the entry index to be removed.
  void RemovePasswordException(const ListValue* args);

  // Remove all saved passwords
  void RemoveAllSavedPasswords(const ListValue* args);

  // Remove All password exceptions
  void RemoveAllPasswordExceptions(const ListValue* args);

  // Get password value for the selected entry.
  // @param value the selected entry index.
  void ShowSelectedPassword(const ListValue* args);

  // Sets the password and exception list contents to the given data.
  // We take ownership of the PasswordForms in the vector.
  void SetPasswordList();
  void SetPasswordExceptionList();

  // A short class to mediate requests to the password store.
  class ListPopulater : public PasswordStoreConsumer {
   public:
    explicit ListPopulater(PasswordManagerHandler* page);
    virtual ~ListPopulater();

    // Send a query to the password store to populate a list.
    virtual void Populate() = 0;

    // Send the password store's reply back to the handler.
    virtual void OnPasswordStoreRequestDone(
        CancelableRequestProvider::Handle handle,
        const std::vector<webkit::forms::PasswordForm*>& result) = 0;

   protected:
    PasswordManagerHandler* page_;
    CancelableRequestProvider::Handle pending_login_query_;
  };

  // A short class to mediate requests to the password store for passwordlist.
  class PasswordListPopulater : public ListPopulater {
   public:
    explicit PasswordListPopulater(PasswordManagerHandler* page);

    // Send a query to the password store to populate a password list.
    virtual void Populate() OVERRIDE;

    // Send the password store's reply back to the handler.
    virtual void OnPasswordStoreRequestDone(
        CancelableRequestProvider::Handle handle,
        const std::vector<webkit::forms::PasswordForm*>& result) OVERRIDE;
  };

  // A short class to mediate requests to the password store for exceptions.
  class PasswordExceptionListPopulater : public ListPopulater {
   public:
    explicit PasswordExceptionListPopulater(PasswordManagerHandler* page);

    // Send a query to the password store to populate a passwordException list.
    virtual void Populate() OVERRIDE;

    // Send the password store's reply back to the handler.
    virtual void OnPasswordStoreRequestDone(
        CancelableRequestProvider::Handle handle,
        const std::vector<webkit::forms::PasswordForm*>& result) OVERRIDE;
  };

  // Password store consumer for populating the password list and exceptions.
  PasswordListPopulater populater_;
  PasswordExceptionListPopulater exception_populater_;

  ScopedVector<webkit::forms::PasswordForm> password_list_;
  ScopedVector<webkit::forms::PasswordForm> password_exception_list_;

  // User's pref
  std::string languages_;

  // Whether to show stored passwords or not.
  BooleanPrefMember show_passwords_;

  DISALLOW_COPY_AND_ASSIGN(PasswordManagerHandler);
};

}  // namespace options2

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS2_PASSWORD_MANAGER_HANDLER2_H_
