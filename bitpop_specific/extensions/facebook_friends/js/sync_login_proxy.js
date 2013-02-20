(function() {
	var args = document.location.search.substring(1).split('&');

	argsParsed = {};

	for (i=0; i < args.length; i++)
	{
	  arg = unescape(args[i]);

	  if (arg.indexOf('=') == -1)
	  {
	      argsParsed[arg.trim()] = true;
	  }
	  else
	  {
	      kvp = arg.split('=');
	      argsParsed[kvp[0].trim()] = kvp[1].trim();
	  }
	}

  if (argsParsed.token && argsParsed.email && argsParsed.type)
		chrome.extension.sendMessage({
			"type": "sync_login_result",
			"result": "SUCCESS",
			"token": argsParsed.token,
			"email": argsParsed.email,
			"backend": argsParsed.type
		}, function(response) {
			window.close();
		});
	else if (argsParsed.message && argsParsed.backend)
		chrome.extension.sendMessage({
			"type": "sync_login_result",
			"result": "ERROR",
			"error_msg": argsParsed.message,
			"backend": argsParsed.backend
		}, function(response) {
			window.close();
		});
})();
