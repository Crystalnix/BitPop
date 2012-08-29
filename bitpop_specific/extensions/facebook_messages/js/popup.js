Array.prototype.contains = function(obj) {
  var i = this.length;
  while (i--) {
    if (this[i] === obj) {
      return true;
    }
  }
  return false;
};

var NUM_MESSAGES_SHOW = 5;
var myUid = chrome.extension.getBackgroundPage().myUid;

window.options.init();
var current = window.options.current;

var DesktopNotifications =
  window.chrome.extension.getBackgroundPage().DesktopNotifications;

function showError(errorMsg) {
  var newDom =
      //"<div class='empty-feed'>" +
      "<span class='error-message'>" + errorMsg + "</span>";// +
      //"</div>";
  $('#feed').empty();
  $('#feed').addClass('empty-feed');
  $('.loading').removeClass('loading');
  $('#feed').append(newDom);

  appendFooter();
}

function appendFooter(numUnread) {
  $('#footer').empty();

  if (!numUnread)
    $('#footer').append("<div class='footer-item centered footer-click' " +
                        ">See All Messages</div>");
  else
    $('#footer').append("<div class='footer-item footer-click'>" +
        "<div class='top'>" +
          "See All Messages" +
        "</div>" +
        "<div class='bottom'>" +
          numUnread.toString() + " unread" +
        "</div>" +
        "</div>");
}

function nameByUid(uid, users) {
  for (var i = 0; i < users.length; i++) {
    if (users[i].uid.toString() == uid.toString())
      return users[i].name;
  }
  return "{Unknown user}";
}

function filterMessagesByThreadId(thread_id, messages) {
  var res = [];
  for (var i = 0; i < messages.length; ++i) {
    if (messages[i].thread_id == thread_id)
      res.push(messages[i]);
  }
  return res;
}

function findResultSet(query_name, data) {
  for (var i = 0; i < data.length; ++i) {
    if (data[i].name == query_name)
      return data[i].fql_result_set;
  }
  return null;
}

function showMessages(data) {
  if (!myUid) {
    showError('Error. User facebook id was not retrieved.');
    return;
  }


  $('.loading').removeClass('loading');
  $('#feed').empty();

  var threads = findResultSet('threads', data);
  var users = findResultSet('users', data);
  var messages = findResultSet('messages', data);

  var messagesByThread = [];
  for (var i = 0; i < threads.length; ++i)
    messagesByThread.push(filterMessagesByThreadId(threads[i].thread_id, messages));

  var newDom = '';

  var footerGoToOnClick = "onclick='chrome.tabs.create({ url: \"http://www.facebook.com/messages\" })'";
  if (threads.length == 0) {
    newDom =
      "<div class='empty-feed'>" +
      "  <span>No New Messages.</span>" +
      "</div>";

    appendFooter();
  } else {
    var total_unread = 0;
    for (var i = 0; i < threads.length; ++i)
      total_unread += threads[i].unread;
    appendFooter(total_unread);
  }


  for (var i = 0; i < Math.min(threads.length, NUM_MESSAGES_SHOW); i++) {
    newDom += formatFeedItem(threads[i], users);
  }

  $('#feed').append(newDom);

  $('#feed .message').each(function() {
    $(this).click(function() {
      var curData = ($(this).index() < threads.length) ?
                        messagesByThread[$(this).index()] :
                        null;

      if (curData) {
        slideToThreadView();

        $('#thread .loading').removeClass('loading');
        $('#thread-feed').empty();
        $('#thread-feed').append(formatThreadItems(curData, users));
        $('.box-wrap').data('antiscroll').rebuild();
      }
    });
  });

  $('#slide-wrap').scrollLeft(0);

  var dn = chrome.extension.getBackgroundPage().DesktopNotifications;
  for (var i = 0; i < threads.length; i++) {
    var index_tub = dn.threads_unseen_before.indexOf(threads[i].thread_id);
    if (index_tub != -1) {
      dn.threads_unseen_before.splice(index_tub, 1);
      localStorage.setCacheItem('xx_' + threads[i].thread_id,
          (new Date()).toString(), { days: 21 });
    }
  }

  dn.start(30000);
}

function formatFeedItem(thread, users) {
  var otherUserId = 0;
  for (var i = 0; i < thread.recipients.length; ++i) {
    if (thread.recipients[i] != thread.viewer_id) {
      otherUserId = thread.recipients[i];
      break;
    }
  }

  var template =
    "<div class='message unread-{{hasUnread}}'>" +
      "<div class='left'>" +
        "<div class='user-photo'>" +
          "<img src='{{photoUrl}}' />" +
        "</div>" +
      "</div>" +
      "<div class='right'>" +
        "<div class='top'>" +
          "<div class='user-name'>" +
            "{{getNameText}}" +
          "</div>" +
          "<div class='unread-count'>" +
            "{{unreadText}}" +
          "</div>" +
          "<div class='time-since'>" +
            "{{time-since}}" +
          "</div>" +
        "</div>" +
        "<div class='bottom'>" +
          "<div class='snippet'>" +
            "{{from_me}}" +
            "<b>{{subject}}</b>" +
            "{{snippet}}" +
          "</div>" +
        "</div>" +
      "</div>" +
    "</div>";


  var d = localStorage.getCacheItem('xx_' + thread.thread_id);
  var should_highlight = (thread.unread.toString() != '0') &&
                         (!d ||
                          (new Date(d)).getTime() <
                            (new Date(thread.updated_time * 1000)).getTime()
                          );

  template = template.replace('{{hasUnread}}',
      should_highlight ? 'true' : 'false');
  template = template.replace('{{photoUrl}}',
      'http://graph.facebook.com/' + otherUserId.toString() + '/picture?type=square');
  template = template.replace('{{getNameText}}', nameByUid(otherUserId, users));
  template = template.replace('{{unreadText}}',
      (thread.unread > 1) ? ("(" + thread.unread.toString() + ")") : '');
  template = template.replace('{{time-since}}',
    humane_date(new Date(thread.updated_time * 1000)));
  template = template.replace('{{from_me}}',
      (thread.snippet_author == thread.viewer_id) ? "<img src='/images/arrow-icon.png' />" : '');
  template = template.replace('{{subject}}', '');
  template = template.replace('{{snippet}}', thread.snippet);

  return template;
}

