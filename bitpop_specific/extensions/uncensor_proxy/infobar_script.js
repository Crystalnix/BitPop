(function() {
  if (document.getElementById('uncensor-proxy-bitpop-infobar'))
    return;

  var jippi_html = '<div class="bitpop-infobar-container">' +
                     '<div class="bitpop-infobar-wrap">' +
                       '<img class="bitpop-infobar-extension-icon" src="I_will_cause_404.jpg" alt="" />' +
                       '<span class="bitpop-infobar-em">Jippi!</span> Site is blocked by ISP, but BitPop helped you get here anyway :)' +
                     '</div>' +
                     '<div class="bitpop-infobar-buttonblock">' +
                       '<div class="bitpop-infobar-close"></div>' +
                       '<a href="#" id="bitpop-infobar-but-try-another" class="bitpop-infobar-button bitpop-infobar-button-green">Try another proxy</a>' +
                     '</div' +
                    '</div>';

  var ask_html = '<div class="bitpop-infobar-container">' +
                   '<div class="bitpop-infobar-wrap">' +
                     '<img class="bitpop-infobar-extension-icon" src="I_will_cause_404.jpg" alt="" />' +
                     'This site is blocked in ' +
                     '<span class="bitpop-infobar-em" id="bitpop-infobar-country-name">*country name*</span>. ' +
                     'Get access to the site?' +
                   '</div>' +
                   '<div class="bitpop-infobar-buttonblock">' +
                     '<div class="bitpop-infobar-close"></div>' +
                     '<a href="#" id="bitpop-infobar-but-yes" class="bitpop-infobar-button bitpop-infobar-button-green">Get access via proxy</a>' +
                     '<a href="#" id="bitpop-infobar-but-cancel" class="bitpop-infobar-button bitpop-infobar-button-gray">Cancel</a>' +
                   '</div>' +
                 '</div>';

  var infobar = document.createElement('div');
  infobar.id = "uncensor-proxy-bitpop-infobar";
  infobar.className = "bitpop-infobar";

  function showBar() {
    var e = document.getElementsByClassName('bitpop-infobar-container')[0];

    e.style.display = 'block';
    setTimeout(function() {
      document.body.style.paddingTop = e.clientTop + e.clientHeight + 'px';
      e.style.top = '0px';
    }, 10);
  }

  function hideBar() {
    var e = document.getElementsByClassName('bitpop-infobar-container')[0];

    e.style.top = '';
    setTimeout(function() {
      e.style.display = '';
      document.body.style.paddingTop = '';
    }, 250);
  }

  function initCommon() {
    var closeButton = infobar.getElementsByClassName('bitpop-infobar-close')[0];
    closeButton.style.backgroundImage =
      'url(' + chrome.extension.getURL('close_bar_h.png') + ')';
    closeButton.onclick = hideBar;

    var icon = infobar.getElementsByClassName('bitpop-infobar-extension-icon')[0];
    icon.src = chrome.extension.getURL('32uncensorp.png');

    document.body.appendChild(infobar);

    showBar();  // call at once
  }

  function initAsk() {
    infobar.innerHTML = ask_html;

    initCommon();

    document.getElementById('bitpop-infobar-but-cancel').onclick = function() {
      hideBar();
      return false;
    };
  }

  function initJippi() {
    infobar.innerHTML = jippi_html;

    initCommon();

    setTimeout(hideBar, 10000);
  }

  var options = bitpop_uncensor_proxy_options;
  if (options.reason == "setAsk" &&
      options.url &&
      options.country_name) {

    initAsk();

    document.getElementById('bitpop-infobar-but-yes').href = options.url;
    document.getElementById('bitpop-infobar-country-name').innerText =
        options.country_name;

    document.getElementById('bitpop-infobar-but-yes').addEventListener('click',
        function (ev) {
          chrome.extension.sendMessage({ 'type': 'enableProxyForDomain',
                                         'domain': options.domain },
                                       function(response) {
                                         document.location.href = options.url;
                                       });
          return false;
        }, false);
  }
  else if (options.reason == 'setJippi' && options.url) {
    initJippi();

    document.getElementById('bitpop-infobar-but-try-another').href = options.url;
  }
}());

