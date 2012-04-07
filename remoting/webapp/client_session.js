// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * Class handling creation and teardown of a remoting client session.
 *
 * This abstracts a <embed> element and controls the plugin which does the
 * actual remoting work.  There should be no UI code inside this class.  It
 * should be purely thought of as a controller of sorts.
 */

'use strict';

/** @suppress {duplicate} */
var remoting = remoting || {};

/**
 * @param {string} hostJid The jid of the host to connect to.
 * @param {string} hostPublicKey The base64 encoded version of the host's
 *     public key.
 * @param {string} sharedSecret The access code for IT2Me or the PIN
 *     for Me2Me.
 * @param {string} authenticationMethods Comma-separated list of
 *     authentication methods the client should attempt to use.
 * @param {string} authenticationTag A host-specific tag to mix into
 *     authentication hashes.
 * @param {string} email The username for the talk network.
 * @param {remoting.ClientSession.Mode} mode The mode of this connection.
 * @param {function(remoting.ClientSession.State,
                    remoting.ClientSession.State):void} onStateChange
 *     The callback to invoke when the session changes state.
 * @constructor
 */
remoting.ClientSession = function(hostJid, hostPublicKey, sharedSecret,
                                  authenticationMethods, authenticationTag,
                                  email, mode, onStateChange) {
  this.state = remoting.ClientSession.State.CREATED;

  this.hostJid = hostJid;
  this.hostPublicKey = hostPublicKey;
  this.sharedSecret = sharedSecret;
  this.authenticationMethods = authenticationMethods;
  this.authenticationTag = authenticationTag;
  this.email = email;
  this.mode = mode;
  this.clientJid = '';
  this.sessionId = '';
  /** @type {remoting.ViewerPlugin} */
  this.plugin = null;
  this.scaleToFit = false;
  this.logToServer = new remoting.LogToServer();
  this.onStateChange = onStateChange;
  /** @type {remoting.ClientSession} */
  var that = this;
  /** @type {function():void} @private */
  this.refocusPlugin_ = function() { that.plugin.focus(); };
};

// Note that the positive values in both of these enums are copied directly
// from chromoting_scriptable_object.h and must be kept in sync. The negative
// values represent states transitions that occur within the web-app that have
// no corresponding plugin state transition.
/** @enum {number} */
remoting.ClientSession.State = {
  CREATED: -3,
  BAD_PLUGIN_VERSION: -2,
  UNKNOWN_PLUGIN_ERROR: -1,
  UNKNOWN: 0,
  CONNECTING: 1,
  INITIALIZING: 2,
  CONNECTED: 3,
  CLOSED: 4,
  CONNECTION_FAILED: 5
};

/** @enum {number} */
remoting.ClientSession.ConnectionError = {
  NONE: 0,
  HOST_IS_OFFLINE: 1,
  SESSION_REJECTED: 2,
  INCOMPATIBLE_PROTOCOL: 3,
  NETWORK_FAILURE: 4
};

// The mode of this session.
/** @enum {number} */
remoting.ClientSession.Mode = {
  IT2ME: 0,
  ME2ME: 1
};

// Keys for connection statistics.
remoting.ClientSession.STATS_KEY_VIDEO_BANDWIDTH = 'video_bandwidth';
remoting.ClientSession.STATS_KEY_VIDEO_FRAME_RATE = 'video_frame_rate';
remoting.ClientSession.STATS_KEY_CAPTURE_LATENCY = 'capture_latency';
remoting.ClientSession.STATS_KEY_ENCODE_LATENCY = 'encode_latency';
remoting.ClientSession.STATS_KEY_DECODE_LATENCY = 'decode_latency';
remoting.ClientSession.STATS_KEY_RENDER_LATENCY = 'render_latency';
remoting.ClientSession.STATS_KEY_ROUNDTRIP_LATENCY = 'roundtrip_latency';

/**
 * The current state of the session.
 * @type {remoting.ClientSession.State}
 */
remoting.ClientSession.prototype.state = remoting.ClientSession.State.UNKNOWN;

/**
 * The last connection error. Set when state is set to CONNECTION_FAILED.
 * @type {remoting.ClientSession.ConnectionError}
 */
remoting.ClientSession.prototype.error =
    remoting.ClientSession.ConnectionError.NONE;

