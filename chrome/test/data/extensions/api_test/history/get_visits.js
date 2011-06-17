// History api test for Chrome.
// browser_tests.exe --gtest_filter=ExtensionHistoryApiTest.GetVisits

// runHistoryTestFns is defined in ./common.js .
runHistoryTestFns([
  function getVisits() {
    // getVisits callback.
    function getVisitsTestVerification() {
      removeItemVisitedListener();

      // Verify that we received the url.
      var query = { 'text': '' };
      chrome.history.search(query, function(results) {
        assertEq(1, results.length);
        assertEq(GOOGLE_URL, results[0].url);

        var id = results[0].id;
        chrome.history.getVisits({ 'url': GOOGLE_URL }, function(results) {
          assertEq(1, results.length);
          assertEq(id, results[0].id);

          // The test has succeeded.
          chrome.test.succeed();
        });
      });
    };
    // getVisits entry point.
    chrome.history.deleteAll(function() {
      setItemVisitedListener(getVisitsTestVerification);
      populateHistory([GOOGLE_URL], function() { });
    });
  }
]);
