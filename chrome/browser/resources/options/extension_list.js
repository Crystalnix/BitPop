// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  'use strict';

  /**
   * A lookup helper function to find the first node that has an id (starting
   * at |node| and going up the parent chain).
   * @param {Element} node The node to start looking at.
   */
  function findIdNode(node) {
    while (node && !node.id) {
      node = node.parentNode;
    }
    return node;
  }

  /**
   * Creates a new list of extensions.
   * @param {Object=} opt_propertyBag Optional properties.
   * @constructor
   * @extends {cr.ui.div}
   */
  var ExtensionsList = cr.ui.define('div');

  var handlersInstalled = false;

  /**
   * @type {Object.<string, boolean>} A map from extension id to a boolean
   *     indicating whether its details section is expanded. This persists
   *     between calls to decorate.
   */
  var showingDetails = {};

  /**
   * @type {Object.<string, boolean>} A map from extension id to a boolean
   *     indicating whether the incognito warning is showing. This persists
   *     between calls to decorate.
   */
  var showingWarning = {};

  ExtensionsList.prototype = {
    __proto__: HTMLDivElement.prototype,

    /** @inheritDoc */
    decorate: function() {
      this.initControlsAndHandlers_();

      this.deleteExistingExtensionNodes_();

      this.showExtensionNodes_();
    },

    /**
     * Initializes the controls (toggle section and button) and installs
     * handlers.
     * @private
     */
    initControlsAndHandlers_: function() {
      // Make sure developer mode section is set correctly as per saved setting.
      var toggleButton = $('toggle-dev-on');
      var toggleSection = $('dev');
      if (this.data_.developerMode) {
        toggleSection.classList.add('dev-open');
        toggleSection.classList.remove('dev-closed');
        toggleButton.checked = true;
      } else {
        toggleSection.classList.remove('dev-open');
        toggleSection.classList.add('dev-closed');
      }

      // Instal global event handlers.
      if (!handlersInstalled) {
        var searchPage = SearchPage.getInstance();
        searchPage.addEventListener('searchChanged',
                                    this.searchChangedHandler_.bind(this));

        // Support full keyboard accessibility without making things ugly
        // for users who click, by hiding some focus outlines when the user
        // clicks anywhere, but showing them when the user presses any key.
        this.ownerDocument.body.classList.add('hide-some-focus-outlines');
        this.ownerDocument.addEventListener('click', (function(e) {
          this.ownerDocument.body.classList.add('hide-some-focus-outlines');
          return true;
        }).bind(this), true);
        this.ownerDocument.addEventListener('keydown', (function(e) {
          this.ownerDocument.body.classList.remove('hide-some-focus-outlines');
          return true;
        }).bind(this), true);

        handlersInstalled = true;
      }
    },

    /**
     * Deletes the existing Extension nodes from the page to make room for new
     * ones.
     * @private
     */
     deleteExistingExtensionNodes_: function() {
      while (this.hasChildNodes()){
        this.removeChild(this.firstChild);
      }
    },

    /**
     * Handles decorating the details section.
     * @param {Element} details The div that the details should be attached to.
     * @param {Object} extension The extension we are showing the details for.
     * @private
     */
     showExtensionNodes_: function() {
      // Iterate over the extension data and add each item to the list.
      for (var i = 0; i < this.data_.extensions.length; i++) {
        var extension = this.data_.extensions[i];
        var id = extension.id;

        var wrapper = this.ownerDocument.createElement('div');

        var expanded = showingDetails[id];
        var butterbar = showingWarning[id];

        wrapper.classList.add(expanded ? 'extension-list-item-expanded' :
                                         'extension-list-item-collaped');
        if (!extension.enabled)
          wrapper.classList.add('disabled');
        wrapper.id = id;
        this.appendChild(wrapper);

        var vboxOuter = this.ownerDocument.createElement('div');
        vboxOuter.classList.add('vbox');
        vboxOuter.classList.add('extension-list-item');
        wrapper.appendChild(vboxOuter);

        var hbox = this.ownerDocument.createElement('div');
        hbox.classList.add('hbox');
        vboxOuter.appendChild(hbox);

        // Add a container div for the zippy, so we can extend the hit area.
        var container = this.ownerDocument.createElement('div');
        // Clicking anywhere on the div expands/collapses the details.
        container.classList.add('extension-zippy-container');
        container.title = expanded ?
            localStrings.getString('extensionSettingsHideDetails') :
            localStrings.getString('extensionSettingsShowDetails');
        container.tabIndex = 0;
        container.setAttribute('role', 'button');
        container.setAttribute('aria-controls', extension.id + '_details');
        container.setAttribute('aria-expanded', expanded);
        container.addEventListener('click', this.handleZippyClick_.bind(this));
        container.addEventListener('keydown',
                                   this.handleZippyKeyDown_.bind(this));
        hbox.appendChild(container);

        // On the far left we have the zippy icon.
        var div = this.ownerDocument.createElement('div');
        div.id = id + '_zippy';
        div.classList.add('extension-zippy-default');
        div.classList.add(expanded ? 'extension-zippy-expanded' :
                                     'extension-zippy-collapsed');
        container.appendChild(div);

        // Next to it, we have the extension icon.
        var icon = this.ownerDocument.createElement('img');
        icon.classList.add('extension-icon');
        icon.src = extension.icon;
        hbox.appendChild(icon);

        // Start a vertical box for showing the details.
        var vbox = this.ownerDocument.createElement('div');
        vbox.classList.add('vbox');
        vbox.classList.add('stretch');
        vbox.classList.add('details-view');
        hbox.appendChild(vbox);

        div = this.ownerDocument.createElement('div');
        vbox.appendChild(div);

        // Title comes next.
        var title = this.ownerDocument.createElement('span');
        title.classList.add('extension-title');
        title.textContent = extension.name;
        vbox.appendChild(title);

        // Followed by version.
        var version = this.ownerDocument.createElement('span');
        version.classList.add('extension-version');
        version.textContent = extension.version;
        vbox.appendChild(version);

        // And the additional info label (unpacked/crashed).
        if (extension.terminated || extension.isUnpacked) {
          var version = this.ownerDocument.createElement('span');
          version.classList.add('extension-version');
          version.textContent = extension.terminated ?
              localStrings.getString('extensionSettingsCrashMessage') :
              localStrings.getString('extensionSettingsInDevelopment');
          vbox.appendChild(version);
        }

        div = this.ownerDocument.createElement('div');
        vbox.appendChild(div);

        // And below that we have description (if provided).
        if (extension.description.length > 0) {
          var description = this.ownerDocument.createElement('span');
          description.classList.add('extension-description');
          description.textContent = extension.description;
          vbox.appendChild(description);
        }

        // Immediately following the description, we have the
        // Options link (optional).
        if (extension.options_url) {
          var link = this.ownerDocument.createElement('a');
          link.classList.add('extension-links-trailing');
          link.textContent = localStrings.getString('extensionSettingsOptions');
          link.href = '#';
          link.addEventListener('click', this.handleOptions_.bind(this));
          vbox.appendChild(link);
        }

        // Then the optional Visit Website link.
        if (extension.homepageUrl) {
          var link = this.ownerDocument.createElement('a');
          link.classList.add('extension-links-trailing');
          link.textContent =
              localStrings.getString('extensionSettingsVisitWebsite');
          link.href = extension.homepageUrl;
          vbox.appendChild(link);
        }

        if (extension.warnings.length > 0) {
          var warningsDiv = this.ownerDocument.createElement('div');
          warningsDiv.classList.add('extension-warnings');

          var warningsHeader = this.ownerDocument.createElement('span');
          warningsHeader.classList.add('extension-warnings-title');
          warningsHeader.textContent =
              localStrings.getString('extensionSettingsWarningsTitle');
          warningsDiv.appendChild(warningsHeader);

          var warningList = this.ownerDocument.createElement('ul');
          for (var j = 0; j < extension.warnings.length; ++j) {
            var warningEntry = this.ownerDocument.createElement('li');
            warningEntry.textContent = extension.warnings[j];
            warningList.appendChild(warningEntry);
          }
          warningsDiv.appendChild(warningList);

          vbox.appendChild(warningsDiv);
        }

        // And now the details section that is normally hidden.
        var details = this.ownerDocument.createElement('div');
        details.classList.add('vbox');
        vbox.appendChild(details);

        this.decorateDetailsSection_(details, extension, expanded, butterbar);

        // And on the right of the details we have the Enable/Enabled checkbox.
        div = this.ownerDocument.createElement('div');
        hbox.appendChild(div);

        var section = this.ownerDocument.createElement('section');
        section.classList.add('extension-enabling');
        div.appendChild(section);

        if (!extension.terminated) {
          // The Enable checkbox.
          var input = this.ownerDocument.createElement('input');
          input.addEventListener('click', this.handleEnable_.bind(this));
          input.type = 'checkbox';
          input.name = 'toggle-' + id;
          input.disabled = !extension.mayDisable;
          if (extension.enabled)
            input.checked = true;
          input.id = 'toggle-' + id;
          section.appendChild(input);
          var label = this.ownerDocument.createElement('label');
          label.classList.add('extension-enabling-label');
          if (extension.enabled)
            label.classList.add('extension-enabling-label-bold');
          label.htmlFor = 'toggle-' + id;
          label.id = 'toggle-' + id + '-label';
          if (extension.enabled) {
            // Enabled (with a d).
            label.textContent =
                localStrings.getString('extensionSettingsEnabled');
          } else {
            // Enable (no d).
            label.textContent =
                localStrings.getString('extensionSettingsEnable');
          }
          section.appendChild(label);
        } else {
          // Extension has been terminated, show a Reload link.
          var link = this.ownerDocument.createElement('a');
          link.classList.add('extension-links-trailing');
          link.id = extension.id;
          link.textContent =
              localStrings.getString('extensionSettingsReload');
          link.href = '#';
          link.addEventListener('click', this.handleReload_.bind(this));
          section.appendChild(link);
        }

        // And, on the far right we have the uninstall button.
        var button = this.ownerDocument.createElement('button');
        button.classList.add('extension-delete');
        button.id = id;
        if (!extension.mayDisable)
          button.disabled = true;
        button.textContent = localStrings.getString('extensionSettingsRemove');
        button.addEventListener('click', this.handleUninstall_.bind(this));
        hbox.appendChild(button);
      }

      // Do one pass to find what the size of the checkboxes should be.
      var minCheckboxWidth = Infinity;
      var maxCheckboxWidth = 0;
      for (var i = 0; i < this.data_.extensions.length; ++i) {
        var label = $('toggle-' + this.data_.extensions[i].id + '-label');
        if (label.offsetWidth > maxCheckboxWidth)
          maxCheckboxWidth = label.offsetWidth;
        if (label.offsetWidth < minCheckboxWidth)
          minCheckboxWidth = label.offsetWidth;
      }

      // Do another pass, making sure checkboxes line up.
      var difference = maxCheckboxWidth - minCheckboxWidth;
      for (var i = 0; i < this.data_.extensions.length; ++i) {
        var label = $('toggle-' + this.data_.extensions[i].id + '-label');
        if (label.offsetWidth < maxCheckboxWidth)
          label.style.WebkitMarginEnd = difference.toString() + 'px';
      }
    },

    /**
     * Handles decorating the details section.
     * @param {Element} details The div that the details should be attached to.
     * @param {Object} extension The extension we are shoting the details for.
     * @param {boolean} expanded Whether to show the details expanded or not.
     * @param {boolean} showButterbar Whether to show the incognito warning or
     *                  not.
     * @private
     */
    decorateDetailsSection_: function(details, extension,
                                      expanded, showButterbar) {
      // This container div is needed because vbox display
      // overrides display:hidden.
      var detailsContents = this.ownerDocument.createElement('div');
      detailsContents.classList.add(expanded ? 'extension-details-visible' :
                                               'extension-details-hidden');
      detailsContents.id = extension.id + '_details';
      details.appendChild(detailsContents);

      var div = this.ownerDocument.createElement('div');
      div.classList.add('informative-text');
      detailsContents.appendChild(div);

      // Keep track of how many items we'll show in the details section.
      var itemsShown = 0;

      if (this.data_.developerMode) {
        // First we have the id.
        var content = this.ownerDocument.createElement('div');
        content.textContent =
            localStrings.getString('extensionSettingsExtensionId') +
                                   ' ' + extension.id;
        div.appendChild(content);
        itemsShown++;

        // Then, the path, if provided by unpacked extension.
        if (extension.isUnpacked) {
          content = this.ownerDocument.createElement('div');
          content.textContent =
              localStrings.getString('extensionSettingsExtensionPath') +
                                     ' ' + extension.path;
          div.appendChild(content);
          itemsShown++;
        }

        // Then, the 'managed, cannot uninstall/disable' message.
        if (!extension.mayDisable) {
          content = this.ownerDocument.createElement('div');
          content.textContent =
              localStrings.getString('extensionSettingsPolicyControlled');
          div.appendChild(content);
          itemsShown++;
        }

        // Then active views:
        if (extension.views.length > 0) {
          var table = this.ownerDocument.createElement('table');
          table.classList.add('extension-inspect-table');
          div.appendChild(table);
          var tr = this.ownerDocument.createElement('tr');
          table.appendChild(tr);
          var td = this.ownerDocument.createElement('td');
          td.classList.add('extension-inspect-left-column');
          tr.appendChild(td);
          var span = this.ownerDocument.createElement('span');
          td.appendChild(span);
          span.textContent =
              localStrings.getString('extensionSettingsInspectViews');

          td = this.ownerDocument.createElement('td');
          for (var i = 0; i < extension.views.length; ++i) {
            // Then active views:
            content = this.ownerDocument.createElement('div');
            var link = this.ownerDocument.createElement('a');
            link.classList.add('extension-links-view');
            link.textContent = extension.views[i].path;
            link.id = extension.id;
            link.href = '#';
            link.addEventListener('click', this.sendInspectMessage_.bind(this));
            content.appendChild(link);

            if (extension.views[i].incognito) {
              var incognito = this.ownerDocument.createElement('span');
              incognito.classList.add('extension-links-view');
              incognito.textContent =
                  localStrings.getString('viewIncognito');
              content.appendChild(incognito);
            }

            td.appendChild(content);
            tr.appendChild(td);

            itemsShown++;
          }
        }
      }

      var content = this.ownerDocument.createElement('div');
      detailsContents.appendChild(content);

      // Then Reload:
      if (extension.enabled && extension.allow_reload) {
        this.addLinkTo_(content,
                        localStrings.getString('extensionSettingsReload'),
                        extension.id,
                        this.handleReload_.bind(this));
        itemsShown++;
      }

      // Then Show (Browser Action) Button:
      if (extension.enabled && extension.enable_show_button) {
        this.addLinkTo_(content,
                        localStrings.getString('extensionSettingsShowButton'),
                        extension.id,
                        this.handleShowButton_.bind(this));
        itemsShown++;
      }

      if (extension.enabled) {
        // The 'allow in incognito' checkbox.
        var label = this.ownerDocument.createElement('label');
        label.classList.add('extension-checkbox-label');
        content.appendChild(label);
        var input = this.ownerDocument.createElement('input');
        input.addEventListener('click',
                               this.handleToggleEnableIncognito_.bind(this));
        input.id = extension.id;
        input.type = 'checkbox';
        if (extension.enabledIncognito)
          input.checked = true;
        label.appendChild(input);
        var span = this.ownerDocument.createElement('span');
        span.classList.add('extension-checkbox-span');
        span.textContent =
            localStrings.getString('extensionSettingsEnableIncognito');
        label.appendChild(span);
        itemsShown++;
      }

      if (extension.enabled && extension.wantsFileAccess) {
        // The 'allow access to file URLs' checkbox.
        label = this.ownerDocument.createElement('label');
        label.classList.add('extension-checkbox-label');
        content.appendChild(label);
        var input = this.ownerDocument.createElement('input');
        input.addEventListener('click',
                               this.handleToggleAllowFileUrls_.bind(this));
        input.id = extension.id;
        input.type = 'checkbox';
        if (extension.allowFileAccess)
          input.checked = true;
        label.appendChild(input);
        var span = this.ownerDocument.createElement('span');
        span.classList.add('extension-checkbox-span');
        span.textContent =
            localStrings.getString('extensionSettingsAllowFileAccess');
        label.appendChild(span);
        itemsShown++;
      }

      if (extension.enabled && !extension.is_hosted_app) {
        // And add a hidden warning message for allowInIncognito.
        content = this.ownerDocument.createElement('div');
        content.id = extension.id + '_incognitoWarning';
        content.classList.add('butter-bar');
        content.hidden = !showButterbar;
        detailsContents.appendChild(content);

        var span = this.ownerDocument.createElement('span');
        span.innerHTML =
            localStrings.getString('extensionSettingsIncognitoWarning');
        content.appendChild(span);
        itemsShown++;
      }

      var zippy = extension.id + '_zippy';
      $(zippy).hidden = !itemsShown;

      // If this isn't expanded now, make sure the newly-added controls
      // are not part of the tab order.
      if (!expanded) {
        var detailsControls = details.querySelectorAll('a, input');
        for (var i = 0; i < detailsControls.length; i++)
          detailsControls[i].tabIndex = -1;
      }
    },

    /**
     * A helper function to add contextual actions for extensions (action links)
     * to the page.
     */
    addLinkTo_: function(parent, linkText, id, handler) {
      var link = this.ownerDocument.createElement('a');
      link.className = 'extension-links-trailing';
      link.textContent = linkText;
      link.id = id;
      link.href = '#';
      link.addEventListener('click', handler);
      parent.appendChild(link);
    },

    /**
     * A lookup helper function to find an extension based on an id.
     * @param {string} id The |id| of the extension to look up.
     * @private
     */
    getExtensionWithId_: function(id) {
      for (var i = 0; i < this.data_.extensions.length; ++i) {
        if (this.data_.extensions[i].id == id)
          return this.data_.extensions[i];
      }
      return null;
    },

    /**
     * Handles a key down on the zippy icon.
     * @param {Event} e Key event.
     * @private
     */
    handleZippyKeyDown_: function(e) {
      if (e.keyCode == 13 || e.keyCode == 32)  // Enter or Space.
        this.handleZippyClick_(e);
    },

    /**
     * Handles the mouseclick on the zippy icon (that expands and collapses the
     * details section).
     * @param {Event} e Mouse event.
     * @private
     */
    handleZippyClick_: function(e) {
      var node = findIdNode(e.target.parentNode);
      var iter = this.firstChild;
      while (iter) {
        var zippy = $(iter.id + '_zippy');
        var details = $(iter.id + '_details');
        var container = zippy.parentElement;
        if (iter.id == node.id) {
          // Toggle visibility.
          if (iter.classList.contains('extension-list-item-expanded')) {
            // Hide yo kids! Hide yo wife!
            showingDetails[iter.id] = false;
            zippy.classList.remove('extension-zippy-expanded');
            zippy.classList.add('extension-zippy-collapsed');
            details.classList.remove('extension-details-visible');
            details.classList.add('extension-details-hidden');
            iter.classList.remove('extension-list-item-expanded');
            iter.classList.add('extension-list-item-collaped');
            container.setAttribute('aria-expanded', 'false');
            container.title =
                localStrings.getString('extensionSettingsShowDetails');
            var detailsControls = details.querySelectorAll('a, input');
            for (var i = 0; i < detailsControls.length; i++)
              detailsControls[i].tabIndex = -1;

            // Hide yo incognito warning.
            var butterBar =
                this.querySelector('#' + iter.id + '_incognitoWarning');
            if (butterBar !== null) {
              butterBar.hidden = true;
              showingWarning[iter.id] = false;
            }
          } else {
            // Show the contents.
            showingDetails[iter.id] = true;
            zippy.classList.remove('extension-zippy-collapsed');
            zippy.classList.add('extension-zippy-expanded');
            details.classList.remove('extension-details-hidden');
            details.classList.add('extension-details-visible');
            iter.classList.remove('extension-list-item-collaped');
            iter.classList.add('extension-list-item-expanded');
            container.setAttribute('aria-expanded', 'true');
            container.title =
                localStrings.getString('extensionSettingsHideDetails');
            var detailsControls = details.querySelectorAll('a, input');
            for (var i = 0; i < detailsControls.length; i++)
              detailsControls[i].tabIndex = 0;
          }
        }
        iter = iter.nextSibling;
      }
    },

    /**
     * Handles the 'searchChanged' event. This is used to limit the number of
     * items to show in the list, when the user is searching for items with the
     * search box. Otherwise, if one match is found, the whole list of
     * extensions would be shown when we only want the matching items to be
     * found.
     * @param {Event} e Change event.
     * @private
     */
    searchChangedHandler_: function(e) {
      var searchString = e.searchText;
      var child = this.firstChild;
      while (child) {
        var extension = this.getExtensionWithId_(child.id);
        if (searchString.length == 0) {
          // Show all.
          child.classList.remove('search-suppress');
        } else {
          // If the search string does not appear within the text of the
          // extension, then hide it.
          if ((extension.name.toLowerCase().indexOf(searchString) < 0) &&
              (extension.version.toLowerCase().indexOf(searchString) < 0) &&
              (extension.description.toLowerCase().indexOf(searchString) < 0)) {
            // Hide yo extension!
            child.classList.add('search-suppress');
          } else {
            // Show yourself!
            child.classList.remove('search-suppress');
          }
        }
        child = child.nextSibling;
      }
    },

    /**
     * Handles the Reload Extension functionality.
     * @param {Event} e Change event.
     * @private
     */
    handleReload_: function(e) {
      var node = findIdNode(e.target);
      chrome.send('extensionSettingsReload', [node.id]);
    },

    /**
     * Handles the Show (Browser Action) Button functionality.
     * @param {Event} e Change event.
     * @private
     */
    handleShowButton_: function(e) {
      var node = findIdNode(e.target);
      chrome.send('extensionSettingsShowButton', [node.id]);
    },

    /**
     * Handles the Enable/Disable Extension functionality.
     * @param {Event} e Change event.
     * @private
     */
    handleEnable_: function(e) {
      var node = findIdNode(e.target.parentNode);
      var extension = this.getExtensionWithId_(node.id);
      chrome.send('extensionSettingsEnable',
                  [node.id, extension.enabled ? 'false' : 'true']);
      chrome.send('extensionSettingsRequestExtensionsData');
    },

    /**
     * Handles the Uninstall Extension functionality.
     * @param {Event} e Change event.
     * @private
     */
    handleUninstall_: function(e) {
      var node = findIdNode(e.target.parentNode);
      chrome.send('extensionSettingsUninstall', [node.id]);
      chrome.send('extensionSettingsRequestExtensionsData');
    },

    /**
     * Handles the View Options link.
     * @param {Event} e Change event.
     * @private
     */
    handleOptions_: function(e) {
      var node = findIdNode(e.target.parentNode);
      var extension = this.getExtensionWithId_(node.id);
      chrome.send('extensionSettingsOptions', [extension.id]);
      e.preventDefault();
    },

    /**
     * Handles the Enable Extension In Incognito functionality.
     * @param {Event} e Change event.
     * @private
     */
    handleToggleEnableIncognito_: function(e) {
      var node = findIdNode(e.target);
      var butterBar = document.getElementById(node.id + '_incognitoWarning');
      butterBar.hidden = !e.target.checked;
      showingWarning[node.id] = e.target.checked;
      chrome.send('extensionSettingsEnableIncognito',
                  [node.id, String(e.target.checked)]);
    },

    /**
     * Handles the Allow On File URLs functionality.
     * @param {Event} e Change event.
     * @private
     */
    handleToggleAllowFileUrls_: function(e) {
      var node = findIdNode(e.target);
      chrome.send('extensionSettingsAllowFileAccess',
                  [node.id, String(e.target.checked)]);
    },

    /**
     * Tell the C++ ExtensionDOMHandler to inspect the page detailed in
     * |viewData|.
     * @param {Event} e Change event.
     * @private
     */
    sendInspectMessage_: function(e) {
      var extension = this.getExtensionWithId_(e.srcElement.id);
      for (var i = 0; i < extension.views.length; ++i) {
        if (extension.views[i].path == e.srcElement.innerText) {
          // TODO(aa): This is ghetto, but WebUIBindings doesn't support sending
          // anything other than arrays of strings, and this is all going to get
          // replaced with V8 extensions soon anyway.
          chrome.send('extensionSettingsInspect', [
            String(extension.views[i].renderProcessId),
            String(extension.views[i].renderViewId)
          ]);
        }
      }
    },
  };

  return {
    ExtensionsList: ExtensionsList
  };
});
