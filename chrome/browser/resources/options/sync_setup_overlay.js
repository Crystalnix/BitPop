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
      $('chooseDataTypesForm').onsubmit = function() {
        self.sendConfiguration_();
        return false;
      };
      $('google-option').onchange = $('explicit-option').onchange = function() {
        self.onRadioChange_();
      };
      $('choose-datatypes-cancel').onclick = $('sync-setup-cancel').onclick =
          $('confirm-everything-cancel').onclick = function() {
        self.closeOverlay_();
      };
      $('customize-link').onclick = function() {
        self.showCustomizePage_(false);
      };
      $('confirm-everything-ok').onclick = function() {
        self.sendConfiguration_();
      };
      $('use-default-link').onclick = function() {
        self.showSyncEverythingPage_();
      };
      $('cancel-no-button').onclick = function() {
        self.hideCancelWarning_();
        return false;
      };
      $('cancel-yes-button').onclick = function() {
        chrome.send('PassphraseCancel', ['']);
        return false;
      };
      $('passphraseForm').onsubmit = $('passphrase-ok').onclick = function() {
        self.sendPassphraseAndClose_();
        return false;
      };
      $('passphrase-cancel').onclick = function() {
        self.showCancelWarning_();
        return false;
      };
    },

    closeOverlay_: function() {
      OptionsPage.closeOverlay();
    },

    /** @inheritDoc */
    didShowPage: function() {
      chrome.send('didShowPage');
    },

    /** @inheritDoc */
    didClosePage: function() {
      chrome.send('didClosePage');
    },

    showCancelWarning_: function() {
      $('cancel-warning-box').hidden = false;
      $('passphrase-ok').disabled = true;
      $('passphrase-cancel').disabled = true;
      $('cancel-no-button').focus();
    },

    sendPassphraseAndClose_: function() {
      var f = $('passphraseForm');
      var result = JSON.stringify({"passphrase": f.passphrase.value});
      chrome.send("Passphrase", [result]);
    },

    getRadioCheckedValue_: function() {
      var f = $('chooseDataTypesForm');
      for (var i = 0; i < f.option.length; ++i) {
        if (f.option[i].checked) {
          return f.option[i].value;
        }
      }

      return undefined;
    },

    // TODO(jhawkins): Remove this method.
    switchToMode_: function(mode) {
      if (mode == "google")
        $('sync-custom-passphrase').hidden = true;
      else if (mode =="explicit")
        $('sync-custom-passphrase').hidden = false;
    },

    onRadioChange_: function() {
      this.switchToMode_(this.getRadioCheckedValue_());
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
      var emptyError = $('emptyerror');
      var mismatchError = $('mismatcherror');
      emptyError.style.display = "none";
      mismatchError.style.display = "none";

      var f = $('chooseDataTypesForm');
      if (this.getRadioCheckedValue_() != "explicit" || f.option[0].disabled)
        return true;

      var customPassphrase = $('custom-passphrase');
      if (customPassphrase.value.length == 0) {
        emptyError.style.display = "block";
        return false;
      }

      var confirmPassphrase = $('confirm-passphrase');
      if (confirmPassphrase.value != customPassphrase.value) {
        mismatchError.style.display = "block";
        return false;
      }

      return true;
    },

    hideCancelWarning_: function() {
      $('cancel-warning-box').hidden = true;
      $('passphrase-ok').disabled = false;
      $('passphrase-cancel').disabled = false;
    },

    sendConfiguration_: function() {
      // Trying to submit, so hide previous errors.
      $('aborted-text').className = "sync-error-hide";
      $('error-text').className = "sync-error-hide";

      if (this.noDataTypesChecked_()) {
        $('error-text').className = "sync-error-show";
        return;
      }

      var f = $('chooseDataTypesForm');
      if (!this.checkPassphraseMatch_())
        return;

      // Don't allow the user to tweak the settings once we send the
      // configuration to the backend.
      this.disableConfigureElements_();

      var syncAll =
        document.getElementById('sync-select-datatypes').selectedIndex == 0;
      var customPassphrase = $('custom-passphrase');

      // These values need to be kept in sync with where they are read in
      // SyncSetupFlow::GetDataTypeChoiceData().
      var result = JSON.stringify({
          "keepEverythingSynced": syncAll,
          "syncBookmarks": syncAll || f.bookmarksCheckbox.checked,
          "syncPreferences": syncAll || f.preferencesCheckbox.checked,
          "syncThemes": syncAll || f.themesCheckbox.checked,
          "syncPasswords": syncAll || f.passwordsCheckbox.checked,
          "syncAutofill": syncAll || f.autofillCheckbox.checked,
          "syncExtensions": syncAll || f.extensionsCheckbox.checked,
          "syncTypedUrls": syncAll || f.typedUrlsCheckbox.checked,
          "syncApps": syncAll || f.appsCheckbox.checked,
          "syncSessions": syncAll || f.sessionsCheckbox.checked,
          "usePassphrase": (this.getRadioCheckedValue_() == 'explicit'),
          "passphrase": customPassphrase.value
      });
      chrome.send("Configure", [result]);
    },

    /**
     * Disables all input elements within the 'Customize Sync Preferences'
     * screen. This is used to prohibit the user from changing the inputs after
     * confirming the customized sync preferences.
     * @private
     */
    disableConfigureElements_: function() {
      var configureElements =
          $('customize-sync-preferences').querySelectorAll('input');
      for (var i = 0; i < configureElements.length; i++)
        configureElements[i].disabled = true;
    },

    setChooseDataTypesCheckboxes_: function(args) {
      // If this frame is on top, the focus should be on it, so pressing enter
      // submits this form.
      if (args.iframeToShow == 'configure') {
        $('choose-datatypes-ok').focus();
      }

      var datatypeSelect = document.getElementById('sync-select-datatypes');
      datatypeSelect.selectedIndex = args.keepEverythingSynced ? 0 : 1;

      $('bookmarksCheckbox').checked = args.syncBookmarks;
      $('preferencesCheckbox').checked = args.syncPreferences;
      $('themesCheckbox').checked = args.syncThemes;

      if (args.passwordsRegistered) {
        $('passwordsCheckbox').checked = args.syncPasswords;
        $('passwordsItem').className = "sync-item-show";
      } else {
        $('passwordsItem').className = "sync-item-hide";
      }
      if (args.autofillRegistered) {
        $('autofillCheckbox').checked = args.syncAutofill;
        $('autofillItem').className = "sync-item-show";
      } else {
        $('autofillItem').className = "sync-item-hide";
      }
      if (args.extensionsRegistered) {
        $('extensionsCheckbox').checked = args.syncExtensions;
        $('extensionsItem').className = "sync-item-show";
      } else {
        $('extensionsItem').className = "sync-item-hide";
      }
      if (args.typedUrlsRegistered) {
        $('typedUrlsCheckbox').checked = args.syncTypedUrls;
        $('omniboxItem').className = "sync-item-show";
      } else {
        $('omniboxItem').className = "sync-item-hide";
      }
      if (args.appsRegistered) {
        $('appsCheckbox').checked = args.syncApps;
        $('appsItem').className = "sync-item-show";
      } else {
        $('appsItem').className = "sync-item-hide";
      }

      this.setCheckboxesToKeepEverythingSynced_(args.keepEverythingSynced);
      if (args.sessionsRegistered) {
        $('sessionsCheckbox').checked = args.syncSessions;
        $('sessionsItem').className = "sync-item-show";
      } else {
        $('sessionsItem').className = "sync-item-hide";
      }
    },

    setEncryptionCheckboxes_: function(args) {
      if (args["usePassphrase"]) {
        $('explicit-option').checked = true;

        // The passphrase, once set, cannot be unset, but we show a reset link.
        $('explicit-option').disabled = true;
        $('google-option').disabled = true;
        $('sync-custom-passphrase').hidden = true;
      } else {
        $('google-option').checked = true;
      }

      this.switchToMode_("");
    },

    setErrorState_: function(args) {
      if (!args.was_aborted)
        return;

      $('aborted-text').className = "sync-error-show";
      $('choose-datatypes-ok').disabled = true;
      $('keepEverythingSyncedRadio').disabled = true;
      $('chooseDataTypesRadio').disabled = true;
    },

    setCheckboxesAndErrors_: function(args) {
      this.setChooseDataTypesCheckboxes_(args);
      this.setEncryptionCheckboxes_(args);
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

      $('sync-setup-configure').classList.remove('hidden');

      if (args) {
        this.setCheckboxesAndErrors_(args);

        // Whether to display the 'Sync everything' confirmation screen or the
        // customize data types screen.
        // TODO(jhawkins): Rename |keepEverythingSynced| to |syncAllDataTypes|.
        var syncEverything = args['syncEverything'];
        var syncAllDataTypes = args['keepEverythingSynced'];
        this.usePassphrase_ = args['usePassphrase'];
        if (syncEverything == false || syncAllDataTypes == false ||
            this.usePassphrase_) {
          this.showCustomizePage_(syncAllDataTypes);
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
        this.switchToMode_("google");
      }

      $('confirm-everything-ok').focus();
    },

    showCustomizePage_: function(syncEverything) {
      document.getElementById('confirm-sync-preferences').hidden = true;
      document.getElementById('customize-sync-preferences').hidden = false;

      // If the user has selected the 'Customize' page on initial set up, it's
      // likely he intends to change the data types.  Select the
      // 'Choose data types' option in this case.
      var index = syncEverything ? 0 : 1;
      document.getElementById('sync-select-datatypes').selectedIndex = index;
      this.setDataTypeCheckboxesEnabled_(!syncEverything);
      $('choose-datatypes-ok').focus();
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
        overlay.children[i].classList.add('hidden');

      if (page == 'login')
        this.showGaiaLogin_(args);
      else if (page == 'configure')
        this.showConfigure_(args);
      else if (page == 'passphrase')
        this.showPassphrase_(args);
      else if (page == 'done')
        this.closeOverlay_();
    },

    setThrobbersVisible_: function(visible) {
      var throbbers = document.getElementsByClassName("throbber");
        for (var i = 0; i < throbbers.length; i++)
          throbbers[i].style.visibility = visible ? "visible" : "hidden";
    },

    showPassphrase_: function(args) {
      $('sync-setup-passphrase').classList.remove('hidden');

      $('passphraseRejectedBody').style.display = "none";
      $('normalBody').style.display = "none";
      $('incorrectPassphrase').style.display = "none";

      if (args["passphrase_creation_rejected"]) {
        $('passphraseRejectedBody').style.display = "block";
      } else {
        $('normalBody').style.display = "block";
      }

      if (args["passphrase_setting_rejected"]) {
        $('incorrectPassphrase').style.display = "block";
      }

      $('passphrase').focus();
    },

    setElementDisplay_: function(id, display) {
      var d = document.getElementById(id);
      if (d)
        d.style.display = display;
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
      this.setElementDisplay_("password-row", "none");
      this.setElementDisplay_("email-row", "none");
      $('create-account-cell').style.visibility = "hidden";

      this.setElementDisplay_("access-code-label-row", "table-row");
      this.setElementDisplay_("access-code-input-row", "table-row");
      this.setElementDisplay_("access-code-help-row", "table-row");
      document.getElementById('access-code').disabled = false;
    },

    showCaptcha_: function(args) {
      this.captchaChallengeActive_ = true;

      // The captcha takes up lots of space, so make room.
      this.setElementDisplay_("top-blurb", "none");
      this.setElementDisplay_("top-blurb-error", "none");
      this.setElementDisplay_("create-account-div", "none");
      document.getElementById('create-account-cell').height = 0;

      // It's showtime for the captcha now.
      this.setElementDisplay_("captcha-div", "block");
      document.getElementById('gaia-email').disabled = true;
      document.getElementById('gaia-passwd').disabled = false;
      document.getElementById('captcha-value').disabled = false;
      document.getElementById('captcha-wrapper').style.backgroundImage =
          url(args.captchaUrl);
    },

    showGaiaLogin_: function(args) {
      $('sync-setup-login').classList.remove('hidden');

      document.getElementById('gaia-email').disabled = false;
      document.getElementById('gaia-passwd').disabled = false;

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
          email.style.display = 'none';
          var span = document.getElementById('email-readonly');
          span.appendChild(document.createTextNode(email.value));
          span.style.display = 'inline';
          this.setElementDisplay_("create-account-div", "none");
        }

        f.accessCode.disabled = true;
      }

      if (1 == args.error) {
        var access_code = document.getElementById('access-code');
        if (access_code.value && access_code.value != "") {
          this.setElementDisplay_("errormsg-0-access-code", 'block');
          this.showAccessCodeRequired_();
        } else {
          this.setElementDisplay_("errormsg-1-password", 'table-row');
        }
        this.setBlurbError_(args.error_message);
      } else if (3 == args.error) {
        this.setElementDisplay_("errormsg-0-connection", 'table-row');
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
      this.setElementDisplay_("errormsg-0-email", 'none');
      this.setElementDisplay_("errormsg-0-password", 'none');
      this.setElementDisplay_("errormsg-1-password", 'none');
      this.setElementDisplay_("errormsg-0-connection", 'none');
      this.setElementDisplay_("errormsg-0-access-code", 'none');
    },

    setBlurbError_: function(error_message) {
      if (this.captchaChallengeActive_)
        return;  // No blurb in captcha challenge mode.

      if (error_message) {
        document.getElementById('error-signing-in').style.display = 'none';
        document.getElementById('error-custom').style.display = 'inline';
        document.getElementById('error-custom').textContent = error_message;
      } else {
        document.getElementById('error-signing-in').style.display = 'inline';
        document.getElementById('error-custom').style.display = 'none';
      }

      $('top-blurb-error').style.visibility = "visible";
      document.getElementById('gaia-email').disabled = false;
      document.getElementById('gaia-passwd').disabled = false;
    },

    setErrorVisibility_: function() {
      this.resetErrorVisibility_();
      var f = $('gaia-login-form');
      var email = $('gaia-email');
      var passwd = $('gaia-passwd');
      if (null == email.value || "" == email.value) {
        this.setElementDisplay_("errormsg-0-email", 'table-row');
        this.setBlurbError_();
        return false;
      }
      if (null == passwd.value || "" == passwd.value) {
        this.setElementDisplay_("errormsg-0-password", 'table-row');
        this.setBlurbError_();
        return false;
      }
      if (!f.accessCode.disabled && (null == f.accessCode.value ||
          "" == f.accessCode.value)) {
        this.setElementDisplay_("errormsg-0-password", 'table-row');
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
      chrome.send("SubmitAuth", [result]);
    },

    showGaiaSuccessAndClose_: function() {
      $('sign-in').value = localStrings.getString('loginSuccess');
      setTimeout(this.closeOverlay_, 1600);
    },

    showSuccessAndSettingUp_: function() {
      $('sign-in').value = localStrings.getString('settingup');
    },

    /** @inheritDoc */
    shouldClose: function() {
      if (!$('cancel-warning-box').hidden) {
        chrome.send('PassphraseCancel', ['']);
        return true;
      } else if (!$('sync-setup-passphrase').classList.contains('hidden')) {
        // The Passphrase page is showing, and the use has pressed escape.
        // Activate the cancel logic in this case.
        this.showCancelWarning_();
        return false;
      }

      return true;
    },
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
