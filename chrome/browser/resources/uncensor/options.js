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
    
    prefs_: {
      'name': 'uncensorPrefs',
      'value': '',
      'managed': false,
      'dict': {}
    },
    
    atStartup: true,
    
    initializePage: function() {
      // Call base class implementation to start preference initialization.
      OptionsPage.prototype.initializePage.call(this);
       
      Preferences.getInstance().addEventListener(this.prefs_.name,
          this.updatePageControlStates_.bind(this));
    },
    
    updatePageControlStates_: function(event) {
      this.prefs_.value = event.value["value"];
      this.prefs_.managed = event.value["managed"];
      
      this.prefs_.dict = JSON.parse(this.prefs_.value);
      if (this.atStartup) {
        this.initUncensorOptions(this.prefs_.dict);
        this.atStartup = false;
      }
    },
    
    save: function() {
      var prefs = this.prefs_.dict;
      prefs.shouldRedirect = document.getElementById('uncensor_always_redirect').checked;
      prefs.showMessage = document.getElementById('uncensor_show_message').checked;
      prefs.notifyUpdate = document.getElementById('uncensor_notify_update').checked;
      Preferences.setStringPref("profile.uncensor", JSON.stringify(prefs));
    },

    insertRow: function(dst, domainPair)
    {
      var table = dst;
      var compartmentTable = null;
      var prefDictionary = null;
      var compartmentDictionary = null;
      var anchorTitle = "";

      switch (dst.id) {
        case 'domain_filter_table':
          prefDictionary = 'domainFilter';
          compartmentDictionary = 'domainExceptions';
          compartmentTable = document.getElementById('domain_exceptions_table');
          anchorTitle = 'Exclude domains. Moves highlighted domain pair to Exceptions list.';
          break;
        case 'domain_exceptions_table':
          prefDictionary = 'domainExceptions';
          compartmentDictionary = 'domainFilter';
          compartmentTable = document.getElementById('domain_filter_table');
          anchorTitle = 'Enable domains. Moves highlighted domain pair to The Filter list.'
          break;
      }

      var tbody = table.getElementsByTagName('tbody').length ? table.getElementsByTagName('tbody')[0] : undefined;
      if (tbody == undefined) {
        tbody = document.createElement('tbody');
        table.appendChild(tbody);
      }

      var newRow = tbody.insertRow(-1);
      newRow.onmouseover = function() { this.className = 'highlight'; };
      newRow.onmouseout = function() { this.className = ''; };

      var oCell = newRow.insertCell(-1);
      oCell.innerHTML = "<img src='web.png' width='16' height='16' alt='' />";
      oCell.vAlign = "middle";
      oCell.style.width = "20px";

      oCell = newRow.insertCell(-1);
      oCell.innerHTML = domainPair.originalDomain;
      oCell.style.width = "50%";

      oCell = newRow.insertCell(-1);
      oCell.innerHTML = domainPair.newLocation;
      oCell.style.width = "50%";

      anchor = document.createElement('a');
      anchor.href = "javascript:void(0)";
      anchor.className = "delete-icon";
      anchor.title = anchorTitle;
      anchor.domainPair = domainPair;
      icon = document.createElement('img');
      icon.src = "delete.png";
      icon.vAlign = "middle";
      icon.width = "16";
      icon.height = "16";
      anchor.appendChild(icon);

      anchor.onclick = function() {
        var row;
        var rowIndex = 0;
        // Search current row index
        for ( ; rowIndex < tbody.getElementsByTagName('tr').length; rowIndex++) {
          row = tbody.getElementsByTagName('tr')[rowIndex];
          rowCells = row.getElementsByTagName('td');
          if (rowCells.length != 3)
            return;
          if (rowCells[1].innerHTML == this.domainPair.originalDomain)
            break;
        }

        insertRow(compartmentTable, this.domainPair);
        tbody.deleteRow(rowIndex);

        var prefs = UncensorOptions.getInstance().prefs_.dict;

        prefs[compartmentDictionary][this.domainPair.originalDomain] = this.domainPair.newLocation;
        delete prefs[prefDictionary][this.domainPair.originalDomain];

        Preferences.setStringPref("profile.uncensor", JSON.stringify(prefs));
      };

      oCell.appendChild(anchor);
    },

    // Make sure the checkbox checked state gets properly initialized from the
    // saved preference.
    initUncensorOptions: function(prefs) {
      if (prefs.shouldRedirect) {
        document.getElementById('uncensor_always_redirect').checked = true;
        document.getElementById('uncensor_never_redirect').checked = false;
      }
      else {
        document.getElementById('uncensor_always_redirect').checked = false;
        document.getElementById('uncensor_never_redirect').checked = true;
      }

      document.getElementById('uncensor_show_message').checked = prefs.showMessage;
      document.getElementById('uncensor_notify_update').checked = prefs.notifyUpdate;
      var filterTable = document.getElementById("domain_filter_table");
      for (var originalDomain in prefs.domainFilter) {
        insertRow(filterTable, { originalDomain: originalDomain,
                              newLocation: prefs.domainFilter[originalDomain] });
      }
      var exceptionsTable = document.getElementById("domain_exceptions_table");
      for (var originalDomain in prefs.domainExceptions) {
        insertRow(exceptionsTable, { originalDomain: originalDomain, 
                              newLocation: prefs.domainExceptions[originalDomain] });
      }

      document.getElementById('uncensor_always_redirect').onclick = save;
      document.getElementById('uncensor_never_redirect').onclick = save;
      document.getElementById('uncensor_show_message').onchange = save;
      document.getElementById('uncensor_notify_update').onchange = save;
    }
  };
  
  // Export
  return {
    UncensorOptions: UncensorOptions
  };

});
