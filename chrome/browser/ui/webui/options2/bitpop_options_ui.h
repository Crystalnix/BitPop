// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS2_BITPOP_OPTIONS_UI_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS2_BITPOP_OPTIONS_UI_H_

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"

class AutocompleteResult;

namespace base {
class DictionaryValue;
class ListValue;
}

namespace options2 {

// The base class handler of Javascript messages of options pages.
class BitpopOptionsPageUIHandler : public content::WebUIMessageHandler,
                             public content::NotificationObserver {
 public:
  BitpopOptionsPageUIHandler();
  virtual ~BitpopOptionsPageUIHandler();

  // Is this handler enabled?
  virtual bool IsEnabled();

  // Collects localized strings for options page.
  virtual void GetLocalizedValues(base::DictionaryValue* localized_strings) = 0;

  // Will be called only once in the life time of the handler. Generally used to
  // add observers, initializes preferences, or start asynchronous calls from
  // various services.
  virtual void InitializeHandler() {}

  // Initialize the page. Called once the DOM is available for manipulation.
  // This will be called when a RenderView is re-used (when navigated to with
  // back/forward or session restored in some cases) or when created.
  virtual void InitializePage() {}

  // Uninitializes the page.  Called just before the object is destructed.
  virtual void Uninitialize() {}

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE {}

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {}

 protected:
  struct OptionsStringResource {
    // The name of the resource in templateData.
    const char* name;
    // The .grd ID for the resource (IDS_*).
    int id;
  };
  // A helper for simplifying the process of registering strings in WebUI.
  static void RegisterStrings(base::DictionaryValue* localized_strings,
                              const OptionsStringResource* resources,
                              size_t length);

  // Registers string resources for a page's header and tab title.
  static void RegisterTitle(base::DictionaryValue* localized_strings,
                            const std::string& variable_name,
                            int title_id);

  content::NotificationRegistrar registrar_;

 private:
  DISALLOW_COPY_AND_ASSIGN(BitpopOptionsPageUIHandler);
};

// An interface for common operations that a host of OptionsPageUIHandlers
// should provide.
class BitpopOptionsPageUIHandlerHost {
 public:
  virtual void InitializeHandlers() = 0;

 protected:
  virtual ~BitpopOptionsPageUIHandlerHost() {}
};

// The WebUI for chrome:settings-frame.
class BitpopOptionsUI : public content::WebUIController,
               			public OptionsPageUIHandlerHost {
 public:
  explicit BitpopOptionsUI(content::WebUI* web_ui);
  virtual ~BitpopOptionsUI();

  // Takes the suggestions from |result| and adds them to |suggestions| so that
  // they can be passed to a JavaScript function.
  static void ProcessAutocompleteSuggestions(
      const AutocompleteResult& result,
      base::ListValue* const suggestions);

  static base::RefCountedMemory* GetFaviconResourceBytes();

  // Overridden from OptionsPageUIHandlerHost:
  virtual void InitializeHandlers() OVERRIDE;

 private:
  // Adds OptionsPageUiHandler to the handlers list if handler is enabled.
  void AddOptionsPageUIHandler(base::DictionaryValue* localized_strings,
                               BitpopOptionsPageUIHandler* handler);

  bool initialized_handlers_;

  std::vector<BitpopOptionsPageUIHandler*> handlers_;

  DISALLOW_COPY_AND_ASSIGN(BitpopOptionsUI);
};

}  // namespace options2

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS2_BITPOP_OPTIONS_UI_H_
