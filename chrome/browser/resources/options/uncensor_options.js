// Copyright (c) 2011 House of Life Property ltd.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  const OptionsPage = options.OptionsPage;
  const ArrayDataModel = cr.ui.ArrayDataModel;

  //
  // UncensorOptions class
  // Encapsulated handling of uncensor extension options page.
  //
  function UncensorOptions() {
    OptionsPage.call(this, 'uncensor',
                     templateData.uncensorPageTabTitle,
                     'uncensorPage');
  }

  cr.addSingletonGetter(UncensorOptions);

  UncensorOptions.prototype = {
    // Inherit BrowserOptions from OptionsPage.
    __proto__: options.OptionsPage.prototype,
    
    initializePage: function() {
       // Call base class implementation to start preference initialization.
       OptionsPage.prototype.initializePage.call(this);
    //   
    //   // TODO: pass the extension's localStorage to this script
    //   // TODO: get rid of accessing elements by form name (ex. document.uncensor_form...)
    //   var prefs = JSON.parse(localStorage.prefs);
    //   setCheckedValue(document.uncensor_form.shouldRedirect, prefs.shouldRedirect ? "1" : "0");
    //   document.uncensor_form.showMessage.checked = prefs.showMessage;
    //   document.uncensor_form.notifyUpdate.checked = prefs.notifyUpdate;
    //   var table = document.getElementById("domain_filter_table");
    //   var rowIndex = 1;
    //   for (var originalDomain in prefs.domainFilter) {
    //     var newRow = table.insertRow(-1);
    //     newRow.onmouseover = function () { this.className = 'highlight'; };
    //     newRow.onmouseout = function() { this.className = ''; };
    // 
    //     var oCell = newRow.insertCell(-1);
    //     oCell.innerHTML = "<img src='chrome://favicon/' width='16' height='16' alt='' />";
    //     oCell.vAlign = "middle";
    //     oCell.width = "20px";
    // 
    //     oCell = newRow.insertCell(-1);
    //     oCell.innerHTML = originalDomain;
    //     oCell.width = "50%";
    // 
    //     oCell = newRow.insertCell(-1);
    //     oCell.innerHTML = prefs.domainFilter[originalDomain];
    //     oCell.width = "50%";
    // 
    //     anchor = document.createElement('a');
    //     anchor.href = "javascript:void(0)";
    //     anchor.className = "delete-icon";
    //     anchor.domainKey = originalDomain;
    //     anchor.rowIndex = rowIndex;
    //     icon = document.createElement('img');
    //     icon.src = "delete.png";
    //     icon.vAlign = "middle";
    //     icon.width = "16";
    //     icon.height = "16";
    //     anchor.appendChild(icon);
    // 
    //     anchor.onclick = function() { 
    //       table.deleteRow(this.rowIndex);
    //       delete prefs.domainFilter[this.domainKey];
    //       localStorage.prefs = JSON.stringify(prefs);
    //     };
    // 
    //     oCell.appendChild(anchor);    
    // 
    //     rowIndex++;
    //   }
    // }
    },
  };
  
  // UncensorOptions.test = function(str) {
  //   alert(str);
  // };
  // 
  // UncensorOptions.pageY = function(elem) {
  //     return elem.offsetParent ? (elem.offsetTop + pageY(elem.offsetParent)) : elem.offsetTop;
  // };
  // 
  // UncensorOptions.buffer = 20; //scroll bar buffer
  
//  UncensorOptions.resizeIframe = function() {
      //var height = document.documentElement.clientHeight;
      //height -= UncensorOptions.pageY(document.getElementById('ifm')) + UncensorOptions.buffer ;
      //height = (height < 0) ? 0 : height;
      //document.getElementById('ifm').style.height = height + 'px';
//      document.getElementById('ifm').height = document.getElementById('ifm').contentWindow.document.body.scrollHeight + "px";
//  }
  
  // Export
  return {
    UncensorOptions: UncensorOptions
  };

});
