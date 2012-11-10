# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'target_defaults': {
    'variables': {
      'common_constants_target': 0,
    },
    'target_conditions': [
      ['common_constants_target==1', {
        'sources': [
          'common/chrome_constants.cc',
          'common/chrome_constants.h',
          'common/chrome_paths.cc',
          'common/chrome_paths.h',
          'common/chrome_paths_android.cc',
          'common/chrome_paths_internal.h',
          'common/chrome_paths_linux.cc',
          'common/chrome_paths_mac.mm',
          'common/chrome_paths_win.cc',
          'common/chrome_switches.cc',
          'common/chrome_switches.h',
          'common/env_vars.cc',
          'common/env_vars.h',
          'common/net/gaia/gaia_constants.cc',
          'common/net/gaia/gaia_constants.h',
          'common/net/test_server_locations.cc',
          'common/net/test_server_locations.h',
          'common/pref_names.cc',
          'common/pref_names.h',
        ],
        'actions': [
          {
            'action_name': 'Make chrome_version.cc',
            'variables': {
              'make_version_cc_path': 'tools/build/make_version_cc.py',
            },
            'inputs': [
              '<(make_version_cc_path)',
              'VERSION',
            ],
            'outputs': [
              '<(INTERMEDIATE_DIR)/chrome_version.cc',
            ],
            'action': [
              'python',
              '<(make_version_cc_path)',
              '<@(_outputs)',
              '<(version_full)',
            ],
            'process_outputs_as_sources': 1,
          },
        ],
      }],
    ],
  },
  'targets': [
    {
      'target_name': 'common_constants',
      'type': 'static_library',
      'variables': {
        'common_constants_target': 1,
      },
      'dependencies': [
        '../base/base.gyp:base',
      ],
      'conditions': [
        ['toolkit_uses_gtk == 1', {
          'dependencies': ['../build/linux/system.gyp:gtk'],
        }],
      ],
    },
  ],
  'conditions': [
    ['OS=="win"', {
      'targets': [
        {
          'target_name': 'common_constants_win64',
          'type': 'static_library',
          'variables': {
            'common_constants_target': 1,
          },
          'dependencies': [
            '../base/base.gyp:base_nacl_win64',
          ],
          'defines': [
            '<@(nacl_win64_defines)',
            'COMPILE_CONTENT_STATICALLY',
          ],
          'configurations': {
            'Common_Base': {
              'msvs_target_platform': 'x64',
            },
          },
        },
      ],
    }],
  ],
}
