// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options.browser_options', function() {
  /** @const */ var DeletableItem = options.DeletableItem;
  /** @const */ var DeletableItemList = options.DeletableItemList;
  /** @const */ var ListSingleSelectionModel = cr.ui.ListSingleSelectionModel;

  /**
   * Creates a new profile list item.
   * @param {Object} profileInfo The profile this item respresents.
   * @constructor
   * @extends {cr.ui.DeletableItem}
   */
  function ProfileListItem(profileInfo) {
    var el = cr.doc.createElement('div');
    el.profileInfo_ = profileInfo;
    ProfileListItem.decorate(el);
    return el;
  }

  /**
   * Decorates an element as a profile list item.
   * @param {!HTMLElement} el The element to decorate.
   */
  ProfileListItem.decorate = function(el) {
    el.__proto__ = ProfileListItem.prototype;
    el.decorate();
  };

  ProfileListItem.prototype = {
    __proto__: DeletableItem.prototype,

    /**
     * @type {string} the file path of this profile list item.
     */
    get profilePath() {
      return this.profileInfo_.filePath;
    },

    /** @override */
    decorate: function() {
      DeletableItem.prototype.decorate.call(this);

      var profileInfo = this.profileInfo_;

      var iconEl = this.ownerDocument.createElement('img');
      iconEl.className = 'profile-img';
      iconEl.src = profileInfo.iconURL;
      this.contentElement.appendChild(iconEl);

      var nameEl = this.ownerDocument.createElement('div');
      if (profileInfo.isCurrentProfile)
        nameEl.classList.add('profile-item-current');
      this.contentElement.appendChild(nameEl);

      var displayName = profileInfo.name;
      if (profileInfo.isCurrentProfile) {
        displayName = loadTimeData.getStringF('profilesListItemCurrent',
                                              profileInfo.name);
      }
      nameEl.textContent = displayName;

      // Ensure that the button cannot be tabbed to for accessibility reasons.
      this.closeButtonElement.tabIndex = -1;
    },
  };

  var ProfileList = cr.ui.define('list');

  ProfileList.prototype = {
    __proto__: DeletableItemList.prototype,

    /** @override */
    decorate: function() {
      DeletableItemList.prototype.decorate.call(this);
      this.selectionModel = new ListSingleSelectionModel();
    },

    /** @override */
    createItem: function(pageInfo) {
      var item = new ProfileListItem(pageInfo);
      return item;
    },

    /** @override */
    deleteItemAtIndex: function(index) {
      ManageProfileOverlay.showDeleteDialog(this.dataModel.item(index));
    },

    /** @override */
    activateItemAtIndex: function(index) {
      // Don't allow the user to edit a profile that is not current.
      var profileInfo = this.dataModel.item(index);
      if (profileInfo.isCurrentProfile)
        ManageProfileOverlay.showManageDialog(profileInfo);
    },
  };

  return {
    ProfileList: ProfileList
  };
});

