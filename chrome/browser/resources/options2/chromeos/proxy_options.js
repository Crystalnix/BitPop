// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {

  var OptionsPage = options.OptionsPage;
  var Preferences = options.Preferences;

  /////////////////////////////////////////////////////////////////////////////
  // ProxyOptions class:

  /**
   * Encapsulated handling of ChromeOS proxy options page.
   * @constructor
   */
  function ProxyOptions(model) {
    OptionsPage.call(this, 'proxy', localStrings.getString('proxyPage'),
                     'proxyPage');
  }

  cr.addSingletonGetter(ProxyOptions);

  /**
   * UI pref change handler.
   */
  function handlePrefUpdate(e) {
    ProxyOptions.getInstance().updateControls();
  }

  /**
   * Monitor pref change of given element.
   */
  function observePrefsUI(el) {
    Preferences.getInstance().addEventListener(el.pref, handlePrefUpdate);
  }

  ProxyOptions.prototype = {
    // Inherit ProxyOptions from OptionsPage.
    __proto__: OptionsPage.prototype,

    /**
     * Initializes ProxyOptions page.
     */
    initializePage: function() {
      // Call base class implementation to start preference initialization.
      OptionsPage.prototype.initializePage.call(this);

      // Set up ignored page.
      options.proxyexceptions.ProxyExceptions.decorate($('ignoredHostList'));

      this.addEventListener('visibleChange', this.handleVisibleChange_);
      $('removeHost').addEventListener('click', this.handleRemoveExceptions_);
      $('addHost').addEventListener('click', this.handleAddException_);
      $('directProxy').addEventListener('click', this.disableManual_);
      $('manualProxy').addEventListener('click', this.enableManual_);
      $('autoProxy').addEventListener('click', this.disableManual_);
      $('proxyAllProtocols').addEventListener('click', this.toggleSingle_);

      observePrefsUI($('directProxy'));
      observePrefsUI($('manualProxy'));
      observePrefsUI($('autoProxy'));
      observePrefsUI($('proxyAllProtocols'));
    },

    proxyListInitialized_: false,

    /**
     * Update controls state.
     * @public
     */
    updateControls: function() {
      this.updateBannerVisibility_();
      this.toggleSingle_();
      if ($('manualProxy').checked) {
        this.enableManual_();
      } else {
        this.disableManual_();
      }
      if (!this.proxyListInitialized_ && this.visible) {
        this.proxyListInitialized_ = true;
        $('ignoredHostList').redraw();
      }
    },

    /**
     * Handler for OptionsPage's visible property change event.
     * @private
     * @param {Event} e Property change event.
     */
    handleVisibleChange_: function(e) {
      this.updateControls();
    },

    /**
     * Updates info banner visibility state. This function shows the banner
     * if proxy is managed or shared-proxies is off for shared network.
     * @private
     */
    updateBannerVisibility_: function() {
      var bannerDiv = $('info-banner');
      // Remove class and listener for click event in case they were added
      // before and updateBannerVisibility_ is called repeatedly.
      bannerDiv.classList.remove("clickable");
      bannerDiv.removeEventListener('click', this.handleSharedProxiesHint_);

      // Show banner and determine its message if necessary.
      var controlledBy = $('directProxy').controlledBy;
      if (controlledBy == '') {
        bannerDiv.hidden = true;
      } else {
        bannerDiv.hidden = false;
        // controlledBy must match strings loaded in proxy_handler.cc and
        // set in proxy_cros_settings_provider.cc.
        $('banner-text').textContent = localStrings.getString(controlledBy);
        if (controlledBy == "enableSharedProxiesBannerText") {
          bannerDiv.classList.add("clickable");
          bannerDiv.addEventListener('click', this.handleSharedProxiesHint_);
        }
      }
    },

    /**
     * Handler for "click" event on yellow banner with enable-shared-proxies
     * hint.
     * @private
     * @param {Event} e Click event fired from info-banner.
     */
    handleSharedProxiesHint_: function(e) {
      OptionsPage.navigateToPage("internet");
    },

    /**
     * Handler for when the user clicks on the checkbox to allow a
     * single proxy usage.
     * @private
     * @param {Event} e Click Event.
     */
    toggleSingle_: function(e) {
      if ($('proxyAllProtocols').checked) {
        $('multiProxy').style.display = 'none';
        $('singleProxy').style.display = 'block';
      } else {
        $('multiProxy').style.display = 'block';
        $('singleProxy').style.display = 'none';
      }
    },

    /**
     * Handler for selecting a radio button that will disable the manual
     * controls.
     * @private
     * @param {Event} e Click event.
     */
    disableManual_: function(e) {
      $('advancedConfig').hidden = true;
      $('proxyAllProtocols').disabled = true;
      $('proxyHostName').disabled = true;
      $('proxyHostPort').disabled = true;
      $('proxyHostSingleName').disabled = true;
      $('proxyHostSinglePort').disabled = true;
      $('secureProxyHostName').disabled = true;
      $('secureProxyPort').disabled = true;
      $('ftpProxy').disabled = true;
      $('ftpProxyPort').disabled = true;
      $('socksHost').disabled = true;
      $('socksPort').disabled = true;
      $('proxyConfig').disabled = $('autoProxy').disabled ||
                                  !$('autoProxy').checked;
    },

    /**
     * Handler for selecting a radio button that will enable the manual
     * controls.
     * @private
     * @param {Event} e Click event.
     */
    enableManual_: function(e) {
      $('advancedConfig').hidden = false;
      $('ignoredHostList').redraw();
      var all_disabled = $('manualProxy').disabled;
      $('newHost').disabled = all_disabled;
      $('removeHost').disabled = all_disabled;
      $('addHost').disabled = all_disabled;
      $('proxyAllProtocols').disabled = all_disabled;
      $('proxyHostName').disabled = all_disabled;
      $('proxyHostPort').disabled = all_disabled;
      $('proxyHostSingleName').disabled = all_disabled;
      $('proxyHostSinglePort').disabled = all_disabled;
      $('secureProxyHostName').disabled = all_disabled;
      $('secureProxyPort').disabled = all_disabled;
      $('ftpProxy').disabled = all_disabled;
      $('ftpProxyPort').disabled = all_disabled;
      $('socksHost').disabled = all_disabled;
      $('socksPort').disabled = all_disabled;
      $('proxyConfig').disabled = true;
    },

    /**
     * Handler for "add" event fired from userNameEdit.
     * @private
     * @param {Event} e Add event fired from userNameEdit.
     */
    handleAddException_: function(e) {
      var exception = $('newHost').value;
      $('newHost').value = '';

      exception = exception.trim();
      if (exception)
        $('ignoredHostList').addException(exception);
    },

    /**
     * Handler for when the remove button is clicked
     * @private
     */
    handleRemoveExceptions_: function(e) {
      var selectedItems = $('ignoredHostList').selectedItems;
      for (var x = 0; x < selectedItems.length; x++) {
        $('ignoredHostList').removeException(selectedItems[x]);
      }
    },

    /**
     * Sets proxy page title using given network name.
     * @param {string} network The network name to use in page title.
     * @public
     */
    setNetworkName: function(network) {
      $('proxy-page-title').textContent =
          localStrings.getStringF('proxyPageTitleFormat', network);
    }
  };

  ProxyOptions.setNetworkName = function(network) {
    ProxyOptions.getInstance().setNetworkName(network);
  };

  // Export
  return {
    ProxyOptions: ProxyOptions
  };

});
