var bitpop;
//var already_created = false;
if (!bitpop) bitpop = {};
bitpop.FriendsSidebar = (function() {
  var localConst = {
    SLIDE_ANIMATION_DURATION: 1000,
    VIEW_WIDTH: 185,
    UPDATE_LIST_INTERVAL: 1 * 60 * 1000,  // 1 min

    END: null
  };

  var self = arguments.callee;  // for use in private functions to ref the enclosing function

  $(document).ready(function() {
    //console.log('onReady 2 START');
    var bgPage = chrome.extension.getBackgroundPage();
    //console.log(bgPage);

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

    $('#search').bind('keyup change', function () {
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

    //console.log('onReady 2 END');
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

  // function sendUpdateFriendListRequest() {
  //   if (localStorage.accessToken)
  //     chrome.extension.sendRequest({ requestType: 'getFriendList' });
  // }

  /*- private message handlers -------*/
  // var onServiceAvailable = function() {
  //   $('#friend_list').removeClass('disabled');
  //   $('#unavail').hide();
  //   sendUpdateFriendListRequest();
  // };

  var onLoggedOut = function() {
    self.slideToLoginView();
    $('#friend_list').remove();
  };

  var onUserStatusChanged = function(uid, status) {
    // commented out as this work was delegated to fb controller extension
    // var statusMap = {
    //   available: 'active',
    //   idle: 'idle',
    //   error: 'error',
    //   unavailable: 'offline'
    // };

    // var st = statusMap[status];
    // if (!st)
    //   st = 'offline';

    for (var i = 0; i < self.friendList.length; ++i) {
      if (self.friendList[i].uid == uid)
        self.friendList[i].online_presence = status;
    }

    self.updateDOM();
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
    if ($('p.error').is(':visible'))
      $('p.error').fadeOut();

    if (!dontAnimate)
      $('#slide-wrap').stop().animate({ scrollLeft: 0 },
          localConst.SLIDE_ANIMATION_DURATION);
    else
      $('#slide-wrap').stop().scrollLeft(0);
  };

  self.sortFriendList = function() {
    var statusesMap = [ 'active', 'idle', 'offline', 'error'];
    self.friendList.sort(function (a,b) {
        if (a.online_presence == b.online_presence)
          return alphabetical(a.name, b.name);
        else {
          return (statusesMap.indexOf(a.online_presence) <
            statusesMap.indexOf(b.online_presence)) ? -1 : 1;
        }
    });
  };

  self.generateFriendsDOM = function() {
    var wrap = $('<ul></ul>');
    for (var i = 0; i < self.friendList.length; i++) {
      if (!self.friendList[i].excluded) {
        var li = $('<li><span class="leftSide"><img alt="" />' +
            self.friendList[i].name + '</span></li>');
        li.attr('id', 'buddy_' + self.friendList[i].uid.toString());
        li.prop('jid', self.friendList[i].uid.toString());
        li.prop('username', self.friendList[i].name);
        li.prop('online_status', self.friendList[i].online_presence);

        $('img', li).attr('src', self.friendList[i].pic_square);
        if (self.friendList[i].online_presence &&
            self.friendList[i].online_presence != 'offline')
          li.append('<img class="status" src="images/' +
              self.friendList[i].online_presence + '.png" alt="" />');
        wrap.append(li);
      }
    }

    return wrap;
  };

  self.updateDOM = function() {
    //if (!already_created) {
      self.applySearchFilter();
      self.sortFriendList();
      var newDom = self.generateFriendsDOM();
      if (newDom.children().length == 0)
        newDom.append('<li><span style="text-align:center">No friends found</span></li>');

      $('#friend_list').remove();
      $('#scrollable-area').append('<div id="friend_list" class="overview"></div>');
      $('#friend_list').append(newDom);

      $('#friend_list li span').click(function() {
        var parent = $(this).parent();
        chrome.bitpop.facebookChat.addChat(
          parent.prop('jid'),
          parent.prop('username'),
          parent.prop('online_status')
        );
      });
    //  already_created = true;
    //}

    if ($('.box-wrap').data('antiscroll'))
      $('.box-wrap').data('antiscroll').rebuild();
    //setTimeout(self.updateDOM, 10000);
  };

  self.updateFriendList = function(response, dontAnimate) {
    // if (response.error) {
    //   console.log(response.error);
    //   $('#friend_list').addClass('disabled');
    //   $('#unavail').show();
    //   return;
    // }

    //if (already_created)
    //  return;

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
    for (var i = 0; i < self.friendList.length; i++) {
      if (s && self.friendList[i].name.toLowerCase().indexOf(s.toLowerCase()) == -1) {
        self.friendList[i].excluded = true;
      } else {
        self.friendList[i].excluded = false;
      }
    }
  }

  /*- initialization -----------------*/
  self.friendList = null;

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

  return self;
})();

