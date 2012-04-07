# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      'target_name': 'database',
      'type': 'static_library',
      'variables': { 'enable_wexit_time_destructors': 1, },
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/sql/sql.gyp:sql',
        '<(DEPTH)/third_party/sqlite/sqlite.gyp:sqlite',
        '<(DEPTH)/webkit/support/webkit_support.gyp:quota',
      ],
      'sources': [
        'databases_table.cc',
        'databases_table.h',
        'database_connections.cc',
        'database_connections.h',
        'database_quota_client.cc',
        'database_quota_client.h',
        'database_tracker.cc',
        'database_tracker.h',
        'database_util.cc',
        'database_util.h',
        'quota_table.cc',
        'quota_table.h',
        'vfs_backend.cc',
        'vfs_backend.h',
      ],
      'conditions': [
        ['inside_chromium_build==0', {
          'dependencies': [
            '<(DEPTH)/webkit/support/setup_third_party.gyp:third_party_headers',
          ],
        }],
      ],
    },
  ],
}
