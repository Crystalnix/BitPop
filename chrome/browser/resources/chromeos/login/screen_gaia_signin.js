// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Oobe signin screen implementation.
 */

cr.define('login', function() {
  // Gaia loading time after which portal check should be fired.
  /** @const */ var GAIA_LOADING_PORTAL_SUSSPECT_TIME_SEC = 5;

  // Maximum Gaia loading time in seconds.
  /** @const */ var MAX_GAIA_LOADING_TIME_SEC = 60;

  /**
   * Creates a new sign in screen div.
   * @constructor
   * @extends {HTMLDivElement}
   */
  var GaiaSigninScreen = cr.ui.define('div');

  /**
   * Registers with Oobe.
   */
  GaiaSigninScreen.register = function() {
    var screen = $('gaia-signin');
    GaiaSigninScreen.decorate(screen);
    Oobe.getInstance().registerScreen(screen);
    window.addEventListener('message',
                            screen.onMessage_.bind(screen), false);
  };

  GaiaSigninScreen.prototype = {
    __proto__: HTMLDivElement.prototype,

    // Frame loading error code (0 - no error).
    error_: 0,

    // Authentication extension's start page URL.
    extensionUrl_: null,

    // Whether extension should be loaded silently.
    silentLoad_: false,

    // Number of times that we reload extension frame.
    retryCount_: 0,

    // Timer id of pending retry.
    retryTimer_: undefined,

    // Whether local version of Gaia page is used.
    // @type {boolean}
    isLocal: false,

    // Whether offline login is allowed.
    // @type {boolean}
    isOfflineAllowed: false,

    // Email of the user, which is logging in using offline mode.
    // @type {string}
    email: '',

    // Timer id of pending load.
    loadingTimer_: undefined,

    /** @inheritDoc */
    decorate: function() {
      this.frame_ = $('signin-frame');

      this.updateLocalizedContent();
    },

    /**
     * Header text of the screen.
     * @type {string}
     */
    get header() {
      return localStrings.getString('signinScreenTitle');
    },

    /**
     * Shows/hides loading UI.
     * @param {boolean} show True to show loading UI.
     * @private
     */
    showLoadingUI_: function(show) {
      $('gaia-loading').hidden = !show;
      this.frame_.hidden = show;

      // Sign-in right panel is hidden if all its items are hidden.
      $('signin-right').hidden = show ||
          ($('createAccount').hidden && $('guestSignin').hidden);
    },

    /**
     * Handler for Gaia loading suspiciously long timeout.
     * @private
     */
    onLoadingSuspiciouslyLong_: function() {
      if (this != Oobe.getInstance().currentScreen)
        return;
      chrome.send('fixCaptivePortal');
      this.loadingTimer_ = window.setTimeout(
          this.onLoadingTimeOut_.bind(this),
          (MAX_GAIA_LOADING_TIME_SEC - GAIA_LOADING_PORTAL_SUSSPECT_TIME_SEC) *
          1000);
    },

    /**
     * Handler for Gaia loading timeout.
     * @private
     */
    onLoadingTimeOut_: function() {
      this.loadingTimer_ = undefined;
      this.clearRetry_();
      $('error-message').showLoadingTimeoutError();
    },

    /**
     * Clears loading timer.
     * @private
     */
    clearLoadingTimer_: function() {
      if (this.loadingTimer_) {
        window.clearTimeout(this.loadingTimer_);
        this.loadingTimer_ = undefined;
      }
    },

    /**
     * Sets up loading timer.
     * @private
     */
    startLoadingTimer_: function() {
      this.clearLoadingTimer_();
      this.loadingTimer_ = window.setTimeout(
          this.onLoadingSuspiciouslyLong_.bind(this),
          GAIA_LOADING_PORTAL_SUSSPECT_TIME_SEC * 1000);
    },

    /**
     * Whether Gaia is loading.
     * @type {boolean}
     */
    get loading() {
      return !$('gaia-loading').hidden;
    },
    set loading(loading) {
      if (loading == this.loading)
        return;

      this.showLoadingUI_(loading);
    },

    /**
     * Event handler that is invoked just before the frame is shown.
     * @param {string} data Screen init payload. Url of auth extension start
     *                      page.
     */
    onBeforeShow: function(data) {
      // Announce the name of the screen, if accessibility is on.
      $('gaia-signin-aria-label').setAttribute(
          'aria-label', localStrings.getString('signinScreenTitle'));

      // Button header is always visible when sign in is presented.
      // Header is hidden once GAIA reports on successful sign in.
      Oobe.getInstance().headerHidden = false;
    },

    /**
     * Loads the authentication extension into the iframe.
     * @param {Object} data Extension parameters bag.
     * @private
     */
    loadAuthExtension_: function(data) {
      this.silentLoad_ = data.silentLoad;
      this.isLocal = data.isLocal;
      this.email = '';

      // Offline sign-in is only allowed for the case when users aren't shown
      // because there is no other way for an user to enter when device is
      // offline.
      this.isOfflineAllowed = !data.isShowUsers;

      this.updateAuthExtension_(data);

      var params = [];
      if (data.gaiaOrigin)
        params.push('gaiaOrigin=' + encodeURIComponent(data.gaiaOrigin));
      if (data.gaiaUrlPath)
        params.push('gaiaUrlPath=' + encodeURIComponent(data.gaiaUrlPath));
      if (data.hl)
        params.push('hl=' + encodeURIComponent(data.hl));
      if (data.localizedStrings) {
        var strings = data.localizedStrings;
        for (var name in strings) {
          params.push(name + '=' + encodeURIComponent(strings[name]));
        }
      }
      if (data.email)
        params.push('email=' + encodeURIComponent(data.email));
      if (data.test_email)
        params.push('test_email=' + encodeURIComponent(data.test_email));
      if (data.test_password)
        params.push('test_password=' + encodeURIComponent(data.test_password));

      var url = data.startUrl;
      if (params.length)
        url += '?' + params.join('&');

      if (data.forceReload || this.extensionUrl_ != url) {
        console.log('Opening extension: ' + data.url +
                    ', opt_email=' + data.email);

        this.error_ = 0;
        this.frame_.src = url;
        this.extensionUrl_ = url;

        this.loading = true;
        this.clearRetry_();
        this.startLoadingTimer_();
      } else if (this.loading) {
        if (this.error_) {
          // An error has occurred, so trying to reload.
          this.doReload();
        } else {
          console.log('Gaia is still loading.');
          // Nothing to do here. Just wait until the extension loads.
        }
      }
    },

    /**
     * Updates the authentication extension with new parameters, if needed.
     * @param {Object} data New extension parameters bag.
     * @private
     */
    updateAuthExtension_: function(data) {
      var reasonLabel = $('gaia-signin-reason');
      if (data.passwordChanged) {
        reasonLabel.textContent =
            localStrings.getString('signinScreenPasswordChanged');
        reasonLabel.hidden = false;
      } else {
        reasonLabel.hidden = true;
      }

      $('createAccount').hidden = !data.createAccount;
      $('guestSignin').hidden = !data.guestSignin;
    },

    /**
     * Checks if message comes from the loaded authentication extension.
     * @param {object} e Payload of the received HTML5 message.
     * @type {boolean}
     */
    isAuthExtMessage_: function(e) {
      return this.extensionUrl_ != null &&
          this.extensionUrl_.indexOf(e.origin) == 0 &&
          e.source == this.frame_.contentWindow;
    },

    /**
     * Event handler that is invoked when HTML5 message is received.
     * @param {object} e Payload of the received HTML5 message.
     */
    onMessage_: function(e) {
      if (!this.isAuthExtMessage_(e)) {
        console.log('GaiaSigninScreen.onMessage_: Unknown message origin, ' +
            'e.origin=' + e.origin);
        return;
      }

      var msg = e.data;
      console.log('GaiaSigninScreen.onMessage_: method=' + msg.method);

      if (msg.method == 'completeLogin') {
        chrome.send('completeLogin', [msg.email, msg.password]);
        this.loading = true;
        // Now that we're in logged in state header should be hidden.
        Oobe.getInstance().headerHidden = true;
      } else if (msg.method == 'loginUILoaded') {
        this.loading = false;
        $('error-message').update();
        this.clearLoadingTimer_();
        // Show deferred error bubble.
        if (this.errorBubble_) {
          this.showErrorBubble(this.errorBubble_[0], this.errorBubble_[1]);
          this.errorBubble_ = undefined;
        }
        this.clearRetry_();
        chrome.send('loginWebuiReady');
        chrome.send('loginVisible');
      } else if (msg.method == 'offlineLogin') {
        this.email = msg.email;
        chrome.send('authenticateUser', [msg.email, msg.password]);
        this.loading = true;
        Oobe.getInstance().headerHidden = true;
      }
    },

    /**
     * Clears input fields and switches to input mode.
     * @param {boolean} takeFocus True to take focus.
     * @param {boolean} forceOnline Whether online sign-in should be forced.
     * If |forceOnline| is false previously used sign-in type will be used.
     */
    reset: function(takeFocus, forceOnline) {
      // Reload and show the sign-in UI if needed.
      if (takeFocus) {
        if (!forceOnline && this.isLocal) {
          // Show 'Cancel' button to allow user to return to the main screen
          // (e.g. this makes sense when connection is back).
          Oobe.getInstance().headerHidden = false;
          $('add-user-header-bar-item').hidden = false;
          $('add-user-button').hidden = true;
          $('cancel-add-user-button').hidden = false;
          // Do nothing, since offline version is reloaded after an error comes.
        } else {
          Oobe.showSigninUI();
        }
      }
    },

    /**
     * Clears retry data.
     * @private
     */
    clearRetry_: function() {
      this.retryCount_ = 0;
      if (this.retryTimer_) {
        window.clearTimeout(this.retryTimer_);
        this.retryTimer_ = undefined;
      }
    },

    /**
     * Reloads extension frame.
     */
    doReload: function() {
      console.log('Reload auth extension frame.');
      this.error_ = 0;
      this.frame_.src = this.extensionUrl_;
      this.retryTimer_ = undefined;
      this.loading = true;
      this.startLoadingTimer_();
    },

    /**
     * Schedules extension frame reload.
     */
    scheduleRetry: function() {
      if (this.retryCount_ >= 3 || this.retryTimer_)
        return;

      /** @const */ var MAX_DELAY = 7200;  // 7200 seconds (i.e. 2 hours)
      /** @const */ var MIN_DELAY = 1;  // 1 second

      var delay = Math.pow(2, this.retryCount_) * 5;
      delay = Math.max(MIN_DELAY, Math.min(MAX_DELAY, delay)) * 1000;

      ++this.retryCount_;
      this.retryTimer_ = window.setTimeout(this.doReload.bind(this), delay);
      console.log('GaiaSigninScreen scheduleRetry in ' + delay + 'ms.');
    },

    /**
     * This method is called when a frame loading error appears.
     * @param {int} error Error code.
     */
    onFrameError: function(error) {
      this.error_ = error;
    },

    /**
     * Updates localized content of the screen that is not updated via template.
     */
    updateLocalizedContent: function() {
      $('createAccount').innerHTML = localStrings.getStringF(
        'createAccount',
        '<a id="createAccountLink" class="signin-link" href="#">',
        '</a>');
      $('guestSignin').innerHTML = localStrings.getStringF(
          'guestSignin',
          '<a id="guestSigninLink" class="signin-link" href="#">',
          '</a>');
      $('createAccountLink').onclick = function() {
        chrome.send('createAccount');
      };
      $('guestSigninLink').onclick = function() {
        chrome.send('launchIncognito');
      };
    },

    /**
     * Shows sign-in error bubble.
     * @param {number} loginAttempts Number of login attemps tried.
     * @param {HTMLElement} content Content to show in bubble.
     */
    showErrorBubble: function(loginAttempts, error) {
      if (this.isLocal) {
        $('add-user-button').hidden = true;
        $('cancel-add-user-button').hidden = false;
        // Reload offline version of the sign-in extension, which will show
        // error itself.
        chrome.send('offlineLogin', [this.email]);
      } else if (!this.loading) {
        $('bubble').showContentForElement($('login-box'), error,
                                          cr.ui.Bubble.Attachment.LEFT);
      } else {
        // Defer the bubble until the frame has been loaded.
        this.errorBubble_ = [loginAttempts, error];
      }
    }
  };

  /**
   * Loads the authentication extension into the iframe.
   * @param {Object} data Extension parameters bag.
   */
  GaiaSigninScreen.loadAuthExtension = function(data) {
    $('gaia-signin').loadAuthExtension_(data);
  };

  /**
   * Updates the authentication extension with new parameters, if needed.
   * @param {Object} data New extension parameters bag.
   */
  GaiaSigninScreen.updateAuthExtension = function(data) {
    $('gaia-signin').updateAuthExtension_(data);
  };

  return {
    GaiaSigninScreen: GaiaSigninScreen
  };
});
