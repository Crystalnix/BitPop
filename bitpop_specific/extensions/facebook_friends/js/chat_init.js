if (location.search !== "?foo") {
  location.search = "?foo";
  throw new Error;  // load everything on the next page;
                        // stop execution on this page
}

$(function() {
    bitpop.chat.init();

    $('.box-wrap').antiscroll();

    function setUserNameCallback(uname) {
      $('.content-friend-name').text(uname);
    }

    function setHeadURL(profile_url) {
      $('#head').click(function() { chrome.tabs.create({ url: profile_url }); });
    }

    chrome.extension.sendMessage(bitpop.CONTROLLER_EXTENSION_ID,
      { type: 'getFBUserNameByUid',
        uid: window.location.hash.substr(1).split('&')[0] },
      function (response) {
        setUserNameCallback(response.uname);
        setHeadURL(response.profile_url);
      }
    );
});
