// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('ntp', function() {
  'use strict';

  var Thumbnail = ntp.Thumbnail;
  var ThumbnailPage = ntp.ThumbnailPage;

  /**
   * Creates a new Most Visited object for tiling.
   * @param {Object=} opt_data The data representing the most visited page.
   * @constructor
   * @extends {Thumbnail}
   * @extends {HTMLAnchorElement}
   */
  function MostVisited(opt_data) {
    var el = cr.doc.createElement('a');
    el.__proto__ = MostVisited.prototype;
    el.initialize();

    if (opt_data)
      el.data = opt_data;

    return el;
  }

  MostVisited.prototype = {
    __proto__: Thumbnail.prototype,

    /**
     * Initializes a MostVisited Thumbnail.
     */
    initialize: function() {
      Thumbnail.prototype.initialize.apply(this, arguments);

      this.addEventListener('click', this.handleClick_);
      this.addEventListener('keydown', this.handleKeyDown_);
      this.addEventListener('carddeselected', this.handleCardDeselected_);
      this.addEventListener('cardselected', this.handleCardSelected_);
    },

    /**
     * Clears the DOM hierarchy for this node, setting it back to the default
     * for a blank thumbnail.
     */
    reset: function() {
      Thumbnail.prototype.reset.apply(this, arguments);

      var closeButton = cr.doc.createElement('div');
      closeButton.className = 'close-button';
      closeButton.title = loadTimeData.getString('removethumbnailtooltip');
      this.appendChild(closeButton);
    },

    /**
     * Update the appearance of this tile according to |data|.
     * @param {Object} data A dictionary of relevant data for the page.
     */
    set data(data) {
      Object.getOwnPropertyDescriptor(Thumbnail.prototype, 'data').set.apply(
          this, arguments);

      if (this.classList.contains('blacklisted') && data) {
        // Animate appearance of new tile.
        this.classList.add('new-tile-contents');
      }
      this.classList.remove('blacklisted');
    },
    get data() {
      return this.data_;
    },

    /**
     * Handles a click on the tile.
     * @param {Event} e The click event.
     * @private
     */
    handleClick_: function(e) {
      if (e.target.classList.contains('close-button')) {
        this.blacklist_();
        e.preventDefault();
      } else {
        ntp.logTimeToClickAndHoverCount('MostVisited');
        // Records an app launch from the most visited page (Chrome will decide
        // whether the url is an app). TODO(estade): this only works for clicks;
        // other actions like "open in new tab" from the context menu won't be
        // recorded. Can this be fixed?
        chrome.send('recordAppLaunchByURL',
                    [encodeURIComponent(this.href),
                     ntp.APP_LAUNCH.NTP_MOST_VISITED]);
        // Records the index of this tile.
        chrome.send('metricsHandler:recordInHistogram',
                    ['NewTabPage.MostVisited', this.index, 8]);
        chrome.send('mostVisitedAction',
                    [ntp.NtpFollowAction.CLICKED_TILE]);
      }
    },

    /**
     * Allow blacklisting most visited site using the keyboard.
     * @private
     */
    handleKeyDown_: function(e) {
      if (!cr.isMac && e.keyCode == 46 || // Del
          cr.isMac && e.metaKey && e.keyCode == 8) { // Cmd + Backspace
        this.blacklist_();
      }
    },

    /**
     * Permanently removes a page from Most Visited.
     * @private
     */
    blacklist_: function() {
      this.tileCell.tilePage.setTileRepositioningState(this.index, true);
      this.showUndoNotification_();
      chrome.send('blacklistURLFromMostVisited', [this.data_.url]);
      this.classList.add('blacklisted');
    },

    /**
     * Shows the undo notification when blacklisting a most visited site.
     * @private
     */
    showUndoNotification_: function() {
      var data = this.data_;
      var tilePage = this.tileCell.tilePage;
      var index = this.index;
      var doUndo = function() {
        tilePage.setTileRepositioningState(index, false);
        chrome.send('removeURLsFromMostVisitedBlacklist', [data.url]);
      };

      var undo = {
        action: doUndo,
        text: loadTimeData.getString('undothumbnailremove'),
      };

      var undoAll = {
        action: function() {
          chrome.send('clearMostVisitedURLsBlacklist');
        },
        text: loadTimeData.getString('restoreThumbnailsShort'),
      };

      ntp.showNotification(
          loadTimeData.getString('thumbnailremovednotification'),
          [undo, undoAll]);
    },

    /**
     * Returns whether this element can be 'removed' from chrome.
     * @return {boolean} True, since most visited pages can always be
     *     blacklisted.
     */
    canBeRemoved: function() {
      return true;
    },
  };

  /**
   * Creates a new MostVisitedPage object.
   * @constructor
   * @extends {ThumbnailPage}
   */
  function MostVisitedPage() {
    var el = new ThumbnailPage();
    el.__proto__ = MostVisitedPage.prototype;
    el.initialize();

    return el;
  }

  MostVisitedPage.prototype = {
    __proto__: ThumbnailPage.prototype,

    TileClass: MostVisited,

    /**
     * Initializes a MostVisitedPage.
     */
    initialize: function() {
      ThumbnailPage.prototype.initialize.apply(this, arguments);

      this.classList.add('most-visited-page');
    },

    /**
     * Handles the 'card deselected' event (i.e. the user clicked to another
     * pane).
     * @private
     * @param {Event} e The CardChanged event.
     */
    handleCardDeselected_: function(e) {
      if (!document.documentElement.classList.contains('starting-up')) {
        chrome.send('mostVisitedAction',
                    [ntp.NtpFollowAction.CLICKED_OTHER_NTP_PANE]);
      }
    },

    /**
     * Handles the 'card selected' event (i.e. the user clicked to select the
     * this page's pane).
     * @private
     * @param {Event} e The CardChanged event.
     */
    handleCardSelected_: function(e) {
      if (!document.documentElement.classList.contains('starting-up'))
        chrome.send('mostVisitedSelected');
    },

    /** @override */
    setDataList: function(dataList) {
      var startTime = Date.now();
      ThumbnailPage.prototype.setDataList.apply(this, arguments);
      this.updateGrid();
      logEvent('mostVisited.layout: ' + (Date.now() - startTime));
    },
  };

  /**
   * Executed once the NTP has loaded. Checks if the Most Visited pane is
   * shown or not. If it is shown, the 'mostVisitedSelected' message is sent
   * to the C++ code, to record the fact that the user has seen this pane.
   */
  MostVisitedPage.onLoaded = function() {
    if (ntp.getCardSlider() &&
        ntp.getCardSlider().currentCardValue &&
        ntp.getCardSlider().currentCardValue.classList
        .contains('most-visited-page')) {
      chrome.send('mostVisitedSelected');
    }
  };

  return {
    MostVisitedPage: MostVisitedPage,
  };
});

document.addEventListener('ntpLoaded', ntp.MostVisitedPage.onLoaded);
