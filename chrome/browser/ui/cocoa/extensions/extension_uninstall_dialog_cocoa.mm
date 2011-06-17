// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include <string>

#include "base/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_uninstall_dialog.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "skia/ext/skia_utils_mac.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/resource/resource_bundle.h"

// static
void ExtensionUninstallDialog::Show(
    Profile* profile,
    ExtensionUninstallDialog::Delegate* delegate,
    const Extension* extension,
    SkBitmap* icon) {
  NSAlert* alert = [[[NSAlert alloc] init] autorelease];

  NSButton* continueButton = [alert addButtonWithTitle:l10n_util::GetNSString(
      IDS_EXTENSION_PROMPT_UNINSTALL_BUTTON)];
  // Clear the key equivalent (currently 'Return') because cancel is the default
  // button.
  [continueButton setKeyEquivalent:@""];

  NSButton* cancelButton = [alert addButtonWithTitle:l10n_util::GetNSString(
      IDS_CANCEL)];
  [cancelButton setKeyEquivalent:@"\r"];

  [alert setMessageText:l10n_util::GetNSStringF(
       IDS_EXTENSION_UNINSTALL_PROMPT_HEADING,
       UTF8ToUTF16(extension->name()))];
  [alert setAlertStyle:NSWarningAlertStyle];
  [alert setIcon:gfx::SkBitmapToNSImage(*icon)];

  if ([alert runModal] == NSAlertFirstButtonReturn)
    delegate->ExtensionDialogAccepted();
  else
    delegate->ExtensionDialogCanceled();
}
