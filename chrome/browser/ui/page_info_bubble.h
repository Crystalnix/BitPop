// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_PAGE_INFO_BUBBLE_H_
#define CHROME_BROWSER_UI_PAGE_INFO_BUBBLE_H_
#pragma once

#include "ui/gfx/native_widget_types.h"

class Profile;
class GURL;

namespace content {
struct SSLStatus;
}

namespace browser {

void ShowPageInfoBubble(gfx::NativeWindow parent,
                        Profile* profile,
                        const GURL& url,
                        const content::SSLStatus& ssl,
                        bool show_history);

}  // namespace browser

#endif  // CHROME_BROWSER_UI_PAGE_INFO_BUBBLE_H_
