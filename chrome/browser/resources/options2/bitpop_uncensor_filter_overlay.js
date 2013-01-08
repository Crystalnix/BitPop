// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix, Viatcheslav Gachkaylo <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  /** @const */ var OptionsPage = options.OptionsPage;
  /** @const */ var ArrayDataModel = cr.ui.ArrayDataModel;

  function fromPrefListToActual(prefList) {
  	var res = {};
  	for (var i = 0; i < prefList.length; i++) {
  		res[prefList[i]['srcDomain']] = prefList[i]['dstDomain'];
  	}
  	return res;
  }

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
      this.setUpList_(this.filterList_);

      this.exceptionList_ = $('domain-exceptions-table');
      this.setUpList_(this.exceptionList_);

      this.filterList_.companion = this.exceptionList_;
      this.exceptionList_.companion = this.filterList_;

      Preferences.getInstance().addEventListener("bitpop.uncensor_domain_filter",
      	this.onFilterChange_.bind(this));
      Preferences.getInstance().addEventListener("bitpop.uncensor_domain_exceptions",
      	this.onExceptionsChange_.bind(this));
    },

    setUpList_: function(list) {
      options.uncensor_filter.DomainRedirectionList.decorate(list);
      list.autoExpands = true;
    },

    updateFiltersList_: function(filter, exceptions) {
    	this.filterSrc_ = filter;
    	this.exceptionsSrc_ = exceptions;

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

      this.initListDiv_('filter-list-div', 'filterList_', newFilter);
      this.initListDiv_('exception-list-div', 'exceptionList_', newExceptions);
    },

		function initListDiv_(divId, dataMemberName, listData) {
			function sortFilterList(fList) {
      	return fList.map(function(x) {
	      	return [x, x.src.toLocaleLowerCase()];
	      }).sort(function(a, b) {
	      	return a[1].localeCompare(b[1]);
	      }).map(function(x) {
	      	return x[0];
	      });
      };

    	if (listData.length > 0) {
    		$(divId).hidden = false;
    		listData = sortFilterList(listData);
    		for (var i = 0; i < listData.length; i++) {
    			listData[i].modelIndex = i;
    		}

    		var modelData = listData.slice().unshift(
	    		{
	    			src: loadTimeData.getString('uncensorOriginalDomainHeader'), 
	    			dst: loadTimeData.getString('uncensorNewLocationHeader'),
	    			isHeader: true
	    		}
    		);

    		this[dataMemberName].dataModel = new ArrayDataModel(listData);
    		this[dataMemberName].arraySrc = listData;
    		this[dataMemberName].isFilterList = (dataMemberName === "filterList_");
    	} else {
    		$(divId).hidden = true;
    	}
    },

    onFilterChange_: function(event) {
    	var filter = JSON.parse(event.value['value']);
    	var actFilter = fromPrefListToActual(filter);

    	this.updateFiltersList_(actFilter, this.exceptionsSrc_);
    },

    onExceptionsChange_: function(event) {
    	var exceptions = JSON.parse(event.value['value']);
    	var actExceptions = fromPrefListToActual(exceptions);

    	this.updateFiltersList_(this.filterSrc_, actExceptions);
    },

    initLists_: function(filterPref, extensionsPref) {
    	function prefValueToObj(prefValue) {
    		var res = {};
    		var val = JSON.parse(prefValue);
    		for (var i = 0; i < val.length; i++) {
    			res[val[i]['srcDomain']] = val[i]['dstDomain'];
    		}
    		return res;
    	};

    	var filter = prefValueToObj(filterPref);
    	var exceptions = prefValueToObj(extensionsPref);

    	this.updateFiltersList_(filter, exceptions);
    },

  };

  BitpopUncensorFilterOverlay.updateFiltersList = function(filter,
                                                        	 exceptions) {
    BitpopUncensorFilterOverlay.getInstance().updateFiltersList_(filter,
                                                         				 exceptions);
  };

	BitpopUncensorFilterOverlay.initLists = function(filterPref,
                                                   exceptionsPref) {
    BitpopUncensorFilterOverlay.getInstance().initLists_(filterPref,
                                                         exceptionsPref);
  };

  BitpopUncensorFilterOverlay.listArrayToObj = function (listArray) {
    var res = {};
    for (var i = 0; i < listArray.length; i++)
      res[listArray[i].src] = listArray[i].dst;

    return res;
  };

  BitpopUncensorFilterOverlay.listArrayToPref = function (listArray) {
  	var res = [];
  	for (var i = 0; i < listArray.length; i++)
  		res.push({ srcDomain: listArray[i].src, dstDomain: listArray[i].dst });

  	return res;
  };

  // Export
  return {
    BitpopUncensorFilterOverlay: BitpopUncensorFilterOverlay,
  };

});

