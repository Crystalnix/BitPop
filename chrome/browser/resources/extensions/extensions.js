// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

<include src="../uber/uber_utils.js"></include>
<include src="extension_list.js"></include>
<include src="pack_extension_overlay.js"></include>

// Used for observing function of the backend datasource for this page by
// tests.
var webui_responded_ = false;

var localStrings = new LocalStrings();

cr.define('extensions', function() {
  var ExtensionsList = options.ExtensionsList;

  /**
   * ExtensionSettings class
   * @class
   */
  function ExtensionSettings() {}

  cr.addSingletonGetter(ExtensionSettings);

  ExtensionSettings.prototype = {
    __proto__: HTMLDivElement.prototype,

    /**
     * Perform initial setup.
     */
    initialize: function() {
      cr.enablePlatformSpecificCSSRules();

      // Set the title.
      var title = localStrings.getString('extensionSettings');
      uber.invokeMethodOnParent('setTitle', {title: title});

      // This will request the data to show on the page and will get a response
      // back in returnExtensionsData.
      chrome.send('extensionSettingsRequestExtensionsData');

      // Set up the developer mode button.
      var toggleDevMode = $('toggle-dev-on');
      toggleDevMode.addEventListener('click',
          this.handleToggleDevMode_.bind(this));

      // Setup the gallery related links and text.
      $('suggest-gallery').innerHTML =
          localStrings.getString('extensionSettingsSuggestGallery');
      $('get-more-extensions').innerHTML =
          localStrings.getString('extensionSettingsGetMoreExtensions');

      // Set up the three dev mode buttons (load unpacked, pack and update).
      $('load-unpacked').addEventListener('click',
          this.handleLoadUnpackedExtension_.bind(this));
      $('pack-extension').addEventListener('click',
          this.handlePackExtension_.bind(this));
      $('update-extensions-now').addEventListener('click',
          this.handleUpdateExtensionNow_.bind(this));

      this.pageHeader_ = $('page-header');

      document.addEventListener('scroll', this.handleScroll_.bind(this));
      window.addEventListener('message', this.handleWindowMessage_.bind(this));

      var packExtensionOverlay = extensions.PackExtensionOverlay.getInstance();
      packExtensionOverlay.initializePage();

      // Trigger the scroll handler to tell the navigation if our page started
      // with some scroll (happens when you use tab restore).
      this.handleScroll_();
    },

    /**
     * Utility function which asks the C++ to show a platform-specific file
     * select dialog, and fire |callback| with the |filePath| that resulted.
     * |selectType| can be either 'file' or 'folder'. |operation| can be 'load',
     * 'packRoot', or 'pem' which are signals to the C++ to do some
     * operation-specific configuration.
     * @private
     */
    showFileDialog_: function(selectType, operation, callback) {
      handleFilePathSelected = function(filePath) {
        callback(filePath);
        handleFilePathSelected = function() {};
      };

      chrome.send('extensionSettingsSelectFilePath', [selectType, operation]);
    },

    /**
     * Handles the Load Unpacked Extension button.
     * @param {Event} e Change event.
     * @private
     */
    handleLoadUnpackedExtension_: function(e) {
      this.showFileDialog_('folder', 'pem', function(filePath) {
        chrome.send('extensionSettingsLoad', [String(filePath)]);
      });

      chrome.send('coreOptionsUserMetricsAction',
                  ['Options_LoadUnpackedExtension']);
    },

    /**
     * Handles the Pack Extension button.
     * @param {Event} e Change event.
     * @private
     */
    handlePackExtension_: function(e) {
      ExtensionSettings.showOverlay($('packExtensionOverlay'));
      chrome.send('coreOptionsUserMetricsAction', ['Options_PackExtension']);
    },

    /**
     * Handles the Update Extension Now button.
     * @param {Event} e Change event.
     * @private
     */
    handleUpdateExtensionNow_: function(e) {
      chrome.send('extensionSettingsAutoupdate', []);
    },

    /**
     * Handles the Toggle Dev Mode button.
     * @param {Event} e Change event.
     * @private
     */
    handleToggleDevMode_: function(e) {
      var dev = $('dev');
      if (!dev.classList.contains('dev-open')) {
        // Make the Dev section visible.
        dev.classList.add('dev-open');
        dev.classList.remove('dev-closed');

        $('load-unpacked').classList.add('dev-button-visible');
        $('load-unpacked').classList.remove('dev-button-hidden');
        $('pack-extension').classList.add('dev-button-visible');
        $('pack-extension').classList.remove('dev-button-hidden');
        $('update-extensions-now').classList.add('dev-button-visible');
        $('update-extensions-now').classList.remove('dev-button-hidden');
      } else {
        // Hide the Dev section.
        dev.classList.add('dev-closed');
        dev.classList.remove('dev-open');

        $('load-unpacked').classList.add('dev-button-hidden');
        $('load-unpacked').classList.remove('dev-button-visible');
        $('pack-extension').classList.add('dev-button-hidden');
        $('pack-extension').classList.remove('dev-button-visible');
        $('update-extensions-now').classList.add('dev-button-hidden');
        $('update-extensions-now').classList.remove('dev-button-visible');
      }

      chrome.send('extensionSettingsToggleDeveloperMode', []);
    },

    /**
     * Called when the page is scrolled; moves elements that are position:fixed
     * but should only behave as if they are fixed for vertical scrolling.
     * @private
     */
    handleScroll_: function() {
      var offset = document.body.scrollLeft * -1;
      this.pageHeader_.style.webkitTransform = 'translateX(' + offset + 'px)';
      uber.invokeMethodOnParent('adjustToScroll', document.body.scrollLeft);
    },

    /**
     * Handles postMessage from chrome://chrome.
     * @param {Event} e The post data.
     */
    handleWindowMessage_: function(e) {
      if (e.data.method === 'frameSelected')
        this.handleFrameSelected_();
      else
        console.error('Received unexpected message', e.data);
    },

    /**
     * This is called when a user selects this frame via the navigation bar
     * frame (and is triggered via postMessage() from the uber page).
     * @private
     */
    handleFrameSelected_: function() {
      document.body.scrollLeft = 0;
    },
  };

  /**
   * Called by the dom_ui_ to re-populate the page with data representing
   * the current state of installed extensions.
   */
  ExtensionSettings.returnExtensionsData = function(extensionsData) {
    webui_responded_ = true;

    $('no-extensions').hidden = true;
    $('suggest-gallery').hidden = true;
    $('get-more-extensions-container').hidden = true;

    if (extensionsData.extensions.length > 0) {
      // Enforce order specified in the data or (if equal) then sort by
      // extension name (case-insensitive).
      extensionsData.extensions.sort(function(a, b) {
        if (a.order == b.order) {
          a = a.name.toLowerCase();
          b = b.name.toLowerCase();
          return a < b ? -1 : (a > b ? 1 : 0);
        } else {
          return a.order < b.order ? -1 : 1;
        }
      });

      $('get-more-extensions-container').hidden = false;
    } else {
      $('no-extensions').hidden = false;
      $('suggest-gallery').hidden = false;
    }

    ExtensionsList.prototype.data_ = extensionsData;

    var extensionList = $('extension-settings-list');
    ExtensionsList.decorate(extensionList);
  }

  // Indicate that warning |message| has occured for pack of |crx_path| and
  // |pem_path| files.  Ask if user wants override the warning.  Send
  // |overrideFlags| to repeated 'pack' call to accomplish the override.
  ExtensionSettings.askToOverrideWarning =
      function(message, crx_path, pem_path, overrideFlags) {
    var closeAlert = function() {
      ExtensionSettings.showOverlay(null);
    };

    alertOverlay.setValues(
        localStrings.getString('packExtensionWarningTitle'),
        message,
        localStrings.getString('packExtensionProceedAnyway'),
        localStrings.getString('cancel'),
        function() {
          chrome.send('pack', [crx_path, pem_path, overrideFlags]);
          closeAlert();
        },
        closeAlert);
    ExtensionSettings.showOverlay($('alertOverlay'));
  }

  /**
   * Sets the given overlay to show. This hides whatever overlay is currently
   * showing, if any.
   * @param {HTMLElement} node The overlay page to show. If falsey, all overlays
   *     are hidden.
   */
  ExtensionSettings.showOverlay = function(node) {
    var currentlyShowingOverlay =
        document.querySelector('#overlay .page.showing');
    if (currentlyShowingOverlay)
      currentlyShowingOverlay.classList.remove('showing');

    if (node)
      node.classList.add('showing');
    overlay.hidden = !node;
  }

  // Export
  return {
    ExtensionSettings: ExtensionSettings
  };
});

var ExtensionSettings = extensions.ExtensionSettings;

// 'load' seems to have a bad interaction with open_sans.woff.
window.addEventListener('DOMContentLoaded', function(e) {
  ExtensionSettings.getInstance().initialize();
});
