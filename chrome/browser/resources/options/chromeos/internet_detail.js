// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options.internet', function() {
  var OptionsPage = options.OptionsPage;

  /*
   * Helper function to set hidden attribute on given element list.
   * @param {Array} elements List of elements to be updated.
   * @param {bool} hidden New hidden value.
   */
  function updateHidden(elements, hidden) {
    for (var i = 0, el; el = elements[i]; i++) {
      el.hidden = hidden;
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  // DetailsInternetPage class:

  /**
   * Encapsulated handling of ChromeOS internet details overlay page.
   * @constructor
   */
  function DetailsInternetPage() {
    OptionsPage.call(this, 'detailsInternetPage', null, 'detailsInternetPage');
  }

  cr.addSingletonGetter(DetailsInternetPage);

  DetailsInternetPage.prototype = {
    __proto__: OptionsPage.prototype,

    /**
     * Initializes DetailsInternetPage page.
     * Calls base class implementation to starts preference initialization.
     */
    initializePage: function() {
      OptionsPage.prototype.initializePage.call(this);
    },

    /**
     * Initializes the controlled setting indicators for the page.
     * @param {Object} data Dictionary with metadata about the settings.
     */
    initializeControlledSettingIndicators: function(data) {
      indicators =
          this.pageDiv.querySelectorAll('.controlled-setting-indicator');
      for (var i = 0; i < indicators.length; i++) {
        var dataProperty = indicators[i].getAttribute('data');
        if (dataProperty && data[dataProperty]) {
          this.initializeIndicator_(indicators[i],
                                    data[dataProperty].controlledBy,
                                    data[dataProperty].default);
        }
      }
    },

    /**
     * Sets up a single controlled setting indicator, setting the controlledBy
     * property and an event handler for resetting to the default value if
     * appropriate.
     * @param {Object} indicator The indicator element.
     * @param {string} controlledBy The entity that controls the setting.
     * @param {Object} defaultValue The default value to reset to, if
     * applicable.
     */
    initializeIndicator_ : function(indicator, controlledBy, defaultValue) {
      var forElement = $(indicator.getAttribute('for'));
      var recommended = controlledBy == 'recommended';
      if (!controlledBy || (recommended && !defaultValue))
        controlledBy = null;

      indicator.controlledBy = controlledBy;

      if (forElement) {
        forElement.disabled = (controlledBy != null) && !recommended;

        // Special handling for radio buttons:
        //  - If the setting is recommended, show the recommended indicator
        //    next to the choice that is recommended.
        //  - Else, show the indicator next to the selected choice.
        if (forElement.type == 'radio') {
          if (recommended)
            indicator.hidden = (defaultValue != forElement.value);
          else
            indicator.hidden = !forElement.checked;
        }

        indicator.setAttribute('allow-reset');
        indicator.addEventListener(
            'reset',
            function(element, e) {
              if (forElement.type == 'radio' || forElement.type == 'checkbox') {
                // The recommended setting indicator is always shown next to
                // the recommended choice.
                forElement.checked = true;
              } else {
                forElement.value = defaultValue;
              }
              e.preventDefault();
            });
      }
    },

    /**
     * Update details page controls.
     * @private
     */
    updateControls_: function() {
      // Only show ipconfig section if network is connected OR if nothing on
      // this device is connected. This is so that you can fix the ip configs
      // if you can't connect to any network.
      // TODO(chocobo): Once ipconfig is moved to flimflam service objects,
      //   we need to redo this logic to allow configuration of all networks.
      $('ipconfigSection').hidden = !this.connected && this.deviceConnected;

      // Network type related.
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .cellular-details'),
          !this.cellular);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .wifi-details'),
          !this.wireless);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .vpn-details'),
          !this.vpn);

      // Cell plan related.
      $('planList').hidden = this.cellplanloading;
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .no-plan-info'),
          !this.cellular || this.cellplanloading || this.hascellplan);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .plan-loading-info'),
          !this.cellular || this.nocellplan || this.hascellplan);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .plan-details-info'),
          !this.cellular || this.nocellplan  || this.cellplanloading);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .gsm-only'),
          !this.cellular || !this.gsm);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .cdma-only'),
          !this.cellular || this.gsm);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .apn-list-view'),
          !this.cellular || !this.gsm);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .apn-details-view'),
          true);

      // Password and shared.
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .password-details'),
          !this.wireless || !this.password);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .shared-network'),
          !this.shared);
      updateHidden(
          cr.doc.querySelectorAll('#detailsInternetPage .prefer-network'),
          !this.showPreferred);
    }
  };

  /**
   * Whether the underlying network is connected. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'connected',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether the underlying network is wifi. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'wireless',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether the underlying network shared wifi. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'shared',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether the underlying network is a vpn. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'vpn',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether the underlying network is ethernet. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'ethernet',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether the underlying network is cellular. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'cellular',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether the network is loading cell plan. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'cellplanloading',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether the network has cell plan(s). Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'hascellplan',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether the network has no cell plan. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'nocellplan',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether the network is gsm. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'gsm',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  /**
   * Whether show password details for network. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(DetailsInternetPage, 'password',
      cr.PropertyKind.JS,
      DetailsInternetPage.prototype.updateControls_);

  // TODO(xiyuan): Check to see if it is safe to remove these attributes.
  cr.defineProperty(DetailsInternetPage, 'hasactiveplan',
      cr.PropertyKind.JS);
  cr.defineProperty(DetailsInternetPage, 'activated',
      cr.PropertyKind.JS);
  cr.defineProperty(DetailsInternetPage, 'connecting',
      cr.PropertyKind.JS);
  cr.defineProperty(DetailsInternetPage, 'connected',
      cr.PropertyKind.JS);

  return {
    DetailsInternetPage: DetailsInternetPage
  };
});
