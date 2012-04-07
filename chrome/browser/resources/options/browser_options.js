// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  const OptionsPage = options.OptionsPage;
  const ArrayDataModel = cr.ui.ArrayDataModel;

  //
  // BrowserOptions class
  // Encapsulated handling of browser options page.
  //
  function BrowserOptions() {
    OptionsPage.call(this, 'browser',
                     templateData.browserPageTabTitle,
                     'browserPage');
  }

  cr.addSingletonGetter(BrowserOptions);

  BrowserOptions.prototype = {
    // Inherit BrowserOptions from OptionsPage.
    __proto__: options.OptionsPage.prototype,

    startup_pages_pref_: {
      'name': 'session.urls_to_restore_on_startup',
      'disabled': false
    },

    /**
     * At autocomplete list that can be attached to a text field during editing.
     * @type {HTMLElement}
     * @private
     */
    autocompleteList_: null,

    // The cached value of the instant.confirm_dialog_shown preference.
    instantConfirmDialogShown_: false,

    /**
     * Initialize BrowserOptions page.
     */
    initializePage: function() {
      // Call base class implementation to start preference initialization.
      OptionsPage.prototype.initializePage.call(this);

      // Wire up controls.
      $('startupUseCurrentButton').onclick = function(event) {
        chrome.send('setStartupPagesToCurrentPages');
      };
      $('defaultSearchManageEnginesButton').onclick = function(event) {
        OptionsPage.navigateToPage('searchEngines');
        chrome.send('coreOptionsUserMetricsAction',
            ['Options_ManageSearchEngines']);
      };
      $('defaultSearchEngine').onchange = this.setDefaultSearchEngine_;

      var self = this;
      $('instantEnabledCheckbox').customChangeHandler = function(event) {
        if (this.checked) {
          if (self.instantConfirmDialogShown_)
            chrome.send('enableInstant');
          else
            OptionsPage.navigateToPage('instantConfirm');
        } else {
          chrome.send('disableInstant');
        }
        return true;
      };

      $('instantFieldTrialCheckbox').addEventListener('change',
          function(event) {
            this.checked = true;
            chrome.send('disableInstant');
          });

      Preferences.getInstance().addEventListener('instant.confirm_dialog_shown',
          this.onInstantConfirmDialogShownChanged_.bind(this));

      Preferences.getInstance().addEventListener('instant.enabled',
          this.onInstantEnabledChanged_.bind(this));

      Preferences.getInstance().addEventListener(
          $('homepageUseNTPButton').pref,
          this.onHomepageUseNTPChanged_);
      var homepageField = $('homepageURL');
      homepageField.addEventListener('focus', function(event) {
        self.autocompleteList_.attachToInput(homepageField);
      });
      homepageField.addEventListener('blur', function(event) {
        self.autocompleteList_.detach();
      });
      homepageField.addEventListener('keydown', function(event) {
        // Remove focus when the user hits enter since people expect feedback
        // indicating that they are done editing.
        if (event.keyIdentifier == 'Enter')
          homepageField.blur();
      });

      // Text fields may change widths when the window changes size, so make
      // sure the suggestion list stays in sync.
      window.addEventListener('resize', function() {
        self.autocompleteList_.syncWidthToInput();
      });

      // Ensure that changes are committed when closing the page.
      window.addEventListener('unload', function() {
        if (document.activeElement == homepageField)
          homepageField.blur();
      });

      if (!cr.isChromeOS) {
        $('defaultBrowserUseAsDefaultButton').onclick = function(event) {
          chrome.send('becomeDefaultBrowser');
        };

        $('autoLaunch').addEventListener('click',
                                         this.handleAutoLaunchChanged_);
      }

      var startupPagesList = $('startupPagesList');
      options.browser_options.StartupPageList.decorate(startupPagesList);
      startupPagesList.autoExpands = true;

      // Check if we are in the guest mode.
      if (cr.commandLine && cr.commandLine.options['--bwsi']) {
        // Hide the startup section.
        $('startupSection').hidden = true;
      } else {
        // Initialize control enabled states.
        Preferences.getInstance().addEventListener('session.restore_on_startup',
            this.updateCustomStartupPageControlStates_.bind(this));
        Preferences.getInstance().addEventListener(
            this.startup_pages_pref_.name,
            this.handleStartupPageListChange_.bind(this));

        this.updateCustomStartupPageControlStates_();
      }

      var suggestionList = new cr.ui.AutocompleteList();
      suggestionList.autoExpands = true;
      suggestionList.suggestionUpdateRequestCallback =
          this.requestAutocompleteSuggestions_.bind(this);
      $('main-content').appendChild(suggestionList);
      this.autocompleteList_ = suggestionList;
      startupPagesList.autocompleteList = suggestionList;
    },

    /**
     * Called when the value of the instant.confirm_dialog_shown preference
     * changes. Cache this value.
     * @param {Event} event Change event.
     * @private
     */
    onInstantConfirmDialogShownChanged_: function(event) {
      this.instantConfirmDialogShown_ = event.value['value'];
    },

    /**
     * Called when the value of the instant.enabled preference changes. Request
     * the state of the Instant field trial experiment.
     * @param {Event} event Change event.
     * @private
     */
    onInstantEnabledChanged_: function(event) {
      chrome.send('getInstantFieldTrialStatus');
    },

    /**
     * Called to set the Instant field trial status.
     * @param {boolean} enabled If true, the experiment is enabled.
     * @private
     */
    setInstantFieldTrialStatus_: function(enabled) {
      $('instantEnabledCheckbox').hidden = enabled;
      $('instantFieldTrialCheckbox').hidden = !enabled;
      $('instantLabel').htmlFor = enabled ? 'instantFieldTrialCheckbox'
                                          : 'instantEnabledCheckbox';
    },

    /**
     * Called when the value of the homepage-use-NTP pref changes.
     * Updates the disabled state of the homepage text field.
     * Notice that the text field can be disabled for other reasons too
     * (it can be managed by policy, for instance).
     * @param {Event} event Change event.
     * @private
     */
    onHomepageUseNTPChanged_: function(event) {
      var homepageField = $('homepageURL');
      var homepageUseURLButton = $('homepageUseURLButton');
      homepageField.setDisabled('radioNotSelected',
                                !homepageUseURLButton.checked);
    },

    /**
     * Update the Default Browsers section based on the current state.
     * @param {string} statusString Description of the current default state.
     * @param {boolean} isDefault Whether or not the browser is currently
     *     default.
     * @param {boolean} canBeDefault Whether or not the browser can be default.
     * @private
     */
    updateDefaultBrowserState_: function(statusString, isDefault,
                                         canBeDefault) {
      var label = $('defaultBrowserState');
      label.textContent = statusString;

      $('defaultBrowserUseAsDefaultButton').disabled = !canBeDefault ||
                                                       isDefault;
    },

    /**
     * Clears the search engine popup.
     * @private
     */
    clearSearchEngines_: function() {
      $('defaultSearchEngine').textContent = '';
    },

    /**
     * Updates the search engine popup with the given entries.
     * @param {Array} engines List of available search engines.
     * @param {number} defaultValue The value of the current default engine.
     * @param {boolean} defaultManaged Whether the default search provider is
     *     managed. If true, the default search provider can't be changed.
     */
    updateSearchEngines_: function(engines, defaultValue, defaultManaged) {
      this.clearSearchEngines_();
      engineSelect = $('defaultSearchEngine');
      engineSelect.disabled = defaultManaged;
      engineCount = engines.length;
      var defaultIndex = -1;
      for (var i = 0; i < engineCount; i++) {
        var engine = engines[i];
        var option = new Option(engine['name'], engine['index']);
        if (defaultValue == option.value)
          defaultIndex = i;
        engineSelect.appendChild(option);
      }
      if (defaultIndex >= 0)
        engineSelect.selectedIndex = defaultIndex;
    },

    /**
     * Returns true if the custom startup page control block should
     * be enabled.
     * @returns {boolean} Whether the startup page controls should be
     *     enabled.
     */
    shouldEnableCustomStartupPageControls: function(pages) {
      return $('startupShowPagesButton').checked &&
          !this.startup_pages_pref_.disabled;
    },

    /**
     * Updates the startup pages list with the given entries.
     * @param {Array} pages List of startup pages.
     * @private
     */
    updateStartupPages_: function(pages) {
      var model = new ArrayDataModel(pages);
      // Add a "new page" row.
      model.push({
        'modelIndex': '-1'
      });
      $('startupPagesList').dataModel = model;
    },

    /**
     * Sets the enabled state of the custom startup page list controls
     * based on the current startup radio button selection.
     * @private
     */
    updateCustomStartupPageControlStates_: function() {
      var disable = !this.shouldEnableCustomStartupPageControls();
      var startupPagesList = $('startupPagesList');
      startupPagesList.disabled = disable;
      startupPagesList.setAttribute('tabindex', disable ? -1 : 0);
      // Explicitly set disabled state for input text elements.
      var inputs = startupPagesList.querySelectorAll("input[type='text']");
      for (var i = 0; i < inputs.length; i++)
        inputs[i].disabled = disable;
      $('startupUseCurrentButton').disabled = disable;
    },

    /**
     * Handles change events of the preference
     * 'session.urls_to_restore_on_startup'.
     * @param {event} preference changed event.
     * @private
     */
    handleStartupPageListChange_: function(event) {
      this.startup_pages_pref_.disabled = event.value['disabled'];
      this.updateCustomStartupPageControlStates_();
    },

    /**
     * Sets the default search engine based on the popup selection.
     */
    setDefaultSearchEngine_: function() {
      var engineSelect = $('defaultSearchEngine');
      var selectedIndex = engineSelect.selectedIndex;
      if (selectedIndex >= 0) {
        var selection = engineSelect.options[selectedIndex];
        chrome.send('setDefaultSearchEngine', [String(selection.value)]);
      }
    },

    /**
     * Sets or clear whether Chrome should Auto-launch on computer startup.
     */
    handleAutoLaunchChanged_: function() {
      chrome.send('toggleAutoLaunch', [Boolean($('autoLaunch').checked)]);
    },

    /**
     * Sends an asynchronous request for new autocompletion suggestions for the
     * the given query. When new suggestions are available, the C++ handler will
     * call updateAutocompleteSuggestions_.
     * @param {string} query List of autocomplete suggestions.
     * @private
     */
    requestAutocompleteSuggestions_: function(query) {
      chrome.send('requestAutocompleteSuggestions', [query]);
    },

    /**
     * Updates the autocomplete suggestion list with the given entries.
     * @param {Array} pages List of autocomplete suggestions.
     * @private
     */
    updateAutocompleteSuggestions_: function(suggestions) {
      var list = this.autocompleteList_;
      // If the trigger for this update was a value being selected from the
      // current list, do nothing.
      if (list.targetInput && list.selectedItem &&
          list.selectedItem['url'] == list.targetInput.value)
        return;
      list.suggestions = suggestions;
    },

    /**
      * Shows the autoLaunch preference and initializes its checkbox value.
      */
    updateAutoLaunchState_: function(enabled) {
      $('autoLaunchOption').hidden = false;
      $('autoLaunch').checked = enabled;
    },
  };

  BrowserOptions.updateDefaultBrowserState = function(statusString, isDefault,
                                                      canBeDefault) {
    if (!cr.isChromeOS) {
      BrowserOptions.getInstance().updateDefaultBrowserState_(statusString,
                                                              isDefault,
                                                              canBeDefault);
    }
  };

  BrowserOptions.updateSearchEngines = function(engines, defaultValue,
                                                defaultManaged) {
    BrowserOptions.getInstance().updateSearchEngines_(engines, defaultValue,
                                                      defaultManaged);
  };

  BrowserOptions.updateStartupPages = function(pages) {
    BrowserOptions.getInstance().updateStartupPages_(pages);
  };

  BrowserOptions.updateAutocompleteSuggestions = function(suggestions) {
    BrowserOptions.getInstance().updateAutocompleteSuggestions_(suggestions);
  };

  BrowserOptions.updateAutoLaunchState = function(enabled) {
    BrowserOptions.getInstance().updateAutoLaunchState_(enabled);
  };

  BrowserOptions.setInstantFieldTrialStatus = function(enabled) {
    BrowserOptions.getInstance().setInstantFieldTrialStatus_(enabled);
  };

  // Export
  return {
    BrowserOptions: BrowserOptions
  };

});
