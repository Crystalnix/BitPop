// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_EXTENSIONS_EXTENSION_DIALOG_H_
#define CHROME_BROWSER_UI_VIEWS_EXTENSIONS_EXTENSION_DIALOG_H_
#pragma once

#include "base/memory/ref_counted.h"
#include "chrome/browser/ui/views/extensions/extension_view.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "views/window/window_delegate.h"

class Browser;
class ExtensionHost;
class GURL;
class Profile;

namespace views {
class View;
class Widget;
}

// Modal dialog containing contents provided by an extension.
// Dialog is automatically centered in the browser window and has fixed size.
// For example, used by the Chrome OS file browser.
class ExtensionDialog : public views::WindowDelegate,
                        public ExtensionView::Container,
                        public NotificationObserver,
                        public base::RefCounted<ExtensionDialog> {
 public:
  // Observer to ExtensionDialog events.
  class Observer {
   public:
    // Called when the ExtensionDialog is closing. Note that it
    // is ref-counted, and thus will be released shortly after
    // making this delegate call.
    virtual void ExtensionDialogIsClosing(ExtensionDialog* popup) = 0;
  };

  virtual ~ExtensionDialog();

  // Create and show a dialog with |url| centered over the browser window.
  // |browser| is the browser to which the pop-up will be attached.
  // |width| and |height| are the size of the dialog in pixels.
  static ExtensionDialog* Show(const GURL& url, Browser* browser,
                               int width,
                               int height,
                               Observer* observer);

  // Notifies the dialog that the observer has been destroyed and should not
  // be sent notifications.
  void ObserverDestroyed();

  // Closes the ExtensionDialog.
  void Close();

  ExtensionHost* host() const { return extension_host_.get(); }

  // views::WindowDelegate overrides.
  virtual bool CanResize() const OVERRIDE;
  virtual bool IsModal() const OVERRIDE;
  virtual bool ShouldShowWindowTitle() const OVERRIDE;
  virtual void DeleteDelegate() OVERRIDE;
  virtual views::View* GetContentsView() OVERRIDE;

  // ExtensionView::Container overrides.
  virtual void OnExtensionMouseMove(ExtensionView* view);
  virtual void OnExtensionMouseLeave(ExtensionView* view);
  virtual void OnExtensionPreferredSizeChanged(ExtensionView* view);

  // NotificationObserver overrides.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  // Use Show() to create instances.
  ExtensionDialog(Browser* browser, ExtensionHost* host, int width, int height,
                  Observer* observer);

  // Window that holds the extension host view.
  views::Widget* window_;

  // The contained host for the view.
  scoped_ptr<ExtensionHost> extension_host_;

  NotificationRegistrar registrar_;

  // The observer of this popup.
  Observer* observer_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionDialog);
};

#endif  // CHROME_BROWSER_UI_VIEWS_EXTENSIONS_EXTENSION_DIALOG_H_
