// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_OMNIBOX_OMNIBOX_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_OMNIBOX_OMNIBOX_API_H_

#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/string16.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/extensions/extension_function.h"

class TabContents;
class TemplateURL;
namespace base {
class ListValue;
}

namespace extensions {

// Event router class for events related to the omnibox API.
class ExtensionOmniboxEventRouter {
 public:
  // The user has just typed the omnibox keyword. This is sent exactly once in
  // a given input session, before any OnInputChanged events.
  static void OnInputStarted(
      Profile* profile, const std::string& extension_id);

  // The user has changed what is typed into the omnibox while in an extension
  // keyword session. Returns true if someone is listening to this event, and
  // thus we have some degree of confidence we'll get a response.
  static bool OnInputChanged(
      Profile* profile,
      const std::string& extension_id,
      const std::string& input, int suggest_id);

  // The user has accepted the omnibox input.
  static void OnInputEntered(
      TabContents* tab_contents,
      const std::string& extension_id,
      const std::string& input);

  // The user has cleared the keyword, or closed the omnibox popup. This is
  // sent at most once in a give input session, after any OnInputChanged events.
  static void OnInputCancelled(
      Profile* profile, const std::string& extension_id);

 private:
  DISALLOW_COPY_AND_ASSIGN(ExtensionOmniboxEventRouter);
};

class OmniboxSendSuggestionsFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("omnibox.sendSuggestions");

 protected:
  virtual ~OmniboxSendSuggestionsFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class OmniboxSetDefaultSuggestionFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("omnibox.setDefaultSuggestion");

 protected:
  virtual ~OmniboxSetDefaultSuggestionFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

struct ExtensionOmniboxSuggestion {
  ExtensionOmniboxSuggestion();
  ~ExtensionOmniboxSuggestion();

  // Populate a suggestion value from a DictionaryValue. If |require_content|
  // is false, then we won't fail if |content| is missing, to support
  // default suggestions.
  bool Populate(const base::DictionaryValue& value, bool require_content);

  // Converts a list of style ranges from the extension into the format expected
  // by the autocomplete system.
  bool ReadStylesFromValue(const base::ListValue& value);

  // Converts this structure to a DictionaryValue suitable for saving to disk.
  scoped_ptr<base::DictionaryValue> ToValue() const;

  // The text that gets put in the edit box.
  string16 content;

  // The text that is displayed in the drop down.
  string16 description;

  // Contains style ranges for the description.
  ACMatchClassifications description_styles;
};

struct ExtensionOmniboxSuggestions {
  ExtensionOmniboxSuggestions();
  ~ExtensionOmniboxSuggestions();

  int request_id;
  std::vector<ExtensionOmniboxSuggestion> suggestions;

 private:
  // This class is passed around by pointer.
  DISALLOW_COPY_AND_ASSIGN(ExtensionOmniboxSuggestions);
};

// If the extension has set a custom default suggestion via
// omnibox.setDefaultSuggestion, apply that to |match|. Otherwise, do nothing.
void ApplyDefaultSuggestionForExtensionKeyword(
    Profile* profile,
    const TemplateURL* keyword,
    const string16& remaining_input,
    AutocompleteMatch* match);

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_OMNIBOX_OMNIBOX_API_H_
