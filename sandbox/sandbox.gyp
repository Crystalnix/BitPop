# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'target_defaults': {
    'variables': {
      'sandbox_windows_target': 0,
    },
    'target_conditions': [
      ['sandbox_windows_target==1', {
        # Files that are shared between the 32-bit and the 64-bit versions
        # of the Windows sandbox library.
        'sources': [
            'src/acl.cc',
            'src/acl.h',
            'src/broker_services.cc',
            'src/broker_services.h',
            'src/crosscall_client.h',
            'src/crosscall_params.h',
            'src/crosscall_server.cc',
            'src/crosscall_server.h',
            'src/dep.cc',
            'src/dep.h',
            'src/eat_resolver.cc',
            'src/eat_resolver.h',
            'src/filesystem_dispatcher.cc',
            'src/filesystem_dispatcher.h',
            'src/filesystem_interception.cc',
            'src/filesystem_interception.h',
            'src/filesystem_policy.cc',
            'src/filesystem_policy.h',
            'src/interception.cc',
            'src/interception.h',
            'src/interception_agent.cc',
            'src/interception_agent.h',
            'src/interception_internal.h',
            'src/interceptors.h',
            'src/internal_types.h',
            'src/ipc_tags.h',
            'src/job.cc',
            'src/job.h',
            'src/named_pipe_dispatcher.cc',
            'src/named_pipe_dispatcher.h',
            'src/named_pipe_interception.cc',
            'src/named_pipe_interception.h',
            'src/named_pipe_policy.cc',
            'src/named_pipe_policy.h',
            'src/nt_internals.h',
            'src/policy_broker.cc',
            'src/policy_broker.h',
            'src/policy_engine_opcodes.cc',
            'src/policy_engine_opcodes.h',
            'src/policy_engine_params.h',
            'src/policy_engine_processor.cc',
            'src/policy_engine_processor.h',
            'src/policy_low_level.cc',
            'src/policy_low_level.h',
            'src/policy_params.h',
            'src/policy_target.cc',
            'src/policy_target.h',
            'src/process_thread_dispatcher.cc',
            'src/process_thread_dispatcher.h',
            'src/process_thread_interception.cc',
            'src/process_thread_interception.h',
            'src/process_thread_policy.cc',
            'src/process_thread_policy.h',
            'src/registry_dispatcher.cc',
            'src/registry_dispatcher.h',
            'src/registry_interception.cc',
            'src/registry_interception.h',
            'src/registry_policy.cc',
            'src/registry_policy.h',
            'src/resolver.cc',
            'src/resolver.h',
            'src/restricted_token_utils.cc',
            'src/restricted_token_utils.h',
            'src/restricted_token.cc',
            'src/restricted_token.h',
            'src/sandbox_factory.h',
            'src/sandbox_nt_types.h',
            'src/sandbox_nt_util.cc',
            'src/sandbox_nt_util.h',
            'src/sandbox_policy_base.cc',
            'src/sandbox_policy_base.h',
            'src/sandbox_policy.h',
            'src/sandbox_types.h',
            'src/sandbox_utils.cc',
            'src/sandbox_utils.h',
            'src/sandbox.cc',
            'src/sandbox.h',
            'src/security_level.h',
            'src/service_resolver.cc',
            'src/service_resolver.h',
            'src/shared_handles.cc',
            'src/shared_handles.h',
            'src/sharedmem_ipc_client.cc',
            'src/sharedmem_ipc_client.h',
            'src/sharedmem_ipc_server.cc',
            'src/sharedmem_ipc_server.h',
            'src/sid.cc',
            'src/sid.h',
            'src/sync_dispatcher.cc',
            'src/sync_dispatcher.h',
            'src/sync_interception.cc',
            'src/sync_interception.h',
            'src/sync_policy.cc',
            'src/sync_policy.h',
            'src/target_interceptions.cc',
            'src/target_interceptions.h',
            'src/target_process.cc',
            'src/target_process.h',
            'src/target_services.cc',
            'src/target_services.h',
            'src/win_utils.cc',
            'src/win_utils.h',
            'src/win2k_threadpool.cc',
            'src/win2k_threadpool.h',
            'src/window.cc',
            'src/window.h',
        ],
      }],
    ],
  },
  'conditions': [
    [ 'os_posix == 1 and OS != "mac" and OS != "linux"', {
      # GYP requires that each file have at least one target defined.
      'targets': [
        {
          'target_name': 'sandbox',
          'type': 'settings',
        },
      ],
    }],
    [ 'OS=="linux" and selinux==0 and clang==0', {
      'targets': [
        {
          'target_name': 'chrome_sandbox',
          'type': 'executable',
          'sources': [
            'linux/suid/linux_util.c',
            'linux/suid/linux_util.h',
            'linux/suid/process_util.h',
            'linux/suid/process_util_linux.c',
            'linux/suid/sandbox.c',
          ],
          'cflags': [
            # For ULLONG_MAX
            '-std=gnu99',
          ],
          'include_dirs': [
            '..',
          ],
        },
        {
          'target_name': 'sandbox',
          'type': 'static_library',
          'conditions': [
            ['target_arch!="arm"', {
               'dependencies': [
                 '../seccompsandbox/seccomp.gyp:seccomp_sandbox',
               ]},
            ],
          ],
        },
      ],
    }],
    [ 'OS=="linux" and (selinux==1 or clang==1)', {
      # GYP requires that each file have at least one target defined.
      'targets': [
        {
          'target_name': 'sandbox',
          'type': 'settings',
        },
      ],
    }],
    [ 'OS=="win"', {
      'targets': [
        {
          'target_name': 'sandbox',
          'type': 'static_library',
          'variables': {
            'sandbox_windows_target': 1,
          },
          'dependencies': [
            '../testing/gtest.gyp:gtest',
            '../base/base.gyp:base',
            '../base/base.gyp:base_static',
          ],
          'export_dependent_settings': [
            '../base/base.gyp:base',
          ],
          'msvs_guid': '881F6A97-D539-4C48-B401-DF04385B2343',
          'sources': [
            # Files that are used by the 32-bit version of Windows sandbox only.
            'src/resolver_32.cc',
            'src/service_resolver_32.cc',
            'src/sidestep_resolver.cc',
            'src/sidestep_resolver.h',
            'src/sidestep\ia32_modrm_map.cpp',
            'src/sidestep\ia32_opcode_map.cpp',
            'src/sidestep\mini_disassembler_types.h',
            'src/sidestep\mini_disassembler.cpp',
            'src/sidestep\mini_disassembler.h',
            'src/sidestep\preamble_patcher_with_stub.cpp',
            'src/sidestep\preamble_patcher.h',
            'src/Wow64.cc',
            'src/Wow64.h',
          ],
          'include_dirs': [
            '..',
          ],
          'copies': [
            {
              'destination': '<(PRODUCT_DIR)',
              'files': [
                'wow_helper/wow_helper.exe',
                'wow_helper/wow_helper.pdb',
              ],
            },
          ],
          'direct_dependent_settings': {
            'include_dirs': [
              'src',
              '..',
            ],
          },
        },
        {
          'target_name': 'sandbox_win64',
          'type': 'static_library',
          'variables': {
            'sandbox_windows_target': 1,
          },
          'dependencies': [
            '../testing/gtest.gyp:gtest',
            '../base/base.gyp:base_nacl_win64',
          ],
          'configurations': {
            'Common_Base': {
              'msvs_target_platform': 'x64',
            },
          },
          'msvs_guid': 'BE3468E6-B314-4310-B449-6FC0C52EE155',
          'sources': [
            # Files that are used by the 64-bit version of Windows sandbox only.
            'src/interceptors_64.cc',
            'src/interceptors_64.h',
            'src/resolver_64.cc',
            'src/service_resolver_64.cc',
            'src/Wow64_64.cc',
          ],
          'include_dirs': [
            '..',
          ],
          'direct_dependent_settings': {
            'include_dirs': [
              'src',
              '..',
            ],
          },
          'defines': [
            '<@(nacl_win64_defines)',
          ]
        },
        {
          'target_name': 'sbox_integration_tests',
          'type': 'executable',
          'dependencies': [
            'sandbox',
            '../testing/gtest.gyp:gtest',
          ],
          'sources': [
            'tests/common/controller.cc',
            'tests/common/controller.h',
            'tests/common/test_utils.cc',
            'tests/common/test_utils.h',
            'tests/integration_tests/integration_tests.cc',
            'src/dep_test.cc',
            'src/file_policy_test.cc',
            'tests/integration_tests/integration_tests_test.cc',
            'src/integrity_level_test.cc',
            'src/ipc_ping_test.cc',
            'src/named_pipe_policy_test.cc',
            'src/policy_target_test.cc',
            'src/process_policy_test.cc',
            'src/registry_policy_test.cc',
            'src/sync_policy_test.cc',
            'src/unload_dll_test.cc',
          ],
        },
        {
          'target_name': 'sbox_validation_tests',
          'type': 'executable',
          'dependencies': [
            'sandbox',
            '../testing/gtest.gyp:gtest',
          ],
          'sources': [
            'tests/common/controller.cc',
            'tests/common/controller.h',
            'tests/validation_tests/unit_tests.cc',
            'tests/validation_tests/commands.cc',
            'tests/validation_tests/commands.h',
            'tests/validation_tests/suite.cc',
          ],
        },
        {
          'target_name': 'sbox_unittests',
          'type': 'executable',
          'dependencies': [
            'sandbox',
            '../testing/gtest.gyp:gtest',
          ],
          'sources': [
            'tests/common/test_utils.cc',
            'tests/common/test_utils.h',
            'tests/unit_tests/unit_tests.cc',
            'src/interception_unittest.cc',
            'src/service_resolver_unittest.cc',
            'src/restricted_token_unittest.cc',
            'src/job_unittest.cc',
            'src/sid_unittest.cc',
            'src/policy_engine_unittest.cc',
            'src/policy_low_level_unittest.cc',
            'src/policy_opcodes_unittest.cc',
            'src/ipc_unittest.cc',
            'src/threadpool_unittest.cc',
            'src/win_utils_unittest.cc',
          ],
        },
        {
          'target_name': 'sandbox_poc',
          'type': 'executable',
          'dependencies': [
            'sandbox',
            'pocdll',
          ],
          'sources': [
            'sandbox_poc/main_ui_window.cc',
            'sandbox_poc/main_ui_window.h',
            'sandbox_poc/resource.h',
            'sandbox_poc/sandbox.cc',
            'sandbox_poc/sandbox.h',
            'sandbox_poc/sandbox.ico',
            'sandbox_poc/sandbox.rc',
          ],
          'link_settings': {
            'libraries': [
              '-lcomctl32.lib',
            ],
          },
          'msvs_settings': {
            'VCLinkerTool': {
              'SubSystem': '2',         # Set /SUBSYSTEM:WINDOWS
            },
          },
        },
        {
          'target_name': 'pocdll',
          'type': 'shared_library',
          'sources': [
            'sandbox_poc/pocdll/exports.h',
            'sandbox_poc/pocdll/fs.cc',
            'sandbox_poc/pocdll/handles.cc',
            'sandbox_poc/pocdll/invasive.cc',
            'sandbox_poc/pocdll/network.cc',
            'sandbox_poc/pocdll/pocdll.cc',
            'sandbox_poc/pocdll/processes_and_threads.cc',
            'sandbox_poc/pocdll/registry.cc',
            'sandbox_poc/pocdll/spyware.cc',
            'sandbox_poc/pocdll/utils.h',
          ],
          'defines': [
            'POCDLL_EXPORTS',
          ],
          'include_dirs': [
            '..',
          ],
        },
      ],
    }],
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
