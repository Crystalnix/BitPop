// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/pending_extension_info.h"

namespace extensions {

PendingExtensionInfo::PendingExtensionInfo(
    const std::string& id,
    const GURL& update_url,
    const Version& version,
    ShouldAllowInstallPredicate should_allow_install,
    bool is_from_sync,
    bool install_silently,
    Extension::Location install_source)
    : id_(id),
      update_url_(update_url),
      version_(version),
      should_allow_install_(should_allow_install),
      is_from_sync_(is_from_sync),
      install_silently_(install_silently),
      install_source_(install_source) {}

PendingExtensionInfo::PendingExtensionInfo()
    : id_(""),
      update_url_(),
      should_allow_install_(NULL),
      is_from_sync_(true),
      install_silently_(false),
      install_source_(Extension::INVALID) {}

bool PendingExtensionInfo::operator==(const PendingExtensionInfo& rhs) const {
  return id_ == rhs.id_;
}

}  // namespace extensions
