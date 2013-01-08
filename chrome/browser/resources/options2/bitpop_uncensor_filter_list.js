// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix, Viatcheslav Gachkaylo <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options.uncensor_filter', function() {
  /** @const */ var DeletableItemList = options.DeletableItemList;
  /** @const */ var DeletableItem = options.DeletableItem;

  var updateFiltersList = options.BitpopUncensorFilterOverlay.updateFiltersList;
  var listArrayToPref = options.BitpopUncensorFilterOverlay.listArrayToPref;
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
      this.editable = false;

      // Construct the src column.
      var srcColEl = this.ownerDocument.createElement('div');
      srcColEl.className = 'src-domain-column';
      if (isHeader in redirect && redirect.isHeader)
        srcColEl.className += ' header-column';
      srcColEl.classList.add('weakrtl');
      this.contentElement.appendChild(srcColEl);

      // Then the keyword column.
      var dstColEl = this.ownerDocument.createElement('div');
      dstColEl.className = 'dst-domain-column';
      if (isHeader in redirect && redirect.isHeader)
        dstColEl.className += ' header-column';
      dstColEl.classList.add('weakrtl');
      this.contentElement.appendChild(dstColEl);

      srcColEl.textContent = redirect.src;
      dstColEl.textContent = redirect.dst;
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
      var item = this.dataModel.item(index);
      var delData = item.redirect_;

      var selfDataArray = this.arraySrc.slice();
      var companionDataArray = this.companion.arraySrc;
      for (var i = 0; i < companionDataArray.length; i++)
        console.assert(companionDataArray[i].src !== delData.src);
      
      if (selfDataArray[delData.modelIndex].src === delData.src)
        selfDataArray.splice(delData.modelIndex, 1);
      
      if (this.isFilterList)
        Preferences.setStringPref("bitpop.uncensor_domain_filter", 
          listArrayToPref(selfDataArray), '');
      else
        Preferences.setStringPref("bitpop.uncensor_domain_exceptions",
          listArrayToPref(selfDataArray), '');
    },

  };

  // Export
  return {
    DomainRedirectionList: DomainRedirectionList
  };

});

