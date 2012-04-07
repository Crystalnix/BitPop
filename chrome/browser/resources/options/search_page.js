// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  const OptionsPage = options.OptionsPage;

  /**
   * Encapsulated handling of a search bubble.
   * @constructor
   */
  function SearchBubble(text) {
    var el = cr.doc.createElement('div');
    SearchBubble.decorate(el);
    el.textContent = text;
    return el;
  }

  SearchBubble.decorate = function(el) {
    el.__proto__ = SearchBubble.prototype;
    el.decorate();
  };

  SearchBubble.prototype = {
    __proto__: HTMLDivElement.prototype,

    decorate: function() {
      this.className = 'search-bubble';

      // We create a timer to periodically update the position of the bubbles.
      // While this isn't all that desirable, it's the only sure-fire way of
      // making sure the bubbles stay in the correct location as sections
      // may dynamically change size at any time.
      var self = this;
      this.intervalId = setInterval(this.updatePosition.bind(this), 250);
    },

  /**
   * Attach the bubble to the element.
   */
    attachTo: function(element) {
      var parent = element.parentElement;
      if (!parent)
        return;
      if (parent.tagName == 'TD') {
        // To make absolute positioning work inside a table cell we need
        // to wrap the bubble div into another div with position:relative.
        // This only works properly if the element is the first child of the
        // table cell which is true for all options pages.
        this.wrapper = cr.doc.createElement('div');
        this.wrapper.className = 'search-bubble-wrapper';
        this.wrapper.appendChild(this);
        parent.insertBefore(this.wrapper, element);
      } else {
        parent.insertBefore(this, element);
      }
    },

    /**
     * Clear the interval timer and remove the element from the page.
     */
    dispose: function() {
      clearInterval(this.intervalId);

      var child = this.wrapper || this;
      var parent = child.parentNode;
      if (parent)
        parent.removeChild(child);
    },

    /**
     * Update the position of the bubble.  Called at creation time and then
     * periodically while the bubble remains visible.
     */
    updatePosition: function() {
      // This bubble is 'owned' by the next sibling.
      var owner = (this.wrapper || this).nextSibling;

      // If there isn't an offset parent, we have nothing to do.
      if (!owner.offsetParent)
        return;

      // Position the bubble below the location of the owner.
      var left = owner.offsetLeft + owner.offsetWidth / 2 -
          this.offsetWidth / 2;
      var top = owner.offsetTop + owner.offsetHeight;

      // Update the position in the CSS.  Cache the last values for
      // best performance.
      if (left != this.lastLeft) {
        this.style.left = left + 'px';
        this.lastLeft = left;
      }
      if (top != this.lastTop) {
        this.style.top = top + 'px';
        this.lastTop = top;
      }
    }
  }

  /**
   * Encapsulated handling of the search page.
   * @constructor
   */
  function SearchPage() {
    OptionsPage.call(this, 'search', templateData.searchPageTabTitle,
        'searchPage');
  }

  cr.addSingletonGetter(SearchPage);

  SearchPage.prototype = {
    // Inherit SearchPage from OptionsPage.
    __proto__: OptionsPage.prototype,

    /**
     * A boolean to prevent recursion. Used by setSearchText_().
     * @type {Boolean}
     * @private
     */
    insideSetSearchText_: false,

    /**
     * Initialize the page.
     */
    initializePage: function() {
      // Call base class implementation to start preference initialization.
      OptionsPage.prototype.initializePage.call(this);

      var self = this;

      // Create a search field element.
      var searchField = document.createElement('input');
      searchField.id = 'search-field';
      searchField.type = 'search';
      searchField.incremental = true;
      searchField.placeholder = localStrings.getString('searchPlaceholder');
      searchField.setAttribute('aria-label', searchField.placeholder);
      this.searchField = searchField;

      // Replace the contents of the navigation tab with the search field.
      self.tab.textContent = '';
      self.tab.appendChild(searchField);
      self.tab.onclick = self.tab.onkeydown = self.tab.onkeypress = undefined;
      self.tab.tabIndex = -1;
      self.tab.setAttribute('role', '');

      // Don't allow the focus on the search navbar. http://crbug.com/77989
      self.tab.onfocus = self.tab.blur;

      // Handle search events. (No need to throttle, WebKit's search field
      // will do that automatically.)
      searchField.onsearch = function(e) {
        self.setSearchText_(this.value);
      };

      // We update the history stack every time the search field blurs. This way
      // we get a history entry for each search, roughly, but not each letter
      // typed.
      searchField.onblur = function(e) {
        var query = SearchPage.canonicalizeQuery(searchField.value);
        if (!query)
          return;

        // Don't push the same page onto the history stack more than once (if
        // the user clicks in the search field and away several times).
        var currentHash = location.hash;
        var newHash = '#' + escape(query);
        if (currentHash == newHash)
          return;

        // If there is no hash on the current URL, the history entry has no
        // search query. Replace the history entry with no search with an entry
        // that does have a search. Otherwise, add it onto the history stack.
        var historyFunction = currentHash ? window.history.pushState :
                                            window.history.replaceState;
        historyFunction.call(
            window.history,
            {pageName: self.name},
            self.title,
            '/' + self.name + newHash);
      };

      // Install handler for key presses.
      document.addEventListener('keydown',
                                this.keyDownEventHandler_.bind(this));

      // Focus the search field by default.
      searchField.focus();
    },

    /**
     * @inheritDoc
     */
    get sticky() {
      return true;
    },

    /**
     * Called after this page has shown.
     */
    didShowPage: function() {
      // This method is called by the Options page after all pages have
      // had their visibilty attribute set.  At this point we can perform the
      // search specific DOM manipulation.
      this.setSearchActive_(true);
    },

    /**
     * Called before this page will be hidden.
     */
    willHidePage: function() {
      // This method is called by the Options page before all pages have
      // their visibilty attribute set.  Before that happens, we need to
      // undo the search specific DOM manipulation that was performed in
      // didShowPage.
      this.setSearchActive_(false);
    },

    /**
     * Update the UI to reflect whether we are in a search state.
     * @param {boolean} active True if we are on the search page.
     * @private
     */
    setSearchActive_: function(active) {
      // It's fine to exit if search wasn't active and we're not going to
      // activate it now.
      if (!this.searchActive_ && !active)
        return;

      this.searchActive_ = active;

      if (active) {
        var hash = location.hash;
        if (hash)
          this.searchField.value = unescape(hash.slice(1));
      } else {
        // Just wipe out any active search text since it's no longer relevant.
        this.searchField.value = '';
      }

      var pagesToSearch = this.getSearchablePages_();
      for (var key in pagesToSearch) {
        var page = pagesToSearch[key];

        if (!active)
          page.visible = false;

        // Update the visible state of all top-level elements that are not
        // sections (ie titles, button strips).  We do this before changing
        // the page visibility to avoid excessive re-draw.
        for (var i = 0, childDiv; childDiv = page.pageDiv.children[i]; i++) {
          if (childDiv.classList.contains('displaytable')) {
            childDiv.setAttribute('searching', active ? 'true' : 'false');
            for (var j = 0, subDiv; subDiv = childDiv.children[j]; j++) {
              if (active) {
                if (subDiv.tagName != 'SECTION')
                  subDiv.classList.add('search-hidden');
              } else {
                subDiv.classList.remove('search-hidden');
              }
            }
          } else {
            if (active)
              childDiv.classList.add('search-hidden');
            else
              childDiv.classList.remove('search-hidden');
          }
        }

        if (active) {
          // When search is active, remove the 'hidden' tag.  This tag may have
          // been added by the OptionsPage.
          page.pageDiv.hidden = false;
        }
      }

      if (active) {
        this.setSearchText_(this.searchField.value);
      } else {
        // After hiding all page content, remove any search results.
        this.unhighlightMatches_();
        this.removeSearchBubbles_();
      }
    },

    /**
     * Set the current search criteria.
     * @param {string} text Search text.
     * @private
     */
    setSearchText_: function(text) {
      // Prevent recursive execution of this method.
      if (this.insideSetSearchText_) return;
      this.insideSetSearchText_ = true;

      // Cleanup the search query string.
      text = SearchPage.canonicalizeQuery(text);

      // Notify listeners about the new search query, some pages may wish to
      // show/hide elements based on the query.
      var event = new cr.Event('searchChanged');
      event.searchText = text;
      this.dispatchEvent(event);

      // Toggle the search page if necessary.
      if (text.length) {
        if (!this.searchActive_)
          OptionsPage.navigateToPage(this.name);
      } else {
        if (this.searchActive_)
          OptionsPage.showDefaultPage();

        this.insideSetSearchText_ = false;
        return;
      }

      var foundMatches = false;
      var bubbleControls = [];

      // Remove any prior search results.
      this.unhighlightMatches_();
      this.removeSearchBubbles_();

      // Generate search text by applying lowercase and escaping any characters
      // that would be problematic for regular expressions.
      var searchText =
          text.toLowerCase().replace(/[-[\]{}()*+?.,\\^$|#\s]/g, '\\$&');

      // Generate a regular expression and replace string for hilighting
      // search terms.
      var regEx = new RegExp('(' + searchText + ')', 'ig');
      var replaceString = '<span class="search-highlighted">$1</span>';

      // Initialize all sections.  If the search string matches a title page,
      // show sections for that page.
      var page, pageMatch, childDiv, length;
      var pagesToSearch = this.getSearchablePages_();
      for (var key in pagesToSearch) {
        page = pagesToSearch[key];
        pageMatch = false;
        if (searchText.length) {
          pageMatch = this.performReplace_(regEx, replaceString, page.tab);
        }
        if (pageMatch)
          foundMatches = true;
        var elements = page.pageDiv.querySelectorAll('.displaytable > section');
        for (var i = 0, node; node = elements[i]; i++) {
          if (pageMatch)
            node.classList.remove('search-hidden');
          else
            node.classList.add('search-hidden');
        }
      }

      if (searchText.length) {
        // Search all top-level sections for anchored string matches.
        for (var key in pagesToSearch) {
          page = pagesToSearch[key];
          var elements =
              page.pageDiv.querySelectorAll('.displaytable > section');
          for (var i = 0, node; node = elements[i]; i++) {
            if (this.performReplace_(regEx, replaceString, node)) {
              node.classList.remove('search-hidden');
              foundMatches = true;
            }
          }
        }

        // Search all sub-pages, generating an array of top-level sections that
        // we need to make visible.
        var subPagesToSearch = this.getSearchableSubPages_();
        var control, node;
        for (var key in subPagesToSearch) {
          page = subPagesToSearch[key];
          if (this.performReplace_(regEx, replaceString, page.pageDiv)) {
            // Reveal the section for this search result.
            section = page.associatedSection;
            if (section)
              section.classList.remove('search-hidden');

            // Identify any controls that should have bubbles.
            var controls = page.associatedControls;
            if (controls) {
              length = controls.length;
              for (var i = 0; i < length; i++)
                bubbleControls.push(controls[i]);
            }

            foundMatches = true;
          }
        }
      }

      // Configure elements on the search results page based on search results.
      if (foundMatches)
        $('searchPageNoMatches').classList.add('search-hidden');
      else
        $('searchPageNoMatches').classList.remove('search-hidden');

      // Create search balloons for sub-page results.
      length = bubbleControls.length;
      for (var i = 0; i < length; i++)
        this.createSearchBubble_(bubbleControls[i], text);

      // Cleanup the recursion-prevention variable.
      this.insideSetSearchText_ = false;
    },

    /**
     * Performs a string replacement based on a regex and replace string.
     * @param {RegEx} regex A regular expression for finding search matches.
     * @param {String} replace A string to apply the replace operation.
     * @param {Element} element An HTML container element.
     * @returns {Boolean} true if the element was changed.
     * @private
     */
    performReplace_: function(regex, replace, element) {
      var found = false;
      var div, child, tmp;

      // Walk the tree, searching each TEXT node.
      var walker = document.createTreeWalker(element,
                                             NodeFilter.SHOW_TEXT,
                                             null,
                                             false);
      var node = walker.nextNode();
      while (node) {
        // Perform a search and replace on the text node value.
        var newValue = node.nodeValue.replace(regex, replace);
        if (newValue != node.nodeValue) {
          // The text node has changed so that means we found at least one
          // match.
          found = true;

          // Create a temporary div element and set the innerHTML to the new
          // value.
          div = document.createElement('div');
          div.innerHTML = newValue;

          // Insert all the child nodes of the temporary div element into the
          // document, before the original node.
          child = div.firstChild;
          while (child = div.firstChild) {
            node.parentNode.insertBefore(child, node);
          };

          // Delete the old text node and advance the walker to the next
          // node.
          tmp = node;
          node = walker.nextNode();
          tmp.parentNode.removeChild(tmp);
        } else {
          node = walker.nextNode();
        }
      }

      return found;
    },

    /**
     * Removes all search highlight tags from the document.
     * @private
     */
    unhighlightMatches_: function() {
      // Find all search highlight elements.
      var elements = document.querySelectorAll('.search-highlighted');

      // For each element, remove the highlighting.
      var parent, i;
      for (var i = 0, node; node = elements[i]; i++) {
        parent = node.parentNode;

        // Replace the highlight element with the first child (the text node).
        parent.replaceChild(node.firstChild, node);

        // Normalize the parent so that multiple text nodes will be combined.
        parent.normalize();
      }
    },

    /**
     * Creates a search result bubble attached to an element.
     * @param {Element} element An HTML element, usually a button.
     * @param {string} text A string to show in the bubble.
     * @private
     */
    createSearchBubble_: function(element, text) {
      // avoid appending multiple bubbles to a button.
      var sibling = element.previousElementSibling;
      if (sibling && (sibling.classList.contains('search-bubble') ||
                      sibling.classList.contains('search-bubble-wrapper')))
        return;

      var parent = element.parentElement;
      if (parent) {
        var bubble = new SearchBubble(text);
        bubble.attachTo(element);
        bubble.updatePosition();
      }
    },

    /**
     * Removes all search match bubbles.
     * @private
     */
    removeSearchBubbles_: function() {
      var elements = document.querySelectorAll('.search-bubble');
      var length = elements.length;
      for (var i = 0; i < length; i++)
        elements[i].dispose();
    },

    /**
     * Builds a list of top-level pages to search.  Omits the search page and
     * all sub-pages.
     * @returns {Array} An array of pages to search.
     * @private
     */
    getSearchablePages_: function() {
      var name, page, pages = [];
      for (name in OptionsPage.registeredPages) {
        if (name != this.name) {
          page = OptionsPage.registeredPages[name];
          if (!page.parentPage)
            pages.push(page);
        }
      }
      return pages;
    },

    /**
     * Builds a list of sub-pages (and overlay pages) to search.  Ignore pages
     * that have no associated controls.
     * @returns {Array} An array of pages to search.
     * @private
     */
    getSearchableSubPages_: function() {
      var name, pageInfo, page, pages = [];
      for (name in OptionsPage.registeredPages) {
        page = OptionsPage.registeredPages[name];
        if (page.parentPage && page.associatedSection)
          pages.push(page);
      }
      for (name in OptionsPage.registeredOverlayPages) {
        page = OptionsPage.registeredOverlayPages[name];
        if (page.associatedSection && page.pageDiv != undefined)
          pages.push(page);
      }
      return pages;
    },

    /**
     * A function to handle key press events.
     * @return {Event} a keydown event.
     * @private
     */
    keyDownEventHandler_: function(event) {
      const ESCAPE_KEY_CODE = 27;
      const FORWARD_SLASH_KEY_CODE = 191;

      switch(event.keyCode) {
        case ESCAPE_KEY_CODE:
          if (event.target == this.searchField) {
            this.setSearchText_('');
            this.searchField.blur();
            event.stopPropagation();
            event.preventDefault();
          }
          break;
        case FORWARD_SLASH_KEY_CODE:
          if (!/INPUT|SELECT|BUTTON|TEXTAREA/.test(event.target.tagName) &&
              !event.ctrlKey && !event.altKey) {
            this.searchField.focus();
            event.stopPropagation();
            event.preventDefault();
          }
          break;
      }
    },
  };

  /**
   * Standardizes a user-entered text query by removing extra whitespace.
   * @param {string} The user-entered text.
   * @return {string} The trimmed query.
   */
  SearchPage.canonicalizeQuery = function(text) {
    // Trim beginning and ending whitespace.
    return text.replace(/^\s+|\s+$/g, '');
  };

  // Export
  return {
    SearchPage: SearchPage
  };

});
