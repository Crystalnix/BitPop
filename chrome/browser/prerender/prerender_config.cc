// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prerender/prerender_config.h"

namespace prerender {

Config::Config() : max_bytes(100 * 1024 * 1024),
                   max_elements(1),
                   rate_limit_enabled(true),
                   max_age(base::TimeDelta::FromSeconds(30)),
                   https_allowed(true) {
}

}  // namespace prerender
