// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/permissions/permissions_api_helpers.h"

#include "base/values.h"
#include "chrome/common/extensions/api/permissions.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "chrome/common/extensions/extension_permission_set.h"
#include "chrome/common/extensions/url_pattern_set.h"

namespace extensions {

using api::permissions::Permissions;

namespace permissions_api_helpers {

namespace {

const char kInvalidOrigin[] =
    "Invalid value for origin pattern *: *";
const char kUnknownPermissionError[] =
    "'*' is not a recognized permission.";

}  // namespace

scoped_ptr<Permissions> PackPermissionSet(const ExtensionPermissionSet* set) {
  Permissions* permissions(new Permissions());

  permissions->permissions.reset(new std::vector<std::string>());
  ExtensionPermissionsInfo* info = ExtensionPermissionsInfo::GetInstance();
  for (ExtensionAPIPermissionSet::const_iterator i = set->apis().begin();
       i != set->apis().end(); ++i) {
    permissions->permissions->push_back(info->GetByID(*i)->name());
  }

  permissions->origins.reset(new std::vector<std::string>());
  URLPatternSet hosts = set->explicit_hosts();
  for (URLPatternSet::const_iterator i = hosts.begin(); i != hosts.end(); ++i)
    permissions->origins->push_back(i->GetAsString());

  return scoped_ptr<Permissions>(permissions);
}

scoped_refptr<ExtensionPermissionSet> UnpackPermissionSet(
    const Permissions& permissions, std::string* error) {
  ExtensionAPIPermissionSet apis;
  std::vector<std::string>* permissions_list = permissions.permissions.get();
  if (permissions_list) {
    ExtensionPermissionsInfo* info = ExtensionPermissionsInfo::GetInstance();
    for (std::vector<std::string>::iterator it = permissions_list->begin();
        it != permissions_list->end(); ++it) {
      ExtensionAPIPermission* permission = info->GetByName(*it);
      if (!permission) {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            kUnknownPermissionError, *it);
        return NULL;
      }
      apis.insert(permission->id());
    }
  }

  URLPatternSet origins;
  if (permissions.origins.get()) {
    for (std::vector<std::string>::iterator it = permissions.origins->begin();
        it != permissions.origins->end(); ++it) {
      URLPattern origin(Extension::kValidHostPermissionSchemes);
      URLPattern::ParseResult parse_result = origin.Parse(*it);
      if (URLPattern::PARSE_SUCCESS != parse_result) {
        *error = ExtensionErrorUtils::FormatErrorMessage(
            kInvalidOrigin,
            *it,
            URLPattern::GetParseResultString(parse_result));
        return NULL;
      }
      origins.AddPattern(origin);
    }
  }

  return scoped_refptr<ExtensionPermissionSet>(
      new ExtensionPermissionSet(apis, origins, URLPatternSet()));
}

}  // namespace permissions_api
}  // namespace extensions
