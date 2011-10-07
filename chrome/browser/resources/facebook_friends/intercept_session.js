(function() {
  //try {
    var access_token = null;
    var hash = window.location.hash.split('#')[1];
    var args = hash.split('&');
    for (var i in args) {
      var_ = args[i].split('=');
      if (var_[0] == 'access_token') {
        access_token = var_[1];
        break;
      }
    }
   
    // if (!access_token) {
    //   chrome.extension.sendRequest({ requestType: 'loggedOut' },
    //                                function () { window.close(); });
    //   return;
    // }

    chrome.extension.sendRequest({ requestType: "setAccessToken", 
                                   access_token: access_token });
    window.close();
  // }
  // catch(e) {
  //   chrome.extension.sendRequest({ requestType: 'loggedOut' },
  //                                  function () { window.close(); });
  // }
})();
