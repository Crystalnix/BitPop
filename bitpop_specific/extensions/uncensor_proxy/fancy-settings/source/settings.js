window.addEvent("domready", function () {
  new FancySettings.initWithManifest(function (settings) {
    settingsStore = new Store('settings');

    settings.manifest.domains_description.element.set('html',
            'A list of sites blocked in <strong>' + 
            settingsStore.get('country_name') +
            '</strong> collected through IP recognition:');

    settings.manifest.update_domains.addEvent('action', function() {
      chrome.extension.getBackgroundPage().updateProxifiedDomains();
    });

    chrome.extension.onRequest.addListener(function(request, sender, 
        sendResponse) {
      if (request && request.reason === 'settingsChanged') {
        settings.manifest.domains.set(settingsStore.get('domains'), true); 
      }
    });


  });
});
