// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var AddLanguageOverlay = options.AddLanguageOverlay;
var AdvancedOptions = options.AdvancedOptions;
var AlertOverlay = options.AlertOverlay;
var AutofillEditAddressOverlay = options.AutofillEditAddressOverlay;
var AutofillEditCreditCardOverlay = options.AutofillEditCreditCardOverlay;
var AutofillOptions = options.AutofillOptions;
var BrowserOptions = options.BrowserOptions;
var ClearBrowserDataOverlay = options.ClearBrowserDataOverlay;
var ContentSettings = options.ContentSettings;
var ContentSettingsExceptionsArea =
    options.contentSettings.ContentSettingsExceptionsArea;
var CookiesView = options.CookiesView;
var FontSettings = options.FontSettings;
var HandlerOptions = options.HandlerOptions;
var HomePageOverlay = options.HomePageOverlay;
var ImportDataOverlay = options.ImportDataOverlay;
var IntentsView = options.IntentsView;
var InstantConfirmOverlay = options.InstantConfirmOverlay;
var LanguageOptions = options.LanguageOptions;
var OptionsPage = options.OptionsPage;
var PasswordManager = options.PasswordManager;
var Preferences = options.Preferences;
var ManageProfileOverlay = options.ManageProfileOverlay;
var ProxyOptions = options.ProxyOptions;
var SearchEngineManager = options.SearchEngineManager;
var SearchPage = options.SearchPage;
var StartupOverlay = options.StartupOverlay;
var SyncSetupOverlay = options.SyncSetupOverlay;
var VirtualKeyboardManager = options.VirtualKeyboardManager;

/**
 * DOMContentLoaded handler, sets up the page.
 */
