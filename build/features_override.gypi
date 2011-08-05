# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file is only included in full-chromium builds, and overrides the
# feature_defines variable in
# third_party/WebKit/Source/WebKit/chromium/features.gypi.
{
  'variables': {
    # WARNING: This list of strings completely replaces the list in
    # features.gypi. Therefore, if an enable is listed in features.gypi
    # but not listed below, it will revert to its hardcoded webkit value.
    'feature_defines': [
      'ENABLE_3D_PLUGIN=1',
      'ENABLE_BLOB=1',
      'ENABLE_BLOB_SLICE=1',
      'ENABLE_CHANNEL_MESSAGING=1',
      'ENABLE_CLIENT_BASED_GEOLOCATION=1',
      'ENABLE_DASHBOARD_SUPPORT=0',
      'ENABLE_DATABASE=1',
      'ENABLE_DATAGRID=0',
      'ENABLE_DATA_TRANSFER_ITEMS=1',
      'ENABLE_DETAILS=1',
      'ENABLE_DEVICE_ORIENTATION=1',
      'ENABLE_DIRECTORY_UPLOAD=1',
      'ENABLE_DOM_STORAGE=1',
      'ENABLE_EVENTSOURCE=1',
      'ENABLE_JAVASCRIPT_I18N_API=1',
      'ENABLE_FILE_SYSTEM=1',
      'ENABLE_FILTERS=1',
      'ENABLE_GEOLOCATION=1',
      'ENABLE_GESTURE_RECOGNIZER=1',
      'ENABLE_ICONDATABASE=0',
      'ENABLE_INDEXED_DATABASE=1',
      'ENABLE_INPUT_SPEECH=1',
      'ENABLE_JAVASCRIPT_DEBUGGER=1',
      'ENABLE_JSC_MULTIPLE_THREADS=0',
      'ENABLE_LEVELDB=1',
      'ENABLE_LINK_PREFETCH=1',
      'ENABLE_METER_TAG=1',
      'ENABLE_MEDIA_STATISTICS=1',
      'ENABLE_MEDIA_STREAM=0',
      'ENABLE_MHTML=1',
      'ENABLE_NOTIFICATIONS=1',
      'ENABLE_OFFLINE_WEB_APPLICATIONS=1',
      'ENABLE_OPENTYPE_SANITIZER=1',
      'ENABLE_ORIENTATION_EVENTS=0',
      'ENABLE_PAGE_VISIBILITY_API=1',
      'ENABLE_PROGRESS_TAG=1',
      'ENABLE_QUOTA=1',
      'ENABLE_REQUEST_ANIMATION_FRAME=1',
      'ENABLE_RUBY=1',
      'ENABLE_SANDBOX=1',
      'ENABLE_SHARED_WORKERS=1',
      'ENABLE_SKIA_GPU=<(use_skia_gpu)',
      'ENABLE_SVG=<(enable_svg)',
      'ENABLE_SVG_ANIMATION=<(enable_svg)',
      'ENABLE_SVG_AS_IMAGE=<(enable_svg)',
      'ENABLE_SVG_FONTS=<(enable_svg)',
      'ENABLE_SVG_FOREIGN_OBJECT=<(enable_svg)',
      'ENABLE_SVG_USE=<(enable_svg)',
      'ENABLE_TOUCH_EVENTS=<(enable_touch_events)',
      'ENABLE_TOUCH_ICON_LOADING=0',
      'ENABLE_V8_SCRIPT_DEBUG_SERVER=1',
      'ENABLE_VIDEO=1',
      'ENABLE_WEB_SOCKETS=1',
      'ENABLE_WEB_TIMING=1',
      'ENABLE_WEBGL=1',
      'ENABLE_WORKERS=1',
      'ENABLE_XHR_RESPONSE_BLOB=1',
      'ENABLE_XPATH=1',
      'ENABLE_XSLT=1',
      'WTF_USE_BUILTIN_UTF8_CODEC=1',
      # WTF_USE_DYNAMIC_ANNOTATIONS=1 may be defined in build/common.gypi
      # We can't define it here because it should be present only
      # in Debug or release_valgrind_build=1 builds.
      'WTF_USE_WEBP=1',
      'WTF_USE_WEBKIT_IMAGE_DECODERS=1',
    ],
    # We have to nest variables inside variables so that they can be overridden
    # through GYP_DEFINES.
    'variables': {
      'use_accelerated_compositing%': 1,
      'use_threaded_compositing%': 0,
      'enable_svg%': 1,
      'enable_touch_events%': 1,
      'use_skia_gpu%': 0,
    },
    'use_accelerated_compositing%': '<(use_accelerated_compositing)',
    'use_threaded_compositing%': '<(use_threaded_compositing)',
    'enable_svg%': '<(enable_svg)',
    'enable_touch_events%': '<(enable_touch_events)',
    'conditions': [
      ['use_accelerated_compositing==1', {
        'feature_defines': [
          'WTF_USE_ACCELERATED_COMPOSITING=1',
          'ENABLE_3D_RENDERING=1',
        ],
      }],
      ['use_accelerated_compositing==1 and OS!="mac"', {
        'feature_defines': [
          'ENABLE_ACCELERATED_2D_CANVAS=1',
        ],
      }],
      ['use_accelerated_compositing==1 and use_threaded_compositing==1', {
        'feature_defines': [
          'WTF_USE_THREADED_COMPOSITING=1',
        ],
      }],
      # TODO(crogers): For the moment Windows is only enabled for
      # Google-branded build, since the FFmpeg DLLs need to be re-built
      # for chromium.
      ['OS=="mac" or OS=="linux" or (OS=="win" and branding=="Chrome")', {
        'feature_defines': [
          'ENABLE_WEB_AUDIO=1',
        ],
      }],
      # Mac OS X uses Accelerate.framework FFT by default instead of FFmpeg.
      ['OS!="mac"', {
        'feature_defines': [
          'WTF_USE_WEBAUDIO_FFMPEG=1',
        ],
      }],
      ['enable_register_protocol_handler==1', {
        'feature_defines': [
          'ENABLE_REGISTER_PROTOCOL_HANDLER=1',
        ],
      }],
    ],
    # TODO: If the need arises, create a mechanism that will intelligently
    # merge the lists rather than replace one with the other. This may
    # require changes in gyp.
  },

}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
