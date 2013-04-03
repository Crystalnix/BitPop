setTimeout(function() { chrome.extension.sendMessage(
	{ "type": "closeTabRequest" }
) }, 1600);
