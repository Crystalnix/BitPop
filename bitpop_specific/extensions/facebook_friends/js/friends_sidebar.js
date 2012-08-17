var bitpop;
if (!bitpop) bitpop = {};
bitpop.FriendsSidebar = (function() {
  var localConst = {
    SLIDE_ANIMATION_DURATION: 1000,
    VIEW_WIDTH: 185,
    UPDATE_LIST_INTERVAL: 1 * 60 * 1000,  // 1 min

    STATUSES_MAP: [ 'active', 'idle', 'offline', 'error' ],
    STATUS_HEADS: [ 'Online', 'Idle', 'Offline', 'Error' ],

    FAVORITES_TIMESPAN: 1000 * 60 * 60 * 24 * 7 * 2, // two weeks

    DOWNWARDS_TRIANGLE: '&#9660',
    RIGHTWARDS_TRIANGLE: '&#9658',

    TEXTAREA_HEIGHT: 80,

    END: null
  };

  function localStorageFavKey(myUid, friendUid) {
    return myUid + ':' + friendUid + ':fav';
  }

  var self = arguments.callee;  // for use in private functions to ref the enclosing function

  $(document).ready(function() {
    var bgPage = chrome.extension.getBackgroundPage();

    // TODO: specify narrower element selector
    $('button').click(function () {
      chrome.extension.sendRequest(bitpop.CONTROLLER_EXTENSION_ID,
        { type: 'login' },
        function (params) {
          if (params.canLogin && $('p.error').is(':visible'))
            $('p.error').fadeOut();
          else if (!params.canLogin)
            $('p.error').fadeIn();
        });
    });

    $('#logout a').click(function() {
      chrome.extension.sendRequest(bitpop.CONTROLLER_EXTENSION_ID,
        { type: 'logout' });
    });

    $('#search').bind('search', function () {
      self.updateDOM();
    });

    if (bgPage && bgPage.friendList) {
      // 2nd param = true is for dontAnimate, we need an instant switch to friends
      // view:
      self.updateFriendList(bgPage.friendList, true);
    }
    else
      // instant move to login screen, dontAnimate = true
      self.slideToLoginView(true);

    $('.toggle-button').live('click', function(e) {
      var curClass = $(this).hasClass('toggle-on') ? 'toggle-on' : 'toggle-off';
      var curStatus = $(this).parent().hasClass('head-active') ? 'active' :
          ($(this).parent().hasClass('head-idle') ? 'idle' :
           ($(this).parent().hasClass('head-offline') ? 'offline' : 'error'));

      if (curClass == 'toggle-on') {
        $(this).removeClass('toggle-on');
        $(this).addClass('toggle-off');
        if ($(this).parent().hasClass('head-on'))
          $(this).parent().removeClass('head-on');
        $(this).parent().addClass('head-off');

        $(this).html(localConst.RIGHTWARDS_TRIANGLE);
        //$(this).parent().next().hide();
        localStorage[curStatus+'_state'] = 'off';
      } else {
        $(this).removeClass('toggle-off');
        $(this).addClass('toggle-on');

        if ($(this).parent().hasClass('head-off'))
          $(this).parent().removeClass('head-off');
        $(this).parent().addClass('head-on');

        $(this).html(localConst.DOWNWARDS_TRIANGLE);
        //$(this).parent().next().show();
        localStorage[curStatus+'_state'] = 'on';
      }
      return false;
    });

    $('#friend_list li span').live('click', function() {
      var parent = $(this).parent();
      chrome.bitpop.facebookChat.addChat(
        parent.prop('jid'),
        parent.prop('username'),
        parent.prop('online_status')
      );
    });

    $('.fav-img').live('click', function(e) {
      var selfImg = this;
      var parent = $(this).parent();
      var bgPage = chrome.extension.getBackgroundPage();
      var myUid = bgPage ? bgPage.myUid : null;
      var lsKey = myUid ? localStorageFavKey(myUid.toString(), parent.prop('jid').toString()) :
                      null;

      if (parent.hasClass('fav')) {
        parent.removeClass('fav');

        if (lsKey) { localStorage.removeItem(lsKey); }
      } else {
        parent.addClass('fav');
        // $(this).stop(true,true).fadeOut('fast').fadeIn('fast'); // blink fast
        if (lsKey) { localStorage[lsKey] = 'yes'; }
      }

      if (parent.prop('online_status') == 'active')
        self.updateDOM();

      e.preventDefault();
      return false;
    });

    $('#status-control').change(function() {
      onStatusControlChange($(this).val());
    });

    $('#head-col2-row1').click(setStatusAreaClicked);
  });

  /*- private ------------------------*/
  // used for sorting of names in friends list
  function alphabetical(a, b) {
    var A = a.toLowerCase();
    var B = b.toLowerCase();
    if (A < B)
      return -1;
    else if (A > B)
     return  1;
    else
     return 0;
  }

  var onLoggedOut = function() {
    self.slideToLoginView();
    $('#friend_list').remove();
  };

  var onUserStatusChanged = function(uid, status) {
    if (!self.friendList)
      return;

    for (var i = 0; i < self.friendList.length; ++i) {
      if (self.friendList[i].uid == uid)
        self.friendList[i].online_presence = status;
    }

    self.updateDOM();
  };

  function onStatusControlChange(value) {
    chrome.extension.sendRequest(bitpop.CONTROLLER_EXTENSION_ID,
      { type: "changeOwnStatus", status: value });

    var bgPage = chrome.extension.getBackgroundPage();
    var myUid = bgPage ? bgPage.myUid : null;
    if (myUid) {
      localStorage[myUid.toString() + ':status'] = value;
    }
  }

  var setStatusAreaClicked = function(ev) {
    console.assert(this == document.getElementById('head-col2-row1'));

    $(this).unbind('click');

    var duration = 600; // ms
    var textareaHeight = localConst.TEXTAREA_HEIGHT; // px
    var imgColumnWidth = $('#sidebar-head #head-col1 img').width() +
                         parseInt($('#sidebar-head #head-col1').css('padding-right'), 10);

    var prevStatus = $(this).text().trim();
    self.statusToSetOnCancel = prevStatus;
    self.isEditingStatus = true;
    if (prevStatus == 'Set your status here.')
      prevStatus = '';

    $('#head-col2-row1').css({
      'border': 'none',
      'padding': '0',
      'max-height': '26px'
    }).stop().text('')
    .animate({ 'margin-left': '-' + imgColumnWidth.toString() + 'px',
               'height'     : textareaHeight.toString() + 'px',
               'max-height' : textareaHeight.toString() + 'px',
             }, duration);

    $('<form id="status-form" style="margin:0; padding:0; width:100%; height:100%; display:-webkit-box; -webkit-box-orient: vertical">' +
        '<div style="-webkit-box-flex:1; position:relative">' +
        '<textarea id="status-input-area" style="position:absolute; left:0; top:0; right:0; bottom:0" wrap="soft" autofocus></textarea></div>' +
        '<div id="t1000" style="margin: 2px 0; text-align:right">' +
          '<button id="cancel-but" class="small-button">Cancel</button>' +
          '<input class="small-button" type="submit" value="Post" />' +
        '</div>' +
      '</form>').hide()
      .appendTo($('#head-col2-row1')).fadeIn(duration);

    $('#head-col1').css('padding-top', '0').stop()
      .animate({ 'padding-top': (textareaHeight - $('#t1000').height() - 2).toString() + 'px' }, duration);

    // $('#status-form').resize(function() {
    //   $('#status-input-area').height($('#status-input-area').parent().height());
    // });

    $('#status-input-area').val(prevStatus).select();

    $('#status-form').submit(statusSubmitted);
    $('#cancel-but').click(function (ev) {
      console.assert(self.statusToSetOnCancel);
      console.assert(self.isEditingStatus);

      returnToShowingStatus();
      setTimeout(function () {
          $('#head-col2-row1').text(self.statusToSetOnCancel);
          self.isEditingStatus = false;
        }, 600);

      ev.stopPropagation();
      return false;
    });

    $('#status-input-area').keypress(function(ev) {
      if (ev.which == 13 && $(this).val()) {
        $('#status-form').submit();
      }
    });
  };

  var returnToShowingStatus = function() {
    var duration = 600;
    var textareaHeight = localConst.TEXTAREA_HEIGHT; // px
    var imgColumnWidth = $('#sidebar-head #head-col1 img').width() +
                         parseInt($('#sidebar-head #head-col1').css('padding-right'), 10);

    $('#head-col1').css('padding-top', (textareaHeight - $('#t1000').height() - 2).toString() + 'px')
      .stop().animate({ 'padding-top' : '0' }, duration);

    $('#status-form').fadeOut(duration);

    $('#head-col2-row1').css({
      'border': 'none',
      'padding': '0',
      'height': textareaHeight.toString() + 'px',
      'margin-left': '-' + imgColumnWidth.toString() + 'px',
      'max-height': textareaHeight.toString() + 'px' }).stop()
      .animate({ 'height': '16px', 'margin-left': '0', 'max-height': '26px' }, duration, 'linear',
          function() {
            $('#status-form').remove();
            $(this).css({
              'border': 'solid 1px #ccc',
              'padding': '0 2px',
              'height': ''
            });
          });

    $('#head-col2-row1').click(setStatusAreaClicked);
  };

  var statusSubmitted = function(ev) {
    var val = $('#status-input-area').val();
    if (!val)
      return false;

    chrome.extension.sendRequest({ type: 'setStatusMessage', msg: val }, function(response) {
      console.assert(self.isEditingStatus);

      if (response.error == 'yes')
        $('#head-col2-row1').text('Set Status Error');
      else
        $('#head-col2-row1').text(val);
      self.isEditingStatus = false;
    });

    returnToShowingStatus();

    return false;
  };

  /*- public -------------------------*/
  self.slideToLoginView = function(dontAnimate) {
    if (!dontAnimate)
      $('#slide-wrap').stop().animate({ scrollLeft: localConst.VIEW_WIDTH },
          localConst.SLIDE_ANIMATION_DURATION);
    else
      $('#slide-wrap').stop().scrollLeft(localConst.VIEW_WIDTH);
  };

  self.slideToFriendsView = function(dontAnimate) {
    var bgPage = chrome.extension.getBackgroundPage();
    var myUid = bgPage ? bgPage.myUid : undefined;
    if (myUid) {
      document.getElementById('head-profile-img').src =
        'http://graph.facebook.com/' + myUid.toString() + '/picture?type=square';
      var oHandler = $('#status-control').data('dd');
      if (oHandler) {
        var value = localStorage[myUid.toString() + ':status'] || 'available';
        oHandler.set('selectedIndex', (value == 'available') ? 0 : 1);
        if (value == 'unavailable')
          onStatusControlChange(value);
      }
    }

    if ($('p.error').is(':visible'))
      $('p.error').fadeOut();

    if (!dontAnimate)
      $('#slide-wrap').stop().animate({ scrollLeft: 0 },
          localConst.SLIDE_ANIMATION_DURATION);
    else
      $('#slide-wrap').stop().scrollLeft(0);
  };

  self.sortFriendList = function() {
    var statusesMap = localConst.STATUSES_MAP;
    self.friendList.sort(function (a,b) {
        if (a.online_presence == b.online_presence) {

          if (a.online_presence == 'active') {
            if (a.inFavorites && !b.inFavorites)
              return -1;
            else if (!a.inFavorites && b.inFavorites)
              return 1;
          }

          return alphabetical(a.name, b.name);
        } else {
          return (statusesMap.indexOf(a.online_presence) <
            statusesMap.indexOf(b.online_presence)) ? -1 : 1;
        }
    });
  };

  self.generateFriendsDOM = function() {
    var bgPage = chrome.extension.getBackgroundPage();
    var myUid = bgPage ? bgPage.myUid : null;

    function createEntry(index) {
      var i = index;

      if (!self.friendList[i].excluded) {
        var li = $('<li><span class="leftSide"><img alt="" />' +
            self.friendList[i].name + '</span></li>');
        li.attr('id', 'buddy_' + self.friendList[i].uid.toString());
        li.prop('jid', self.friendList[i].uid.toString());
        li.prop('username', self.friendList[i].name);
        li.prop('online_status', self.friendList[i].online_presence);

        $('img', li).attr('src', self.friendList[i].pic_square);

        li.append('<div class="fav-img"></div>');

        if (myUid && localStorage[myUid + ':' + self.friendList[i].uid + ':fav'])
          li.addClass('fav');

        if (self.friendList[i].online_presence &&
            self.friendList[i].online_presence != 'offline')
          li.append('<img class="status" src="images/' +
              self.friendList[i].online_presence + '.png" alt="" />');

        if (self.friendList[i].customFavorite)
          li.addClass('fav');

        return li;
      }
      return null;
    }

    var s = $('#search').val();
    if (s.length === 0) {
      // Check against inbox data for the time of last update for the friend
      var statusesMap = localConst.STATUSES_MAP;
      var i = 0;

      var content = $('<div class="list-wrap"></div>');
      for (var statusIndex = 0; statusIndex < statusesMap.length && i < self.friendList.length; statusIndex++) {
        var status = statusesMap[statusIndex];

        var status_wrap = $('<ul class="list list-' + status + '"></ul>');
        while (i < self.friendList.length &&
               self.friendList[i].online_presence === status) {
          var entry = createEntry(i);
          if (entry) { status_wrap.append(entry); }

          if ((i + 1) < self.friendList.length && self.friendList[i+1].online_presence == 'active' &&
              self.friendList[i].inFavorites && !self.friendList[i+1].inFavorites) {
            status_wrap.append('<li class="online-fav-divider">MORE ONLINE FRIENDS</li>');
          }

          i++;
        }
        if (status_wrap.children().length !== 0) {  // has entries under current category?
          var toggle_state = localStorage[status+'_state'] || 'on';
          var status_head = $('<div class="head head-' + status +
                                ' head-' + toggle_state + '">' +
                                '<div class="toggle-button toggle-' + toggle_state +
                                  '">' +
                                  ((toggle_state == 'on') ? localConst.DOWNWARDS_TRIANGLE :
                                                            localConst.RIGHTWARDS_TRIANGLE) +
                                '</div>' +
                              '</div>')
            .append('<span>' + localConst.STATUS_HEADS[statusIndex] + '</span>');

          content.append(status_head);
          content.append(status_wrap);
        }
      }

      return content;
    } else {  // s.length !== 0
      var wrap = $('<ul class="list"></ul>');
      for (var i = 0; i < self.friendList.length; i++) {
        var e = createEntry(i);
        if (e)
          wrap.append(e);
      }
      return wrap;
    }
    return null;
  };

  self.addLastUpdatedStats = function() {
    var bgPage = chrome.extension.getBackgroundPage();
    var inboxData = bgPage ? bgPage.inboxData : null;
    var myUid = bgPage ? bgPage.myUid : null;

    if (!inboxData || !myUid)
      return;

    var todayDate = new Date();

    var processedIndices = [];  // have this to avoid duplicate favs

    for (var i = 0; i < inboxData.length; i++) {
      for (var j = 0; j < inboxData[i].to.data.length; j++) {
        var friend = inboxData[i].to.data[j];
        if (!friend || !friend.id || friend.id.toString() == myUid.toString()) // skip self
          continue;
        for (var k = 0; k < self.friendList.length; k++) {
          if (processedIndices.indexOf(k) == -1 &&
                friend.id.toString() == self.friendList[k].uid.toString() &&
                self.friendList[k].online_presence == 'active') {

            self.friendList[k].lastUpdated = new Date(inboxData[i].updated_time);
            if ((todayDate.getTime() -
                    self.friendList[k].lastUpdated.getTime()) <
                localConst.FAVORITES_TIMESPAN) {
              self.friendList[k].inFavorites = true;
              //self.onlineFavNum++;
              processedIndices.push(k);
            }
          }
        }
      }
    }
  };

  self.addCustomFavorites = function() {
    var bgPage = chrome.extension.getBackgroundPage();
    var myUid = bgPage ? bgPage.myUid : null;

    if (!myUid)
      return;

    var todayDate = new Date();

    for (var i = 0; i < self.friendList.length; i++) {
      if (localStorage[localStorageFavKey(myUid.toString(),
                                          self.friendList[i].uid.toString())
                      ]) {  // check if user set a favorite flag by clicking on a star
        if (self.friendList[i].online_presence == 'active' &&
            !self.friendList[i].inFavorites) {  // may be added before by addLastUpdatedStats
          self.friendList[i].inFavorites = true;
          //self.onlineFavNum++;
        }

        self.friendList[i].customFavorite = true;
      }
      else if (self.friendList[i].online_presence == 'active') {
        if (self.friendList[i].customFavorite) {
          delete self.friendList[i].customFavorite;
        }
        if (self.friendList[i].inFavorites && !(self.friendList[i].lastUpdated &&
              ((todayDate.getTime() -
                      self.friendList[i].lastUpdated.getTime()) <
                  localConst.FAVORITES_TIMESPAN))) {
          delete self.friendList[i].inFavorites;
          //self.onlineFavNum--;
        }
      }
    }
  };

  self.updateDOM = function() {
    //self.onlineFavNum = 0;  // number of online users being in favorites group

    self.applySearchFilter();
    self.addLastUpdatedStats();
    self.addCustomFavorites();
    self.sortFriendList();
    var newDom = self.generateFriendsDOM();
    if (newDom.children().length == 0)
      newDom.append('<li><span style="text-align:center">No friends found</span></li>');

    $('#friend_list').remove();
    $('#scrollable-area').append('<div id="friend_list" class="overview"></div>');
    $('#friend_list').append(newDom);

    if ($('.box-wrap').data('antiscroll'))
      $('.box-wrap').data('antiscroll').rebuild();
  };

  self.updateFriendList = function(response, dontAnimate) {
    self.slideToFriendsView(dontAnimate);
    $('#unavail').hide();
    $('#logout').show();

    self.friendList = response;
    var statuses = chrome.extension.getBackgroundPage() ?
        chrome.extension.getBackgroundPage().statuses : null;
    for (var i = 0; i < self.friendList.length; i++) {
      if (statuses && statuses[self.friendList[i].uid.toString()]) {
        self.friendList[i].online_presence =
          statuses[self.friendList[i].uid.toString()];
      }
      else if (self.friendList[i].online_presence === null) {
        self.friendList[i].online_presence = 'offline';
      }
    }

    self.updateDOM();
  };

  self.applySearchFilter = function() {
    var s = $('#search').val();
    //if (!s)
    //  return;

    for (var i = 0; i < self.friendList.length; i++) {
      if (s && self.friendList[i].name.toLowerCase().indexOf(s.toLowerCase()) == -1) {
        self.friendList[i].excluded = true;
      } else {
        self.friendList[i].excluded = false;
      }
    }
  };

  self.requestStatusMessage = function() {

  };

  /*- initialization -----------------*/
  self.friendList = null;
  self.onlineFavNum = 0;

  chrome.extension.onRequestExternal.addListener(function(request, sender, sendResponse) {
    if (!request.type)
      return;
    switch (request.type) {
    case 'friendListReceived':
      if (!request.data)
        return;
      self.updateFriendList(request.data);
      break;
    case 'loggedOut':
      onLoggedOut();
      break;
    case 'userStatusChanged':
      if (!request.uid || !request.status)
        return;
      onUserStatusChanged(request.uid, request.status);
      break;
    default:
      break;
    }
  });

  chrome.extension.onRequest.addListener(function(request, sender, sendResponse) {
    if (request.type && request.type == 'inboxDataAvailable') {
      self.updateDOM();
    } else if (request.type && request.type == 'statusMessageUpdate') {
      if (request.msg) {
        if (!self.isEditingStatus)
          $('#head-col2-row1').text(request.msg);
        else
          self.statusToSetOnCancel = request.msg;
      }
    }

    if (request.type && sendResponse) sendResponse();
  });

  return self;
})();

