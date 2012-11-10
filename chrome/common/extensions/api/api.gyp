# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      'target_name': 'api',
      'type': 'static_library',
      'sources': [
        '<@(idl_schema_files)',
        '<@(json_schema_files)',
      ],
      'includes': [
        '../../../../build/json_schema_bundle_compile.gypi',
        '../../../../build/json_schema_compile.gypi',
      ],
      'variables': {
        'chromium_code': 1,
        'json_schema_files': [
          'bookmarks.json',
          'content_settings.json',
          'cookies.json',
          'debugger.json',
          'events.json',
          'experimental_record.json',
          'file_browser_handler_internal.json',
          'i18n.json',
          'font_settings.json',
          'history.json',
          'page_capture.json',
          'permissions.json',
          'storage.json',
          'tabs.json',
          'web_navigation.json',
          'windows.json',
        ],
        'idl_schema_files': [
          'alarms.idl',
          'app_window.idl',
          'downloads.idl',
          'experimental_bluetooth.idl',
          'experimental_discovery.idl',
          'experimental_dns.idl',
          'experimental_identity.idl',
          'experimental_idltest.idl',
          'experimental_media_galleries.idl',
          'experimental_push_messaging.idl',
          'experimental_usb.idl',
          'file_system.idl',
          'serial.idl',
          'socket.idl',
        ],
        'cc_dir': 'chrome/common/extensions/api',
        'root_namespace': 'extensions::api',
      },
      'conditions': [
        ['OS=="android"', {
          'idl_schema_files!': [
            'experimental_usb.idl',
          ],
        }],
        ['OS!="chromeos"', {
          'json_schema_files!': [
            'file_browser_handler_internal.json',
          ],
        }],
      ],
    },
  ],
}
