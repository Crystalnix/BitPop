// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/extension_process_policy.h"

#include "chrome/common/extensions/extension_set.h"

namespace extensions {

const Extension* GetNonBookmarkAppExtension(
    const ExtensionSet& extensions, const ExtensionURLInfo& url) {
  // Exclude bookmark apps, which do not use the app process model.
  const Extension* extension = extensions.GetExtensionOrAppByURL(url);
  if (extension && extension->from_bookmark())
    extension = NULL;
  return extension;
}

bool CrossesExtensionProcessBoundary(
    const ExtensionSet& extensions,
    const ExtensionURLInfo& old_url,
    const ExtensionURLInfo& new_url) {
  const Extension* old_url_extension = GetNonBookmarkAppExtension(extensions,
                                                                  old_url);
  const Extension* new_url_extension = GetNonBookmarkAppExtension(extensions,
                                                                  new_url);

  // TODO(creis): Temporary workaround for crbug.com/59285: Do not swap process
  // to navigate from a hosted app to a normal page or another hosted app
  // (unless either is the web store).  This is because we do not yet support
  // postMessage calls from outside the app back into it (e.g., as in Facebook
  // OAuth 2.0).  This will be removed when http://crbug.com/99202 is fixed.
  bool old_url_is_hosted_app = old_url_extension &&
      !old_url_extension->web_extent().is_empty();
  bool new_url_is_normal_or_hosted = !new_url_extension ||
      !new_url_extension->web_extent().is_empty();
  bool either_is_web_store =
      (old_url_extension &&
       old_url_extension->id() == extension_misc::kWebStoreAppId) ||
      (new_url_extension &&
       new_url_extension->id() == extension_misc::kWebStoreAppId);
  if (old_url_is_hosted_app &&
      new_url_is_normal_or_hosted &&
      !either_is_web_store)
    return false;

  return old_url_extension != new_url_extension;
}

}  // namespace extensions
