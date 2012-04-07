// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/permissions/permissions_api.h"

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/extensions/api/permissions/permissions_api_helpers.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/permissions_updater.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/api/permissions.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "chrome/common/extensions/url_pattern_set.h"
#include "googleurl/src/gurl.h"

using extensions::PermissionsUpdater;
using extensions::permissions_api_helpers::PackPermissionSet;
using extensions::permissions_api_helpers::UnpackPermissionSet;
using namespace extensions::api::permissions;

namespace {

const char kCantRemoveRequiredPermissionsError[] =
    "You cannot remove required permissions.";
const char kNotInOptionalPermissionsError[] =
    "Optional permissions must be listed in extension manifest.";
const char kNotWhitelistedError[] =
    "The optional permissions API does not support '*'.";
const char kUserGestureRequiredError[] =
    "This function must be called during a user gesture";

enum AutoConfirmForTest {
  DO_NOT_SKIP = 0,
  PROCEED,
  ABORT
};
AutoConfirmForTest auto_confirm_for_tests = DO_NOT_SKIP;
bool ignore_user_gesture_for_tests = false;

} // namespace

bool ContainsPermissionsFunction::RunImpl() {
  scoped_ptr<Contains::Params> params(Contains::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  scoped_refptr<ExtensionPermissionSet> permissions =
      UnpackPermissionSet(params->permissions, &error_);
  if (!permissions.get())
    return false;

  result_.reset(Contains::Result::Create(
      GetExtension()->GetActivePermissions()->Contains(*permissions)));
  return true;
}

bool GetAllPermissionsFunction::RunImpl() {
  scoped_ptr<Permissions> permissions =
      PackPermissionSet(GetExtension()->GetActivePermissions());
  result_.reset(GetAll::Result::Create(*permissions));
  return true;
}

bool RemovePermissionsFunction::RunImpl() {
  scoped_ptr<Remove::Params> params(Remove::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  scoped_refptr<ExtensionPermissionSet> permissions =
      UnpackPermissionSet(params->permissions, &error_);
  if (!permissions.get())
    return false;

  const Extension* extension = GetExtension();
  ExtensionPermissionsInfo* info = ExtensionPermissionsInfo::GetInstance();

  // Make sure they're only trying to remove permissions supported by this API.
  ExtensionAPIPermissionSet apis = permissions->apis();
  for (ExtensionAPIPermissionSet::const_iterator i = apis.begin();
       i != apis.end(); ++i) {
    const ExtensionAPIPermission* api = info->GetByID(*i);
    if (!api->supports_optional()) {
      error_ = ExtensionErrorUtils::FormatErrorMessage(
          kNotWhitelistedError, api->name());
      return false;
    }
  }

  // Make sure we don't remove any required pemissions.
  const ExtensionPermissionSet* required = extension->required_permission_set();
  scoped_refptr<ExtensionPermissionSet> intersection(
      ExtensionPermissionSet::CreateIntersection(permissions.get(), required));
  if (!intersection->IsEmpty()) {
    error_ = kCantRemoveRequiredPermissionsError;
    result_.reset(Remove::Result::Create(false));
    return false;
  }

  PermissionsUpdater(profile()).RemovePermissions(extension, permissions.get());
  result_.reset(Remove::Result::Create(true));
  return true;
}

// static
void RequestPermissionsFunction::SetAutoConfirmForTests(bool should_proceed) {
  auto_confirm_for_tests = should_proceed ? PROCEED : ABORT;
}

// static
void RequestPermissionsFunction::SetIgnoreUserGestureForTests(
    bool ignore) {
  ignore_user_gesture_for_tests = ignore;
}

RequestPermissionsFunction::RequestPermissionsFunction() {}
RequestPermissionsFunction::~RequestPermissionsFunction() {}

bool RequestPermissionsFunction::RunImpl() {
  if (!user_gesture() && !ignore_user_gesture_for_tests) {
    error_ = kUserGestureRequiredError;
    return false;
  }

  scoped_ptr<Request::Params> params(Request::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  requested_permissions_ = UnpackPermissionSet(params->permissions, &error_);
  if (!requested_permissions_.get())
    return false;

  ExtensionPermissionsInfo* info = ExtensionPermissionsInfo::GetInstance();
  ExtensionPrefs* prefs = profile()->GetExtensionService()->extension_prefs();

  // Make sure they're only requesting permissions supported by this API.
  ExtensionAPIPermissionSet apis = requested_permissions_->apis();
  for (ExtensionAPIPermissionSet::const_iterator i = apis.begin();
       i != apis.end(); ++i) {
    const ExtensionAPIPermission* api = info->GetByID(*i);
    if (!api->supports_optional()) {
      error_ = ExtensionErrorUtils::FormatErrorMessage(
          kNotWhitelistedError, api->name());
      return false;
    }
  }

  // The requested permissions must be defined as optional in the manifest.
  if (!GetExtension()->optional_permission_set()->Contains(
          *requested_permissions_)) {
    error_ = kNotInOptionalPermissionsError;
    result_.reset(Request::Result::Create(false));
    return false;
  }

  // We don't need to prompt the user if the requested permissions are a subset
  // of the granted permissions set.
  const ExtensionPermissionSet* granted =
      prefs->GetGrantedPermissions(GetExtension()->id());
  if (granted && granted->Contains(*requested_permissions_)) {
    PermissionsUpdater perms_updater(profile());
    perms_updater.AddPermissions(GetExtension(), requested_permissions_.get());
    result_.reset(Request::Result::Create(true));
    SendResponse(true);
    return true;
  }

  // Filter out the granted permissions so we only prompt for new ones.
  requested_permissions_ = ExtensionPermissionSet::CreateDifference(
      requested_permissions_.get(), granted);

  AddRef();  // Balanced in InstallUIProceed() / InstallUIAbort().

  // We don't need to show the prompt if there are no new warnings, or if
  // we're skipping the confirmation UI. All extension types but INTERNAL
  // are allowed to silently increase their permission level.
  if (auto_confirm_for_tests == PROCEED ||
      requested_permissions_->GetWarningMessages().size() == 0) {
    InstallUIProceed();
  } else if (auto_confirm_for_tests == ABORT) {
    // Pretend the user clicked cancel.
    InstallUIAbort(true);
  } else {
    CHECK_EQ(DO_NOT_SKIP, auto_confirm_for_tests);
    install_ui_.reset(new ExtensionInstallUI(profile()));
    install_ui_->ConfirmPermissions(
        this, GetExtension(), requested_permissions_.get());
  }

  return true;
}

void RequestPermissionsFunction::InstallUIProceed() {
  PermissionsUpdater perms_updater(profile());
  perms_updater.AddPermissions(GetExtension(), requested_permissions_.get());

  result_.reset(Request::Result::Create(true));
  SendResponse(true);

  Release();  // Balanced in RunImpl().
}

void RequestPermissionsFunction::InstallUIAbort(bool user_initiated) {
  result_.reset(Request::Result::Create(false));
  SendResponse(true);

  Release();  // Balanced in RunImpl().
}
