// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_POPUP_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_POPUP_CONTROLLER_H_

#pragma once

#import "chrome/browser/ui/cocoa/extensions/extension_popup_controller.h"

@interface FacebookPopupController : ExtensionPopupController {
}

+ (FacebookPopupController*)showURL:(GURL)url inBrowser:(Browser*)browser
    anchoredAt:(NSPoint)anchoredAt
    arrowLocation:(info_bubble::BubbleArrowLocation)arrowLocation
    devMode:(BOOL)devMode;

+ (FacebookPopupController*)popup;

@end

#endif   // CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_POPUP_CONTROLLER_H_