function load() {
  // Decorate the existing elements in the document.
  cr.ui.decorate('input[pref][type=checkbox]', options.PrefCheckbox);
  cr.ui.decorate('input[pref][type=number]', options.PrefNumber);
  cr.ui.decorate('input[pref][type=radio]', options.PrefRadio);
  cr.ui.decorate('input[pref][type=range]', options.PrefRange);
  cr.ui.decorate('select[pref]', options.PrefSelect);
  cr.ui.decorate('input[pref][type=text]', options.PrefTextField);
  cr.ui.decorate('input[pref][type=url]', options.PrefTextField);
  cr.ui.decorate('button[pref]', options.PrefButton);
  cr.ui.decorate('#content-settings-page input[type=radio]:not(.handler-radio)',
      options.ContentSettingsRadio);
  cr.ui.decorate('#content-settings-page input[type=radio].handler-radio',
      options.HandlersEnabledRadio);
  cr.ui.decorate('span.controlled-setting-indicator',
      options.ControlledSettingIndicator);

  var menuOffPattern = /(^\?|&)menu=off($|&)/;
  var menuDisabled = menuOffPattern.test(window.location.search);
  // document.documentElement.setAttribute('hide-menu', menuDisabled);
  // We can't use an attribute on the html element because of webkit bug
  // 12519. Instead, we add a class.
  if (menuDisabled)
    document.documentElement.classList.add('hide-menu');

  localStrings = new LocalStrings();

  // Top level pages.
  OptionsPage.register(SearchPage.getInstance());
  OptionsPage.register(BrowserOptions.getInstance());

  // BrowserOptions sub-pages.
  OptionsPage.registerSubPage(AdvancedOptions.getInstance(),
                              BrowserOptions.getInstance(),
                              [$('advancedOptionsButton')]);
  OptionsPage.registerSubPage(SearchEngineManager.getInstance(),
                              BrowserOptions.getInstance(),
                              [$('defaultSearchManageEnginesButton')]);
  if (cr.isChromeOS) {
    OptionsPage.registerSubPage(AccountsOptions.getInstance(),
                                BrowserOptions.getInstance(),
                                [$('manage-accounts-button')]);
    OptionsPage.registerSubPage(ChangePictureOptions.getInstance(),
                                BrowserOptions.getInstance(),
                                [$('change-picture-button')]);
    OptionsPage.registerSubPage(InternetOptions.getInstance(),
                                BrowserOptions.getInstance(),
                                [$('internet-options-button')]);
  }

  // AdvancedOptions sub-pages.
  OptionsPage.registerSubPage(AutofillOptions.getInstance(),
                              AdvancedOptions.getInstance(),
                              [$('autofill-settings')]);
  OptionsPage.registerSubPage(PasswordManager.getInstance(),
                              AdvancedOptions.getInstance(),
                              [$('manage-passwords')]);
  OptionsPage.registerSubPage(ContentSettings.getInstance(),
                              AdvancedOptions.getInstance(),
                              [$('privacyContentSettingsButton')]);
  OptionsPage.registerSubPage(FontSettings.getInstance(),
                              AdvancedOptions.getInstance(),
                              [$('fontSettingsCustomizeFontsButton')]);
  OptionsPage.registerSubPage(LanguageOptions.getInstance(),
                              AdvancedOptions.getInstance(),
                              [$('language-button')]);
  if (!cr.isWindows && !cr.isMac) {
    OptionsPage.registerSubPage(CertificateManager.getInstance(),
                                AdvancedOptions.getInstance(),
                                [$('certificatesManageButton')]);
  }

  // LanguageOptions sub-pages.
  if (cr.isChromeOS) {
    OptionsPage.registerSubPage(
        new OptionsPage('languageChewing',
                        templateData.languageChewingPageTabTitle,
                        'languageChewingPage'),
        LanguageOptions.getInstance());
    OptionsPage.registerSubPage(
        new OptionsPage('languageHangul',
                        templateData.languageHangulPageTabTitle,
                        'languageHangulPage'),
        LanguageOptions.getInstance());
    OptionsPage.registerSubPage(
        new OptionsPage('languageMozc',
                        templateData.languageMozcPageTabTitle,
                        'languageMozcPage'),
        LanguageOptions.getInstance());
    OptionsPage.registerSubPage(
        new OptionsPage('languagePinyin',
                        templateData.languagePinyinPageTabTitle,
                        'languagePinyinPage'),
        LanguageOptions.getInstance());
    // Only use the VirtualKeyboardManager if the keyboard DOM elements (which
    // it will assume exists) are present (i.e. if we were built with
    // USE_VIRTUAL_KEYBOARD).
    if ($('language-options-virtual-keyboard')) {
      OptionsPage.registerSubPage(VirtualKeyboardManager.getInstance(),
                                  LanguageOptions.getInstance());
    }
  }

  // ContentSettings sub-pages.
  OptionsPage.registerSubPage(ContentSettingsExceptionsArea.getInstance(),
                              ContentSettings.getInstance());
  OptionsPage.registerSubPage(CookiesView.getInstance(),
                              ContentSettings.getInstance(),
                              [$('privacyContentSettingsButton'),
                               $('show-cookies-button')]);
  if (HandlerOptions && $('manage-handlers-button')) {
    OptionsPage.registerSubPage(HandlerOptions.getInstance(),
                                ContentSettings.getInstance(),
                                [$('manage-handlers-button')]);
  }
  if (IntentsView && $('manage-intents-button')) {
    OptionsPage.registerSubPage(IntentsView.getInstance(),
                                ContentSettings.getInstance(),
                                [$('manage-intents-button')]);
  }

  // InternetOptions sub-pages.
  if (cr.isChromeOS) {
    OptionsPage.registerSubPage(ProxyOptions.getInstance(),
                                InternetOptions.getInstance());
  }

  // Overlays.
  OptionsPage.registerOverlay(AddLanguageOverlay.getInstance(),
                              LanguageOptions.getInstance());
  OptionsPage.registerOverlay(AlertOverlay.getInstance());
  OptionsPage.registerOverlay(AutofillEditAddressOverlay.getInstance(),
                              AutofillOptions.getInstance());
  OptionsPage.registerOverlay(AutofillEditCreditCardOverlay.getInstance(),
                              AutofillOptions.getInstance());
  OptionsPage.registerOverlay(ClearBrowserDataOverlay.getInstance(),
                              AdvancedOptions.getInstance(),
                              [$('privacyClearDataButton')]);
  OptionsPage.registerOverlay(HomePageOverlay.getInstance(),
                              BrowserOptions.getInstance(),
                              [$('toolbarShowHomeButton')]);
  OptionsPage.registerOverlay(ImportDataOverlay.getInstance(),
                              BrowserOptions.getInstance());
  OptionsPage.registerOverlay(InstantConfirmOverlay.getInstance(),
                              BrowserOptions.getInstance());
  OptionsPage.registerOverlay(ManageProfileOverlay.getInstance(),
                              BrowserOptions.getInstance());
  OptionsPage.registerOverlay(StartupOverlay.getInstance(),
                              BrowserOptions.getInstance());
  OptionsPage.registerOverlay(SyncSetupOverlay.getInstance(),
                              BrowserOptions.getInstance());
  if (cr.isChromeOS) {
    OptionsPage.registerOverlay(BluetoothOptions.getInstance(),
                                AdvancedOptions.getInstance(),
                                [$('bluetooth-add-device')]);
    OptionsPage.registerOverlay(BluetoothPairing.getInstance(),
                                AdvancedOptions.getInstance());
    OptionsPage.registerOverlay(DetailsInternetPage.getInstance(),
                                InternetOptions.getInstance());
    OptionsPage.registerOverlay(KeyboardOverlay.getInstance(),
                                BrowserOptions.getInstance(),
                                [$('keyboard-settings-button')]);
    OptionsPage.registerOverlay(PointerOverlay.getInstance(),
                                BrowserOptions.getInstance(),
                                [$('pointer-settings-button')]);
    OptionsPage.registerSubPage(ProxyOptions.getInstance(),
                                InternetOptions.getInstance());
  }
  if (!cr.isWindows && !cr.isMac) {
    OptionsPage.registerOverlay(CertificateBackupOverlay.getInstance(),
                                CertificateManager.getInstance());
    OptionsPage.registerOverlay(CertificateEditCaTrustOverlay.getInstance(),
                                CertificateManager.getInstance());
    OptionsPage.registerOverlay(CertificateImportErrorOverlay.getInstance(),
                                CertificateManager.getInstance());
    OptionsPage.registerOverlay(CertificateRestoreOverlay.getInstance(),
                                CertificateManager.getInstance());
  }

  Preferences.getInstance().initialize();
  OptionsPage.initialize();

  var path = document.location.pathname;

  if (path.length > 1) {
    // Skip starting slash and remove trailing slash (if any).
    var pageName = path.slice(1).replace(/\/$/, '');
    // Proxy page is now per network and only reachable from internet details.
    if (pageName != 'proxy') {
      // Show page, but don't update history (there's already an entry for it).
      OptionsPage.showPageByName(pageName, false);
    }
  } else {
    OptionsPage.showDefaultPage();
  }

  var subpagesNavTabs = document.querySelectorAll('.subpages-nav-tabs');
  for(var i = 0; i < subpagesNavTabs.length; i++) {
    subpagesNavTabs[i].onclick = function(event) {
      OptionsPage.showTab(event.srcElement);
    }
  }

  // Allow platform specific CSS rules.
  cr.enablePlatformSpecificCSSRules();

  if (navigator.plugins['Shockwave Flash'])
    document.documentElement.setAttribute('hasFlashPlugin', '');
}

document.addEventListener('DOMContentLoaded', load);

window.onbeforeunload = function() {
  options.OptionsPage.willClose();
};

window.onpopstate = function(e) {
  options.OptionsPage.setState(e.state);
};
