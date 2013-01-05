// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix, Viatcheslav Gachkaylo <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  /** @const */ var OptionsPage = options.OptionsPage;
  /** @const */ var ArrayDataModel = cr.ui.ArrayDataModel;

  function BitpopUncensorFilterOverlay() {
    this.activeNavTab = null;
    OptionsPage.call(this, 'uncensorFilter',
                     loadTimeData.getString('uncensorFilterOverlayTitle'),
                     'bitpop-uncensor-filter-overlay-page');
  }

  cr.addSingletonGetter(BitpopUncensorFilterOverlay);

  BitpopUncensorFilterOverlay.prototype = {
    __proto__: OptionsPage.prototype,

    filterList_: null,

    exceptionList_: null,

    /** inheritDoc */
    initializePage: function() {
      OptionsPage.prototype.initializePage.call(this);

      this.filterList_ = $('domain-filter-table');
      this.setUpList_(this.defaultsList_);

      this.exceptionList_ = $('domain-exceptions-table');
      this.setUpList_(this.exceptionList_);
    },

    setUpList_: function(list) {
      options.uncensor_filter.DomainRedirectionList.decorate(list);
      list.autoExpands = true;
    },

    updateFiltersList_: function(filter, exceptions) {
      var newFilter = [];
      for (var i in filter) {
      	if (filter.hasOwnProperty(i) && !(i in exceptions))
      	  newFilter.push({ src: i, dst: filter[i] });
      }

      var newExceptions = [];
      for (var i in exceptions) {
      	if (exceptions.hasOwnProperty(i))
      		newExceptions.push({ src: i, dst: exceptions[i] });
      }

      function sortFilterList(fList) {
      	return fList.map(function(x) {
	      	return [x, x.src.toLocaleLowerCase()];
	      }).sort(function(a, b) {
	      	return a[1].localeCompare(b[1]);
	      }).map(function(x) {
	      	return x[0];
	      });
      };

      var self = this;
      function initListDiv(divId, dataMemberName, listData) {
      	if (listData.length > 0) {
      		$(divId).hidden = false;
      		listData = sortFilterList(listData);
      		self[dataMemberName].dataModel = new ArrayDataModel(listData);
      	} else {
      		$(divId).hidden = true;
      	}
      }

      initListDiv('filter-list-div', 'filterList_', newFilter);
      initListDiv('exception-list-div', 'exceptionList_', newExceptions);
    },
  };

  BitpopUncensorFilterOverlay.updateFiltersList = function(filter,
                                                        	 exceptions) {
    BitpopUncensorFilterOverlay.getInstance().updateFiltersList_(filter,
                                                         				 exceptions);
  };

  // Export
  return {
    BitpopUncensorFilterOverlay: BitpopUncensorFilterOverlay
  };

});

