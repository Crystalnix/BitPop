// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Counter used to give webkit animations unique names.
var animationCounter = 0;

function addAnimation(code) {
  var name = 'anim' + animationCounter;
  animationCounter++;
  var rules = document.createTextNode(
      '@-webkit-keyframes ' + name + ' {' + code + '}');
  var el = document.createElement('style');
  el.type = 'text/css';
  el.appendChild(rules);
  document.body.appendChild(el);

  return name;
}

function fadeInElement(el) {
  if (el.classList.contains('visible'))
    return;
  el.classList.remove('closing');
  el.style.height = 'auto';
  var height = el.offsetHeight;
  el.style.height = height + 'px';
  var animName = addAnimation(
    '0% { opacity: 0; height: 0; } ' +
    '80% { height: ' + (height + 4) + 'px; }' +
    '100% { opacity: 1; height: ' + height + 'px; }');
  el.style.webkitAnimationName = animName;
  el.classList.add('visible');
  el.addEventListener('webkitAnimationEnd', function() {
    el.style.height = '';
    el.style.webkitAnimationName = '';
  }, false );
}

function fadeOutElement(el) {
  if (!el.classList.contains('visible'))
    return;
  el.style.webkitAnimationName = '';
  el.classList.add('closing');
  el.classList.remove('visible');
}

function showLoadingAnimation() {
  $('dancing-dots-text').classList.remove('hidden');
  $('overlay-layer').classList.remove('invisible');
}

function hideLoadingAnimation() {
  var overlayLayer = $('overlay-layer');
  overlayLayer.addEventListener('webkitTransitionEnd', loadingAnimationCleanup);
  overlayLayer.classList.add('invisible');
}

function loadingAnimationCleanup() {
  $('dancing-dots-text').classList.add('hidden');
  $('overlay-layer').removeEventListener('webkitTransitionEnd',
                                         loadingAnimationCleanup);
}
