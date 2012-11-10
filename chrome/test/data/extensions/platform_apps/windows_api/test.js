// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var callbackPass = chrome.test.callbackPass;

chrome.experimental.app.onLaunched.addListener(function() {
  chrome.test.runTests([
   function testCreateWindow() {
     chrome.app.window.create('test.html', {}, callbackPass(function (win) {
       chrome.test.assertTrue(typeof win.window === 'object');
       chrome.test.assertEq('about:blank', win.location.href);
       chrome.test.assertEq('<html><head></head><body></body></html>',
           win.document.documentElement.outerHTML);
     }))
   },

   function testUpdateWindowWidth() {
     chrome.app.window.create('test.html',
         {width:512, height:384, frame:'custom'},
         callbackPass(function(win) {
           chrome.test.assertEq(512, win.innerWidth);
           chrome.test.assertEq(384, win.innerHeight);
           var oldWidth = win.outerWidth, oldHeight = win.outerHeight;
           win.resizeBy(-256, 0);
           chrome.test.assertEq(oldWidth - 256, win.outerWidth);
           chrome.test.assertEq(oldHeight, win.outerHeight);
         }));
   },

   /*function testMaximize() {
     chrome.app.window.create('test.html', {width: 200, height: 200},
         callbackPass(function(win) {
           win.onresize = callbackPass(function(e) {
             // Crude test to check we're somewhat maximized.
             chrome.test.assertTrue(
                 win.outerHeight > screen.availHeight * 0.8);
             chrome.test.assertTrue(
                 win.outerWidth > screen.availWidth * 0.8);
           });
           win.chrome.app.window.maximize();
         }));
   },*/

   /*function testRestore() {
     chrome.app.window.create('test.html', {width: 200, height: 200},
         callbackPass(function(win) {
           var oldWidth = win.innerWidth;
           var oldHeight = win.innerHeight;
           win.onresize = callbackPass(function() {
             chrome.test.assertTrue(win.innerWidth != oldWidth);
             chrome.test.assertTrue(win.innerHeight != oldHeight);
             // Seems like every time we resize, we get two resize events.
             // See http://crbug.com/133869.
             win.onresize = callbackPass(function() {
               // Ignore the immediately following resize, as it's a clone of
               // the one we just got.
               win.onresize = callbackPass(function() {
                 chrome.test.assertEq(oldWidth, win.innerWidth);
                 chrome.test.assertEq(oldHeight, win.innerHeight);
               });
             })
             win.chrome.app.window.restore();
           });
           win.chrome.app.window.maximize();
         }));
   },*/
  ]);
});
