// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/keyboard_overlay_delegate.h"

#include <algorithm>

#include "base/memory/scoped_ptr.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/ui/views/html_dialog_view.h"
#include "chrome/browser/ui/webui/html_dialog_ui.h"
#include "chrome/common/url_constants.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/screen.h"

using content::WebContents;
using content::WebUIMessageHandler;

static const int kBaseWidth = 1252;
static const int kBaseHeight = 516;
static const int kHorizontalMargin = 28;

KeyboardOverlayDelegate::KeyboardOverlayDelegate(
    const std::wstring& title)
    : title_(WideToUTF16Hack(title)),
      view_(NULL) {
}

KeyboardOverlayDelegate::~KeyboardOverlayDelegate() {
}

ui::ModalType KeyboardOverlayDelegate::GetDialogModalType() const {
  return ui::MODAL_TYPE_SYSTEM;
}

string16 KeyboardOverlayDelegate::GetDialogTitle() const {
  return title_;
}

GURL KeyboardOverlayDelegate::GetDialogContentURL() const {
  std::string url_string(chrome::kChromeUIKeyboardOverlayURL);
  return GURL(url_string);
}

void KeyboardOverlayDelegate::GetWebUIMessageHandlers(
    std::vector<WebUIMessageHandler*>* handlers) const {
}

void KeyboardOverlayDelegate::GetDialogSize(
    gfx::Size* size) const {
  using std::min;
  DCHECK(view_);
  gfx::Rect rect = gfx::Screen::GetMonitorAreaNearestWindow(
      view_->native_view());
  const int width = min(kBaseWidth, rect.width() - kHorizontalMargin);
  const int height = width * kBaseHeight / kBaseWidth;
  size->SetSize(width, height);
}

std::string KeyboardOverlayDelegate::GetDialogArgs() const {
  return "[]";
}

void KeyboardOverlayDelegate::OnDialogClosed(
    const std::string& json_retval) {
  // Re-enable Shift+Alt. crosbug.com/17208.
  chromeos::input_method::InputMethodManager::GetInstance()->AddHotkeys();
  delete this;
  return;
}

void KeyboardOverlayDelegate::OnCloseContents(WebContents* source,
                                              bool* out_close_dialog) {
}

bool KeyboardOverlayDelegate::ShouldShowDialogTitle() const {
  return false;
}

bool KeyboardOverlayDelegate::HandleContextMenu(
    const ContextMenuParams& params) {
  return true;
}
