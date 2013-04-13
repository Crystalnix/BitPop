// Copyright (c) 2013 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix, Viatcheslav Gachkaylo <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options.uncensor_filter', function() {
  /** @const */ var DeletableItemList = options.DeletableItemList;
  /** @const */ var DeletableItem = options.DeletableItem;
  var ListSingleSelectionModel = cr.ui.ListSingleSelectionModel;

  ///** @const */ var ListSelectionController = cr.ui.ListSelectionController;

  function DomainRedirectionListItem(redirect) {
    var el = cr.doc.createElement('div');
    el.redirect_ = redirect;
    DomainRedirectionListItem.decorate(el);
    return el;
  }

  DomainRedirectionListItem.decorate = function(el) {
    el.__proto__ = DomainRedirectionListItem.prototype;
    el.decorate();
  };

  DomainRedirectionListItem.prototype = {
    __proto__: DeletableItem.prototype,

    srcField_: null,
    dstField_: null,

    /** @inheritDoc */
    decorate: function() {
      DeletableItem.prototype.decorate.call(this);

      var redirect = this.redirect_;

      if (('isHeader' in redirect) && redirect.isHeader)
        this.deletable = false;
      else
        this.deletable = true;

      // Construct the src column.
      var srcColEl = this.ownerDocument.createElement('div');
      srcColEl.className = 'src-domain-column';
      if (('isHeader' in redirect) && redirect.isHeader)
        srcColEl.classList.add('header-column');
      srcColEl.classList.add('weakrtl');
      this.contentElement.appendChild(srcColEl);

      // Then the keyword column.
      var dstColEl = this.ownerDocument.createElement('div');
      dstColEl.className = 'dst-domain-column';
      if (('isHeader' in redirect) && redirect.isHeader)
        dstColEl.classList.add('header-column');
      dstColEl.classList.add('weakrtl');
      this.contentElement.appendChild(dstColEl);

      srcColEl.textContent = redirect.srcDomain;
      dstColEl.textContent = redirect.dstDomain;
    },
  };

  var DomainRedirectionList = cr.ui.define('list');

  DomainRedirectionList.prototype = {
    __proto__: DeletableItemList.prototype,

    decorate: function() {
      DeletableItemList.prototype.decorate.call(this);
      this.selectionModel = new ListSingleSelectionModel();
    },

    /** @inheritDoc */
    createItem: function(redirect) {
      return new DomainRedirectionListItem(redirect);
    },

    /** @inheritDoc */
    deleteItemAtIndex: function(index) {
      var item = this.items[index];
      var delData = this.dataModel.item(index);

      // A vaabshe-to:
      // console.assert(this.arraySrc && this.arraySrc.length &&
      //                  this.arraySrc.length !== 0)
      var selfDataArray = (this.arraySrc && this.arraySrc.slice()) || [];
      var companionDataArray = (this.companion.arraySrc &&
          this.companion.arraySrc.slice()) || [];

      for (var i = 0; i < companionDataArray.length; i++)
        console.assert(companionDataArray[i].srcDomain !== delData.srcDomain);

      if (selfDataArray[delData.modelIndex].srcDomain === delData.srcDomain) {
        companionDataArray.push(selfDataArray[delData.modelIndex]);
        selfDataArray.splice(delData.modelIndex, 1);
      }

      function modelToPref(modelArray) {
        var res = {};
        for (var i = 0; i < modelArray.length; i++)
          res[modelArray[i].srcDomain] = modelArray[i].dstDomain;
        return JSON.stringify(res);
      }

      if (this.isFilterList)
        //Preferences.setStringPref("bitpop.uncensor_domain_exceptions",
        //    modelToPref(companionDataArray), '');
        chrome.send("changeUncensorExceptions", [modelToPref(companionDataArray)]);
      else
        //Preferences.setStringPref("bitpop.uncensor_domain_exceptions",
        //    modelToPref(companionDataArray), '');
        chrome.send("changeUncensorExceptions", [modelToPref(selfDataArray)]);
    },

  };

  // Export
  return {
    DomainRedirectionList: DomainRedirectionList
  };

});

