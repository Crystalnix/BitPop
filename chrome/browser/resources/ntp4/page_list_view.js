// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview PageListView implementation.
 * PageListView manages page list, dot list, switcher buttons and handles apps
 * pages callbacks from backend.
 *
 * Note that you need to have AppLauncherHandler in your WebUI to use this code.
 */

cr.define('ntp4', function() {
  'use strict';

  /**
   * Object for accessing localized strings.
   * @type {!LocalStrings}
   */
  var localStrings = new LocalStrings;

  /**
   * Creates a PageListView object.
   * @constructor
   * @extends {Object}
   */
  function PageListView() {
  }

  PageListView.prototype = {
    /**
     * The CardSlider object to use for changing app pages.
     * @type {CardSlider|undefined}
     */
    cardSlider: undefined,

    /**
     * The frame div for this.cardSlider.
     * @type {!Element|undefined}
     */
    sliderFrame: undefined,

    /**
     * The 'page-list' element.
     * @type {!Element|undefined}
     */
    pageList: undefined,

    /**
     * A list of all 'tile-page' elements.
     * @type {!NodeList|undefined}
     */
    tilePages: undefined,

    /**
     * A list of all 'apps-page' elements.
     * @type {!NodeList|undefined}
     */
    appsPages: undefined,

    /**
     * The Most Visited page.
     * @type {!Element|undefined}
     */
    mostVisitedPage: undefined,

    /**
     * The 'dots-list' element.
     * @type {!Element|undefined}
     */
    dotList: undefined,

    /**
     * The left and right paging buttons.
     * @type {!Element|undefined}
     */
    pageSwitcherStart: undefined,
    pageSwitcherEnd: undefined,

    /**
     * The 'trash' element.  Note that technically this is unnecessary,
     * JavaScript creates the object for us based on the id.  But I don't want
     * to rely on the ID being the same, and JSCompiler doesn't know about it.
     * @type {!Element|undefined}
     */
    trash: undefined,

    /**
     * The type of page that is currently shown. The value is a numerical ID.
     * @type {number}
     */
    shownPage: 0,

    /**
     * The index of the page that is currently shown, within the page type.
     * For example if the third Apps page is showing, this will be 2.
     * @type {number}
     */
    shownPageIndex: 0,

    /**
     * EventTracker for managing event listeners for page events.
     * @type {!EventTracker}
     */
    eventTracker: new EventTracker,

    /**
     * If non-null, this is the ID of the app to highlight to the user the next
     * time getAppsCallback runs. "Highlight" in this case means to switch to
     * the page and run the new tile animation.
     * @type {String}
     */
    highlightAppId: null,

    /**
     * Initializes page list view.
     * @param {!Element} pageList A DIV element to host all pages.
     * @param {!Element} dotList An UL element to host nav dots. Each dot
     *     represents a page.
     * @param {!Element} cardSliderFrame The card slider frame that hosts
     *     pageList and switcher buttons.
     * @param {!Element|undefined} opt_trash Optional trash element.
     * @param {!Element|undefined} opt_pageSwitcherStart Optional start page
     *     switcher button.
     * @param {!Element|undefined} opt_pageSwitcherEnd Optional end page
     *     switcher button.
     */
    initialize: function(pageList, dotList, cardSliderFrame, opt_trash,
                         opt_pageSwitcherStart, opt_pageSwitcherEnd) {
      this.pageList = pageList;

      this.dotList = dotList;
      cr.ui.decorate(this.dotList, ntp4.DotList);

      this.trash = opt_trash;
      if (this.trash)
        new ntp4.Trash(this.trash);

      this.pageSwitcherStart = opt_pageSwitcherStart;
      if (this.pageSwitcherStart)
        ntp4.initializePageSwitcher(this.pageSwitcherStart);

      this.pageSwitcherEnd = opt_pageSwitcherEnd;
      if (this.pageSwitcherEnd)
        ntp4.initializePageSwitcher(this.pageSwitcherEnd);

      this.shownPage = templateData.shown_page_type;
      this.shownPageIndex = templateData.shown_page_index;

      // Request data on the apps so we can fill them in.
      // Note that this is kicked off asynchronously.  'getAppsCallback' will be
      // invoked at some point after this function returns.
      chrome.send('getApps');

      document.addEventListener('keydown', this.onDocKeyDown_.bind(this));
      // Prevent touch events from triggering any sort of native scrolling
      document.addEventListener('touchmove', function(e) {
        e.preventDefault();
      }, true);

      this.tilePages = this.pageList.getElementsByClassName('tile-page');
      this.appsPages = this.pageList.getElementsByClassName('apps-page');

      // Initialize the cardSlider without any cards at the moment
      this.sliderFrame = cardSliderFrame;
      this.cardSlider = new cr.ui.CardSlider(this.sliderFrame, this.pageList,
          this.sliderFrame.offsetWidth);
      this.cardSlider.initialize();

      // Handle events from the card slider.
      this.pageList.addEventListener('cardSlider:card_changed',
                                     this.onCardChanged_.bind(this));
      this.pageList.addEventListener('cardSlider:card_added',
                                     this.onCardAdded_.bind(this));
      this.pageList.addEventListener('cardSlider:card_removed',
                                     this.onCardRemoved_.bind(this));

      // Ensure the slider is resized appropriately with the window
      window.addEventListener('resize', this.onWindowResize_.bind(this));

      // Update apps when online state changes.
      window.addEventListener('online',
          this.updateOfflineEnabledApps_.bind(this));
      window.addEventListener('offline',
          this.updateOfflineEnabledApps_.bind(this));
    },

    /**
     * Appends a tile page.
     *
     * @param {TilePage} page The page element.
     * @param {string} title The title of the tile page.
     * @param {bool} titleIsEditable If true, the title can be changed.
     * @param {TilePage} opt_refNode Optional reference node to insert in front
     *     of.
     * When opt_refNode is falsey, |page| will just be appended to the end of
     * the page list.
     */
    appendTilePage: function(page, title, titleIsEditable, opt_refNode) {
      if (opt_refNode) {
        var refIndex = this.getTilePageIndex(opt_refNode);
        this.cardSlider.insertCardAtIndex(page, refIndex);
      } else {
        this.cardSlider.appendCard(page);
      }

      // Remember special MostVisitedPage.
      if (typeof ntp4.MostVisitedPage != 'undefined' &&
          page instanceof ntp4.MostVisitedPage) {
        assert(this.tilePages.length == 1,
               'MostVisitedPage should be added as first tile page');
        this.mostVisitedPage = page;
      }

      // If we're appending an AppsPage and it's a temporary page, animate it.
      var animate = page instanceof ntp4.AppsPage &&
                    page.classList.contains('temporary');
      // Make a deep copy of the dot template to add a new one.
      var newDot = new ntp4.NavDot(page, title, titleIsEditable, animate);
      page.navigationDot = newDot;
      this.dotList.insertBefore(newDot, opt_refNode ? opt_refNode.navigationDot
                                                    : null);
      // Set a tab index on the first dot.
      if (this.dotList.dots.length == 1)
        newDot.tabIndex = 3;

      this.eventTracker.add(page, 'pagelayout', this.onPageLayout_.bind(this));
    },

    /**
     * Called by chrome when an existing app has been disabled or
     * removed/uninstalled from chrome.
     * @param {Object} appData A data structure full of relevant information for
     *     the app.
     * @param {boolean} isUninstall True if the app is being uninstalled;
     *     false if the app is being disabled.
     * @param {boolean} fromPage True if the removal was from the current page.
     */
    appRemoved: function(appData, isUninstall, fromPage) {
      var app = $(appData.id);
      assert(app, 'trying to remove an app that doesn\'t exist');

      if (!isUninstall)
        app.replaceAppData(appData);
      else
        app.remove(!!fromPage);
    },

    /**
     * @return {boolean} If the page is still starting up.
     * @private
     */
    isStartingUp_: function() {
      return document.documentElement.classList.contains('starting-up');
    },

    /**
     * Callback invoked by chrome with the apps available.
     *
     * Note that calls to this function can occur at any time, not just in
     * response to a getApps request. For example, when a user
     * installs/uninstalls an app on another synchronized devices.
     * @param {Object} data An object with all the data on available
     *        applications.
     */
    getAppsCallback: function(data) {
      var startTime = Date.now();

      // Remember this to select the correct card when done rebuilding.
      var prevCurrentCard = this.cardSlider.currentCard;

      // Make removal of pages and dots as quick as possible with less DOM
      // operations, reflows, or repaints. We set currentCard = 0 and remove
      // from the end to not encounter any auto-magic card selections in the
      // process and we hide the card slider throughout.
      this.cardSlider.currentCard = 0;

      // Clear any existing apps pages and dots.
      // TODO(rbyers): It might be nice to preserve animation of dots after an
      // uninstall. Could we re-use the existing page and dot elements?  It
      // seems unfortunate to have Chrome send us the entire apps list after an
      // uninstall.
      while (this.appsPages.length > 0)
        this.removeTilePageAndDot_(this.appsPages[this.appsPages.length - 1]);

      // Get the array of apps and add any special synthesized entries
      var apps = data.apps;

      // Get a list of page names
      var pageNames = data.appPageNames;

      function stringListIsEmpty(list) {
        for (var i = 0; i < list.length; i++) {
          if (list[i])
            return false;
        }
        return true;
      }

      // Sort by launch ordinal
      apps.sort(function(a, b) {
        return a.app_launch_ordinal > b.app_launch_ordinal ? 1 :
          a.app_launch_ordinal < b.app_launch_ordinal ? -1 : 0;
      });

      // An app to animate (in case it was just installed).
      var highlightApp;

      // Add the apps, creating pages as necessary
      for (var i = 0; i < apps.length; i++) {
        var app = apps[i];
        var pageIndex = app.page_index || 0;
        while (pageIndex >= this.appsPages.length) {
          var pageName = localStrings.getString('appDefaultPageName');
          if (this.appsPages.length < pageNames.length)
            pageName = pageNames[this.appsPages.length];

          var origPageCount = this.appsPages.length;
          this.appendTilePage(new ntp4.AppsPage(), pageName, true);
          // Confirm that appsPages is a live object, updated when a new page is
          // added (otherwise we'd have an infinite loop)
          assert(this.appsPages.length == origPageCount + 1,
                 'expected new page');
        }

        if (app.id == this.highlightAppId)
          highlightApp = app;
        else
          this.appsPages[pageIndex].appendApp(app);
      }

      ntp4.AppsPage.setPromo(data.showPromo ? data : null);

      this.cardSlider.currentCard = prevCurrentCard;

      // Tell the slider about the pages.
      this.updateSliderCards();

      if (highlightApp)
        this.appAdded(highlightApp, true);

      // Mark the current page.
      this.cardSlider.currentCardValue.navigationDot.classList.add('selected');
      logEvent('apps.layout: ' + (Date.now() - startTime));

      document.documentElement.classList.remove('starting-up');
    },

    /**
     * Called by chrome when a new app has been added to chrome or has been
     * enabled if previously disabled.
     * @param {Object} appData A data structure full of relevant information for
     *     the app.
     */
    appAdded: function(appData, opt_highlight) {
      if (appData.id == this.highlightAppId) {
        opt_highlight = true;
        this.highlightAppId = null;
      }

      var pageIndex = appData.page_index || 0;

      if (pageIndex >= this.appsPages.length) {
        while (pageIndex >= this.appsPages.length) {
          this.appendTilePage(new ntp4.AppsPage(),
                              localStrings.getString('appDefaultPageName'),
                              true);
        }
        this.updateSliderCards();
      }

      var page = this.appsPages[pageIndex];
      var app = $(appData.id);
      if (app)
        app.replaceAppData(appData);
      else
        page.appendApp(appData, opt_highlight);
    },

    /**
     * Callback invoked by chrome whenever an app preference changes.
     * @param {Object} data An object with all the data on available
     *     applications.
     */
    appsPrefChangedCallback: function(data) {
      for (var i = 0; i < data.apps.length; ++i) {
        $(data.apps[i].id).appData = data.apps[i];
      }

      // Set the App dot names. Skip the first dot (Most Visited).
      var dots = this.dotList.getElementsByClassName('dot');
      var start = this.mostVisitedPage ? 1 : 0;
      for (var i = start; i < dots.length; ++i) {
        dots[i].displayTitle = data.appPageNames[i - start] || '';
      }
    },

    /**
     * Invoked whenever the pages in apps-page-list have changed so that
     * the Slider knows about the new elements.
     */
    updateSliderCards: function() {
      var pageNo = Math.max(0, Math.min(this.cardSlider.currentCard,
                                        this.tilePages.length - 1));
      this.cardSlider.setCards(Array.prototype.slice.call(this.tilePages),
                               pageNo);
      switch (this.shownPage) {
        case templateData['apps_page_id']:
          this.cardSlider.selectCardByValue(
              this.appsPages[Math.min(this.shownPageIndex,
                                      this.appsPages.length - 1)]);
          break;
        case templateData['most_visited_page_id']:
          if (this.mostVisitedPage)
            this.cardSlider.selectCardByValue(this.mostVisitedPage);
          break;
      }
    },

    /**
     * Called whenever tiles should be re-arranging themselves out of the way
     * of a moving or insert tile.
     */
    enterRearrangeMode: function() {
      var tempPage = new ntp4.AppsPage();
      tempPage.classList.add('temporary');
      var pageName = localStrings.getString('appDefaultPageName');
      this.appendTilePage(tempPage, pageName, true);

      if (ntp4.getCurrentlyDraggingTile().firstChild.canBeRemoved())
        $('footer').classList.add('showing-trash-mode');
    },

    /**
     * Invoked whenever some app is released
     */
    leaveRearrangeMode: function() {
      var tempPage = document.querySelector('.tile-page.temporary');
      var dot = tempPage.navigationDot;
      if (!tempPage.tileCount && tempPage != this.cardSlider.currentCardValue) {
        this.removeTilePageAndDot_(tempPage, true);
      } else {
        tempPage.classList.remove('temporary');
        this.saveAppPageName(tempPage,
                             localStrings.getString('appDefaultPageName'));
      }

      $('footer').classList.remove('showing-trash-mode');
    },

    /**
     * Callback for the 'pagelayout' event.
     * @param {Event} e The event.
     */
    onPageLayout_: function(e) {
      if (Array.prototype.indexOf.call(this.tilePages, e.currentTarget) !=
          this.cardSlider.currentCard) {
        return;
      }

      this.updatePageSwitchers();
    },

    /**
     * Adjusts the size and position of the page switchers according to the
     * layout of the current card.
     */
    updatePageSwitchers: function() {
      if (!this.pageSwitcherStart || !this.pageSwitcherEnd)
        return;

      var page = this.cardSlider.currentCardValue;

      this.pageSwitcherStart.hidden = !page ||
          (this.cardSlider.currentCard == 0);
      this.pageSwitcherEnd.hidden = !page ||
          (this.cardSlider.currentCard == this.cardSlider.cardCount - 1);

      if (!page)
        return;

      var pageSwitcherLeft = isRTL() ? this.pageSwitcherEnd
                                     : this.pageSwitcherStart;
      var pageSwitcherRight = isRTL() ? this.pageSwitcherStart
                                      : this.pageSwitcherEnd;
      var scrollbarWidth = page.scrollbarWidth;
      pageSwitcherLeft.style.width =
          (page.sideMargin + 13) + 'px';
      pageSwitcherLeft.style.left = '0';
      pageSwitcherRight.style.width =
          (page.sideMargin - scrollbarWidth + 13) + 'px';
      pageSwitcherRight.style.right = scrollbarWidth + 'px';

      var offsetTop = page.querySelector('.tile-page-content').offsetTop + 'px';
      pageSwitcherLeft.style.top = offsetTop;
      pageSwitcherRight.style.top = offsetTop;
      pageSwitcherLeft.style.paddingBottom = offsetTop;
      pageSwitcherRight.style.paddingBottom = offsetTop;
    },

    /**
     * Returns the index of the given apps page.
     * @param {AppsPage} page The AppsPage we wish to find.
     * @return {number} The index of |page| or -1 if it is not in the
     *    collection.
     */
    getAppsPageIndex: function(page) {
      return Array.prototype.indexOf.call(this.appsPages, page);
    },

    /**
     * Handler for cardSlider:card_changed events from this.cardSlider.
     * @param {Event} e The cardSlider:card_changed event.
     * @private
     */
    onCardChanged_: function(e) {
      var page = e.cardSlider.currentCardValue;

      // Don't change shownPage until startup is done (and page changes actually
      // reflect user actions).
      if (!this.isStartingUp_()) {
        if (page.classList.contains('apps-page')) {
          this.shownPage = templateData.apps_page_id;
          this.shownPageIndex = this.getAppsPageIndex(page);
        } else if (page.classList.contains('most-visited-page')) {
          this.shownPage = templateData.most_visited_page_id;
          this.shownPageIndex = 0;
        } else {
          console.error('unknown page selected');
        }
        chrome.send('pageSelected', [this.shownPage, this.shownPageIndex]);
      }

      // Update the active dot
      var curDot = this.dotList.getElementsByClassName('selected')[0];
      if (curDot)
        curDot.classList.remove('selected');
      page.navigationDot.classList.add('selected');
      this.updatePageSwitchers();
    },

    /**
     * Listen for card additions to update the page switchers or the current
     * card accordingly.
     * @param {Event} e A card removed or added event.
     */
    onCardAdded_: function(e) {
      // When the second arg passed to insertBefore is falsey, it acts just like
      // appendChild.
      this.pageList.insertBefore(e.addedCard, this.tilePages[e.addedIndex]);
      if (!this.isStartingUp_())
        this.updatePageSwitchers();
    },

    /**
     * Listen for card removals to update the page switchers or the current card
     * accordingly.
     * @param {Event} e A card removed or added event.
     */
    onCardRemoved_: function(e) {
      e.removedCard.parentNode.removeChild(e.removedCard);
      if (!this.isStartingUp_())
        this.updatePageSwitchers();
    },

    /**
     * Save the name of an apps page.
     * Store the apps page name into the preferences store.
     * @param {AppsPage} appsPage The app page for which we wish to save.
     * @param {string} name The name of the page.
     */
    saveAppPageName: function(appPage, name) {
      var index = this.getAppsPageIndex(appPage);
      assert(index != -1);
      chrome.send('saveAppPageName', [name, index]);
    },

    /**
     * Window resize handler.
     * @private
     */
    onWindowResize_: function(e) {
      this.cardSlider.resize(this.sliderFrame.offsetWidth);
      this.updatePageSwitchers();
    },

    /**
     * Listener for offline status change events. Updates apps that are
     * not offline-enabled to be grayscale if the browser is offline.
     * @private
     */
    updateOfflineEnabledApps_: function() {
      var apps = document.querySelectorAll('.app');
      for (var i = 0; i < apps.length; ++i) {
        if (apps[i].appData.enabled && !apps[i].appData.offline_enabled) {
          apps[i].setIcon();
          apps[i].loadIcon();
        }
      }
    },

    /**
     * Handler for key events on the page. Ctrl-Arrow will switch the visible
     * page.
     * @param {Event} e The KeyboardEvent.
     * @private
     */
    onDocKeyDown_: function(e) {
      if (!e.ctrlKey || e.altKey || e.metaKey || e.shiftKey)
        return;

      var direction = 0;
      if (e.keyIdentifier == 'Left')
        direction = -1;
      else if (e.keyIdentifier == 'Right')
        direction = 1;
      else
        return;

      var cardIndex =
          (this.cardSlider.currentCard + direction +
           this.cardSlider.cardCount) % this.cardSlider.cardCount;
      this.cardSlider.selectCard(cardIndex, true);

      e.stopPropagation();
    },

    /**
     * Returns the index of a given tile page.
     * @param {TilePage} page The TilePage we wish to find.
     * @return {number} The index of |page| or -1 if it is not in the
     *    collection.
     */
    getTilePageIndex: function(page) {
      return Array.prototype.indexOf.call(this.tilePages, page);
    },

    /**
     * Removes a page and navigation dot (if the navdot exists).
     * @param {TilePage} page The page to be removed.
     * @param {boolean=} opt_animate If the removal should be animated.
     */
    removeTilePageAndDot_: function(page, opt_animate) {
      if (page.navigationDot)
        page.navigationDot.remove(opt_animate);
      this.cardSlider.removeCard(page);
    },
  };

  return {
    PageListView: PageListView
  };
});
