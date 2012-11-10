// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_CONTEXT_MENU_CONTEXT_MENU_API_H__
#define CHROME_BROWSER_EXTENSIONS_API_CONTEXT_MENU_CONTEXT_MENU_API_H__

#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/extensions/menu_manager.h"
#include "chrome/common/extensions/url_pattern_set.h"

class MenuItem;

namespace base {
class DictionaryValue;
}

namespace extensions {

class ExtensionContextMenuFunction : public SyncExtensionFunction {
 protected:
  virtual ~ExtensionContextMenuFunction() {}

  // Helper function to read and parse a list of menu item contexts.
  bool ParseContexts(const base::DictionaryValue& properties,
                     const char* key,
                     MenuItem::ContextList* result);

  // Looks in properties for the "type" key, and reads the value in |result|. On
  // error, returns false and puts an error message into error_. If the key is
  // not present, |result| is set to |default_value| and the return value is
  // true.
  bool ParseType(const base::DictionaryValue& properties,
                 const MenuItem::Type& default_value,
                 MenuItem::Type* result);

  // Helper to read and parse the "checked" property.
  bool ParseChecked(MenuItem::Type type,
                    const base::DictionaryValue& properties,
                    bool default_value,
                    bool* checked);

  // Helper to read an ID from the Value*. The ID can be either a string or
  // integer.
  bool ParseID(const Value* value, MenuItem::Id* result);

  // If the parentId key was specified in properties, this will try looking up
  // an MenuItem with that id and set it into |result|. Returns false
  // on error, with an explanation written into error_. Note that if the
  // parentId key is not in properties, this will return true and leave |result|
  // unset. Also, it is considered an error if the item found has a type other
  // than NORMAL.
  bool GetParent(const base::DictionaryValue& properties,
                 const MenuManager& manager,
                 MenuItem** result);
};

class CreateContextMenuFunction : public ExtensionContextMenuFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("contextMenus.create")

 protected:
  virtual ~CreateContextMenuFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class UpdateContextMenuFunction : public ExtensionContextMenuFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("contextMenus.update")

 protected:
  virtual ~UpdateContextMenuFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class RemoveContextMenuFunction : public ExtensionContextMenuFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("contextMenus.remove")

 protected:
  virtual ~RemoveContextMenuFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class RemoveAllContextMenusFunction : public ExtensionContextMenuFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("contextMenus.removeAll")

 protected:
  virtual ~RemoveAllContextMenusFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_CONTEXT_MENU_CONTEXT_MENU_API_H__
