# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'includes': [
    'ipc.gypi',
  ],
  'targets': [
    {
      'target_name': 'ipc',
      'type': '<(component)',
      'variables': {
        'ipc_target': 1,
      },
      'dependencies': [
        '../base/base.gyp:base',
        '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
      ],
      # TODO(gregoryd): direct_dependent_settings should be shared with the
      # 64-bit target, but it doesn't work due to a bug in gyp
      'direct_dependent_settings': {
        'include_dirs': [
          '..',
        ],
      },
    },
    {
      'target_name': 'ipc_tests',
      'type': '<(gtest_target_type)',
      'dependencies': [
        'ipc',
        'test_support_ipc',
        '../base/base.gyp:base',
        '../base/base.gyp:base_i18n',
        '../base/base.gyp:test_support_base',
        '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
        '../testing/gtest.gyp:gtest',
      ],
      'include_dirs': [
        '..'
      ],
      'sources': [
        'file_descriptor_set_posix_unittest.cc',
        'ipc_channel_posix_unittest.cc',
        'ipc_fuzzing_tests.cc',
        'ipc_message_unittest.cc',
        'ipc_message_utils_unittest.cc',
        'ipc_send_fds_test.cc',
        'ipc_sync_channel_unittest.cc',
        'ipc_sync_message_unittest.cc',
        'ipc_sync_message_unittest.h',
        'ipc_tests.cc',
        'ipc_tests.h',
        'sync_socket_unittest.cc',
      ],
      'conditions': [
        ['toolkit_uses_gtk == 1', {
          'dependencies': [
            '../build/linux/system.gyp:gtk',
          ],
        }],
        ['OS == "android" and gtest_target_type == "shared_library"', {
          'dependencies': [
            '../testing/android/native_test.gyp:native_test_native_code',
          ],
        }],
        ['os_posix == 1 and OS != "mac" and OS != "android"', {
          'conditions': [
            ['linux_use_tcmalloc==1', {
              'dependencies': [
                '../base/allocator/allocator.gyp:allocator',
              ],
            }],
          ],
        }]
      ],
    },
    {
      'target_name': 'test_support_ipc',
      'type': 'static_library',
      'dependencies': [
        'ipc',
        '../base/base.gyp:base',
        '../testing/gtest.gyp:gtest',
      ],
      'sources': [
        'ipc_multiprocess_test.cc',
        'ipc_multiprocess_test.h',
        'ipc_test_sink.cc',
        'ipc_test_sink.h',
      ],
    },
  ],
  'conditions': [
    ['OS=="win"', {
      'targets': [
        {
          'target_name': 'ipc_win64',
          'type': '<(component)',
          'variables': {
            'ipc_target': 1,
          },
          'dependencies': [
            '../base/base.gyp:base_nacl_win64',
            '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations_win64',
          ],
          # TODO(gregoryd): direct_dependent_settings should be shared with the
          # 32-bit target, but it doesn't work due to a bug in gyp
          'direct_dependent_settings': {
            'include_dirs': [
              '..',
            ],
          },
          'configurations': {
            'Common_Base': {
              'msvs_target_platform': 'x64',
            },
          },
        },
      ],
    }],
    # Special target to wrap a gtest_target_type==shared_library
    # ipc_tests into an android apk for execution.
    # See base.gyp for TODO(jrg)s about this strategy.
    ['OS == "android" and gtest_target_type == "shared_library"', {
      'targets': [
        {
          'target_name': 'ipc_tests_apk',
          'type': 'none',
          'dependencies': [
            '../base/base.gyp:base_java',
            'ipc_tests',
          ],
          'variables': {
            'test_suite_name': 'ipc_tests',
            'input_shlib_path': '<(SHARED_LIB_DIR)/<(SHARED_LIB_PREFIX)ipc_tests<(SHARED_LIB_SUFFIX)',
            'input_jars_paths': ['<(PRODUCT_DIR)/lib.java/chromium_base.jar',],
          },
          'includes': [ '../build/apk_test.gypi' ],
        }],
    }],
  ],
}
