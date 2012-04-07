// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  /////////////////////////////////////////////////////////////////////////////
  // OptionsPage class:

  /**
   * Base class for options page.
   * @constructor
   * @param {string} name Options page name, also defines id of the div element
   *     containing the options view and the name of options page navigation bar
   *     item as name+'PageNav'.
   * @param {string} title Options page title, used for navigation bar
   * @extends {EventTarget}
   */
  function OptionsPage(name, title, pageDivName) {
    this.name = name;
    this.title = title;
    this.pageDivName = pageDivName;
    this.pageDiv = $(this.pageDivName);
    this.tab = null;
  }

  const SUBPAGE_SHEET_COUNT = 2;

  const HORIZONTAL_OFFSET = 155;

  /**
   * Main level option pages. Maps lower-case page names to the respective page
   * object.
   * @protected
   */
  OptionsPage.registeredPages = {};

  /**
   * Pages which are meant to behave like modal dialogs. Maps lower-case overlay
   * names to the respective overlay object.
   * @protected
   */
  OptionsPage.registeredOverlayPages = {};

  /**
   * Whether or not |initialize| has been called.
   * @private
   */
  OptionsPage.initialized_ = false;

  /**
   * Gets the default page (to be shown on initial load).
   */
  OptionsPage.getDefaultPage = function() {
    return BrowserOptions.getInstance();
  };

  /**
   * Shows the default page.
   */
  OptionsPage.showDefaultPage = function() {
    this.navigateToPage(this.getDefaultPage().name);
  };

  /**
   * "Navigates" to a page, meaning that the page will be shown and the
   * appropriate entry is placed in the history.
   * @param {string} pageName Page name.
   */
  OptionsPage.navigateToPage = function(pageName) {
    this.showPageByName(pageName, true);
  };

  /**
   * Shows a registered page. This handles both top-level pages and sub-pages.
   * @param {string} pageName Page name.
   * @param {boolean} updateHistory True if we should update the history after
   *     showing the page.
   * @private
   */
  OptionsPage.showPageByName = function(pageName, updateHistory) {
    // Find the currently visible root-level page.
    var rootPage = null;
    for (var name in this.registeredPages) {
      var page = this.registeredPages[name];
      if (page.visible && !page.parentPage) {
        rootPage = page;
        break;
      }
    }

    // Find the target page.
    var targetPage = this.registeredPages[pageName.toLowerCase()];
    if (!targetPage || !targetPage.canShowPage()) {
      // If it's not a page, try it as an overlay.
      if (!targetPage && this.showOverlay_(pageName, rootPage)) {
        if (updateHistory)
          this.updateHistoryState_();
        return;
      } else {
        targetPage = this.getDefaultPage();
      }
    }

    pageName = targetPage.name.toLowerCase();
    var targetPageWasVisible = targetPage.visible;

    // Determine if the root page is 'sticky', meaning that it
    // shouldn't change when showing a sub-page.  This can happen for special
    // pages like Search.
    var isRootPageLocked =
        rootPage && rootPage.sticky && targetPage.parentPage;

    // Notify pages if they will be hidden.
    for (var name in this.registeredPages) {
      var page = this.registeredPages[name];
      if (!page.parentPage && isRootPageLocked)
        continue;
      if (page.willHidePage && name != pageName &&
          !page.isAncestorOfPage(targetPage))
        page.willHidePage();
    }

    // Update visibilities to show only the hierarchy of the target page.
    for (var name in this.registeredPages) {
      var page = this.registeredPages[name];
      if (!page.parentPage && isRootPageLocked)
        continue;
      page.visible = name == pageName ||
          (!document.documentElement.classList.contains('hide-menu') &&
           page.isAncestorOfPage(targetPage));
    }

    // Update the history and current location.
    if (updateHistory)
      this.updateHistoryState_();

    // Always update the page title.
    this.setTitle_(targetPage.title);

    // Notify pages if they were shown.
    for (var name in this.registeredPages) {
      var page = this.registeredPages[name];
      if (!page.parentPage && isRootPageLocked)
        continue;
      if (!targetPageWasVisible && page.didShowPage && (name == pageName ||
          page.isAncestorOfPage(targetPage)))
        page.didShowPage();
    }
  };

  /**
   * Updates the parts of the UI necessary for correctly hiding or displaying
   * subpages.
   * @private
   */
  OptionsPage.updateDisplayForShowOrHideSubpage_ = function() {
    OptionsPage.updateSubpageBackdrop_();
    OptionsPage.updateAriaHiddenForPages_();
    OptionsPage.updateScrollPosition_();
  };

  /**
   * Sets the aria-hidden attribute for pages which have been 'overlapped' by a
   * sub-page, and removes aria-hidden from the topmost page or subpage.
   * @private
   */
  OptionsPage.updateAriaHiddenForPages_ = function() {
    var visiblePages = OptionsPage.getVisiblePages_();

    // |visiblePages| is empty when switching top-level pages.
    if (!visiblePages.length)
      return;

    var topmostPage = visiblePages.pop();

    for (var i = 0; i < visiblePages.length; ++i) {
      var page = visiblePages[i];
      var nestingLevel = page.nestingLevel;
      var container = nestingLevel > 0 ?
        $('subpage-sheet-container-' + nestingLevel) : $('page-container');
      container.setAttribute('aria-hidden', true);
    }

    var topmostPageContainer = topmostPage.nestingLevel > 0 ?
        $('subpage-sheet-container-' + topmostPage.nestingLevel) :
        $('page-container');
    topmostPageContainer.removeAttribute('aria-hidden');
  };

  /**
   * Sets the title of the page. This is accomplished by calling into the
   * parent page API.
   * @param {String} title The title string.
   * @private
   */
  OptionsPage.setTitle_ = function(title) {
    uber.invokeMethodOnParent('setTitle', {title: title});
  };

  /**
   * Updates the visibility and stacking order of the subpage backdrop
   * according to which subpage is topmost and visible.
   * @private
   */
  OptionsPage.updateSubpageBackdrop_ = function () {
    var topmostPage = OptionsPage.getTopmostVisibleNonOverlayPage_();
    var nestingLevel = topmostPage ? topmostPage.nestingLevel : 0;

    var subpageBackdrop = $('subpage-backdrop');
    if (nestingLevel > 0) {
      var container = $('subpage-sheet-container-' + nestingLevel);
      subpageBackdrop.style.zIndex =
          parseInt(window.getComputedStyle(container).zIndex) - 1;
      subpageBackdrop.hidden = false;
    } else {
      subpageBackdrop.hidden = true;
    }
  };

  /**
   * Scrolls the page to the correct position (the top when opening a subpage,
   * or the old scroll position a previously hidden subpage becomes visible).
   * @private
   */
  OptionsPage.updateScrollPosition_ = function () {
    var topmostPage = OptionsPage.getTopmostVisibleNonOverlayPage_();
    var nestingLevel = topmostPage ? topmostPage.nestingLevel : 0;

    var container = (nestingLevel > 0) ?
       $('subpage-sheet-container-' + nestingLevel) : $('page-container');

    var scrollTop = container.oldScrollTop || 0;
    container.oldScrollTop = undefined;
    window.scroll(document.body.scrollLeft, scrollTop);
  };

  /**
   * Pushes the current page onto the history stack, overriding the last page
   * if it is the generic chrome://settings/.
   * @private
   */
  OptionsPage.updateHistoryState_ = function() {
    var page = this.getTopmostVisiblePage();
    var path = location.pathname;
    if (path)
      path = path.slice(1).replace(/\/$/, '');  // Remove trailing slash.
    // The page is already in history (the user may have clicked the same link
    // twice). Do nothing.
    if (path == page.name)
      return;

    // If there is no path, the current location is chrome://settings/.
    // Override this with the new page.
    var historyFunction = path ? window.history.pushState :
                                 window.history.replaceState;
    historyFunction.call(window.history,
                         {pageName: page.name},
                         page.title,
                         '/' + page.name);
    // Update tab title.
    this.setTitle_(page.title);
  };

  /**
   * Shows a registered Overlay page. Does not update history.
   * @param {string} overlayName Page name.
   * @param {OptionPage} rootPage The currently visible root-level page.
   * @return {boolean} whether we showed an overlay.
   */
  OptionsPage.showOverlay_ = function(overlayName, rootPage) {
    var overlay = this.registeredOverlayPages[overlayName.toLowerCase()];
    if (!overlay || !overlay.canShowPage())
      return false;

    if ((!rootPage || !rootPage.sticky) && overlay.parentPage)
      this.showPageByName(overlay.parentPage.name, false);

    if (!overlay.visible) {
      overlay.visible = true;
      if (overlay.didShowPage) overlay.didShowPage();
    }

    return true;
  };

  /**
   * Returns whether or not an overlay is visible.
   * @return {boolean} True if an overlay is visible.
   * @private
   */
  OptionsPage.isOverlayVisible_ = function() {
    return this.getVisibleOverlay_() != null;
  };

  /**
   * Returns the currently visible overlay, or null if no page is visible.
   * @return {OptionPage} The visible overlay.
   */
  OptionsPage.getVisibleOverlay_ = function() {
    for (var name in this.registeredOverlayPages) {
      var page = this.registeredOverlayPages[name];
      if (page.visible)
        return page;
    }
    return null;
  };

  /**
   * Closes the visible overlay. Updates the history state after closing the
   * overlay.
   */
  OptionsPage.closeOverlay = function() {
    var overlay = this.getVisibleOverlay_();
    if (!overlay)
      return;

    overlay.visible = false;

    if (overlay.didClosePage) overlay.didClosePage();
    this.updateHistoryState_();
  };

  /**
   * Hides the visible overlay. Does not affect the history state.
   * @private
   */
  OptionsPage.hideOverlay_ = function() {
    var overlay = this.getVisibleOverlay_();
    if (overlay)
      overlay.visible = false;
  };

  /**
   * Returns the pages which are currently visible, ordered by nesting level
   * (ascending).
   * @return {Array.OptionPage} The pages which are currently visible, ordered
   * by nesting level (ascending).
   */
  OptionsPage.getVisiblePages_ = function() {
    var visiblePages = [];
    for (var name in this.registeredPages) {
      var page = this.registeredPages[name];
      if (page.visible)
        visiblePages[page.nestingLevel] = page;
    }
    return visiblePages;
  };

  /**
   * Returns the topmost visible page (overlays excluded).
   * @return {OptionPage} The topmost visible page aside any overlay.
   * @private
   */
  OptionsPage.getTopmostVisibleNonOverlayPage_ = function() {
    var topPage = null;
    for (var name in this.registeredPages) {
      var page = this.registeredPages[name];
      if (page.visible &&
          (!topPage || page.nestingLevel > topPage.nestingLevel))
        topPage = page;
    }

    return topPage;
  };

  /**
   * Returns the topmost visible page, or null if no page is visible.
   * @return {OptionPage} The topmost visible page.
   */
  OptionsPage.getTopmostVisiblePage = function() {
    // Check overlays first since they're top-most if visible.
    return this.getVisibleOverlay_() || this.getTopmostVisibleNonOverlayPage_();
  };

  /**
   * Closes the topmost open subpage, if any.
   * @private
   */
  OptionsPage.closeTopSubPage_ = function() {
    var topPage = this.getTopmostVisiblePage();
    if (topPage && !topPage.isOverlay && topPage.parentPage) {
      if (topPage.willHidePage)
        topPage.willHidePage();
      topPage.visible = false;
    }

    this.updateHistoryState_();
  };

  /**
   * Closes all subpages below the given level.
   * @param {number} level The nesting level to close below.
   */
  OptionsPage.closeSubPagesToLevel = function(level) {
    var topPage = this.getTopmostVisiblePage();
    while (topPage && topPage.nestingLevel > level) {
      if (topPage.willHidePage)
        topPage.willHidePage();
      topPage.visible = false;
      topPage = topPage.parentPage;
    }

    this.updateHistoryState_();
  };

  /**
   * Updates managed banner visibility state based on the topmost page.
   */
  OptionsPage.updateManagedBannerVisibility = function() {
    var topPage = this.getTopmostVisiblePage();
    if (topPage)
      topPage.updateManagedBannerVisibility();
  };

  /**
  * Shows the tab contents for the given navigation tab.
  * @param {!Element} tab The tab that the user clicked.
  */
  OptionsPage.showTab = function(tab) {
    // Search parents until we find a tab, or the nav bar itself. This allows
    // tabs to have child nodes, e.g. labels in separately-styled spans.
    while (tab && !tab.classList.contains('subpages-nav-tabs') &&
           !tab.classList.contains('tab')) {
      tab = tab.parentNode;
    }
    if (!tab || !tab.classList.contains('tab'))
      return;

    // Find tab bar of the tab.
    var tabBar = tab;
    while (tabBar && !tabBar.classList.contains('subpages-nav-tabs')) {
      tabBar = tabBar.parentNode;
    }
    if (!tabBar)
      return;

    if (tabBar.activeNavTab != null) {
      tabBar.activeNavTab.classList.remove('active-tab');
      $(tabBar.activeNavTab.getAttribute('tab-contents')).classList.
          remove('active-tab-contents');
    }

    tab.classList.add('active-tab');
    $(tab.getAttribute('tab-contents')).classList.add('active-tab-contents');
    tabBar.activeNavTab = tab;
  };

  /**
   * Registers new options page.
   * @param {OptionsPage} page Page to register.
   */
  OptionsPage.register = function(page) {
    this.registeredPages[page.name.toLowerCase()] = page;
    page.initializePage();
  };

  /**
   * Find an enclosing section for an element if it exists.
   * @param {Element} element Element to search.
   * @return {OptionPage} The section element, or null.
   * @private
   */
  OptionsPage.findSectionForNode_ = function(node) {
    while (node = node.parentNode) {
      if (node.nodeName == 'SECTION')
        return node;
    }
    return null;
  };

  /**
   * Registers a new Sub-page.
   * @param {OptionsPage} subPage Sub-page to register.
   * @param {OptionsPage} parentPage Associated parent page for this page.
   * @param {Array} associatedControls Array of control elements that lead to
   *     this sub-page. The first item is typically a button in a root-level
   *     page. There may be additional buttons for nested sub-pages.
   */
  OptionsPage.registerSubPage = function(subPage,
                                         parentPage,
                                         associatedControls) {
    this.registeredPages[subPage.name.toLowerCase()] = subPage;
    subPage.parentPage = parentPage;
    if (associatedControls) {
      subPage.associatedControls = associatedControls;
      if (associatedControls.length) {
        subPage.associatedSection =
            this.findSectionForNode_(associatedControls[0]);
      }
    }
    subPage.tab = undefined;
    subPage.initializePage();
  };

  /**
   * Registers a new Overlay page.
   * @param {OptionsPage} overlay Overlay to register.
   * @param {OptionsPage} parentPage Associated parent page for this overlay.
   * @param {Array} associatedControls Array of control elements associated with
   *   this page.
   */
  OptionsPage.registerOverlay = function(overlay,
                                         parentPage,
                                         associatedControls) {
    this.registeredOverlayPages[overlay.name.toLowerCase()] = overlay;
    overlay.parentPage = parentPage;
    if (associatedControls) {
      overlay.associatedControls = associatedControls;
      if (associatedControls.length) {
        overlay.associatedSection =
            this.findSectionForNode_(associatedControls[0]);
      }
    }

    // Reverse the button strip for views. See the documentation of
    // reverseButtonStrip_() for an explanation of why this is necessary.
    if (cr.isViews)
      this.reverseButtonStrip_(overlay);

    overlay.tab = undefined;
    overlay.isOverlay = true;
    overlay.initializePage();
  };

  /**
   * Reverses the child elements of a button strip. This is necessary because
   * WebKit does not alter the tab order for elements that are visually reversed
   * using -webkit-box-direction: reverse, and the button order is reversed for
   * views.  See https://bugs.webkit.org/show_bug.cgi?id=62664 for more
   * information.
   * @param {Object} overlay The overlay containing the button strip to reverse.
   * @private
   */
  OptionsPage.reverseButtonStrip_ = function(overlay) {
    var buttonStrips = overlay.pageDiv.querySelectorAll('.button-strip');

    // Reverse all button-strips in the overlay.
    for (var j = 0; j < buttonStrips.length; j++) {
      var buttonStrip = buttonStrips[j];

      var childNodes = buttonStrip.childNodes;
      for (var i = childNodes.length - 1; i >= 0; i--)
        buttonStrip.appendChild(childNodes[i]);
    }
  };

  /**
   * Callback for window.onpopstate.
   * @param {Object} data State data pushed into history.
   */
  OptionsPage.setState = function(data) {
    if (data && data.pageName) {
      // It's possible an overlay may be the last top-level page shown.
      if (this.isOverlayVisible_() &&
          !this.registeredOverlayPages[data.pageName.toLowerCase()]) {
        this.hideOverlay_();
      }

      this.showPageByName(data.pageName, false);
    }
  };

  /**
   * Callback for window.onbeforeunload. Used to notify overlays that they will
   * be closed.
   */
  OptionsPage.willClose = function() {
    var overlay = this.getVisibleOverlay_();
    if (overlay && overlay.didClosePage)
      overlay.didClosePage();
  };

  /**
   * Freezes/unfreezes the scroll position of given level's page container.
   * @param {boolean} freeze Whether the page should be frozen.
   * @param {number} level The level to freeze/unfreeze.
   * @private
   */
  OptionsPage.setPageFrozenAtLevel_ = function(freeze, level) {
    var container = level == 0 ? $('page-container')
                               : $('subpage-sheet-container-' + level);

    if (container.classList.contains('frozen') == freeze)
      return;

    if (freeze) {
      // Lock the width, since auto width computation may change.
      container.style.width = window.getComputedStyle(container).width;
      container.oldScrollTop = document.body.scrollTop;
      container.classList.add('frozen');
      var verticalPosition =
          container.getBoundingClientRect().top - container.oldScrollTop;
      container.style.top = verticalPosition + 'px';
      this.updateFrozenElementHorizontalPosition_(container);
    } else {
      container.classList.remove('frozen');
      container.style.top = '';
      container.style.left = '';
      container.style.right = '';
      container.style.width = '';
    }
  };

  /**
   * Freezes/unfreezes the scroll position of visible pages based on the current
   * page stack.
   */
  OptionsPage.updatePageFreezeStates = function() {
    var topPage = OptionsPage.getTopmostVisiblePage();
    if (!topPage)
      return;
    var nestingLevel = topPage.isOverlay ? 100 : topPage.nestingLevel;
    for (var i = 0; i <= SUBPAGE_SHEET_COUNT; i++) {
      this.setPageFrozenAtLevel_(i < nestingLevel, i);
    }
  };

  /**
   * Initializes the complete options page.  This will cause all C++ handlers to
   * be invoked to do final setup.
   */
  OptionsPage.initialize = function() {
    chrome.send('coreOptionsInitialize');
    this.initialized_ = true;

    this.fixedHeaders_ = document.querySelectorAll('header');

    document.addEventListener('scroll', this.handleScroll_.bind(this));
    window.addEventListener('resize', this.handleResize_.bind(this));
    window.addEventListener('message', this.handleWindowMessage_.bind(this));

    if (!document.documentElement.classList.contains('hide-menu')) {
      // Close subpages if the user clicks on the html body. Listen in the
      // capturing phase so that we can stop the click from doing anything.
      document.body.addEventListener('click',
                                     this.bodyMouseEventHandler_.bind(this),
                                     true);
      // We also need to cancel mousedowns on non-subpage content.
      document.body.addEventListener('mousedown',
                                     this.bodyMouseEventHandler_.bind(this),
                                     true);

      var self = this;
      // Hook up the close buttons.
      subpageCloseButtons = document.querySelectorAll('.close-subpage');
      for (var i = 0; i < subpageCloseButtons.length; i++) {
        subpageCloseButtons[i].onclick = function() {
          self.closeTopSubPage_();
        };
      }

      // Install handler for key presses.
      document.addEventListener('keydown',
                                this.keyDownEventHandler_.bind(this));

      document.addEventListener('focus', this.manageFocusChange_.bind(this),
                                true);
    }

    // Trigger the resize and scroll handlers manually to set the initial state.
    this.handleResize_(null);
    this.handleScroll_();
  };

  /**
   * Does a bounds check for the element on the given x, y client coordinates.
   * @param {Element} e The DOM element.
   * @param {number} x The client X to check.
   * @param {number} y The client Y to check.
   * @return {boolean} True if the point falls within the element's bounds.
   * @private
   */
  OptionsPage.elementContainsPoint_ = function(e, x, y) {
    var clientRect = e.getBoundingClientRect();
    return x >= clientRect.left && x <= clientRect.right &&
        y >= clientRect.top && y <= clientRect.bottom;
  };

  /**
   * Called when focus changes; ensures that focus doesn't move outside
   * the topmost subpage/overlay.
   * @param {Event} e The focus change event.
   * @private
   */
  OptionsPage.manageFocusChange_ = function(e) {
    var focusableItemsRoot;
    var topPage = this.getTopmostVisiblePage();
    if (!topPage)
      return;

    if (topPage.isOverlay) {
      // If an overlay is visible, that defines the tab loop.
      focusableItemsRoot = topPage.pageDiv;
    } else {
      // If a subpage is visible, use its parent as the tab loop constraint.
      // (The parent is used because it contains the close button.)
      if (topPage.nestingLevel > 0)
        focusableItemsRoot = topPage.pageDiv.parentNode;
    }

    if (focusableItemsRoot && !focusableItemsRoot.contains(e.target))
      topPage.focusFirstElement();
  };

  /**
   * Called when the page is scrolled; moves elements that are position:fixed
   * but should only behave as if they are fixed for vertical scrolling.
   * @private
   */
  OptionsPage.handleScroll_ = function() {
    this.updateAllFrozenElementPositions_();
    this.updateAllHeaderElementPositions_();
  };

  /**
   * Updates all frozen pages to match the horizontal scroll position.
   * @private
   */
  OptionsPage.updateAllFrozenElementPositions_ = function() {
    var frozenElements = document.querySelectorAll('.frozen');
    for (var i = 0; i < frozenElements.length; i++)
      this.updateFrozenElementHorizontalPosition_(frozenElements[i]);
  };

  /**
   * Update the left of all the position: fixed; header elements.
   * @private
   */
  OptionsPage.updateAllHeaderElementPositions_ = function() {
    var translate = 'translateX(' + (document.body.scrollLeft * -1) + 'px)';
    for (var i = 0; i < this.fixedHeaders_.length; ++i)
      this.fixedHeaders_[i].style.webkitTransform = translate;

    uber.invokeMethodOnParent('adjustToScroll', document.body.scrollLeft);
  };

  /**
   * Updates the given frozen element to match the horizontal scroll position.
   * @param {HTMLElement} e The frozen element to update
   * @private
   */
  OptionsPage.updateFrozenElementHorizontalPosition_ = function(e) {
    if (document.documentElement.dir == 'rtl')
      e.style.right = HORIZONTAL_OFFSET + 'px';
    else
      e.style.left = HORIZONTAL_OFFSET - document.body.scrollLeft + 'px';
  };

  /**
   * Called when the page is resized; adjusts the size of elements that depend
   * on the veiwport.
   * @param {Event} e The resize event.
   * @private
   */
  OptionsPage.handleResize_ = function(e) {
    // Set an explicit height equal to the viewport on all the subpage
    // containers shorter than the viewport. This is used instead of
    // min-height: 100% so that there is an explicit height for the subpages'
    // min-height: 100%.
    var viewportHeight = document.documentElement.clientHeight;
    var subpageContainers =
        document.querySelectorAll('.subpage-sheet-container');
    for (var i = 0; i < subpageContainers.length; i++) {
      if (subpageContainers[i].scrollHeight > viewportHeight)
        subpageContainers[i].style.removeProperty('height');
      else
        subpageContainers[i].style.height = viewportHeight + 'px';
    }
  };

  /**
   * Handles postMessage from chrome://chrome.
   * @param {Event} e The post data.
   * @private
   */
  OptionsPage.handleWindowMessage_ = function(e) {
    if (e.data.method === 'frameSelected')
      this.handleFrameSelected_();
    else
      console.error('Received unexpected message', e.data);
  };

  /**
   * We receive this event via postMessage() when this page is selected via the
   * navigation.
   * @private
   */
  OptionsPage.handleFrameSelected_ = function() {
    document.body.scrollLeft = 0;
  };

  /**
   * A function to handle mouse events (mousedown or click) on the html body by
   * closing subpages and/or stopping event propagation.
   * @return {Event} a mousedown or click event.
   * @private
   */
  OptionsPage.bodyMouseEventHandler_ = function(event) {
    // Do nothing if a subpage isn't showing.
    var topPage = this.getTopmostVisiblePage();
    if (!topPage || topPage.isOverlay || !topPage.parentPage)
      return;

    // Don't close subpages if a user is clicking in a select element.
    // This is necessary because WebKit sends click events with strange
    // coordinates when a user selects a new entry in a select element.
    // See: http://crbug.com/87199
    if (event.srcElement.nodeName == 'SELECT')
      return;

    // Do nothing if the client coordinates are not within the source element.
    // This occurs if the user toggles a checkbox by pressing spacebar.
    // This is a workaround to prevent keyboard events from closing the window.
    // See: crosbug.com/15678
    if (event.clientX == -document.body.scrollLeft &&
        event.clientY == -document.body.scrollTop) {
      return;
    }

    // Figure out which page the click happened in.
    for (var level = topPage.nestingLevel; level >= 0; level--) {
      var clickIsWithinLevel = level == 0 ? true :
          OptionsPage.elementContainsPoint_(
              $('subpage-sheet-' + level), event.clientX, event.clientY);

      if (!clickIsWithinLevel)
        continue;

      // Event was within the topmost page; do nothing.
      if (topPage.nestingLevel == level)
        return;

      // Block propgation of both clicks and mousedowns, but only close subpages
      // on click.
      if (event.type == 'click')
        this.closeSubPagesToLevel(level);
      event.stopPropagation();
      event.preventDefault();
      return;
    }
  };

  /**
   * A function to handle key press events.
   * @return {Event} a keydown event.
   * @private
   */
  OptionsPage.keyDownEventHandler_ = function(event) {
    // Close the top overlay or sub-page on esc.
    if (event.keyCode == 27) {  // Esc
      if (this.isOverlayVisible_())
        this.closeOverlay();
      else
        this.closeTopSubPage_();
    }
  };

  OptionsPage.setClearPluginLSODataEnabled = function(enabled) {
    if (enabled) {
      document.documentElement.setAttribute(
          'flashPluginSupportsClearSiteData', '');
    } else {
      document.documentElement.removeAttribute(
          'flashPluginSupportsClearSiteData');
    }
  };

  /**
   * Re-initializes the C++ handlers if necessary. This is called if the
   * handlers are torn down and recreated but the DOM may not have been (in
   * which case |initialize| won't be called again). If |initialize| hasn't been
   * called, this does nothing (since it will be later, once the DOM has
   * finished loading).
   */
  OptionsPage.reinitializeCore = function() {
    if (this.initialized_)
      chrome.send('coreOptionsInitialize');
  }

  OptionsPage.prototype = {
    __proto__: cr.EventTarget.prototype,

    /**
     * The parent page of this option page, or null for top-level pages.
     * @type {OptionsPage}
     */
    parentPage: null,

    /**
     * The section on the parent page that is associated with this page.
     * Can be null.
     * @type {Element}
     */
    associatedSection: null,

    /**
     * An array of controls that are associated with this page.  The first
     * control should be located on a top-level page.
     * @type {OptionsPage}
     */
    associatedControls: null,

    /**
     * Initializes page content.
     */
    initializePage: function() {},

    /**
     * Updates managed banner visibility state. This function iterates over
     * all input fields of a window and if any of these is marked as managed
     * it triggers the managed banner to be visible. The banner can be enforced
     * being on through the managed flag of this class but it can not be forced
     * being off if managed items exist.
     */
    updateManagedBannerVisibility: function() {
      var bannerDiv = $('managed-prefs-banner');

      var controlledByPolicy = false;
      var controlledByExtension = false;
      var inputElements = this.pageDiv.querySelectorAll('input[controlled-by]');
      for (var i = 0, len = inputElements.length; i < len; i++) {
        if (inputElements[i].controlledBy == 'policy')
          controlledByPolicy = true;
        else if (inputElements[i].controlledBy == 'extension')
          controlledByExtension = true;
      }
      if (!controlledByPolicy && !controlledByExtension) {
        bannerDiv.hidden = true;
      } else {
        bannerDiv.hidden = false;
        var height = window.getComputedStyle(bannerDiv).height;
        if (controlledByPolicy && !controlledByExtension) {
          $('managed-prefs-text').textContent =
              templateData.policyManagedPrefsBannerText;
        } else if (!controlledByPolicy && controlledByExtension) {
          $('managed-prefs-text').textContent =
              templateData.extensionManagedPrefsBannerText;
        } else if (controlledByPolicy && controlledByExtension) {
          $('managed-prefs-text').textContent =
              templateData.policyAndExtensionManagedPrefsBannerText;
        }
      }
    },

    /**
     * Gets page visibility state.
     */
    get visible() {
      return !this.pageDiv.hidden;
    },

    /**
     * Sets page visibility.
     */
    set visible(visible) {
      if ((this.visible && visible) || (!this.visible && !visible))
        return;

      this.setContainerVisibility_(visible);
      this.pageDiv.hidden = !visible;

      OptionsPage.updatePageFreezeStates();

      // The managed prefs banner is global, so after any visibility change
      // update it based on the topmost page, not necessarily this page
      // (e.g., if an ancestor is made visible after a child).
      OptionsPage.updateManagedBannerVisibility();

      // A subpage was shown or hidden.
      if (!this.isOverlay && this.nestingLevel > 0)
        OptionsPage.updateDisplayForShowOrHideSubpage_();
      else if (this.isOverlay && !visible)
        OptionsPage.updateScrollPosition_();

      cr.dispatchPropertyChange(this, 'visible', visible, !visible);
    },

    /**
     * Shows or hides this page's container.
     * @param {boolean} visible Whether the container should be visible or not.
     * @private
     */
    setContainerVisibility_: function(visible) {
      var container = null;
      if (this.isOverlay) {
        container = $('overlay');
      } else {
        var nestingLevel = this.nestingLevel;
        if (nestingLevel > 0)
          container = $('subpage-sheet-container-' + nestingLevel);
      }
      var isSubpage = !this.isOverlay;

      if (!container)
        return;

      if (visible)
        uber.invokeMethodOnParent('beginInterceptingEvents');

      if (container.hidden != visible) {
        if (visible) {
          // If the container is set hidden and then immediately set visible
          // again, the fadeCompleted_ callback would cause it to be erroneously
          // hidden again. Removing the transparent tag avoids that.
          container.classList.remove('transparent');
        }
        return;
      }

      if (visible) {
        container.hidden = false;
        if (isSubpage) {
          var computedStyle = window.getComputedStyle(container);
          container.style.WebkitPaddingStart =
              parseInt(computedStyle.WebkitPaddingStart, 10) + 100 + 'px';
        }
        // Separate animating changes from the removal of display:none.
        window.setTimeout(function() {
          container.classList.remove('transparent');
          if (isSubpage)
            container.style.WebkitPaddingStart = '';
        });
      } else {
        var self = this;
        container.addEventListener('webkitTransitionEnd', function f(e) {
          if (e.propertyName != 'opacity')
            return;
          container.removeEventListener('webkitTransitionEnd', f);
          self.fadeCompleted_(container);
        });
        container.classList.add('transparent');
      }
    },

    /**
     * Called when a container opacity transition finishes.
     * @param {HTMLElement} container The container element.
     * @private
     */
    fadeCompleted_: function(container) {
      if (container.classList.contains('transparent')) {
        container.hidden = true;
        if (this.nestingLevel == 1)
          uber.invokeMethodOnParent('stopInterceptingEvents');
      }
    },

    /**
     * Focuses the first control on the page.
     */
    focusFirstElement: function() {
      // Sets focus on the first interactive element in the page.
      var focusElement =
          this.pageDiv.querySelector('button, input, list, select');
      if (focusElement)
        focusElement.focus();
    },

    /**
     * The nesting level of this page.
     * @type {number} The nesting level of this page (0 for top-level page)
     */
    get nestingLevel() {
      var level = 0;
      var parent = this.parentPage;
      while (parent) {
        level++;
        parent = parent.parentPage;
      }
      return level;
    },

    /**
     * Whether the page is considered 'sticky', such that it will
     * remain a top-level page even if sub-pages change.
     * @type {boolean} True if this page is sticky.
     */
    get sticky() {
      return false;
    },

    /**
     * Checks whether this page is an ancestor of the given page in terms of
     * subpage nesting.
     * @param {OptionsPage} page
     * @return {boolean} True if this page is nested under |page|
     */
    isAncestorOfPage: function(page) {
      var parent = page.parentPage;
      while (parent) {
        if (parent == this)
          return true;
        parent = parent.parentPage;
      }
      return false;
    },

    /**
     * Whether it should be possible to show the page.
     * @return {boolean} True if the page should be shown
     */
    canShowPage: function() {
      return true;
    },
  };

  // Export
  return {
    OptionsPage: OptionsPage
  };
});
