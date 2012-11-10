// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PASSWORD_MANAGER_PASSWORD_MANAGER_DELEGATE_IMPL_H_
#define CHROME_BROWSER_PASSWORD_MANAGER_PASSWORD_MANAGER_DELEGATE_IMPL_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/password_manager/password_manager_delegate.h"

class TabContents;

class PasswordManagerDelegateImpl : public PasswordManagerDelegate {
 public:
  explicit PasswordManagerDelegateImpl(TabContents* contents)
      : tab_contents_(contents) {}

  // PasswordManagerDelegate implementation.
  virtual void FillPasswordForm(
      const webkit::forms::PasswordFormFillData& form_data) OVERRIDE;
  virtual void AddSavePasswordInfoBarIfPermitted(
      PasswordFormManager* form_to_save) OVERRIDE;
  virtual Profile* GetProfile() OVERRIDE;
  virtual bool DidLastPageLoadEncounterSSLErrors() OVERRIDE;

 private:
  TabContents* tab_contents_;

  DISALLOW_COPY_AND_ASSIGN(PasswordManagerDelegateImpl);
};

#endif  // CHROME_BROWSER_PASSWORD_MANAGER_PASSWORD_MANAGER_DELEGATE_IMPL_H_
