// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_APP_WINDOW_APP_WINDOW_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_APP_WINDOW_APP_WINDOW_API_H_

#include "chrome/browser/extensions/extension_function.h"

class ShellWindow;

namespace extensions {

class AppWindowExtensionFunction : public SyncExtensionFunction {
 protected:
  virtual ~AppWindowExtensionFunction() {}

  // Invoked with the current shell window.
  virtual bool RunWithWindow(ShellWindow* window) = 0;

 private:
  virtual bool RunImpl() OVERRIDE;
};

class AppWindowCreateFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("app.window.create");

 protected:
  virtual ~AppWindowCreateFunction() {}
  virtual bool RunImpl() OVERRIDE;
};

class AppWindowFocusFunction : public AppWindowExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("app.window.focus");

 protected:
  virtual ~AppWindowFocusFunction() {}
  virtual bool RunWithWindow(ShellWindow* window) OVERRIDE;
};

class AppWindowMaximizeFunction : public AppWindowExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("app.window.maximize");

 protected:
  virtual ~AppWindowMaximizeFunction() {}
  virtual bool RunWithWindow(ShellWindow* window) OVERRIDE;
};

class AppWindowMinimizeFunction : public AppWindowExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("app.window.minimize");

 protected:
  virtual ~AppWindowMinimizeFunction() {}
  virtual bool RunWithWindow(ShellWindow* window) OVERRIDE;
};

class AppWindowRestoreFunction : public AppWindowExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("app.window.restore");

 protected:
  virtual ~AppWindowRestoreFunction() {}
  virtual bool RunWithWindow(ShellWindow* window) OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_APP_WINDOW_APP_WINDOW_API_H_
