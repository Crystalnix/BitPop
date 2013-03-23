# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  # TODO(jrg): Update this action and other jni generators to only
  # require specifying the java directory and generate the rest.
  # TODO(jrg): when doing the above, make sure we support multiple
  # output directories (e.g. browser/jni and common/jni if needed).
  'sources': [
    'public/android/java/src/org/chromium/content/app/ContentMain.java',
    'public/android/java/src/org/chromium/content/app/LibraryLoader.java',
    'public/android/java/src/org/chromium/content/app/SandboxedProcessService.java',
    'public/android/java/src/org/chromium/content/browser/AndroidBrowserProcess.java',
    'public/android/java/src/org/chromium/content/browser/BrowserProcessSurfaceTexture.java',
    'public/android/java/src/org/chromium/content/browser/ContentSettings.java',
    'public/android/java/src/org/chromium/content/browser/ContentVideoView.java',
    'public/android/java/src/org/chromium/content/browser/ContentViewCore.java',
    'public/android/java/src/org/chromium/content/browser/ContentViewRenderView.java',
    'public/android/java/src/org/chromium/content/browser/ContentViewStatics.java',
    'public/android/java/src/org/chromium/content/browser/DeviceOrientation.java',
    'public/android/java/src/org/chromium/content/browser/DownloadController.java',
    'public/android/java/src/org/chromium/content/browser/ImeAdapter.java',
    'public/android/java/src/org/chromium/content/browser/InterstitialPageDelegateAndroid.java',
    'public/android/java/src/org/chromium/content/browser/LoadUrlParams.java',
    'public/android/java/src/org/chromium/content/browser/LocationProvider.java',
    'public/android/java/src/org/chromium/content/browser/SandboxedProcessLauncher.java',
    'public/android/java/src/org/chromium/content/browser/SmoothScroller.java',
    'public/android/java/src/org/chromium/content/browser/TouchPoint.java',
    'public/android/java/src/org/chromium/content/browser/WebContentsObserverAndroid.java',
    'public/android/java/src/org/chromium/content/common/CommandLine.java',
    'public/android/java/src/org/chromium/content/common/DeviceInfo.java',
    'public/android/java/src/org/chromium/content/common/SurfaceCallback.java',
    'public/android/java/src/org/chromium/content/common/SurfaceTextureListener.java',
    'public/android/java/src/org/chromium/content/common/TraceEvent.java',
   ],
  'variables': {
    'jni_gen_dir': 'content'
  },
  'includes': [ '../build/jni_generator.gypi' ],
}
