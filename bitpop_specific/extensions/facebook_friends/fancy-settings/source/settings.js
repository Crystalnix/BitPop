window.addEvent("domready", function () {
    // Option 1: Use the manifest:
    new FancySettings.initWithManifest(function (settings) {
        // settings.manifest.myButton.addEvent("action", function () {
        //     alert("You clicked me!");
        // });
    });
});
