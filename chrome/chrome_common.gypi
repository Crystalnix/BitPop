# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'target_defaults': {
    'variables': {
      'chrome_common_target': 0,
    },
    'target_conditions': [
      ['chrome_common_target==1', {
        'include_dirs': [
          '..',
        ],
        'conditions': [
          ['OS=="win"', {
            'include_dirs': [
              '<(DEPTH)/third_party/wtl/include',
            ],
          }],
        ],
        'sources': [
          # .cc, .h, and .mm files under chrome/common that are used on all
          # platforms, including both 32-bit and 64-bit Windows.
          # Test files are not included.
          'common/about_handler.cc',
          'common/about_handler.h',
          'common/app_mode_common_mac.h',
          'common/app_mode_common_mac.mm',
          'common/attrition_experiments.h',
          'common/attributed_string_coder_mac.h',
          'common/attributed_string_coder_mac.mm',
          'common/auto_start_linux.cc',
          'common/auto_start_linux.h',
          'common/autofill_messages.h',
          'common/child_process_logging.h',
          'common/child_process_logging_linux.cc',
          'common/child_process_logging_mac.mm',
          'common/child_process_logging_win.cc',
          'common/chrome_version_info.cc',
          'common/chrome_version_info.h',
          'common/content_settings.cc',
          'common/content_settings.h',
          'common/content_settings_helper.cc',
          'common/content_settings_helper.h',
          'common/content_settings_types.h',
          'common/external_ipc_fuzzer.h',
          'common/external_ipc_fuzzer.cc',
          'common/guid.cc',
          'common/guid.h',
          'common/guid_posix.cc',
          'common/guid_win.cc',
          'common/icon_messages.h',
          'common/instant_types.h',
          'common/logging_chrome.cc',
          'common/logging_chrome.h',
          'common/metrics_helpers.cc',
          'common/metrics_helpers.h',
          'common/multi_process_lock.h',
          'common/multi_process_lock_linux.cc',
          'common/multi_process_lock_mac.cc',
          'common/multi_process_lock_win.cc',
          'common/nacl_cmd_line.cc',
          'common/nacl_cmd_line.h',
          'common/nacl_messages.cc',
          'common/nacl_messages.h',
          'common/nacl_types.h',
          'common/profiling.cc',
          'common/profiling.h',
          'common/ref_counted_util.h',
          'common/safe_browsing/safebrowsing_messages.h',
          'common/switch_utils.cc',
          'common/switch_utils.h',
          'common/time_format.cc',
          'common/time_format.h',
          'common/win_safe_util.cc',
          'common/win_safe_util.h',
        ],
      }],
    ],
  },
  'targets': [
    {
      'target_name': 'common',
      'type': 'static_library',
      'msvs_guid': '899F1280-3441-4D1F-BA04-CCD6208D9146',
      'variables': {
        'chrome_common_target': 1,
      },
      # TODO(gregoryd): This could be shared with the 64-bit target, but
      # it does not work due to a gyp issue.
      'direct_dependent_settings': {
        'include_dirs': [
          '..',
        ],
      },
      'dependencies': [
        # TODO(gregoryd): chrome_resources and chrome_strings could be
        #  shared with the 64-bit target, but it does not work due to a gyp
        # issue.
        'app/policy/cloud_policy_codegen.gyp:policy',
        'chrome_resources',
        'chrome_strings',
        'common_constants',
        'common_net',
        'default_plugin/default_plugin.gyp:default_plugin',
        'safe_browsing_csd_proto',
        'theme_resources',
        'theme_resources_standard',
        '../app/app.gyp:app_base',
        '../app/app.gyp:app_resources',
        '../base/base.gyp:base',
        '../base/base.gyp:base_i18n',
        '../base/base.gyp:base_static',
        '../build/temp_gyp/googleurl.gyp:googleurl',
        '../content/content.gyp:content_common',
        '../ipc/ipc.gyp:ipc',
        '../net/net.gyp:net',
        '../printing/printing.gyp:printing',
        '../skia/skia.gyp:skia',
        '../third_party/bzip2/bzip2.gyp:bzip2',
        '../third_party/icu/icu.gyp:icui18n',
        '../third_party/icu/icu.gyp:icuuc',
        '../third_party/libxml/libxml.gyp:libxml',
        '../third_party/sqlite/sqlite.gyp:sqlite',
        '../third_party/zlib/zlib.gyp:zlib',
        '../webkit/support/webkit_support.gyp:glue',
      ],
      'sources': [
        # .cc, .h, and .mm files under chrome/common that are not required for
        # building 64-bit Windows targets. Test files are not included.
        'common/automation_constants.cc',
        'common/automation_constants.h',
        'common/automation_messages.cc',
        'common/automation_messages.h',
        'common/automation_messages_internal.h',
        'common/badge_util.cc',
        'common/badge_util.h',
        'common/bzip2_error_handler.cc',
        'common/chrome_content_client.cc',
        'common/chrome_content_client.h',
        'common/chrome_content_plugin_client.cc',
        'common/chrome_content_plugin_client.h',
        'common/cloud_print/cloud_print_proxy_info.cc',
        'common/cloud_print/cloud_print_proxy_info.h',
        'common/common_api.h',
        'common/common_glue.cc',
        'common/common_message_generator.cc',
        'common/common_message_generator.h',
        'common/common_param_traits.cc',
        'common/common_param_traits.h',
        'common/default_plugin.cc',
        'common/default_plugin.h',
        'common/deprecated/event_sys-inl.h',
        'common/deprecated/event_sys.h',
        'common/extensions/extension.cc',
        'common/extensions/extension.h',
        'common/extensions/extension_action.cc',
        'common/extensions/extension_action.h',
        'common/extensions/extension_constants.cc',
        'common/extensions/extension_constants.h',
        'common/extensions/extension_error_utils.cc',
        'common/extensions/extension_error_utils.h',
        'common/extensions/extension_file_util.cc',
        'common/extensions/extension_file_util.h',
        'common/extensions/extension_icon_set.cc',
        'common/extensions/extension_icon_set.h',
        'common/extensions/extension_l10n_util.cc',
        'common/extensions/extension_l10n_util.h',
        'common/extensions/extension_localization_peer.cc',
        'common/extensions/extension_localization_peer.h',
        'common/extensions/extension_message_bundle.cc',
        'common/extensions/extension_message_bundle.h',
        'common/extensions/extension_messages.cc',
        'common/extensions/extension_messages.h',
        'common/extensions/extension_resource.cc',
        'common/extensions/extension_resource.h',
        'common/extensions/extension_set.cc',
        'common/extensions/extension_set.h',
        'common/extensions/extension_sidebar_defaults.h',
        'common/extensions/extension_sidebar_utils.cc',
        'common/extensions/extension_sidebar_utils.h',
        'common/extensions/extension_unpacker.cc',
        'common/extensions/extension_unpacker.h',
        'common/extensions/file_browser_handler.cc',
        'common/extensions/file_browser_handler.h',
        'common/extensions/update_manifest.cc',
        'common/extensions/update_manifest.h',
        'common/extensions/url_pattern.cc',
        'common/extensions/url_pattern.h',
        'common/extensions/url_pattern_set.cc',
        'common/extensions/url_pattern_set.h',
        'common/extensions/user_script.cc',
        'common/extensions/user_script.h',
        'common/favicon_url.cc',
        'common/favicon_url.h',
        'common/important_file_writer.cc',
        'common/important_file_writer.h',
        'common/json_pref_store.cc',
        'common/json_pref_store.h',
        'common/json_schema_validator.cc',
        'common/json_schema_validator.h',
        'common/jstemplate_builder.cc',
        'common/jstemplate_builder.h',
        'common/launchd_mac.h',
        'common/launchd_mac.mm',
        'common/libxml_utils.cc',
        'common/libxml_utils.h',
        'common/native_window_notification_source.h',
        'common/persistent_pref_store.h',
        'common/pref_store.cc',
        'common/pref_store.h',
        'common/print_messages.h',
        'common/random.cc',
        'common/random.h',
        'common/render_messages.cc',
        'common/render_messages.h',
        '<(protoc_out_dir)/chrome/common/safe_browsing/csd.pb.cc',
        '<(protoc_out_dir)/chrome/common/safe_browsing/csd.pb.h',
        'common/search_provider.h',
        'common/service_messages.h',
        'common/service_process_util.cc',
        'common/service_process_util.h',
        'common/service_process_util_linux.cc',
        'common/service_process_util_mac.mm',
        'common/service_process_util_posix.cc',
        'common/service_process_util_posix.h',
        'common/service_process_util_win.cc',
        'common/spellcheck_common.cc',
        'common/spellcheck_common.h',
        'common/spellcheck_messages.h',
        'common/sqlite_utils.cc',
        'common/sqlite_utils.h',
        'common/text_input_client_messages.cc',
        'common/text_input_client_messages.h',
        'common/thumbnail_score.cc',
        'common/thumbnail_score.h',
        'common/url_constants.cc',
        'common/url_constants.h',
        'common/utility_messages.h',
        'common/visitedlink_common.cc',
        'common/visitedlink_common.h',
        'common/web_apps.cc',
        'common/web_apps.h',
        'common/web_resource/web_resource_unpacker.cc',
        'common/web_resource/web_resource_unpacker.h',
        'common/worker_thread_ticker.cc',
        'common/worker_thread_ticker.h',
        'common/zip.cc',  # Requires zlib directly.
        'common/zip.h',
      ],
      'conditions': [
        ['toolkit_uses_gtk == 1', {
          'dependencies': [
            '../build/linux/system.gyp:gtk',
          ],
          'export_dependent_settings': [
            '../third_party/sqlite/sqlite.gyp:sqlite',
          ],
          'link_settings': {
            'libraries': [
              '-lX11',
              '-lXrender',
              '-lXss',
              '-lXext',
            ],
          },
        },],
        ['os_posix == 1 and OS != "mac"', {
          'include_dirs': [
            '<(SHARED_INTERMEDIATE_DIR)',
          ],
          # Because posix_version generates a header, we must set the
          # hard_dependency flag.
          'hard_dependency': 1,
          'actions': [
            {
              'action_name': 'posix_version',
              'variables': {
                'lastchange_path':
                  '<(SHARED_INTERMEDIATE_DIR)/build/LASTCHANGE',
                'version_py_path': 'tools/build/version.py',
                'version_path': 'VERSION',
                'template_input_path': 'common/chrome_version_info_posix.h.version',
              },
              'conditions': [
                [ 'branding == "Chrome"', {
                  'variables': {
                     'branding_path':
                       'app/theme/google_chrome/BRANDING',
                  },
                }, { # else branding!="Chrome"
                  'variables': {
                     'branding_path':
                       'app/theme/chromium/BRANDING',
                  },
                }],
              ],
              'inputs': [
                '<(template_input_path)',
                '<(version_path)',
                '<(branding_path)',
                '<(lastchange_path)',
              ],
              'outputs': [
                '<(SHARED_INTERMEDIATE_DIR)/chrome/common/chrome_version_info_posix.h',
              ],
              'action': [
                'python',
                '<(version_py_path)',
                '-f', '<(version_path)',
                '-f', '<(branding_path)',
                '-f', '<(lastchange_path)',
                '<(template_input_path)',
                '<@(_outputs)',
              ],
              'message': 'Generating version information',
            },
          ],
        }],
        ['OS=="linux" and selinux==1', {
          'dependencies': [
            '../build/linux/system.gyp:selinux',
          ],
        }],
        ['OS=="mac"', {
          'include_dirs': [
            '../third_party/GTM',
          ],
        }],
        ['remoting==1', {
          'dependencies': [
            '../remoting/remoting.gyp:remoting_client_plugin',
          ],
        }],
      ],
      'export_dependent_settings': [
        '../app/app.gyp:app_base',
        '../base/base.gyp:base',
      ],
    },
    {
      'target_name': 'common_net',
      'type': 'static_library',
      'sources': [
        'common/net/http_return.h',
        'common/net/net_resource_provider.cc',
        'common/net/net_resource_provider.h',
        'common/net/predictor_common.h',
        'common/net/gaia/gaia_auth_consumer.cc',
        'common/net/gaia/gaia_auth_consumer.h',
        'common/net/gaia/gaia_auth_fetcher.cc',
        'common/net/gaia/gaia_auth_fetcher.h',
        'common/net/gaia/gaia_authenticator.cc',
        'common/net/gaia/gaia_authenticator.h',
        'common/net/gaia/gaia_oauth_client.cc',
        'common/net/gaia/gaia_oauth_client.h',
        'common/net/gaia/google_service_auth_error.cc',
        'common/net/gaia/google_service_auth_error.h',
        'common/net/x509_certificate_model.cc',
        'common/net/x509_certificate_model_nss.cc',
        'common/net/x509_certificate_model_openssl.cc',
        'common/net/x509_certificate_model.h',
      ],
      'dependencies': [
        'chrome_resources',
        'chrome_strings',
        '../app/app.gyp:app_base',
        '../base/base.gyp:base',
        '../gpu/gpu.gyp:gpu_ipc',
        '../net/net.gyp:net_resources',
        '../net/net.gyp:net',
        '../third_party/icu/icu.gyp:icui18n',
        '../third_party/icu/icu.gyp:icuuc',
      ],
      'conditions': [
        ['os_posix == 1 and OS != "mac"', {
            'dependencies': [
              '../build/linux/system.gyp:ssl',
            ],
          },
          {  # else: OS is not in the above list
            'sources!': [
              'common/net/x509_certificate_model_nss.cc',
              'common/net/x509_certificate_model_openssl.cc',
            ],
          },
        ],
        ['use_openssl==1', {
            'sources!': [
              'common/net/x509_certificate_model_nss.cc',
            ],
          },
          {  # else !use_openssl: remove the unneeded files
            'sources!': [
              'common/net/x509_certificate_model_openssl.cc',
            ],
          },
        ],
       ],
    },
    {
      # Protobuf compiler / generator for the safebrowsing client-side detection
      # (csd) request protocol buffer which is used both in the renderer and in
      # the browser.
      'target_name': 'safe_browsing_csd_proto',
      'type': 'none',
      'sources': [ 'common/safe_browsing/csd.proto' ],
      'rules': [
        {
          'rule_name': 'genproto',
          'extension': 'proto',
          'inputs': [
            '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)protoc<(EXECUTABLE_SUFFIX)',
          ],
          'variables': {
            # The protoc compiler requires a proto_path argument with the
            # directory containing the .proto file.
            # There's no generator variable that corresponds to this, so fake
            # it.
            'rule_input_relpath': 'common/safe_browsing',
          },
          'outputs': [
            '<(protoc_out_dir)/chrome/<(rule_input_relpath)/<(RULE_INPUT_ROOT).pb.h',
            '<(protoc_out_dir)/chrome/<(rule_input_relpath)/<(RULE_INPUT_ROOT).pb.cc',
          ],
          'action': [
            '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)protoc<(EXECUTABLE_SUFFIX)',
            '--proto_path=./<(rule_input_relpath)',
            './<(rule_input_relpath)/<(RULE_INPUT_ROOT)<(RULE_INPUT_EXT)',
            '--cpp_out=<(protoc_out_dir)/chrome/<(rule_input_relpath)',
          ],
          'message': 'Generating C++ code from <(RULE_INPUT_PATH)',
        },
      ],
      'dependencies': [
        '../third_party/protobuf/protobuf.gyp:protobuf_lite',
        '../third_party/protobuf/protobuf.gyp:protoc#host',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(protoc_out_dir)',
        ]
      },
      'export_dependent_settings': [
        '../third_party/protobuf/protobuf.gyp:protobuf_lite',
      ],
      'hard_dependency': 1,
    },
  ],
  'conditions': [
    ['OS=="win"', {
      'targets': [
        {
          'target_name': 'common_nacl_win64',
          'type': 'static_library',
          'msvs_guid': '3AB5C5E9-470C-419B-A0AE-C7381FB632FA',
          'variables': {
            'chrome_common_target': 1,
          },
          'dependencies': [
            # TODO(gregoryd): chrome_resources and chrome_strings could be
            #  shared with the 32-bit target, but it does not work due to a gyp
            # issue.
            'chrome_resources',
            'chrome_strings',
            'common_constants_win64',
            'app/policy/cloud_policy_codegen.gyp:policy_win64',
            '../app/app.gyp:app_base_nacl_win64',
            '../app/app.gyp:app_resources',
            '../base/base.gyp:base_nacl_win64',
            '../ipc/ipc.gyp:ipc_win64',
            '../third_party/libxml/libxml.gyp:libxml',
          ],
          'include_dirs': [
            '../third_party/icu/public/i18n',
            '../third_party/icu/public/common',
            # We usually get these skia directories by adding a dependency on
            # skia, bu we don't need it for NaCl's 64-bit Windows support. The
            # directories are required for resolving the includes in any case.
            '../third_party/skia/include/config',
            '../third_party/skia/include/core',
            '../skia/config',
            '../skia/config/win',
          ],
          'defines': [
            '<@(nacl_win64_defines)',
          ],
          'sources': [
            '../webkit/glue/webkit_glue_dummy.cc',
            'common/url_constants.cc',
            # TODO(bradnelson): once automatic generation of 64 bit targets on
            # Windows is ready, take this out and add a dependency on
            # content_common.gypi.
            '../content/common/file_system/file_system_dispatcher_dummy.cc',
            '../content/common/message_router.cc',
            '../content/common/quota_dispatcher_dummy.cc',
            '../content/common/resource_dispatcher_dummy.cc',
            '../content/common/socket_stream_dispatcher_dummy.cc',
          ],
          'export_dependent_settings': [
            'app/policy/cloud_policy_codegen.gyp:policy_win64',
          ],
          # TODO(gregoryd): This could be shared with the 32-bit target, but
          # it does not work due to a gyp issue.
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
  ],
}