/**
 * Chromoting session API version (for this javascript).
 * This is compared with the plugin API version to verify that they are
 * compatible.
 *
 * @const
 * @private
 */
remoting.ClientSession.prototype.API_VERSION_ = 4;

/**
 * The oldest API version that we support.
 * This will differ from the |API_VERSION_| if we maintain backward
 * compatibility with older API versions.
 *
 * @const
 * @private
 */
remoting.ClientSession.prototype.API_MIN_VERSION_ = 2;

/**
 * The id of the client plugin
 *
 * @const
 */
remoting.ClientSession.prototype.PLUGIN_ID = 'session-client-plugin';

/**
 * Callback to invoke when the state is changed.
 *
 * @param {remoting.ClientSession.State} oldState The previous state.
 * @param {remoting.ClientSession.State} newState The current state.
 */
remoting.ClientSession.prototype.onStateChange =
    function(oldState, newState) { };

/**
 * Adds <embed> element to |container| and readies the sesion object.
 *
 * @param {Element} container The element to add the plugin to.
 * @param {string} oauth2AccessToken A valid OAuth2 access token.
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.createPluginAndConnect =
    function(container, oauth2AccessToken) {
  this.plugin = /** @type {remoting.ViewerPlugin} */
      document.createElement('embed');
  this.plugin.id = this.PLUGIN_ID;
  this.plugin.src = 'about://none';
  this.plugin.type = 'pepper-application/x-chromoting';
  this.plugin.width = 0;
  this.plugin.height = 0;
  this.plugin.tabIndex = 0;  // Required, otherwise focus() doesn't work.
  container.appendChild(this.plugin);

  this.plugin.focus();
  this.plugin.addEventListener('blur', this.refocusPlugin_, false);

  if (!this.isPluginVersionSupported_(this.plugin)) {
    // TODO(ajwong): Remove from parent.
    delete this.plugin;
    this.setState_(remoting.ClientSession.State.BAD_PLUGIN_VERSION);
    return;
  }

  // Enable scale-to-fit if the plugin is new enough for high-quality scaling.
  this.setScaleToFit(this.plugin.apiVersion >= 3);

  /** @type {remoting.ClientSession} */ var that = this;
  /** @param {string} msg The IQ stanza to send. */
  this.plugin.sendIq = function(msg) { that.sendIq_(msg); };
  /** @param {string} msg The message to log. */
  this.plugin.debugInfo = function(msg) {
    remoting.debug.log('plugin: ' + msg);
  };

  // TODO(ajwong): Is it even worth having this class handle these events?
  // Or would it be better to just allow users to pass in their own handlers
  // and leave these blank by default?
  /**
   * @param {number} status The plugin status.
   * @param {number} error The plugin error status, if any.
   */
  this.plugin.connectionInfoUpdate = function(status, error) {
    that.connectionInfoUpdateCallback(status, error);
  };
  this.plugin.desktopSizeUpdate = function() { that.onDesktopSizeChanged_(); };

  // TODO(garykac): Clean exit if |connect| isn't a function.
  if (typeof this.plugin.connect === 'function') {
    this.connectPluginToWcs_(oauth2AccessToken);
  } else {
    remoting.debug.log('ERROR: remoting plugin not loaded');
    this.setState_(remoting.ClientSession.State.UNKNOWN_PLUGIN_ERROR);
  }
};

/**
 * Deletes the <embed> element from the container, without sending a
 * session_terminate request.  This is to be called when the session was
 * disconnected by the Host.
 *
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.removePlugin = function() {
  if (this.plugin) {
    this.plugin.removeEventListener('blur', this.refocusPlugin_, false);
    var parentNode = this.plugin.parentNode;
    parentNode.removeChild(this.plugin);
    this.plugin = null;
  }
};

/**
 * Deletes the <embed> element from the container and disconnects.
 *
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.disconnect = function() {
  // The plugin won't send a state change notification, so we explicitly log
  // the fact that the connection has closed.
  this.logToServer.logClientSessionStateChange(
      remoting.ClientSession.State.CLOSED,
      remoting.ClientSession.ConnectionError.NONE, this.mode);
  if (remoting.wcs) {
    remoting.wcs.setOnIq(function(stanza) {});
    this.sendIq_(
        '<cli:iq ' +
            'to="' + this.hostJid + '" ' +
            'type="set" ' +
            'id="session-terminate" ' +
            'xmlns:cli="jabber:client">' +
          '<jingle ' +
              'xmlns="urn:xmpp:jingle:1" ' +
              'action="session-terminate" ' +
              'initiator="' + this.clientJid + '" ' +
              'sid="' + this.sessionId + '">' +
            '<reason><success/></reason>' +
          '</jingle>' +
        '</cli:iq>');
  }
  this.removePlugin();
};

/**
 * Enables or disables the client's scale-to-fit feature.
 *
 * @param {boolean} scaleToFit True to enable scale-to-fit, false otherwise.
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.setScaleToFit = function(scaleToFit) {
  this.scaleToFit = scaleToFit;
  this.updateDimensions();
}

/**
 * Returns whether the client is currently scaling the host to fit the tab.
 *
 * @return {boolean} The current scale-to-fit setting.
 */
