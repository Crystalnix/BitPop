# Copyright (c) 2010 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      'target_name': 'appcache',
      'type': '<(library)',
      'msvs_guid': '0B945915-31A7-4A07-A5B5-568D737A39B1',
      'dependencies': [
        '<(DEPTH)/app/app.gyp:app_base',
        '<(DEPTH)/net/net.gyp:net',
      ],
      'sources': [
        # This list contains all .h and .cc in appcache except for test code.
        'appcache.cc',
        'appcache.h',
        'appcache_backend_impl.cc',
        'appcache_backend_impl.h',
        'appcache_database.cc',
        'appcache_database.h',
        'appcache_disk_cache.cc',
        'appcache_disk_cache.h',
        'appcache_entry.h',
        'appcache_frontend_impl.cc',
        'appcache_frontend_impl.h',
        'appcache_group.cc',
        'appcache_group.h',
        'appcache_histograms.cc',
        'appcache_histograms.h',
        'appcache_host.cc',
        'appcache_host.h',
        'appcache_interceptor.cc',
        'appcache_interceptor.h',
        'appcache_interfaces.cc',
        'appcache_interfaces.h',
        'appcache_policy.h',
        'appcache_request_handler.cc',
        'appcache_request_handler.h',
        'appcache_response.cc',
        'appcache_response.h',
        'appcache_service.cc',
        'appcache_service.h',
        'appcache_storage.cc',
        'appcache_storage.h',
        'appcache_storage_impl.cc',
        'appcache_storage_impl.h',
        'appcache_thread.cc',
        'appcache_thread.h',
        'appcache_working_set.cc',
        'appcache_working_set.h',
        'appcache_update_job.cc',
        'appcache_update_job.h',
        'appcache_url_request_job.cc',
        'appcache_url_request_job.h',
        'manifest_parser.cc',
        'manifest_parser.h',
        'view_appcache_internals_job.h',
        'view_appcache_internals_job.cc',
        'web_application_cache_host_impl.cc',
        'web_application_cache_host_impl.h',
        'webkit_appcache.gypi',
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
