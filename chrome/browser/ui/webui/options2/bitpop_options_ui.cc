// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/options_ui.h"

#include <algorithm>
#include <vector>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/singleton.h"
#include "base/message_loop.h"
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/threading/thread.h"
#include "base/time.h"
#include "base/values.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_result.h"
#include "chrome/browser/browser_about_handler.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/options2/autofill_options_handler.h"
#include "chrome/browser/ui/webui/options2/browser_options_handler.h"
#include "chrome/browser/ui/webui/options2/clear_browser_data_handler.h"
#include "chrome/browser/ui/webui/options2/content_settings_handler.h"
#include "chrome/browser/ui/webui/options2/cookies_view_handler.h"
#include "chrome/browser/ui/webui/options2/core_options_handler.h"
#include "chrome/browser/ui/webui/options2/font_settings_handler.h"
#include "chrome/browser/ui/webui/options2/handler_options_handler.h"
#include "chrome/browser/ui/webui/options2/home_page_overlay_handler.h"
#include "chrome/browser/ui/webui/options2/import_data_handler.h"
#include "chrome/browser/ui/webui/options2/language_options_handler.h"
#include "chrome/browser/ui/webui/options2/manage_profile_handler.h"
#include "chrome/browser/ui/webui/options2/media_galleries_handler.h"
#include "chrome/browser/ui/webui/options2/options_sync_setup_handler.h"
#include "chrome/browser/ui/webui/options2/password_manager_handler.h"
#include "chrome/browser/ui/webui/options2/search_engine_manager_handler.h"
#include "chrome/browser/ui/webui/options2/startup_pages_handler.h"
#include "chrome/browser/ui/webui/options2/web_intents_settings_handler.h"
#include "chrome/browser/ui/webui/theme_source.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/time_format.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_ui.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/options2_resources.h"
#include "grit/theme_resources.h"
#include "net/base/escape.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"

using content::RenderViewHost;

namespace {

const char kLocalizedStringsFile[] = "strings.js";
const char kOptionsBundleJsFile[]  = "options_bundle.js";

}  // namespace

namespace options2 {

////////////////////////////////////////////////////////////////////////////////
//
// BitpopOptionsUIHTMLSource
//
////////////////////////////////////////////////////////////////////////////////

class BitpopOptionsUIHTMLSource : public ChromeURLDataManager::DataSource {
 public:
  // The constructor takes over ownership of |localized_strings|.
  explicit BitpopOptionsUIHTMLSource(DictionaryValue* localized_strings);

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id);
  virtual std::string GetMimeType(const std::string&) const;

 private:
  virtual ~BitpopOptionsUIHTMLSource();

  // Localized strings collection.
  scoped_ptr<DictionaryValue> localized_strings_;

  DISALLOW_COPY_AND_ASSIGN(BitpopOptionsUIHTMLSource);
};

BitpopOptionsUIHTMLSource::BitpopOptionsUIHTMLSource(DictionaryValue* localized_strings)
    : DataSource(chrome::kChromeUIBitpopSettingsFrameHost, MessageLoop::current()) {
  DCHECK(localized_strings);
  localized_strings_.reset(localized_strings);
}

void BitpopOptionsUIHTMLSource::StartDataRequest(const std::string& path,
                                            bool is_incognito,
                                            int request_id) {
  scoped_refptr<base::RefCountedMemory> response_bytes;
  SetFontAndTextDirection(localized_strings_.get());

  if (path == kLocalizedStringsFile) {
    // Return dynamically-generated strings from memory.
    jstemplate_builder::UseVersion2 version;
    std::string strings_js;
    jstemplate_builder::AppendJsonJS(localized_strings_.get(), &strings_js);
    response_bytes = base::RefCountedString::TakeString(&strings_js);
  } else if (path == kOptionsBundleJsFile) {
    // Return (and cache) the options javascript code.
    response_bytes = ui::ResourceBundle::GetSharedInstance().
        LoadDataResourceBytes(IDR_OPTIONS2_BITPOP_BUNDLE_JS, ui::SCALE_FACTOR_NONE);
  } else {
    // Return (and cache) the main options html page as the default.
    response_bytes = ui::ResourceBundle::GetSharedInstance().
        LoadDataResourceBytes(IDR_OPTIONS2_BITPOP_HTML, ui::SCALE_FACTOR_NONE);
  }

  SendResponse(request_id, response_bytes);
}

std::string BitpopOptionsUIHTMLSource::GetMimeType(const std::string& path) const {
  if (path == kLocalizedStringsFile || path == kOptionsBundleJsFile)
    return "application/javascript";

  return "text/html";
}

BitpopOptionsUIHTMLSource::~BitpopOptionsUIHTMLSource() {}

////////////////////////////////////////////////////////////////////////////////
//
// BitpopOptionsPageUIHandler
//
////////////////////////////////////////////////////////////////////////////////

BitpopOptionsPageUIHandler::BitpopOptionsPageUIHandler() {
}

BitpopOptionsPageUIHandler::~BitpopOptionsPageUIHandler() {
}

bool BitpopOptionsPageUIHandler::IsEnabled() {
  return true;
}

// static
void BitpopOptionsPageUIHandler::RegisterStrings(
    DictionaryValue* localized_strings,
    const OptionsStringResource* resources,
    size_t length) {
  for (size_t i = 0; i < length; ++i) {
    localized_strings->SetString(
        resources[i].name, l10n_util::GetStringUTF16(resources[i].id));
  }
}

