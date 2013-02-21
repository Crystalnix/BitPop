/*
 * @require:
 *   jQuery
 *   common.js
 */

var bitpop;

bitpop.chat = (function() {
  function setMsgValue(val) {
    $('#msg').val('');
    setTimeout(function() {
      $('#msg').val(val);
    }, 1);
  }

  var public = {
    init: function() {
      friendUid = window.location.hash.slice(1).split('&')[0];
      myUid = window.location.hash.slice(1).split('&')[1];
      lastMessageUid = null;

      function appendFromLocalStorage() {
        var lsKey = myUid + ':' + friendUid;
        if (lsKey in localStorage) {
          var msgs = JSON.parse(localStorage[lsKey]);
          var refDate = new Date(msgs[msgs.length-1].time);
          for (var i = 0; i < msgs.length; ++i) {
            var msgDate = new Date(msgs[i].time);
            appendMessage(msgs[i].msg, msgDate, msgs[i].me);
          }
        }
      }

      (function initChat() {
        appendFromLocalStorage();

        setTimeout(function() {
            $('#msg').focus();
            // scroll the chat div to bottom
            scrollToBottom(true); // don't animate
        }, 200);

        var msgText = localStorage.getItem('msg:' + myUid + ':' + friendUid);
        if (msgText) {
          setMsgValue(msgText);
          setTimeout(function() {
            moveCaretToEnd(document.getElementById('msg'));
            $('#msg').focus();
            $('#msg').prop('scrollTop', $('#msg').prop('scrollHeight'));
          }, 100);
        }

        function moveCaretToEnd(el) {
          if (typeof el.selectionStart == "number") {
            el.selectionStart = el.selectionEnd = el.value.length;
          } else if (typeof el.createTextRange != "undefined") {
            el.focus();
            var range = el.createTextRange();
            range.collapse(false);
            range.select();
          }
        }
      })();

      chrome.extension.onMessageExternal.addListener(function (request, sender, sendResponse) {
        if (request.type == 'newMessage') {
          if (friendUid == request.from) {
            appendMessage(bitpop.preprocessMessageText(request.body), new Date(), false);
            if ($('.box-wrap').data('antiscroll')) {
              $('.box-wrap').data('antiscroll').rebuild();
            }
          }
        }
      });

      function onMessageSent(response, meMsg, uidTo) {
        if (response.error) {
          $('#chat').append(
            '<div class="error-message">Message not sent. ' + response.error + '</div>');

          lastMessageUid = 0;

          // scroll the chat div to bottom
          scrollToBottom();

          return;
        }
        else
          $('#chat .error-message').hide();

        var escMsg = bitpop.preprocessMessageText(meMsg);
        bitpop.saveToLocalStorage(myUid,
          uidTo, escMsg, new Date(), true);
        //escMsg = (new Date()).bitpopFormat() + '<br />' + escMsg;
        //$('#chat').append('<li class="me">' + escMsg + '</li>');
        appendMessage(escMsg, new Date(), true);

        setMsgValue('');

        if ($('.box-wrap').data('antiscroll')) {
          $('.box-wrap').data('antiscroll').rebuild();
        }
      }

      $('#msgForm').submit(function () {
        if (typingExpired) {
          clearTimeout(typingExpired);
          typingExpired = null;
        }

        var meMsg = $('#msg').val();
        var uidTo = friendUid;
        var request = { message: meMsg,
                        uidTo: uidTo,
                        state: 'active',
                        type: 'sendChatMessage'};
        chrome.extension.sendMessage(bitpop.CONTROLLER_EXTENSION_ID, request,
            function(response) {
              onMessageSent(response, meMsg, uidTo);
            });
        $(this).data('composing', false);
        return false;
      });

      $('#msg').keydown(function(ev) {
        if (ev.which === 13) {
          $('#msgForm').submit();
          ev.preventDefault();
          return false;
        }
      });

      // send typing notifications to friend's XMPP client
      $('#msg').keypress(function(ev) {
        if (ev.which !== 13) {  // not a <Return>
          var composing = $(this).parent().data('composing');
          if (!composing) {
            var uidTo = friendUid;
            var request = { message: '',
                            uidTo: uidTo,
                            state: 'composing',
                            type: 'sendChatMessage'};

            chrome.extension.sendMessage(bitpop.CONTROLLER_EXTENSION_ID, request,
              function(response) {});
            $(this).parent().data('composing', true);
            if (typingExpired) {
              clearTimeout(typingExpired);
              delete typingExpired;
            }
            var that = this;
            typingExpired = setTimeout(function() {
              if ($(that).parent().data('composing')) {
                chrome.extension.sendMessage(bitpop.CONTROLLER_EXTENSION_ID,
                  {
                    message: '',
                    uidTo: uidTo,
                    state: 'active',
                    type: 'sendChatMessage'
                  },
                  function (response) {}
                );
                $(that).parent().data('composing', false);
              }
              delete typingExpired;
            }, 5000);
          }
        }
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
          $('#msg').focus();

          var len = $('#msg').val().length;
          var msgEl = document.getElementById('msg');
          msgEl.setSelectionRange(len, len);
          msgEl.scrollTop = 999999;
        });
      });

      //$('#msg').scrollable({ horizontal: false });
      $("#msg").bind("mousewheel", function(ev) {
        var scrollTop = $(this).scrollTop();
        $(this).scrollTop(scrollTop-Math.round(ev.originalEvent.wheelDelta/40));
      });

      $(window).unload(function () {
        localStorage.setItem('msg:' + myUid + ':' + friendUid, $('#msg').val());
      });
    }, // end of public function init
    sendInvite: sendInvite,
    scrollToBottom: scrollToBottom,
    atBottom: atBottom
  };

  function scrollToBottom(dontAnimate) {
    if (dontAnimate)
      $('.antiscroll-inner').stop().scrollTop($('.box-inner').height());
    else
      $('.antiscroll-inner').stop().animate({ scrollTop: $('.box-inner').height() }, 400);
  }

  // return Boolean
  function atBottom() {
    return $('.antiscroll-inner').scrollTop() == ($('.box-inner').height() - $('.antiscroll-inner').height() + 5) ||
      $('.antiscroll-inner').height() + 5 >= $('.box-inner').height();
  }

  function isSameMinute(date1, date2) {
    if (!date1 || !date2)
      return false;

    return (date1.getFullYear() == date2.getFullYear() &&
            date1.getMonth() == date2.getMonth() &&
            date1.getDate() == date2.getDate() &&
            date1.getHours() == date2.getHours() &&
            date1.getMinutes() == date2.getMinutes());
  }

  function isSameDay(date1, date2) {
    if (!date1 || !date2)
      return false;

    return (date1.getFullYear() == date2.getFullYear() &&
            date1.getMonth() == date2.getMonth() &&
            date1.getDate() == date2.getDate());
  }

  function dateToString(date, isShortDate) {
    var hrs = date.getHours();
    var mins = date.getMinutes();

    mins = (mins < 10) ? '0' + mins.toString() : mins.toString();

    var res = '';

    if (hrs == 12)
      res = hrs.toString() + ':' + mins + 'PM';
    else if (hrs == 0)
      res = '12:' + mins + 'AM';
    else if (hrs > 12)
      res = (hrs - 12).toString() + ':' + mins + ' PM';
    else
      res = hrs.toString() + ':' + mins + ' AM';

    if (!isShortDate) {
      var monthsToStr = [ 'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec' ];
      res = monthsToStr[date.getMonth()] + ' ' + date.getDate().toString() + ', ' + date.getFullYear().toString() + ' at ' + res;
    }

    return res;
  }

  function appendMessage(msg, msgDate, me) {
    var refDate = new Date();
    var uid = me ? myUid : friendUid;
    var dateDiffersForMoreThan1Min = lastMessageTime ? !isSameMinute(lastMessageTime, msgDate) : true;
    var createMessageGroup = (lastMessageUid != uid) || dateDiffersForMoreThan1Min;

    if (createMessageGroup) {
      var profileUrl = 'http://www.facebook.com/profile.php?id=' + uid.toString();
      $('.composing-notification').before(
          '<div class="message-group clearfix">' +
            (dateDiffersForMoreThan1Min ? '<div class="chat-timestamp">' + dateToString(msgDate, !refDate || isSameDay(msgDate, refDate)) + '</div>' : '') +
            '<a class="profile-link" tabIndex="-1" href="' + profileUrl + '">' +
            '<img class="profile-photo" src="http://graph.facebook.com/' +
              uid.toString() + '/picture" alt="">' +
            '</a>' +
            '<div class="messages"></div>' +
          '</div>'
        );
      $('#chat div.message-group:nth-last-child(2) a.profile-link').click(function() {
        chrome.tabs.create({ url: profileUrl });
        return false;
      });
    }

    $('#chat div.message-group:nth-last-child(2) .messages').append(
        '<div class="chat-message">' + msg + '</div>');

    lastMessageUid = uid;

    // scroll the chat div to bottom
    scrollToBottom(true);

    lastMessageTime = msgDate;
  }

  function sendInvite() {
    chrome.extension.sendMessage(
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
    bitpop.saveToLocalStorage(myUid,
      friendUid, escMsg, new Date(), true);
    appendMessage(escMsg, new Date(), true);
  }

  // Private data members ============================================
  var lastMessageTime = null;
  var lastOutputTime = null;
  var friendUid = null;
  var myUid = null;
  var typingExpired = null;

  return public;
})();
