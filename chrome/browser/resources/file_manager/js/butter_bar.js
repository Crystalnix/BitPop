// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * The minimum about of time to display the butter bar for, in ms.
 * Justification is 1000ms for minimum display time plus 300ms for transition
 * duration.
 */
var MINIMUM_BUTTER_DISPLAY_TIME_MS = 1300;

/**
 * Butter bar is shown on top of the file list and is used to show the copy
 * progress and other messages.
 * @constructor
 * @param {HTMLElement} dialogDom FileManager top-level div.
 * @param {FileCopyManagerWrapper} copyManager The copy manager.
 */
function ButterBar(dialogDom, copyManager) {
  this.dialogDom_ = dialogDom;
  this.butter_ = this.dialogDom_.querySelector('#butter-bar');
  this.document_ = this.butter_.ownerDocument;
  this.copyManager_ = copyManager;
  this.hideTimeout_ = null;
  this.showTimeout_ = null;
  this.lastShowTime_ = 0;

  this.copyManager_.addEventListener('copy-progress',
                                     this.onCopyProgress_.bind(this));
}

/**
 * @return {boolean} True if visible.
 * @private
 */
ButterBar.prototype.isVisible_ = function() {
  return this.butter_.classList.contains('visible');
};

/**
 * @return {boolean} True if displaying an error.
 * @private
 */
ButterBar.prototype.isError_ = function() {
  return this.butter_.classList.contains('error');
};

  /**
 * Show butter bar.
 * @param {string} message The message to be shown.
 * @param {object} opt_options Options: 'actions', 'progress', 'timeout'.
 */
ButterBar.prototype.show = function(message, opt_options) {
  this.clearShowTimeout_();
  this.clearHideTimeout_();

  var actions = this.butter_.querySelector('.actions');
  actions.textContent = '';
  if (opt_options && 'actions' in opt_options) {
    for (var label in opt_options.actions) {
      var link = this.document_.createElement('a');
      link.addEventListener('click', function(callback) {
        callback();
        return false;
      }.bind(null, opt_options.actions[label]));
      actions.appendChild(link);
    }
    actions.hidden = false;
  } else {
    actions.hidden = true;
  }

  this.butter_.querySelector('.progress-bar').hidden =
    !(opt_options && 'progress' in opt_options);

  this.butter_.classList.remove('error');
  this.butter_.classList.remove('visible');  // Will be shown in update_
  this.update_(message, opt_options);
};

/**
 * Show error message in butter bar.
 * @private
 * @param {string} message Message.
 * @param {object} opt_options Same as in show().
 */
ButterBar.prototype.showError_ = function(message, opt_options) {
  this.show(message, opt_options);
  this.butter_.classList.add('error');
};

/**
 * Set message and/or progress.
 * @private
 * @param {string} message Message.
 * @param {object} opt_options Same as in show().
 */
ButterBar.prototype.update_ = function(message, opt_options) {
  if (!opt_options)
    opt_options = {};

  this.clearHideTimeout_();

  var timeout = ('timeout' in opt_options) ? opt_options.timeout : 10 * 1000;
  if (timeout) {
    this.hideTimeout_ = setTimeout(function() {
      this.hideTimeout_ = null;
      this.hide_();
    }.bind(this), timeout);
  }

  this.butter_.querySelector('.butter-message').textContent = message;
  if (message && !this.isVisible_()) {
    // The butter bar is made visible on the first non-empty message.
    this.butter_.classList.add('visible');
    this.lastShowTime_ = Date.now();
  }
  if (opt_options && 'progress' in opt_options) {
    this.butter_.querySelector('.progress-track').style.width =
        (opt_options.progress * 100) + '%';
  }
};

/**
 * Hide butter bar. There might be some delay before hiding so that butter bar
 * would be shown for no less than the minimal time.
 * @param {boolean} opt_force If true hide immediately.
 * @private
 */
