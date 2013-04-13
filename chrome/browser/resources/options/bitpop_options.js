// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix, Viatcheslav Gachkaylo <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  var OptionsPage = options.OptionsPage;
  var ArrayDataModel = cr.ui.ArrayDataModel;
  var RepeatingButton = cr.ui.RepeatingButton;

  //
  // BrowserOptions class
  // Encapsulated handling of browser options page.
  //
  function BrowserOptions() {
    OptionsPage.call(this, 'bitpopSettings', loadTimeData.getString('bitpopSettingsTitle'),
                     'bitpop-settings');
  }

  cr.addSingletonGetter(BrowserOptions);

  BrowserOptions.prototype = {
    __proto__: options.OptionsPage.prototype,

    /**
     * Track if page initialization is complete.  All C++ UI handlers have the
     * chance to manipulate page content within their InitializePage mathods.
     * This flag is set to true after all initializers have been called.
     * @type (boolean}
     * @private
     */
    initializationComplete_: false,

    initializePage: function() {
      OptionsPage.prototype.initializePage.call(this);
      var self = this;

      // Ensure that navigation events are unblocked on uber page. A reload of
      // the settings page while an overlay is open would otherwise leave uber
      // page in a blocked state, where tab switching is not possible.
      uber.invokeMethodOnParent('stopInterceptingEvents');

      window.addEventListener('message', this.handleWindowMessage_.bind(this));

      $('open-facebook-notifications-options').onclick = function (event) {
        chrome.send('openFacebookNotificationsOptions');
      };

      $('open-uncensor-filter-lists').onclick = function(event) {
        OptionsPage.navigateToPage('uncensorFilter');
      };

      $('open-proxy-domain-settings').onclick = function(event) {
        OptionsPage.navigateToPage('uncensorBlockedSites');
      };
    },

    didShowPage: function() {
      $('search-field').focus();
    },

   /**
    * Called after all C++ UI handlers have called InitializePage to notify
    * that initialization is complete.
    * @private
    */
    notifyInitializationComplete_: function() {
      this.initializationComplete_ = true;
      cr.dispatchSimpleEvent(document, 'initializationComplete');
    },
    
    /**
     * Handler for messages sent from the main uber page.
     * @param {Event} e The 'message' event from the uber page.
     * @private
     */
    handleWindowMessage_: function(e) {
      if (e.data.method == 'frameSelected')
        $('search-field').focus();
    },
  };

  //Forward public APIs to private implementations.
  [
    'notifyInitializationComplete'
  ].forEach(function(name) {
    BrowserOptions[name] = function() {
      var instance = BrowserOptions.getInstance();
      return instance[name + '_'].apply(instance, arguments);
    };
  });

  return {
    BrowserOptions: BrowserOptions
  };

});

