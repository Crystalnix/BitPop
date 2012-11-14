window.addEvent("domready", function () {
    // Option 1: Use the manifest:
    new FancySettings.initWithManifest(function (settings) {
        // settings.manifest.myButton.addEvent("action", function () {
        //     alert("You clicked me!");
        // });
        var emptySpace = document.createElement('div');
        emptySpace.className = 'tab';

        var container = document.getElementById('tab-container');
        container.appendChild(emptySpace);

        var backLink = document.createElement('div');
        backLink.className = 'tab';
        backLink.innerText = 'Back to main settings';
        backLink.addEventListener('click', function (e) {
          chrome.tabs.update(null, { url: 'chrome://settings' });
          e.preventDefault();
        });

        container.appendChild(backLink);
    });
});
