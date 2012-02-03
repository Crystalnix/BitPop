/*
 * @require:
 *   jQuery
 *   common.js
 */

var bitpop;
//if (!bitpop)
//  throw new Error('Chat dependencies not loaded.');

bitpop.chat = (function() {
  var public = {
    init: function() {
      friendUid = window.location.hash.slice(1);
      lastMessageUid = null;
      
      $('#msg').focus();

      (function initChat() {
        var lsKey = chrome.extension.getBackgroundPage().myUid + ':' + friendUid;
        if (lsKey in localStorage) {
          var msgs = JSON.parse(localStorage[lsKey]);
          for (var i = 0; i < msgs.length; ++i) {
            var msgDate = new Date(Date.parse(msgs[i].time));
            appendMessage(msgs[i].msg, msgDate, msgs[i].me);
          }
        }
      })();

      chrome.extension.onRequestExternal.addListener(function (request, sender, sendResponse) {
        if (request.type == 'newMessage') {
          if (friendUid == request.from) {
            appendMessage(bitpop.preprocessMessageText(request.body), new Date(), false);
          }
        }
      });

      $('#msgForm').submit(function () {
        var meMsg = $('#msg').val();
        var uidTo = friendUid;
        var request = { message: meMsg,
                        uidTo: uidTo,
                        type: 'sendChatMessage'};
        chrome.extension.sendRequest(bitpop.CONTROLLER_EXTENSION_ID, request);
        var escMsg = bitpop.preprocessMessageText(meMsg);
        bitpop.saveToLocalStorage(chrome.extension.getBackgroundPage().myUid,
          uidTo, escMsg, new Date(), true);
        //escMsg = (new Date()).bitpopFormat() + '<br />' + escMsg;
        //$('#chat').append('<li class="me">' + escMsg + '</li>');
        appendMessage(escMsg, new Date(), true);

        $('#msg').val('');
        $('#msg').focus();
        return false;
      });

      $('#pastePageLink').click(function() {
        chrome.tabs.getSelected(null, function(tab) {
          var msgValue = $('#msg').val();
          if (msgValue == "")
            $('#msg').val(tab.url);
          else if (msgValue.charAt(msgValue.length - 1) == ' ')
            $('#msg').val(msgValue + tab.url);
          else
            $('#msg').val(msgValue + ' ' + tab.url);
          // Set the cursor at the end of input:
          $('#msg').focus().val($('#msg').val());
        });
      });
    }, // end of public function init
    sendInvite: sendInvite
  };

  function appendMessage(msg, msgDate, me) {
    // if (!lastMessageTime || (msgDate - lastOutputTime >= 5 * 60 * 1000)) {
    //   $('#chat').append('<li class="time">' +
    //     msgDate.bitpopFormat(msgDate.isTodayDate()) + '</li>');
    //   lastOutputTime = msgDate;
    // }

    var uid = me ? chrome.extension.getBackgroundPage().myUid : friendUid;
    var createMessageGroup = (lastMessageUid != uid);

    if (createMessageGroup) {
      var profileUrl = 'http://www.facebook.com/profile.php?id=' + uid.toString(); 
      $('#chat').append(
          '<div class="message-group clearfix">' +
            '<a class="profile-link" href="' + profileUrl + '">' +
            '<img class="profile-photo" src="http://graph.facebook.com/' +
              uid.toString() + '/picture" alt="">' +
            '</a>' +
            '<div class="messages"></div>' +
          '</div>'
        );
      $('#chat div.message-group:last-child a.profile-link').click(function() {
        chrome.tabs.create({ url: profileUrl });
        return false;
      });
    }
    
    $('#chat div.message-group:last-child .messages').append(
        '<div class="chat-message">' + msg + '</div>');

    lastMessageUid = uid;

    // scroll the chat div to bottom
    $('#chat').scrollTop($('#chat')[0].scrollHeight);

    lastMessageTime = msgDate;
  }

  function sendInvite() {
    chrome.extension.sendRequest(
        bitpop.CONTROLLER_EXTENSION_ID,
        {
          type: 'sendInvite',
          uidTo: friendUid
        },
        function (response) {
          inviteCallback(response.msg);
        });
    return false;
  }

  function inviteCallback(msg) {
    var escMsg = bitpop.preprocessMessageText(msg);
    bitpop.saveToLocalStorage(chrome.extension.getBackgroundPage().myUid,
      friendUid, escMsg, new Date(), true);
    appendMessage(escMsg, new Date(), true);
  }

  // Private data members ============================================
  var lastMessageTime = null;
  var lastOutputTime = null;
  var friendUid = null;

  return public;
})();