remoting.ClientSession.prototype.getScaleToFit = function() {
  return this.scaleToFit;
}

/**
 * Sends an IQ stanza via the http xmpp proxy.
 *
 * @private
 * @param {string} msg XML string of IQ stanza to send to server.
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.sendIq_ = function(msg) {
  remoting.debug.logIq(true, msg);
  // Extract the session id, so we can close the session later.
  var parser = new DOMParser();
  var iqNode = parser.parseFromString(msg, 'text/xml').firstChild;
  var jingleNode = iqNode.firstChild;
  if (jingleNode) {
    var action = jingleNode.getAttribute('action');
    if (jingleNode.nodeName == 'jingle' && action == 'session-initiate') {
      this.sessionId = jingleNode.getAttribute('sid');
    }
  }

  // Send the stanza.
  if (remoting.wcs) {
    remoting.wcs.sendIq(msg);
  } else {
    remoting.debug.log('Tried to send IQ before WCS was ready.');
    this.setState_(remoting.ClientSession.State.CONNECTION_FAILED);
  }
};

/**
 * @private
 * @param {remoting.ViewerPlugin} plugin The embed element for the plugin.
 * @return {boolean} True if the plugin and web-app versions are compatible.
 */
remoting.ClientSession.prototype.isPluginVersionSupported_ = function(plugin) {
  return this.API_VERSION_ >= plugin.apiMinVersion &&
      plugin.apiVersion >= this.API_MIN_VERSION_;
};

/**
 * Connects the plugin to WCS.
 *
 * @private
 * @param {string} oauth2AccessToken A valid OAuth2 access token.
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.connectPluginToWcs_ =
    function(oauth2AccessToken) {
  this.clientJid = remoting.wcs.getJid();
  if (this.clientJid == '') {
    remoting.debug.log('Tried to connect without a full JID.');
  }
  remoting.debug.setJids(this.clientJid, this.hostJid);
  /** @type {remoting.ClientSession} */
  var that = this;
  /** @param {string} stanza The IQ stanza received. */
  var onIq = function(stanza) {
    remoting.debug.logIq(false, stanza);
    if (that.plugin.onIq) {
      that.plugin.onIq(stanza);
    } else {
      // plugin.onIq may not be set after the plugin has been shut
      // down. Particularly this happens when we receive response to
      // session-terminate stanza.
      remoting.debug.log(
          'plugin.onIq is not set so dropping incoming message.');
    }
  }
  remoting.wcs.setOnIq(onIq);
  if (that.plugin.apiVersion < 4) {
    // Client plugin versions prior to 4 didn't support the last two
    // parameters.
    that.plugin.connect(this.hostJid, this.hostPublicKey, this.clientJid,
                        this.sharedSecret);
  } else {
    that.plugin.connect(this.hostJid, this.hostPublicKey, this.clientJid,
                        this.sharedSecret, this.authenticationMethods,
                        this.authenticationTag);
  }
};

/**
 * Callback that the plugin invokes to indicate that the connection
 * status has changed.
 *
 * @param {number} status The plugin's status.
 * @param {number} error The plugin's error state, if any.
 */
