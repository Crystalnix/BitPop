// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('extensions', function() {
  /**
   * PackExtensionOverlay class
   * Encapsulated handling of the 'Pack Extension' overlay page.
   * @constructor
   */
  function PackExtensionOverlay() {
  }

  cr.addSingletonGetter(PackExtensionOverlay);

  PackExtensionOverlay.prototype = {
    /**
     * Initialize the page.
     */
    initializePage: function() {
      $('packExtensionDismiss').addEventListener('click',
          this.handleDismiss_.bind(this));
      $('packExtensionCommit').addEventListener('click',
          this.handleCommit_.bind(this));
      $('browseExtensionDir').addEventListener('click',
          this.handleBrowseExtensionDir_.bind(this));
      $('browsePrivateKey').addEventListener('click',
          this.handleBrowsePrivateKey_.bind(this));
    },

    /**
     * Handles a click on the dismiss button.
     * @param {Event} e
     */
    handleDismiss_: function(e) {
      ExtensionSettings.showOverlay(null);
    },

    /**
     * Handles a click on the pack button.
     * @param {Event} e
     */
    handleCommit_: function(e) {
      var extensionPath = $('extensionRootDir').value;
      var privateKeyPath = $('extensionPrivateKey').value;
      chrome.send('pack', [extensionPath, privateKeyPath, 0]);
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
     * Handles the showing of the extension directory browser.
     * @param {Event} e Change event.
     * @private
     */
    handleBrowseExtensionDir_: function(e) {
      this.showFileDialog_('folder', 'load', function(filePath) {
        $('extensionRootDir').value = filePath;
      });
    },

    /**
     * Handles the showing of the extension private key file.
     * @param {Event} e Change event.
     * @private
     */
    handleBrowsePrivateKey_: function(e) {
      this.showFileDialog_('file', 'load', function(filePath) {
        $('extensionPrivateKey').value = filePath;
      });
    },
  };

  /**
   * Wrap up the pack process by showing the success |message| and closing
   * the overlay.
   * @param {String} message The message to show to the user.
   */
  PackExtensionOverlay.showSuccessMessage = function(message) {
    alertOverlay.setValues(
        localStrings.getString('packExtensionOverlay'),
        message,
        localStrings.getString('ok'),
        '',
        function() {
          ExtensionSettings.showOverlay($('packExtensionOverlay'));
        },
        null);
    ExtensionSettings.showOverlay($('alertOverlay'));
  };

  /**
   * Post an alert overlay showing |message|, and upon acknowledgement, close
   * the alert overlay and return to showing the PackExtensionOverlay.
   */
  PackExtensionOverlay.showError = function(message) {
    alertOverlay.setValues(
        localStrings.getString('packExtensionErrorTitle'),
        message,
        localStrings.getString('ok'),
        '',
        function() {
          ExtensionSettings.showOverlay($('packExtensionOverlay'));
        },
        null);
    ExtensionSettings.showOverlay($('alertOverlay'));
  };

  // Export
  return {
    PackExtensionOverlay: PackExtensionOverlay
  };
});

// Update the C++ call so this isn't necessary.
var PackExtensionOverlay = extensions.PackExtensionOverlay;
