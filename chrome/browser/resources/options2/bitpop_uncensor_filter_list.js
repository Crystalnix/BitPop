// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix, Viatcheslav Gachkaylo <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options.uncensor_filter', function() {
  /** @const */ var DeletableItemList = options.DeletableItemList;
  /** @const */ var DeletableItem = options.DeletableItem;
  ///** @const */ var ListSelectionController = cr.ui.ListSelectionController;

  function DomainRedirectionListItem(redirect) {
    var el = cr.doc.createElement('div');
    el.redirect_ = redirect;
    DomainRedirectionListItem.decorate(el);
    return el;
  }

  SearchEngineListItem.decorate = function(el) {
    el.__proto__ = DomainRedirectionListItem.prototype;
    el.decorate();
  };

  SearchEngineListItem.prototype = {
    __proto__: DeletableItem.prototype,

    srcField_: null,
    dstField_: null,

    /** @inheritDoc */
    decorate: function() {
      DeletableItem.prototype.decorate.call(this);

      var redirect = this.redirect_;
      
      this.deletable = true;

      // Construct the src column.
      var nameColEl = this.ownerDocument.createElement('div');
      nameColEl.className = 'src-domain-column';
      nameColEl.classList.add('weakrtl');
      this.contentElement.appendChild(nameColEl);

      // Then the keyword column.
      var keywordEl = this.createEditableTextCell(engine['keyword']);
      keywordEl.className = 'keyword-column';
      keywordEl.classList.add('weakrtl');
      this.contentElement.appendChild(keywordEl);

      // And the URL column.
      var urlEl = this.createEditableTextCell(engine['url']);
      var urlWithButtonEl = this.ownerDocument.createElement('div');
      urlWithButtonEl.appendChild(urlEl);
      urlWithButtonEl.className = 'url-column';
      urlWithButtonEl.classList.add('weakrtl');
      this.contentElement.appendChild(urlWithButtonEl);
      // Add the Make Default button. Temporary until drag-and-drop re-ordering
      // is implemented. When this is removed, remove the extra div above.
      if (engine['canBeDefault']) {
        var makeDefaultButtonEl = this.ownerDocument.createElement('button');
        makeDefaultButtonEl.className = 'custom-appearance list-inline-button';
        makeDefaultButtonEl.textContent =
            loadTimeData.getString('makeDefaultSearchEngineButton');
        makeDefaultButtonEl.onclick = function(e) {
          chrome.send('managerSetDefaultSearchEngine', [engine['modelIndex']]);
        };
        // Don't select the row when clicking the button.
        makeDefaultButtonEl.onmousedown = function(e) {
          e.stopPropagation();
        };
        urlWithButtonEl.appendChild(makeDefaultButtonEl);
      }

      // Do final adjustment to the input fields.
      this.nameField_ = nameEl.querySelector('input');
      // The editable field uses the raw name, not the display name.
      this.nameField_.value = engine['name'];
      this.keywordField_ = keywordEl.querySelector('input');
      this.urlField_ = urlEl.querySelector('input');

      if (engine['urlLocked'])
        this.urlField_.disabled = true;

      if (this.isPlaceholder) {
        this.nameField_.placeholder =
            loadTimeData.getString('searchEngineTableNamePlaceholder');
        this.keywordField_.placeholder =
            loadTimeData.getString('searchEngineTableKeywordPlaceholder');
        this.urlField_.placeholder =
            loadTimeData.getString('searchEngineTableURLPlaceholder');
      }

      var fields = [this.nameField_, this.keywordField_, this.urlField_];
        for (var i = 0; i < fields.length; i++) {
        fields[i].oninput = this.startFieldValidation_.bind(this);
      }

      // Listen for edit events.
      if (engine['canBeEdited']) {
        this.addEventListener('edit', this.onEditStarted_.bind(this));
        this.addEventListener('canceledit', this.onEditCancelled_.bind(this));
        this.addEventListener('commitedit', this.onEditCommitted_.bind(this));
      } else {
        this.editable = false;
      }
    },
  };

  var DomainRedirectionList = cr.ui.define('list');

  DomainRedirectionList.prototype = {
    __proto__: DeletableItemList.prototype,

    /** @inheritDoc */
    createItem: function(redirect) {
      return new DomainRedirectionListItem(redirect);
    },

    /** @inheritDoc */
    deleteItemAtIndex: function(index) {
      var modelIndex = this.dataModel.item(index)['modelIndex'];
    },
  };

  // Export
  return {
    DomainRedirectionList: DomainRedirectionList
  };

});