remoting.ClientSession.prototype.connectionInfoUpdateCallback =
    function(status, error) {
  // Old plugins didn't pass the status and error values, so get them directly.
  // Note that there is a race condition inherent in this approach.
  if (typeof(status) == 'undefined') {
    status = this.plugin.status;
  }
  if (typeof(error) == 'undefined') {
    error = this.plugin.error;
  }

  if (status == this.plugin.STATUS_CONNECTED) {
    this.onDesktopSizeChanged_();
  } else if (status == this.plugin.STATUS_FAILED) {
    this.error = /** @type {remoting.ClientSession.ConnectionError} */ (error);
  }
  this.setState_(/** @type {remoting.ClientSession.State} */ (status));
};

/**
 * @private
 * @param {remoting.ClientSession.State} newState The new state for the session.
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.setState_ = function(newState) {
  var oldState = this.state;
  this.state = newState;
  if (this.onStateChange) {
    this.onStateChange(oldState, newState);
  }
  this.logToServer.logClientSessionStateChange(this.state, this.error,
      this.mode);
};

/**
 * This is a callback that gets called when the window is resized.
 *
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.onResize = function() {
  this.updateDimensions();
};

/**
 * This is a callback that gets called when the plugin notifies us of a change
 * in the size of the remote desktop.
 *
 * @private
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.onDesktopSizeChanged_ = function() {
  remoting.debug.log('desktop size changed: ' +
                     this.plugin.desktopWidth + 'x' +
                     this.plugin.desktopHeight);
  this.updateDimensions();
};

/**
 * Refreshes the plugin's dimensions, taking into account the sizes of the
 * remote desktop and client window, and the current scale-to-fit setting.
 *
 * @return {void} Nothing.
 */
remoting.ClientSession.prototype.updateDimensions = function() {
  if (this.plugin.desktopWidth == 0 ||
      this.plugin.desktopHeight == 0)
    return;

  var windowWidth = window.innerWidth;
  var windowHeight = window.innerHeight;
  var scale = 1.0;

  if (this.getScaleToFit()) {
    var scaleFitHeight = 1.0 * windowHeight / this.plugin.desktopHeight;
    var scaleFitWidth = 1.0 * windowWidth / this.plugin.desktopWidth;
    scale = Math.min(1.0, scaleFitHeight, scaleFitWidth);
  }

  // Resize the plugin if necessary.
  this.plugin.width = this.plugin.desktopWidth * scale;
  this.plugin.height = this.plugin.desktopHeight * scale;

  // Position the container.
  // TODO(wez): We should take into account scrollbars when positioning.
  var parentNode = this.plugin.parentNode;
  if (this.plugin.width < windowWidth)
    parentNode.style.left = (windowWidth - this.plugin.width) / 2 + 'px';
  else
    parentNode.style.left = '0';
  if (this.plugin.height < windowHeight)
    parentNode.style.top = (windowHeight - this.plugin.height) / 2 + 'px';
  else
    parentNode.style.top = '0';

  remoting.debug.log('plugin dimensions: ' +
                     parentNode.style.left + ',' +
                     parentNode.style.top + '-' +
                     this.plugin.width + 'x' + this.plugin.height + '.');
  this.plugin.setScaleToFit(this.getScaleToFit());
};

/**
 * Returns an associative array with a set of stats for this connection.
 *
 * @return {Object.<string, number>} The connection statistics.
 */
remoting.ClientSession.prototype.stats = function() {
  var dict = {};
  dict[remoting.ClientSession.STATS_KEY_VIDEO_BANDWIDTH] =
      this.plugin.videoBandwidth;
  dict[remoting.ClientSession.STATS_KEY_VIDEO_FRAME_RATE] =
      this.plugin.videoFrameRate;
  dict[remoting.ClientSession.STATS_KEY_CAPTURE_LATENCY] =
      this.plugin.videoCaptureLatency;
  dict[remoting.ClientSession.STATS_KEY_ENCODE_LATENCY] =
      this.plugin.videoEncodeLatency;
  dict[remoting.ClientSession.STATS_KEY_DECODE_LATENCY] =
      this.plugin.videoDecodeLatency;
  dict[remoting.ClientSession.STATS_KEY_RENDER_LATENCY] =
      this.plugin.videoRenderLatency;
  dict[remoting.ClientSession.STATS_KEY_ROUNDTRIP_LATENCY] =
      this.plugin.roundTripLatency;
  return dict;
};

/**
 * Logs statistics.
 *
 * @param {Object.<string, number>} stats
 */
remoting.ClientSession.prototype.logStatistics = function(stats) {
  this.logToServer.logStatistics(stats, this.mode);
};
