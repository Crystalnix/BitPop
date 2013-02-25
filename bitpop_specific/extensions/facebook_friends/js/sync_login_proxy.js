(function() {
	var args = document.location.search.substring(1);
  var dict = { "type": "sync_login_result", "args": args };
	chrome.extension.sendMessage(dict);
})();