function formatThreadItems(messages, users) {
  console.assert(myUid !== null);

  var template0 =
    "<div class='message unread-false thread-item'>" +
      "<div class='left'>" +
        "<div class='user-photo'>" +
          "<img src='{{photoUrl}}' />" +
        "</div>" +
      "</div>" +
      "<div class='right'>" +
        "<div class='top'>" +
          "<div class='user-name'>" +
            "{{getNameText}}" +
          "</div>" +
          "<div class='time-since'>" +
            "{{time-since}}" +
          "</div>" +
        "</div>" +
        "<div class='bottom'>" +
          "<div class='snippet'>" +
            "<b>{{subject}}</b>" +
            "{{snippet}}" +
          "</div>" +
        "</div>" +
      "</div>" +
    "</div>";

  var res = '';

  //if (!itemData.comments || itemData.comments.data.length == 0) {
  //  var template = template0.replace('{{photoUrl}}',
  //      'http://graph.facebook.com/' + itemData.from.id.toString() + '/picture?type=square');
  //  template = template.replace('{{getNameText}}', itemData.from.name);
  //  template = template.replace('{{time-since}}',
  //    humane_date(itemData.updated_time));
  //  template = template.replace('{{subject}}', '');
  //  template = template.replace('{{snippet}}', itemData.message);

  //  res += template;
  //} else {
  for (var i = messages.length-1; i >= 0; --i) {
      var template = template0.replace('{{photoUrl}}',
          'http://graph.facebook.com/' + messages[i].author_id.toString() + '/picture?type=square');
      template = template.replace('{{getNameText}}',
          nameByUid(messages[i].author_id, users));
      template = template.replace('{{time-since}}',
        humane_date(new Date(messages[i].created_time * 1000)));
      template = template.replace('{{subject}}', '');
      template = template.replace('{{snippet}}',
          messages[i].body);

      res += template;
    }
  //}

  return res;
}

var slideToThreadView = function(dontAnimate) {
  if (!dontAnimate)
    $('#slide-wrap').stop().animate({ scrollLeft: 350 },
        500);
  else
    $('#slide-wrap').stop().scrollLeft(350);
};

var slideToFeedView = function(dontAnimate) {
  if (!dontAnimate)
    $('#slide-wrap').stop().animate({ scrollLeft: 0 },
        500);
  else
    $('#slide-wrap').stop().scrollLeft(0);
};

var onNewMessageClick = function () {
  chrome.tabs.getSelected(null,function(tab) {
      var tablink = tab.url;
      if (tablink.indexOf('chrome://') != -1)
        tablink = 'http://www.bitpop.com';

      chrome.windows.create({ url: "http://www.facebook.com/dialog/send?app_id=190635611002798&display=popup&link=" + encodeURIComponent(tablink) + "&redirect_uri=https://www.facebook.com/connect/login_success.html", type: "popup", width: 998, height: 421 })
  });
};

window.onload = function() {

  $('#send-new').click(onNewMessageClick);
  $('#back-to-summary').click(function() {
    slideToFeedView();
    return false;
  });
  $('.footer-click').live('click', function() {
    chrome.tabs.create({ url: "http://www.facebook.com/messages" });
  });

  $('.box-wrap').antiscroll();

  $(function(){setTimeout(function(){$('#slide-wrap').scrollLeft(0);},100)});

  $('.thread-item').live('click', function() {
      chrome.tabs.create({ url: 'http://www.facebook.com/messages' });
  });

  var queryObj = {
    threads: "SELECT thread_id, subject, snippet, snippet_author, " +
             "object_id, unread, viewer_id, message_count, updated_time, " +
             "subject, recipients FROM thread WHERE folder_id = 0 LIMIT " +
               NUM_MESSAGES_SHOW,
    users: "SELECT uid, name FROM user " +
           "WHERE uid IN (SELECT recipients FROM thread WHERE " +
           "folder_id = 0 LIMIT " + NUM_MESSAGES_SHOW + ")",
    messages: "SELECT thread_id, author_id, body, created_time FROM message WHERE " +
                "thread_id IN (SELECT thread_id FROM thread WHERE " +
                  "folder_id=0 LIMIT " + NUM_MESSAGES_SHOW + ")"
  };
  var query = JSON.stringify(queryObj);

  chrome.extension.sendRequest(current.controllerExtensionId,
      {
        //type: 'graphApiCall',
        //path: '/me/inbox',
        //params: { fields: 'id,from,to,updated_time,unread,unseen,comments,message',
        //limit: NUM_MESSAGES_SHOW }
        type: 'fqlQuery',
        query: query
      },
      function (response) {
        if (response.error)
          showError(response.error);
        else
          showMessages(response);
      }
  );
};
