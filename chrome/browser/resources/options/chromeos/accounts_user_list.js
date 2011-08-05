// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options.accounts', function() {
  const List = cr.ui.List;
  const ListItem = cr.ui.ListItem;
  const ArrayDataModel = cr.ui.ArrayDataModel;

  /**
   * Creates a new user list.
   * @param {Object=} opt_propertyBag Optional properties.
   * @constructor
   * @extends {cr.ui.List}
   */
  var UserList = cr.ui.define('list');

  UserList.prototype = {
    __proto__: List.prototype,

    pref: 'cros.accounts.users',

    /** @inheritDoc */
    decorate: function() {
      List.prototype.decorate.call(this);

      // HACK(arv): http://crbug.com/40902
      window.addEventListener('resize', this.redraw.bind(this));

      this.addEventListener('click', this.handleClick_);

      var self = this;

      // Listens to pref changes.
      Preferences.getInstance().addEventListener(this.pref,
          function(event) {
            self.load_(event.value);
          });
    },

    createItem: function(user) {
      return new UserListItem(user);
    },

    /**
     * Finds the index of user by given email.
     * @private
     * @param {string} email The email address to look for.
     * @return {number} The index of the found user or -1 if not found.
     */
    findUserByEmail_: function(email) {
      var dataModel = this.dataModel;
      if (!dataModel)
        return -1;

      var length = dataModel.length;
      for (var i = 0; i < length; ++i) {
        var user = dataModel.item(i);
        if (user.email == email) {
          return i;
        }
      }

      return -1;
    },

    /**
     * Adds given user to model and update backend.
     * @param {Object} user A user to be added to user list.
     */
    addUser: function(user) {
      var index = this.findUserByEmail_(user.email);
      if (index == -1) {
        this.dataModel.push(user);
        chrome.send('whitelistUser', [user.email]);
      }
    },

    /**
     * Removes given user from model and update backend.
     */
    removeUser: function(user) {
      var dataModel = this.dataModel;

      var index = dataModel.indexOf(user);
      if (index >= 0) {
        dataModel.splice(index, 1);
        chrome.send('unwhitelistUser', [user.email]);
      }
    },

    /**
     * Update given user's account picture.
     * @param {String} email Email ofthe user to update.
     * @param {String} imageUrl Updated account picture url.
     */
    updateAccountPicture: function(email, imageUrl) {
      var index = this.findUserByEmail_(email);
      if (index >= 0) {
        var item = this.getListItemByIndex(index);
        if (item)
          item.setPicture(imageUrl);
      }
    },

    /**
     * Handles the clicks on the list and triggers user removal if the click
     * is on the remove user button.
     * @private
     * @param {!Event} e The click event object.
     */
    handleClick_: function(e) {
      // Handle left button click
      if (e.button == 0) {
        var el = e.target;
        if (el.classList.contains('remove-user-button')) {
          this.removeUser(el.parentNode.user);
        }
      }
    },

    /**
     * Loads given user list.
     * @param {Array} users An array of user object.
     */
    load_: function(users) {
      this.dataModel = new ArrayDataModel(users);
    }
  };

  /**
   * Whether the user list is disabled. Only used for display purpose.
   * @type {boolean}
   */
  cr.defineProperty(UserList, 'disabled', cr.PropertyKind.BOOL_ATTR);

  /**
   * Creates a new user list item.
   * @param user The user account this represents.
   * @constructor
   * @extends {cr.ui.ListItem}
   */
  function UserListItem(user) {
    var el = cr.doc.createElement('div');
    el.user = user;
    UserListItem.decorate(el);
    return el;
  }

  /**
   * Decorates an element as a user account item.
   * @param {!HTMLElement} el The element to decorate.
   */
  UserListItem.decorate = function(el) {
    el.__proto__ = UserListItem.prototype;
    el.decorate();
  };

  UserListItem.prototype = {
    __proto__: ListItem.prototype,

    /** @inheritDoc */
    decorate: function() {
      ListItem.prototype.decorate.call(this);

      this.className = 'user-list-item';

      this.icon_ = this.ownerDocument.createElement('img');
      this.icon_.className = 'user-icon';
      this.icon_.src = 'chrome://userimage/' + this.user.email +
                       '?id=' + (new Date()).getTime();

      var labelEmail = this.ownerDocument.createElement('span');
      labelEmail.className = 'user-email-label';
      labelEmail.textContent = this.user.email;

      var labelName = this.ownerDocument.createElement('span');
      labelName.className = 'user-name-label';
      labelName.textContent = this.user.owner ?
          localStrings.getStringF('username_format', this.user.name) :
          this.user.name;

      var emailNameBlock = this.ownerDocument.createElement('div');
      emailNameBlock.className = 'user-email-name-block';
      emailNameBlock.appendChild(labelEmail);
      emailNameBlock.appendChild(labelName);
      emailNameBlock.title = this.user.owner ?
          localStrings.getStringF('username_format', this.user.email) :
          this.user.email;

      this.appendChild(this.icon_);
      this.appendChild(emailNameBlock);

      if (!this.user.owner) {
        var removeButton = this.ownerDocument.createElement('button');
        removeButton.classList.add('raw-button');
        removeButton.classList.add('remove-user-button');
        this.appendChild(removeButton);
      }
    },

    /**
     * Set user picture to givem url.
     * @param {String} imageUrl Account picture url.
     */
    setPicture: function(imageUrl) {
      this.icon_.src = imageUrl;
    }
  };

  return {
    UserList: UserList
  };
});
