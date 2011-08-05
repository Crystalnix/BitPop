# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    # These are defined here because we want to be able to compile them on
    # the buildbots without needed the OpenGL ES 2.0 conformance tests
    # which are not open source.
    'bootstrap_sources_native': [
      'native/main.cc',
    ],
    'conditions': [
      ['OS=="linux"', {
        'bootstrap_sources_native': [
          'native/egl_native.cc',
          'native/egl_native_linux.cc',
        ],
      }],
      ['OS=="win"', {
        'bootstrap_sources_native': [
          'native/egl_native.cc',
          'native/egl_native_win.cc',
        ],
      }],
    ],
  },
  'targets': [
    {
      'target_name': 'egl_native',
      'type': 'static_library',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/gpu/gpu.gyp:command_buffer_service',
      ],
      'include_dirs': ['egl/native'],
      'sources': [
        'egl/config.cc',
        'egl/config.h',
        'egl/display.cc',
        'egl/display.h',
        'egl/egl.cc',
        'egl/surface.cc',
        'egl/surface.h',
      ],
      'direct_dependent_settings': {
        'include_dirs': ['egl/native'],
      },
    },
    {
      'target_name': 'egl_main_native',
      'type': 'static_library',
      'dependencies': [
        'egl_native',
      ],
      'conditions': [
        ['OS=="linux"', {
          'dependencies': ['../../build/linux/system.gyp:gtk'],
        }],
      ],
      'include_dirs': ['egl/native'],
      'sources': [
        '<@(bootstrap_sources_native)',
      ],
      'direct_dependent_settings': {
        'include_dirs': ['egl/native'],
      },
      'defines': ['GTF_GLES20'],
    },
    {
      'target_name': 'gles2_conform_support',
      'type': 'executable',
      'dependencies': [
        'egl_native',
        '<(DEPTH)/gpu/gpu.gyp:gles2_c_lib_nocheck',
        '<(DEPTH)/third_party/expat/expat.gyp:expat',
      ],
      'conditions': [
        ['OS=="linux"', {
          'dependencies': ['../../build/linux/system.gyp:gtk'],
        }],
      ],
      'defines': [
        'GLES2_CONFORM_SUPPORT_ONLY',
        'GTF_GLES20',
      ],
      'sources': [
        '<@(bootstrap_sources_native)',
        'gles2_conform_support.c'
      ],
    },
  ],
}


# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
