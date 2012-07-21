// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('ntp4', function() {
  'use strict';

  /**
   * Creates a new NewsPage object. This object contains news iframe and controls
   * its layout.
   * @constructor
   * @extends {HTMLDivElement}
   */
  function NewsPage(url) {
    var el = cr.doc.createElement('div');
    el.url_ = url;
    el.__proto__ = NewsPage.prototype;
    el.initialize();

    return el;
  }

  NewsPage.prototype = {
    __proto__: HTMLDivElement.prototype,

    initialize: function() {
      this.className = 'tile-page news-page';

      this.newsIframe_ = this.ownerDocument.createElement('iframe');
      this.newsIframe_.className = 'news-iframe';
      this.newsIframe_.style.height = '100%';
      this.newsIframe_.src = this.url_;
      this.appendChild(this.newsIframe_);

      this.addEventListener('mousewheel', this.onMouseWheel_);
    },

    /**
     * Scrolls the page in response to a mousewheel event.
     * @param {Event} e The mousewheel event.
     */
    handleMouseWheel: function(e) {
      this.newsIframe_.scrollTop -= e.wheelDeltaY / 3;
    },

    /**
     * Handles mouse wheels on |this|. We handle this explicitly because we want
     * a consistent experience whether the user scrolls on the page or on the
     * page switcher (handleMouseWheel provides a common conversion factor
     * between wheel delta and scroll delta).
     * @param {Event} e The mousewheel event.
     * @private
     */
    onMouseWheel_: function(e) {
      if (e.wheelDeltaY == 0)
        return;

      this.handleMouseWheel(e);
      e.preventDefault();
      e.stopPropagation();
    },

  };

  return { NewsPage: NewsPage, };
});
