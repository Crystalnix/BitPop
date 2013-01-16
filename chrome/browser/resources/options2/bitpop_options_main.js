// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix, Viatcheslav Gachkaylo <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var BrowserOptions = options.BrowserOptions;
var BitpopProxyDomainSettingsOverlay = options.BitpopProxyDomainSettingsOverlay;
var BitpopUncensorFilterOverlay = options.BitpopUncensorFilterOverlay;
var OptionsFocusManager = options.OptionsFocusManager;
var OptionsPage = options.OptionsPage;
var Preferences = options.Preferences;
var SearchPage = options.SearchPage;

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

  // Top level pages.
  OptionsPage.register(SearchPage.getInstance());
  OptionsPage.register(BrowserOptions.getInstance());

  // Overlays.
  // OptionsPage.registerOverlay(AutofillOptions.getInstance(),
  //                             BitpopOptions.getInstance(),
  //                             [$('autofill-settings')]);
  OptionsPage.registerOverlay(BitpopProxyDomainSettingsOverlay.getInstance(),
                              BrowserOptions.getInstance(),
                              [$('open-proxy-domain-settings')]);
  OptionsPage.registerOverlay(BitpopUncensorFilterOverlay.getInstance(),
                              BrowserOptions.getInstance(),
                              [$('open-uncensor-filter-lists')])


  OptionsFocusManager.getInstance().initialize();
  Preferences.getInstance().initialize();
  OptionsPage.initialize();

  var path = document.location.pathname;

  if (path.length > 1) {
    // Skip starting slash and remove trailing slash (if any).
    var pageName = path.slice(1).replace(/\/$/, '');
    OptionsPage.showPageByName(pageName, true, {replaceState: true});
  } else {
    OptionsPage.showDefaultPage();
  }

  var subpagesNavTabs = document.querySelectorAll('.subpages-nav-tabs');
  for (var i = 0; i < subpagesNavTabs.length; i++) {
    subpagesNavTabs[i].onclick = function(event) {
      OptionsPage.showTab(event.srcElement);
    };
  }

  if (navigator.plugins['Shockwave Flash'])
    document.documentElement.setAttribute('hasFlashPlugin', '');

  window.setTimeout(function() {
    document.documentElement.classList.remove('loading');
  });
}

document.documentElement.classList.add('loading');
document.addEventListener('DOMContentLoaded', load);

/**
 * Listener for the |beforeunload| event.
 */
window.onbeforeunload = function() {
  options.OptionsPage.willClose();
};

/**
 * Listener for the |popstate| event.
 * @param {Event} e The |popstate| event.
 */
window.onpopstate = function(e) {
  options.OptionsPage.setState(e.state);
};
