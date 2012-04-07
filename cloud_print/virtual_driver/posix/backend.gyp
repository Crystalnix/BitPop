# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'target_defaults': {
    'variables': { 'chromium_code':1 },
  },
  'targets': [
    {
      'target_name': 'GCP-driver',
      'type': 'executable',
      'dependencies': [
        '../../../base/base.gyp:base',
      ],
      'sources': [
        'printer_driver_util_linux.cc',
        'printer_driver_util_posix.cc',
        'printer_driver_util_posix.h',
        'printer_driver_util_mac.mm',
        'virtual_driver_posix.cc',
        '../virtual_driver_switches.cc',
        '../virtual_driver_switches.h',
      ],
      'conditions': [
        ['OS=="mac"', {
          'sources!': ['../virtual_driver_switches.cc'],
          'libraries': ['ScriptingBridge.framework'],
        }],
     ],
    },
    {
      'target_name': 'virtual_driver_posix_unittests',
      'type': 'executable',
      'dependencies': [
        '../../../base/base.gyp:base',
        '../../../base/base.gyp:test_support_base',
        '../../../testing/gmock.gyp:gmock',
        '../../../testing/gtest.gyp:gtest',
      ],
      'sources': [
        'virtual_driver_posix_tests.cc',
        'printer_driver_util_posix.cc',
      ],
  }],
  'conditions': [
     ['OS=="mac"', {
      'targets' : [
        {
          'target_name': 'GCP-install',
          'type': 'executable',
          'dependencies': [
            '../../../base/base.gyp:base',
          ],
          'sources' : [
            'install_cloud_print_driver_mac.mm',
            'installer_util_mac.h',
            'installer_util_mac.mm'
          ],
        },
       {
          'target_name': 'GCP-uninstall',
          'type': 'executable',
          'dependencies': [
            '../../../base/base.gyp:base',
          ],
          'sources' : [
            'installer_util_mac.h',
            'installer_util_mac.mm',
            'uninstall_cloud_print_driver_mac.mm',
          ],
        },
      ],
      }],
    ],
}
