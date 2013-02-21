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

  function callback() {
  	window.close();
  }

  var dict = { "type": "sync_login_result" };

  if (argsParsed.token && argsParsed.email && argsParsed.type) {
  	dict["result"] = "SUCCESS";
  	dict["token"] = argsParsed.token;
  	dict["email"] = argsParsed.email;
  	dict["backend"] = argsParsed.type;
  } else if (argsParsed.message && argsParsed.backend) {
  	dict["result"] = "ERROR";
  	dict["error_msg"] = argsParsed.message;
  	dict["backend"] = argsParsed.backend;
  } else
  	return;

	chrome.extension.sendMessage({
			"type": "sync_login_result",
			"result": "SUCCESS",
			"token": argsParsed.token,
			"email": argsParsed.email,
			"backend": argsParsed.type
		}, function(response) {
			callback();
		});
})();