void BitpopOptionsPageUIHandler::RegisterTitle(DictionaryValue* localized_strings,
                                         const std::string& variable_name,
                                         int title_id) {
  localized_strings->SetString(variable_name,
      l10n_util::GetStringUTF16(title_id));
  localized_strings->SetString(variable_name + "TabTitle",
      l10n_util::GetStringFUTF16(IDS_OPTIONS_TAB_TITLE,
          l10n_util::GetStringUTF16(IDS_SETTINGS_TITLE),
          l10n_util::GetStringUTF16(title_id)));
}

////////////////////////////////////////////////////////////////////////////////
//
// BitpopOptionsUI
//
////////////////////////////////////////////////////////////////////////////////

BitpopOptionsUI::BitpopOptionsUI(content::WebUI* web_ui)
    : WebUIController(web_ui),
      initialized_handlers_(false) {
  DictionaryValue* localized_strings = new DictionaryValue();

  CoreOptionsHandler* core_handler;

  core_handler = new CoreOptionsHandler();

  core_handler->set_handlers_host(this);
  AddBitpopOptionsPageUIHandler(localized_strings, core_handler);

  AddBitpopOptionsPageUIHandler(localized_strings, new AutofillOptionsHandler());

  BrowserOptionsHandler* browser_options_handler = new BrowserOptionsHandler();
  AddBitpopOptionsPageUIHandler(localized_strings, browser_options_handler);

  AddBitpopOptionsPageUIHandler(localized_strings, new ClearBrowserDataHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new ContentSettingsHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new CookiesViewHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new FontSettingsHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new HomePageOverlayHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new MediaGalleriesHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new WebIntentsSettingsHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new LanguageOptionsHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new ManageProfileHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new PasswordManagerHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new SearchEngineManagerHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new ImportDataHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new StartupPagesHandler());
  AddBitpopOptionsPageUIHandler(localized_strings, new OptionsSyncSetupHandler(
      g_browser_process->profile_manager()));

  AddBitpopOptionsPageUIHandler(localized_strings, new HandlerOptionsHandler());

  // |localized_strings| ownership is taken over by this constructor.
  BitpopOptionsUIHTMLSource* html_source =
      new BitpopOptionsUIHTMLSource(localized_strings);

  // Set up the chrome://settings-frame/ source.
  Profile* profile = Profile::FromWebUI(web_ui);
  ChromeURLDataManager::AddDataSource(profile, html_source);

  // Set up the chrome://theme/ source.
  ThemeSource* theme = new ThemeSource(profile);
  ChromeURLDataManager::AddDataSource(profile, theme);
}

BitpopOptionsUI::~BitpopOptionsUI() {
  // Uninitialize all registered handlers. Deleted by WebUIImpl.
  for (size_t i = 0; i < handlers_.size(); ++i)
    handlers_[i]->Uninitialize();
}

// static
void BitpopOptionsUI::ProcessAutocompleteSuggestions(
    const AutocompleteResult& result,
    base::ListValue* const suggestions) {
  for (size_t i = 0; i < result.size(); ++i) {
    const AutocompleteMatch& match = result.match_at(i);
    AutocompleteMatch::Type type = match.type;
    if (type != AutocompleteMatch::HISTORY_URL &&
        type != AutocompleteMatch::HISTORY_TITLE &&
        type != AutocompleteMatch::HISTORY_BODY &&
        type != AutocompleteMatch::HISTORY_KEYWORD &&
        type != AutocompleteMatch::NAVSUGGEST)
      continue;
    base::DictionaryValue* entry = new base::DictionaryValue();
    entry->SetString("title", match.description);
    entry->SetString("displayURL", match.contents);
    entry->SetString("url", match.destination_url.spec());
    suggestions->Append(entry);
  }
}

// static
base::RefCountedMemory* BitpopOptionsUI::GetFaviconResourceBytes() {
  return ui::ResourceBundle::GetSharedInstance().
      LoadDataResourceBytes(IDR_SETTINGS_FAVICON, ui::SCALE_FACTOR_100P);
}

void BitpopOptionsUI::InitializeHandlers() {
  Profile* profile = Profile::FromWebUI(web_ui());
  DCHECK(!profile->IsOffTheRecord() || Profile::IsGuestSession());

  // A new web page DOM has been brought up in an existing renderer, causing
  // this method to be called twice. If that happens, ignore the second call.
  if (!initialized_handlers_) {
    for (size_t i = 0; i < handlers_.size(); ++i)
      handlers_[i]->InitializeHandler();
    initialized_handlers_ = true;

  }

  // Always initialize the page as when handlers are left over we still need to
  // do various things like show/hide sections and send data to the Javascript.
  for (size_t i = 0; i < handlers_.size(); ++i)
    handlers_[i]->InitializePage();
}

void BitpopOptionsUI::AddBitpopOptionsPageUIHandler(DictionaryValue* localized_strings,
                                        BitpopOptionsPageUIHandler* handler_raw) {
  scoped_ptr<BitpopOptionsPageUIHandler> handler(handler_raw);
  DCHECK(handler.get());
  // Add only if handler's service is enabled.
  if (handler->IsEnabled()) {
    // Add handler to the list and also pass the ownership.
    web_ui()->AddMessageHandler(handler.release());
    handler_raw->GetLocalizedValues(localized_strings);
    handlers_.push_back(handler_raw);
  }
}

}  // namespace options2
