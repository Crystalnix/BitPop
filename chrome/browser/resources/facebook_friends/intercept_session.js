(function() {
  var access_token = null;
  var hash = window.location.hash.split('#')[1];
  var args = hash.split('&');
  for (var i in args) {
    var_ = args[i].split('=');
    if (var_[0] == 'access_token')
      access_token = var_[1];
  }
  chrome.extension.sendRequest({message: "setSession", session: hash}, function() {
    window.close();
  });
})();