/**
* Bookmark handling code for chrome extensions
*
* Copyright (c) 2011 Ankit Ahuja
* Dual licensed under GPL and MIT licenses.
**/

function createBookmark(name, url, parentId, callback) {
  function makeCreateBookmarkRequest(id) {
    var properties = {
      parentId: id,
      title: name
    };

    if (url) {
      properties.url = url;
    }

    chrome.bookmarks.create(properties, callback);
  }

  if (!parentId) {
    getRootBookmark(function(bookmark) {
      makeCreateBookmarkRequest(bookmark.id);
    });
  }
  else
    makeCreateBookmarkRequest(parentId);
}

function loadBookmark(id, title, callback) {
  if (id) {
    chrome.bookmarks.get(id, callback);
    return;
  }

  if (title) {
    chrome.bookmarks.search('stylebot', function(bookmarks) {
      callback(bookmarks);
    });
  }
}

function saveBookmark(id, url, callback) {
  chrome.bookmarks.update(id, {url: url}, callback);
}

function removeBookmark(id, callback) {
  chrome.bookmarks.remove(id, callback);
}

function removeBookmarkTree(id, callback) {
  chrome.bookmarks.removeTree(id, callback);
}

function getRootBookmark(callback) {
  chrome.bookmarks.getTree(function(bookmarks) {
    callback(bookmarks[0].children[1]);
  });
}