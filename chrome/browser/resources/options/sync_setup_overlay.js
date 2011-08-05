// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  const OptionsPage = options.OptionsPage;

  // Variable to track if a captcha challenge was issued. If this gets set to
  // true, it stays that way until we are told about successful login from
  // the browser.  This means subsequent errors (like invalid password) are
  // rendered in the captcha state, which is basically identical except we
  // don't show the top error blurb "Error Signing in" or the "Create
  // account" link.
  var captchaChallengeActive_ = false;

  // True if the synced account uses a custom passphrase.
  var usePassphrase_ = false;

  /**
   * SyncSetupOverlay class
   * Encapsulated handling of the 'Sync Setup' overlay page.
   * @class
   */
  function SyncSetupOverlay() {
    OptionsPage.call(this, 'syncSetup',
                     templateData.syncSetupOverlayTitle,
                     'sync-setup-overlay');
  }

  cr.addSingletonGetter(SyncSetupOverlay);

  SyncSetupOverlay.prototype = {
    __proto__: OptionsPage.prototype,

    /**
     * Initializes the page.
     */
    initializePage: function() {
      OptionsPage.prototype.initializePage.call(this);

      var acct_text = $('gaia-account-text');
      var translated_text = acct_text.textContent;
      var posGoogle = translated_text.indexOf('Google');
      if (posGoogle != -1) {
        var ltr = templateData['textdirection'] == 'ltr';
        var googleIsAtEndOfSentence = posGoogle != 0;
        if (googleIsAtEndOfSentence == ltr) {
          // We're in ltr and in the translation the word 'Google' is AFTER the
          // word 'Account' OR we're in rtl and 'Google' is BEFORE 'Account'.
          var logo_td = $('gaia-logo');
          logo_td.parentNode.appendChild(logo_td);
        }
        acct_text.textContent = translated_text.replace('Google','');
      }

      var self = this;
      $('gaia-login-form').onsubmit = function() {
        self.sendCredentialsAndClose_();
        return false;
      };
      $('google-option').onchange = $('explicit-option').onchange = function() {
        self.onPassphraseRadioChanged_();
      };
      $('choose-datatypes-cancel').onclick = $('sync-setup-cancel').onclick =
          $('confirm-everything-cancel').onclick = function() {
        self.closeOverlay_();
      };
      $('customize-link').onclick = function() {
        self.showCustomizePage_(null, true);
      };
      $('confirm-everything-ok').onclick = function() {
        self.sendConfiguration_();
      };
      $('use-default-link').onclick = function() {
        self.showSyncEverythingPage_();
      };
    },

    showOverlay_: function() {
      OptionsPage.navigateToPage('syncSetup');
    },

    closeOverlay_: function() {
      OptionsPage.closeOverlay();
    },

    /** @inheritDoc */
    didShowPage: function() {
      chrome.send('SyncSetupAttachHandler');
    },

    /** @inheritDoc */
    didClosePage: function() {
      chrome.send('SyncSetupDidClosePage');
    },

    sendPassphraseAndClose_: function() {
      var f = $('choose-data-types-form');
      var result = JSON.stringify({"passphrase": f.passphrase.value});
      chrome.send('SyncSetupPassphrase', [result]);
      return false;
    },

    getEncryptionRadioCheckedValue_: function() {
      var f = $('choose-data-types-form');
      for (var i = 0; i < f.encrypt.length; ++i) {
        if (f.encrypt[i].checked) {
          return f.encrypt[i].value;
        }
      }

      return undefined;
    },

    getPassphraseRadioCheckedValue_: function() {
      var f = $('choose-data-types-form');
      for (var i = 0; i < f.option.length; ++i) {
        if (f.option[i].checked) {
          return f.option[i].value;
        }
      }

      return undefined;
    },

    disableEncryptionRadioGroup_: function() {
      var f = $('choose-data-types-form');
      for (var i = 0; i < f.encrypt.length; ++i)
        f.encrypt[i].disabled = true;
    },

    onPassphraseRadioChanged_: function() {
      var visible = this.getPassphraseRadioCheckedValue_() == "explicit";
      $('sync-custom-passphrase').hidden = !visible;
    },

    checkAllDataTypeCheckboxes_: function() {
      var checkboxes = document.getElementsByName("dataTypeCheckbox");
      for (var i = 0; i < checkboxes.length; i++) {
        // Only check the visible ones (since there's no way to uncheck
        // the invisible ones).
        if (checkboxes[i].parentElement.className == "sync-item-show") {
          checkboxes[i].checked = true;
        }
      }
    },

    setDataTypeCheckboxesEnabled_: function(enabled) {
      var checkboxes = document.getElementsByName("dataTypeCheckbox");
      var labels = document.getElementsByName("dataTypeLabel");
      for (var i = 0; i < checkboxes.length; i++) {
        checkboxes[i].disabled = !enabled;
        if (checkboxes[i].disabled) {
          labels[i].className = "sync-label-inactive";
        } else {
          labels[i].className = "sync-label-active";
        }
      }
    },

    setCheckboxesToKeepEverythingSynced_: function(value) {
      this.setDataTypeCheckboxesEnabled_(!value);
      if (value)
        this.checkAllDataTypeCheckboxes_();
    },

    // Returns true if at least one data type is enabled and no data types are
    // checked. (If all data type checkboxes are disabled, it's because "keep
    // everything synced" is checked.)
    noDataTypesChecked_: function() {
      var checkboxes = document.getElementsByName("dataTypeCheckbox");
      var atLeastOneChecked = false;
      var atLeastOneEnabled = false;
      for (var i = 0; i < checkboxes.length; i++) {
        if (!checkboxes[i].disabled &&
            checkboxes[i].parentElement.className == "sync-item-show") {
          atLeastOneEnabled = true;
          if (checkboxes[i].checked) {
            atLeastOneChecked = true;
          }
        }
      }

      return atLeastOneEnabled && !atLeastOneChecked;
    },

    checkPassphraseMatch_: function() {
      var emptyError = $('empty-error');
      var mismatchError = $('mismatch-error');
      emptyError.hidden = true;
      mismatchError.hidden = true;

      var f = $('choose-data-types-form');
      if (this.getPassphraseRadioCheckedValue_() != "explicit" ||
          f.option[0].disabled)
        return true;

      var customPassphrase = $('custom-passphrase');
      if (customPassphrase.value.length == 0) {
        emptyError.hidden = false;
        return false;
      }

      var confirmPassphrase = $('confirm-passphrase');
      if (confirmPassphrase.value != customPassphrase.value) {
        mismatchError.hidden = false;
        return false;
      }

      return true;
    },

    sendConfiguration_: function() {
      // Trying to submit, so hide previous errors.
      $('aborted-text').hidden = true;
      $('error-text').hidden = true;

      if (this.noDataTypesChecked_()) {
        $('error-text').hidden = false;
        return;
      }

      var f = $('choose-data-types-form');
      if (!this.checkPassphraseMatch_())
        return;

      // Don't allow the user to tweak the settings once we send the
      // configuration to the backend.
      this.setInputElementsDisabledState_(true);

      var syncAll =
        document.getElementById('sync-select-datatypes').selectedIndex == 0;
      var usePassphrase = this.getPassphraseRadioCheckedValue_() == 'explicit';
      var encryptAllData = this.getEncryptionRadioCheckedValue_() == 'all';

      // These values need to be kept in sync with where they are read in
      // SyncSetupFlow::GetDataTypeChoiceData().
      var result = JSON.stringify({
          "keepEverythingSynced": syncAll,
          "syncBookmarks": syncAll || $('bookmarks-checkbox').checked,
          "syncPreferences": syncAll || $('preferences-checkbox').checked,
          "syncThemes": syncAll || $('themes-checkbox').checked,
          "syncPasswords": syncAll || $('passwords-checkbox').checked,
          "syncAutofill": syncAll || $('autofill-checkbox').checked,
          "syncExtensions": syncAll || $('extensions-checkbox').checked,
          "syncTypedUrls": syncAll || $('typed-urls-checkbox').checked,
          "syncApps": syncAll || $('apps-checkbox').checked,
          "syncSessions": syncAll || $('sessions-checkbox').checked,
          "encryptAllData": encryptAllData,
          "usePassphrase": usePassphrase,
          "passphrase": $('custom-passphrase').value
      });
      chrome.send('SyncSetupConfigure', [result]);
    },

    /**
     * Sets the disabled property of all input elements within the 'Customize
     * Sync Preferences' screen. This is used to prohibit the user from changing
     * the inputs after confirming the customized sync preferences, or resetting
     * the state when re-showing the dialog.
     * @param disabled True if controls should be set to disabled.
     * @private
     */
    setInputElementsDisabledState_: function(disabled) {
      var configureElements =
          $('customize-sync-preferences').querySelectorAll('input');
      for (var i = 0; i < configureElements.length; i++)
        configureElements[i].disabled = disabled;
    },

    setChooseDataTypesCheckboxes_: function(args) {
      // If this frame is on top, the focus should be on it, so pressing enter
      // submits this form.
      if (args.iframeToShow == 'configure') {
        $('choose-datatypes-ok').focus();
      }

      var datatypeSelect = document.getElementById('sync-select-datatypes');
      datatypeSelect.selectedIndex = args.keepEverythingSynced ? 0 : 1;

      $('bookmarks-checkbox').checked = args.syncBookmarks;
      $('preferences-checkbox').checked = args.syncPreferences;
      $('themes-checkbox').checked = args.syncThemes;

      if (args.passwordsRegistered) {
        $('passwords-checkbox').checked = args.syncPasswords;
        $('passwords-item').className = "sync-item-show";
      } else {
        $('passwords-item').className = "sync-item-hide";
      }
      if (args.autofillRegistered) {
        $('autofill-checkbox').checked = args.syncAutofill;
        $('autofill-item').className = "sync-item-show";
      } else {
        $('autofill-item').className = "sync-item-hide";
      }
      if (args.extensionsRegistered) {
        $('extensions-checkbox').checked = args.syncExtensions;
        $('extensions-item').className = "sync-item-show";
      } else {
        $('extensions-item').className = "sync-item-hide";
      }
      if (args.typedUrlsRegistered) {
        $('typed-urls-checkbox').checked = args.syncTypedUrls;
        $('omnibox-item').className = "sync-item-show";
      } else {
        $('omnibox-item').className = "sync-item-hide";
      }
      if (args.appsRegistered) {
        $('apps-checkbox').checked = args.syncApps;
        $('apps-item').className = "sync-item-show";
      } else {
        $('apps-item').className = "sync-item-hide";
      }

      this.setCheckboxesToKeepEverythingSynced_(args.keepEverythingSynced);
      if (args.sessionsRegistered) {
        $('sessions-checkbox').checked = args.syncSessions;
        $('sessions-item').className = "sync-item-show";
      } else {
        $('sessions-item').className = "sync-item-hide";
      }
    },

    setEncryptionRadios_: function(args) {
      if (args['encryptAllData']) {
        $('encrypt-all-option').checked = true;
        this.disableEncryptionRadioGroup_();
      } else {
        $('encrypt-sensitive-option').checked = true;
      }
    },

    setPassphraseRadios_: function(args) {
      if (args['usePassphrase']) {
        $('explicit-option').checked = true;

        // The passphrase, once set, cannot be unset, but we show a reset link.
        $('explicit-option').disabled = true;
        $('google-option').disabled = true;
        $('sync-custom-passphrase').hidden = true;
      } else {
        $('google-option').checked = true;
      }
    },

    setErrorState_: function(args) {
      if (!args.was_aborted)
        return;

      $('aborted-text').hidden = false;
      $('choose-datatypes-ok').disabled = true;
    },

    setCheckboxesAndErrors_: function(args) {
      this.setChooseDataTypesCheckboxes_(args);
      this.setEncryptionRadios_(args);
      this.setPassphraseRadios_(args);
      this.setErrorState_(args);
    },

    // Called once, when this html/js is loaded.
    showConfigure_: function(args) {
      var datatypeSelect = document.getElementById('sync-select-datatypes');
      var self = this;
      datatypeSelect.onchange = function() {
        var syncAll = this.selectedIndex == 0;
        self.setCheckboxesToKeepEverythingSynced_(syncAll);
      };

      this.resetPage_('sync-setup-configure');
      $('sync-setup-configure').hidden = false;

      // onsubmit is changed when submitting a passphrase. Reset it to its
      // default.
      $('choose-data-types-form').onsubmit = function() {
        self.sendConfiguration_();
        return false;
      };

      if (args) {
        if (!args['encryptionEnabled'])
          $('customize-sync-encryption').hidden = true;
        this.setCheckboxesAndErrors_(args);

        // Whether to display the 'Sync everything' confirmation page or the
        // customize data types page.
        var keepEverythingSynced = args['keepEverythingSynced'];
        this.usePassphrase_ = args['usePassphrase'];
        if (args['showSyncEverythingPage'] == false || this.usePassphrase_ ||
            keepEverythingSynced == false || args['show_passphrase']) {
          this.showCustomizePage_(args, keepEverythingSynced);
        } else {
          this.showSyncEverythingPage_();
        }
      }
    },

    showSyncEverythingPage_: function() {
      $('confirm-sync-preferences').hidden = false;
      $('customize-sync-preferences').hidden = true;

      // Reset the selection to 'Sync everything'.
      $('sync-select-datatypes').selectedIndex = 0;

      // The default state is to sync everything.
      this.setCheckboxesToKeepEverythingSynced_(true);

      // If the account is not synced with a custom passphrase, reset the
      // passphrase radio when switching to the 'Sync everything' page.
      if (!this.usePassphrase_) {
        $('google-option').checked = true;
        $('sync-custom-passphrase').hidden = true;
      }

      $('confirm-everything-ok').focus();
    },

    /**
     * Reveals the UI for entering a custom passphrase after initial setup. This
     * may happen if the user forgot to enter the correct (or any) custom
     * passphrase during initial setup.
     * @param {Array} args The args that contain the passphrase UI
     *     configuration.
     * @private
     */
    showPassphraseContainer_: function(args) {
      $('choose-data-types-form').onsubmit =
          this.sendPassphraseAndClose_.bind(this);
      $('sync-custom-passphrase-container').hidden = true;
      $('sync-existing-passphrase-container').hidden = false;

      if (args["passphrase_creation_rejected"])
        $('passphrase-rejected-body').hidden = false;
      else
        $('normal-body').hidden = false;

      if (args["passphrase_setting_rejected"])
        $('incorrect-passphrase').hidden = false;

      $('sync-passphrase-warning').hidden = false;

      $('passphrase').focus();
    },

    showCustomizePage_: function(args, syncEverything) {
      $('confirm-sync-preferences').hidden = true;
      $('customize-sync-preferences').hidden = false;

      $('sync-custom-passphrase-container').hidden = false;
      $('sync-existing-passphrase-container').hidden = true;

      // If the user has selected the 'Customize' page on initial set up, it's
      // likely he intends to change the data types. Select the
      // 'Choose data types' option in this case.
      var index = syncEverything ? 0 : 1;
      document.getElementById('sync-select-datatypes').selectedIndex = index;
      this.setDataTypeCheckboxesEnabled_(!syncEverything);
      $('choose-datatypes-ok').focus();

      if (args && args['show_passphrase'])
        this.showPassphraseContainer_(args);
    },

    attach_: function() {
      chrome.send('SyncSetupAttachHandler');
    },

    showSyncSetupPage_: function(page, args) {
      if (page == 'settingUp') {
        this.setThrobbersVisible_(true);
        return;
      } else {
        this.setThrobbersVisible_(false);
      }

      // Hide an existing visible overlay.
      var overlay = $('sync-setup-overlay');
      for (var i = 0; i < overlay.children.length; i++)
        overlay.children[i].hidden = true;

      this.setInputElementsDisabledState_(false);

      if (page == 'login')
        this.showGaiaLogin_(args);
      else if (page == 'configure' || page == 'passphrase')
        this.showConfigure_(args);

      if (page == 'done')
        this.closeOverlay_();
      else
        this.showOverlay_();
    },

    setThrobbersVisible_: function(visible) {
      var throbbers = document.getElementsByClassName("throbber");
        for (var i = 0; i < throbbers.length; i++)
          throbbers[i].style.visibility = visible ? "visible" : "hidden";
    },

    loginSetFocus_: function() {
      var email = $('gaia-email');
      var passwd = $('gaia-passwd');
      if (email && (email.value == null || email.value == "")) {
        email.focus();
      } else if (passwd) {
        passwd.focus();
      }
    },

    showAccessCodeRequired_: function() {
      $('password-row').hidden = true;
      $('email-row').hidden = true;
      $('create-account-cell').style.visibility = "hidden";

      $('access-code-label-row').hidden = false;
      $('access-code-input-row').hidden = false;
      $('access-code-help-row').hidden = false;
      $('access-code').disabled = false;
    },

    showCaptcha_: function(args) {
      this.captchaChallengeActive_ = true;

      // The captcha takes up lots of space, so make room.
      $('top-blurb-error').hidden = true;
      $('create-account-div').hidden = true;
      $('create-account-cell').hidden = true;

      // It's showtime for the captcha now.
      $('captcha-div').hidden = false;
      $('gaia-email').disabled = true;
      $('gaia-passwd').disabled = false;
      $('captcha-value').disabled = false;
      $('captcha-wrapper').style.backgroundImage = url(args.captchaUrl);
    },

    /**
     * Reset the state of all descendant elements of a root element to their
     * initial state.
     * The initial state is specified by adding a class to the descendant
     * element in sync_setup_overlay.html.
     * @param pageElementId The root page element id.
     * @private
     */
    resetPage_: function(pageElementId) {
      var page = $(pageElementId);
      var forEach = function(arr, fn) {
        var length = arr.length;
        for (var i = 0; i < length; i++) {
          fn(arr[i]);
        }
      };

      forEach(page.getElementsByClassName('reset-hidden'),
          function(elt) { elt.hidden = true; });
      forEach(page.getElementsByClassName('reset-shown'),
          function(elt) { elt.hidden = false; });
      forEach(page.getElementsByClassName('reset-disabled'),
          function(elt) { elt.disabled = true; });
      forEach(page.getElementsByClassName('reset-enabled'),
          function(elt) { elt.disabled = false; });
      forEach(page.getElementsByClassName('reset-visibility-hidden'),
          function(elt) { elt.style.visibility = 'hidden'; });
      forEach(page.getElementsByClassName('reset-value'),
          function(elt) { elt.value = ''; });
    },

    showGaiaLogin_: function(args) {
      this.resetPage_('sync-setup-login');
      $('sync-setup-login').hidden = false;

      var f = $('gaia-login-form');
      var email = $('gaia-email');
      var passwd = $('gaia-passwd');
      if (f) {
        if (args.user != undefined) {
          if (email.value != args.user)
            passwd.value = ""; // Reset the password field
          email.value = args.user;
        }

        if (!args.editable_user) {
          email.hidden = true;
          var span = $('email-readonly');
          span.textContent = email.value;
          span.hidden = false;
          $('create-account-div').hidden = true;
        }

        f.accessCode.disabled = true;
      }

      if (1 == args.error) {
        var access_code = document.getElementById('access-code');
        if (access_code.value && access_code.value != "") {
          $('errormsg-0-access-code').hidden = false;
          this.showAccessCodeRequired_();
        } else {
          $('errormsg-1-password').hidden = false;
        }
        this.setBlurbError_(args.error_message);
      } else if (3 == args.error) {
        $('errormsg-0-connection').hidden = false;
        this.setBlurbError_(args.error_message);
      } else if (4 == args.error) {
        this.showCaptcha_(args);
      } else if (8 == args.error) {
        this.showAccessCodeRequired_();
      } else if (args.error_message) {
        this.setBlurbError_(args.error_message);
      }

      $('sign-in').disabled = false;
      $('sign-in').value = templateData['signin'];
      this.loginSetFocus_();
    },

    resetErrorVisibility_: function() {
      $("errormsg-0-email").hidden = true;
      $("errormsg-0-password").hidden = true;
      $("errormsg-1-password").hidden = true;
      $("errormsg-0-connection").hidden = true;
      $("errormsg-0-access-code").hidden = true;
    },

    setBlurbError_: function(error_message) {
      if (this.captchaChallengeActive_)
        return;  // No blurb in captcha challenge mode.

      if (error_message) {
        $('error-signing-in').hidden = true;
        $('error-custom').hidden = false;
        $('error-custom').textContent = error_message;
      } else {
        $('error-signing-in').hidden = false;
        $('error-custom').hidden = true;
      }

      $('top-blurb-error').style.visibility = "visible";
      $('gaia-email').disabled = false;
      $('gaia-passwd').disabled = false;
    },

    setErrorVisibility_: function() {
      this.resetErrorVisibility_();
      var f = $('gaia-login-form');
      var email = $('gaia-email');
      var passwd = $('gaia-passwd');
      if (null == email.value || "" == email.value) {
        $('errormsg-0-email').hidden = false;
        this.setBlurbError_();
        return false;
      }
      // Don't enforce password being non-blank when checking access code (it
      // will have been cleared when the page was displayed).
      if (f.accessCode.disabled && (null == passwd.value ||
          "" == passwd.value)) {
        $('errormsg-0-password').hidden = false;
        this.setBlurbError_();
        return false;
      }
      if (!f.accessCode.disabled && (null == f.accessCode.value ||
          "" == f.accessCode.value)) {
        $('errormsg-0-password').hidden = false;
        return false;
      }
      return true;
    },

    sendCredentialsAndClose_: function() {
      if (!this.setErrorVisibility_()) {
        return false;
      }

      $('gaia-email').disabled = true;
      $('gaia-passwd').disabled = true;
      $('captcha-value').disabled = true;
      $('access-code').disabled = true;

      $('logging-in-throbber').style.visibility = "visible";

      var f = $('gaia-login-form');
      var email = $('gaia-email');
      var passwd = $('gaia-passwd');
      var result = JSON.stringify({"user" : email.value,
                                   "pass" : passwd.value,
                                   "captcha" : f.captchaValue.value,
                                   "access_code" : f.accessCode.value});
      $('sign-in').disabled = true;
      chrome.send('SyncSetupSubmitAuth', [result]);
    },

    showSuccessAndClose_: function() {
      $('sign-in').value = localStrings.getString('loginSuccess');
      setTimeout(this.closeOverlay_, 1600);
    },

    showSuccessAndSettingUp_: function() {
      $('sign-in').value = localStrings.getString('settingUp');
    },

    /** @inheritDoc */
    shouldClose: function() {
      if (!$('cancel-warning-box').hidden) {
        chrome.send('SyncSetupPassphraseCancel', ['']);
        return true;
      } else if (!$('sync-setup-passphrase').hidden) {
        // The Passphrase page is showing, and the use has pressed escape.
        // Activate the cancel logic in this case.
        this.showCancelWarning_();
        return false;
      }

      return true;
    },
  };

  SyncSetupOverlay.showSyncDialog = function() {
    SyncSetupOverlay.getInstance().attach_();
  };

  SyncSetupOverlay.showSyncSetupPage = function(page, args) {
    SyncSetupOverlay.getInstance().showSyncSetupPage_(page, args);
  };

  SyncSetupOverlay.showSuccessAndClose = function() {
    SyncSetupOverlay.getInstance().showSuccessAndClose_();
  };

  SyncSetupOverlay.showSuccessAndSettingUp = function() {
    SyncSetupOverlay.getInstance().showSuccessAndSettingUp_();
  };

  // Export
  return {
    SyncSetupOverlay: SyncSetupOverlay
  };
});