ButterBar.prototype.hide_ = function(opt_force) {
  this.clearHideTimeout_();

  if (!this.isVisible_())
    return;

  var delay =
      MINIMUM_BUTTER_DISPLAY_TIME_MS - (Date.now() - this.lastShowTime_);

  if (opt_force || delay <= 0) {
    this.butter_.classList.remove('visible');
  } else {
    // Reschedule hide to comply with the minimal display time.
    this.hideTimeout_ = setTimeout(function() {
      this.hideTimeout_ = null;
      this.hide_(true);
    }.bind(this), delay);
  }
};

/**
 * If butter bar shows an error message, close it.
 * @return {boolean} True if butter bar was closed.
 */
ButterBar.prototype.hideError = function() {
  if (this.isVisible_() && this.isError_()) {
    this.hide_(true /* force */);
    return true;
  } else {
    return false;
  }
};

/**
 * Clear the show timeout if it is set.
 * @private
 */
ButterBar.prototype.clearShowTimeout_ = function() {
  if (this.showTimeout_) {
    clearTimeout(this.showTimeout_);
    this.showTimeout_ = null;
  }
};

/**
 * Clear the hide timeout if it is set.
 * @private
 */
ButterBar.prototype.clearHideTimeout_ = function() {
  if (this.hideTimeout_) {
    clearTimeout(this.hideTimeout_);
    this.hideTimeout_ = null;
  }
};

/**
 * @private
 * @return {string?} The type of operation.
 */
ButterBar.prototype.transferType_ = function() {
  var progress = this.progress_;
  if (!progress ||
      progress.pendingMoves === 0 && progress.pendingCopies === 0)
    return 'TRANSFER';

  if (progress.pendingMoves > 0) {
    if (progress.pendingCopies > 0)
      return 'TRANSFER';
    return 'MOVE';
  }

  return 'COPY';
};

/**
 * Set up butter bar for showing copy progress.
 * @private
 */
ButterBar.prototype.showProgress_ = function() {
  this.progress_ = this.copyManager_.getStatus();
  var options = {progress: this.progress_.percentage, actions: {}, timeout: 0};

  var type = this.transferType_();
  var progressString = (this.progress_.pendingItems === 1) ?
          strf(type + '_FILE_NAME', this.progress_.filename) :
          strf(type + '_ITEMS_REMAINING', this.progress_.pendingItems);

  if (this.isVisible_()) {
    this.update_(progressString, options);
  } else {
    options.actions[str('CANCEL_LABEL')] =
        this.copyManager_.requestCancel.bind(this.copyManager_);
    this.show(progressString, options);
  }
};

/**
 * 'copy-progress' event handler. Show progress or an appropriate message.
 * @private
 * @param {cr.Event} event A 'copy-progress' event from FileCopyManager.
 */
ButterBar.prototype.onCopyProgress_ = function(event) {
  if (event.reason != 'PROGRESS')
    this.clearShowTimeout_();

  switch (event.reason) {
    case 'BEGIN':
      this.showTimeout_ = setTimeout(function() {
        this.showTimeout_ = null;
        this.showProgress_();
      }.bind(this), 500);
      break;

    case 'PROGRESS':
      this.showProgress_();
      break;

    case 'SUCCESS':
      this.hide_();
      break;

    case 'CANCELLED':
      this.show(str(this.transferType_() + '_CANCELLED'), {timeout: 1000});
      break;

    case 'ERROR':
      if (event.error.reason === 'TARGET_EXISTS') {
        var name = event.error.data.name;
        if (event.error.data.isDirectory)
          name += '/';
        this.showError_(strf(this.transferType_() +
                             '_TARGET_EXISTS_ERROR', name));
      } else if (event.error.reason === 'FILESYSTEM_ERROR') {
        if (event.error.data.toGDrive &&
            event.error.data.code === FileError.QUOTA_EXCEEDED_ERR) {
          // The alert will be shown in FileManager.onCopyProgress_.
          this.hide_();
        } else {
          this.showError_(strf(this.transferType_() + '_FILESYSTEM_ERROR',
                               getFileErrorString(event.error.data.code)));
          }
      } else {
        this.showError_(strf(this.transferType_() + '_UNEXPECTED_ERROR',
                             event.error));
      }
      break;

    default:
      console.log('Unknown "copy-progress" event reason: ' + event.reason);
  }
};
