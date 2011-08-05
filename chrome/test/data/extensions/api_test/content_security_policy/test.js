window.externalCanary = "Alive";

chrome.test.getConfig(function(config) {

  function inlineScriptDoesNotRun() {
    chrome.test.assertEq(window.inlineCanary, undefined);
    chrome.test.runNextTest();
  }

  function externalScriptDoesRun() {
    // This test is somewhat zen in the sense that if external scripts are
    // blocked, we don't be able to even execute the test harness...
    chrome.test.assertEq(window.externalCanary, "Alive");
    chrome.test.runNextTest();
  }

  chrome.test.runTests([
    inlineScriptDoesNotRun,
    externalScriptDoesRun
  ]);
});
