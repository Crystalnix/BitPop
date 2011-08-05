// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/extensions/extension_dialog.h"

#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/window.h"  // CreateViewsWindow
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/common/notification_details.h"
#include "content/common/notification_source.h"
#include "content/common/notification_type.h"
#include "googleurl/src/gurl.h"
#include "views/window/window.h"

ExtensionDialog::ExtensionDialog(Browser* browser, ExtensionHost* host,
                                 int width, int height,
                                 Observer* observer)
    : extension_host_(host),
      observer_(observer) {
  AddRef();  // Balanced in DeleteDelegate();
  gfx::NativeWindow parent = browser->window()->GetNativeHandle();
  window_ = browser::CreateViewsWindow(
      parent, gfx::Rect(), this /* views::WindowDelegate */);

  // Center the window over the browser.
  gfx::Point center = browser->window()->GetBounds().CenterPoint();
  int x = center.x() - width / 2;
  int y = center.y() - height / 2;
  window_->SetBounds(gfx::Rect(x, y, width, height));

  host->view()->SetContainer(this /* ExtensionView::Container */);

  // Listen for the containing view calling window.close();
  registrar_.Add(this, NotificationType::EXTENSION_HOST_VIEW_SHOULD_CLOSE,
                 Source<Profile>(host->profile()));

  window_->Show();
  window_->Activate();
  // Ensure the DOM JavaScript can respond immediately to keyboard shortcuts.
  host->render_view_host()->view()->Focus();
}

ExtensionDialog::~ExtensionDialog() {
}

// static
ExtensionDialog* ExtensionDialog::Show(
    const GURL& url,
    Browser* browser,
    int width,
    int height,
    Observer* observer) {
  CHECK(browser);
  ExtensionProcessManager* manager =
      browser->profile()->GetExtensionProcessManager();
  DCHECK(manager);
  if (!manager)
    return NULL;
  ExtensionHost* host = manager->CreateDialogHost(url, browser);
  return new ExtensionDialog(browser, host, width, height, observer);
}

void ExtensionDialog::ObserverDestroyed() {
  observer_ = NULL;
}

void ExtensionDialog::Close() {
  if (!window_)
    return;

  if (observer_)
    observer_->ExtensionDialogIsClosing(this);

  window_->Close();
  window_ = NULL;
}

/////////////////////////////////////////////////////////////////////////////
// views::WindowDelegate overrides.

bool ExtensionDialog::CanResize() const {
  return false;
}

bool ExtensionDialog::IsModal() const {
  return true;
}

bool ExtensionDialog::ShouldShowWindowTitle() const {
  return false;
}

void ExtensionDialog::DeleteDelegate() {
  // The window has finished closing.  Allow ourself to be deleted.
  Release();
}

views::View* ExtensionDialog::GetContentsView() {
  return extension_host_->view();
}

/////////////////////////////////////////////////////////////////////////////
// ExtensionView::Container overrides.

void ExtensionDialog::OnExtensionMouseMove(ExtensionView* view) {
}

void ExtensionDialog::OnExtensionMouseLeave(ExtensionView* view) {
}

void ExtensionDialog::OnExtensionPreferredSizeChanged(ExtensionView* view) {
}

/////////////////////////////////////////////////////////////////////////////
// NotificationObserver overrides.

void ExtensionDialog::Observe(NotificationType type,
                             const NotificationSource& source,
                             const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::EXTENSION_HOST_VIEW_SHOULD_CLOSE:
      // If we aren't the host of the popup, then disregard the notification.
      if (Details<ExtensionHost>(host()) != details)
        return;
      Close();
      break;
    default:
      NOTREACHED() << L"Received unexpected notification";
      break;
  }
}
