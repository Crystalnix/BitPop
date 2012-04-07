// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('oobe', function() {
  /**
   * Creates a new oobe screen div.
   * @constructor
   * @extends {HTMLDivElement}
   */
  var OAuthEnrollmentScreen = cr.ui.define('div');

  /**
   * Registers with Oobe.
   */
  OAuthEnrollmentScreen.register = function() {
    var screen = $('oauth-enrollment');
    OAuthEnrollmentScreen.decorate(screen);
    Oobe.getInstance().registerScreen(screen);
    window.addEventListener('message',
                            screen.onMessage_.bind(screen), false);
  };

  /**
   * Switches between the different steps in the enrollment flow.
   * @param screen {string} the steps to show, one of "signin", "working",
   * "error", "success".
   */
  OAuthEnrollmentScreen.showStep = function(step) {
    $('oauth-enrollment').showStep(step);
  };

  /**
   * Sets an error message and switches to the error screen.
   * @param message {string} the error message.
   * @param retry {bool} whether the retry link should be shown.
   */
  OAuthEnrollmentScreen.showError = function(message, retry) {
    $('oauth-enrollment').showError(message, retry);
  };

  /**
   * Sets a progressing message and switches to the working screen.
   * @param message {string} the progress message.
   */

  OAuthEnrollmentScreen.showWorking = function(message) {
    $('oauth-enrollment').showWorking(message);
  };

  OAuthEnrollmentScreen.prototype = {
    __proto__: HTMLDivElement.prototype,

    /**
     * URL to load in the sign in frame.
     */
    signInUrl_: null,

    /**
     * Whether this is a manual or auto enrollment.
     */
    isAutoEnrollment_: false,

    /**
     * Enrollment steps with names and buttons to show.
     */
    steps_: null,

    /**
     * Dialog to confirm that auto-enrollment should really be cancelled.
     * This is only created the first time it's used.
     */
    confirmDialog_: null,

    /** @inheritDoc */
    decorate: function() {
      $('oauth-enroll-error-retry').addEventListener('click',
                                                     this.doRetry_.bind(this));
      $('oauth-enroll-cancel-auto-link').addEventListener(
          'click',
          this.confirmCancelAutoEnrollment_.bind(this));
      var links = document.querySelectorAll('.oauth-enroll-explain-link');
      for (var i = 0; i < links.length; i++) {
        links[i].addEventListener('click', this.showStep.bind(this, 'explain'));
      }
    },

    /**
     * Header text of the screen.
     * @type {string}
     */
    get header() {
      return localStrings.getString('oauthEnrollScreenTitle');
    },

    /**
     * Buttons in oobe wizard's button strip.
     * @type {array} Array of Buttons.
     */
    get buttons() {
      var buttons = [];

      var cancelButton = this.ownerDocument.createElement('button');
      cancelButton.id = 'oauth-enroll-cancel-button';
      cancelButton.textContent =
          localStrings.getString('oauthEnrollCancel');
      cancelButton.addEventListener('click', function(e) {
        chrome.send('oauthEnrollClose', ['cancel']);
      });
      buttons.push(cancelButton);

      var tryAgainButton = this.ownerDocument.createElement('button');
      tryAgainButton.id = 'oauth-enroll-try-again-button';
      tryAgainButton.hidden = true;
      tryAgainButton.textContent =
          localStrings.getString('oauthEnrollRetry');
      tryAgainButton.addEventListener('click', this.doRetry_.bind(this));
      buttons.push(tryAgainButton);

      var explainButton = this.ownerDocument.createElement('button');
      explainButton.id = 'oauth-enroll-explain-button';
      explainButton.hidden = true;
      explainButton.textContent =
          localStrings.getString('oauthEnrollExplainButton');
      explainButton.addEventListener('click', this.doRetry_.bind(this));
      buttons.push(explainButton);

      var doneButton = this.ownerDocument.createElement('button');
      doneButton.id = 'oauth-enroll-done-button';
      doneButton.hidden = true;
      doneButton.textContent =
          localStrings.getString('oauthEnrollDone');
      doneButton.addEventListener('click', function(e) {
        chrome.send('oauthEnrollClose', ['done']);
      });
      buttons.push(doneButton);

      return buttons;
    },

    /**
     * Event handler that is invoked just before the frame is shown.
     * @param data {dictionary} Screen init payload, contains the signin frame
     * URL.
     */
    onBeforeShow: function(data) {
      var url = data.signin_url;
      if (data.gaiaOrigin)
        url += '?gaiaOrigin=' + encodeURIComponent(data.gaiaOrigin);
      if (data.test_email) {
        url += '&test_email=' + encodeURIComponent(data.test_email);
        url += '&test_password=' + encodeURIComponent(data.test_password);
      }
      this.signInUrl_ = url;
      this.isAutoEnrollment_ = data.is_auto_enrollment;

      $('oauth-enroll-signin-frame').contentWindow.location.href =
          this.signInUrl_;

      // The cancel button is not available during auto-enrollment.
      var cancel = this.isAutoEnrollment_ ? null : 'cancel';
      // During auto-enrollment the user must try again from the error screen.
      var error_cancel = this.isAutoEnrollment_ ? 'try-again' : 'cancel';
      this.steps_ = [
        { name: 'signin',
          button: cancel },
        { name: 'working',
          button: cancel },
        { name: 'error',
          button: error_cancel,
          focusButton: this.isAutoEnrollment_ },
        { name: 'explain',
          button: 'explain',
          focusButton: true },
        { name: 'success',
          button: 'done',
          focusButton: true },
      ];

      var links = document.querySelectorAll('.oauth-enroll-explain-link');
      for (var i = 0; i < links.length; i++)
        links[i].hidden = !this.isAutoEnrollment_;

      this.showStep('signin');
    },

    /**
     * Cancels enrollment and drops the user back to the login screen.
     */
    cancel: function() {
      if (!this.isAutoEnrollment_)
        chrome.send('oauthEnrollClose', ['cancel']);
    },

    /**
     * Switches between the different steps in the enrollment flow.
     * @param step {string} the steps to show, one of "signin", "working",
     * "error", "success".
     */
    showStep: function(step) {
      $('oauth-enroll-cancel-button').hidden = true;
      $('oauth-enroll-try-again-button').hidden = true;
      $('oauth-enroll-explain-button').hidden = true;
      $('oauth-enroll-done-button').hidden = true;
      for (var i = 0; i < this.steps_.length; i++) {
        var theStep = this.steps_[i];
        var active = (theStep.name == step);
        $('oauth-enroll-step-' + theStep.name).hidden = !active;
        if (active && theStep.button) {
          var button = $('oauth-enroll-' + theStep.button + '-button');
          button.hidden = false;
          if (theStep.focusButton)
            button.focus();
        }
      }
    },

    /**
     * Sets an error message and switches to the error screen.
     * @param message {string} the error message.
     * @param retry {bool} whether the retry link should be shown.
     */
    showError: function(message, retry) {
      $('oauth-enroll-error-message').textContent = message;
      $('oauth-enroll-error-retry').hidden = !retry || this.isAutoEnrollment_;
      this.showStep('error');
    },

    /**
     * Sets a progressing message and switches to the working screen.
     * @param message {string} the progress message.
     */
    showWorking: function(message) {
      $('oauth-enroll-working-message').textContent = message;
      this.showStep('working');
    },

    /**
     * Retries the enrollment process after an error occurred in a previous
     * attempt. This goes to the C++ side through |chrome| first to clean up the
     * profile, so that the next attempt is performed with a clean state.
     */
    doRetry_: function() {
      chrome.send('oauthEnrollRetry', []);
    },

    /**
     * Handler for cancellations of an enforced auto-enrollment.
     */
    confirmCancelAutoEnrollment_: function() {
      if (!this.confirmDialog_) {
        this.confirmDialog_ = new cr.ui.dialogs.ConfirmDialog(document.body);
        this.confirmDialog_.setOkLabel(
            localStrings.getString('oauthEnrollCancelAutoEnrollmentConfirm'));
        this.confirmDialog_.setCancelLabel(
            localStrings.getString('oauthEnrollCancelAutoEnrollmentGoBack'));
        this.confirmDialog_.setInitialFocusOnCancel();
      }
      this.confirmDialog_.show(
          localStrings.getString('oauthEnrollCancelAutoEnrollmentReally'),
          this.onConfirmCancelAutoEnrollment_.bind(this));
    },

    /**
     * Handler for confirmation of cancellation of auto-enrollment.
     */
    onConfirmCancelAutoEnrollment_: function() {
      chrome.send('oauthEnrollClose', ['autocancel']);
    },

    /**
     * Checks if a given HTML5 message comes from the URL loaded into the signin
     * frame.
     * @param m {object} HTML5 message.
     * @type {bool} whether the message comes from the signin frame.
     */
    isSigninMessage_: function(m) {
      return this.signInUrl_ != null &&
          this.signInUrl_.indexOf(m.origin) == 0 &&
          m.source == $('oauth-enroll-signin-frame').contentWindow;
    },

    /**
     * Event handler for HTML5 messages.
     * @param m {object} HTML5 message.
     */
    onMessage_: function(m) {
      var msg = m.data;
      if (msg.method == 'completeLogin' && this.isSigninMessage_(m))
        chrome.send('oauthEnrollCompleteLogin', [ msg.email, msg.password ]);
    }
  };

  return {
    OAuthEnrollmentScreen: OAuthEnrollmentScreen
  };
});
