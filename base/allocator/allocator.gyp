# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'jemalloc_dir': '../../third_party/jemalloc/chromium',
    'tcmalloc_dir': '../../third_party/tcmalloc/chromium',
  },
  'targets': [
    {
      'target_name': 'allocator',
      'type': 'static_library',
      'msvs_guid': 'C564F145-9172-42C3-BFCB-60FDEA124321',
      'include_dirs': [
        '.',
        '<(tcmalloc_dir)/src/base',
        '<(tcmalloc_dir)/src',
        '../..',
      ],
      'direct_dependent_settings': {
        'configurations': {
          'Common_Base': {
            'msvs_settings': {
              'VCLinkerTool': {
                'IgnoreDefaultLibraryNames': ['libcmtd.lib', 'libcmt.lib'],
                'AdditionalDependencies': [
                  '<(SHARED_INTERMEDIATE_DIR)/allocator/libcmt.lib'
                ],
              },
            },
          },
        },
        'conditions': [
          ['OS=="win"', {
            'defines': [
              ['PERFTOOLS_DLL_DECL', '']
            ],
          }],
        ],
      },
      'sources': [
        # Generated for our configuration from tcmalloc's build
        # and checked in.
        '<(tcmalloc_dir)/src/config.h',
        '<(tcmalloc_dir)/src/config_linux.h',
        '<(tcmalloc_dir)/src/config_win.h',

        # all tcmalloc native and forked files
        '<(tcmalloc_dir)/src/addressmap-inl.h',
        '<(tcmalloc_dir)/src/base/atomicops-internals-linuxppc.h',
        '<(tcmalloc_dir)/src/base/atomicops-internals-macosx.h',
        '<(tcmalloc_dir)/src/base/atomicops-internals-x86-msvc.h',
        '<(tcmalloc_dir)/src/base/atomicops-internals-x86.cc',
        '<(tcmalloc_dir)/src/base/atomicops-internals-x86.h',
        '<(tcmalloc_dir)/src/base/atomicops.h',
        '<(tcmalloc_dir)/src/base/basictypes.h',
        '<(tcmalloc_dir)/src/base/commandlineflags.h',
        '<(tcmalloc_dir)/src/base/cycleclock.h',
        # We don't list dynamic_annotations.c since its copy is already
        # present in the dynamic_annotations target.
        '<(tcmalloc_dir)/src/base/dynamic_annotations.h',
        '<(tcmalloc_dir)/src/base/elfcore.h',
        '<(tcmalloc_dir)/src/base/googleinit.h',
        '<(tcmalloc_dir)/src/base/linux_syscall_support.h',
        '<(tcmalloc_dir)/src/base/linuxthreads.cc',
        '<(tcmalloc_dir)/src/base/linuxthreads.h',
        '<(tcmalloc_dir)/src/base/logging.cc',
        '<(tcmalloc_dir)/src/base/logging.h',
        '<(tcmalloc_dir)/src/base/low_level_alloc.cc',
        '<(tcmalloc_dir)/src/base/low_level_alloc.h',
        '<(tcmalloc_dir)/src/base/simple_mutex.h',
        '<(tcmalloc_dir)/src/base/spinlock.cc',
        '<(tcmalloc_dir)/src/base/spinlock.h',
        '<(tcmalloc_dir)/src/base/spinlock_linux-inl.h',
        '<(tcmalloc_dir)/src/base/spinlock_posix-inl.h',
        '<(tcmalloc_dir)/src/base/spinlock_win32-inl.h',
        '<(tcmalloc_dir)/src/base/stl_allocator.h',
        '<(tcmalloc_dir)/src/base/sysinfo.cc',
        '<(tcmalloc_dir)/src/base/sysinfo.h',
        '<(tcmalloc_dir)/src/base/thread_annotations.h',
        '<(tcmalloc_dir)/src/base/thread_lister.c',
        '<(tcmalloc_dir)/src/base/thread_lister.h',
        '<(tcmalloc_dir)/src/base/vdso_support.cc',
        '<(tcmalloc_dir)/src/base/vdso_support.h',
        '<(tcmalloc_dir)/src/central_freelist.cc',
        '<(tcmalloc_dir)/src/central_freelist.h',
        '<(tcmalloc_dir)/src/common.cc',
        '<(tcmalloc_dir)/src/common.h',
        '<(tcmalloc_dir)/src/debugallocation.cc',
        '<(tcmalloc_dir)/src/getpc.h',
        '<(tcmalloc_dir)/src/google/heap-checker.h',
        '<(tcmalloc_dir)/src/google/heap-profiler.h',
        '<(tcmalloc_dir)/src/google/malloc_extension.h',
        '<(tcmalloc_dir)/src/google/malloc_extension_c.h',
        '<(tcmalloc_dir)/src/google/malloc_hook.h',
        '<(tcmalloc_dir)/src/google/malloc_hook_c.h',
        '<(tcmalloc_dir)/src/google/profiler.h',
        '<(tcmalloc_dir)/src/google/stacktrace.h',
        '<(tcmalloc_dir)/src/google/tcmalloc.h',
        '<(tcmalloc_dir)/src/heap-checker-bcad.cc',
        '<(tcmalloc_dir)/src/heap-checker.cc',
        '<(tcmalloc_dir)/src/heap-profile-table.cc',
        '<(tcmalloc_dir)/src/heap-profile-table.h',
        '<(tcmalloc_dir)/src/heap-profiler.cc',
        '<(tcmalloc_dir)/src/internal_logging.cc',
        '<(tcmalloc_dir)/src/internal_logging.h',
        '<(tcmalloc_dir)/src/linked_list.h',
        '<(tcmalloc_dir)/src/malloc_extension.cc',
        '<(tcmalloc_dir)/src/malloc_hook-inl.h',
        '<(tcmalloc_dir)/src/malloc_hook.cc',
        '<(tcmalloc_dir)/src/maybe_threads.cc',
        '<(tcmalloc_dir)/src/maybe_threads.h',
        '<(tcmalloc_dir)/src/memfs_malloc.cc',
        '<(tcmalloc_dir)/src/memory_region_map.cc',
        '<(tcmalloc_dir)/src/memory_region_map.h',
        '<(tcmalloc_dir)/src/packed-cache-inl.h',
        '<(tcmalloc_dir)/src/page_heap.cc',
        '<(tcmalloc_dir)/src/page_heap.h',
        '<(tcmalloc_dir)/src/page_heap_allocator.h',
        '<(tcmalloc_dir)/src/pagemap.h',
        '<(tcmalloc_dir)/src/profile-handler.cc',
        '<(tcmalloc_dir)/src/profile-handler.h',
        '<(tcmalloc_dir)/src/profiledata.cc',
        '<(tcmalloc_dir)/src/profiledata.h',
        '<(tcmalloc_dir)/src/profiler.cc',
        '<(tcmalloc_dir)/src/raw_printer.cc',
        '<(tcmalloc_dir)/src/raw_printer.h',
        '<(tcmalloc_dir)/src/sampler.cc',
        '<(tcmalloc_dir)/src/sampler.h',
        '<(tcmalloc_dir)/src/span.cc',
        '<(tcmalloc_dir)/src/span.h',
        '<(tcmalloc_dir)/src/stack_trace_table.cc',
        '<(tcmalloc_dir)/src/stack_trace_table.h',
        '<(tcmalloc_dir)/src/stacktrace.cc',
        '<(tcmalloc_dir)/src/stacktrace_config.h',
        '<(tcmalloc_dir)/src/stacktrace_generic-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_libunwind-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_powerpc-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_win32-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_with_context.cc',
        '<(tcmalloc_dir)/src/stacktrace_x86-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_x86_64-inl.h',
        '<(tcmalloc_dir)/src/static_vars.cc',
        '<(tcmalloc_dir)/src/static_vars.h',
        '<(tcmalloc_dir)/src/symbolize.cc',
        '<(tcmalloc_dir)/src/symbolize.h',
        '<(tcmalloc_dir)/src/system-alloc.cc',
        '<(tcmalloc_dir)/src/system-alloc.h',
        '<(tcmalloc_dir)/src/tcmalloc.cc',
        '<(tcmalloc_dir)/src/tcmalloc_guard.h',
        '<(tcmalloc_dir)/src/thread_cache.cc',
        '<(tcmalloc_dir)/src/thread_cache.h',
        '<(tcmalloc_dir)/src/windows/config.h',
        '<(tcmalloc_dir)/src/windows/get_mangled_names.cc',
        '<(tcmalloc_dir)/src/windows/google/tcmalloc.h',
        '<(tcmalloc_dir)/src/windows/ia32_modrm_map.cc',
        '<(tcmalloc_dir)/src/windows/ia32_opcode_map.cc',
        '<(tcmalloc_dir)/src/windows/mingw.h',
        '<(tcmalloc_dir)/src/windows/mini_disassembler.cc',
        '<(tcmalloc_dir)/src/windows/mini_disassembler.h',
        '<(tcmalloc_dir)/src/windows/mini_disassembler_types.h',
        '<(tcmalloc_dir)/src/windows/override_functions.cc',
        '<(tcmalloc_dir)/src/windows/patch_functions.cc',
        '<(tcmalloc_dir)/src/windows/port.cc',
        '<(tcmalloc_dir)/src/windows/port.h',
        '<(tcmalloc_dir)/src/windows/preamble_patcher.cc',
        '<(tcmalloc_dir)/src/windows/preamble_patcher.h',
        '<(tcmalloc_dir)/src/windows/preamble_patcher_with_stub.cc',

        # jemalloc files
        '<(jemalloc_dir)/jemalloc.c',
        '<(jemalloc_dir)/jemalloc.h',
        '<(jemalloc_dir)/ql.h',
        '<(jemalloc_dir)/qr.h',
        '<(jemalloc_dir)/rb.h',

        'allocator_shim.cc',
        'allocator_shim.h',
        'generic_allocators.cc',
        'win_allocator.cc',        
      ],
      # sources! means that these are not compiled directly.
      'sources!': [
        # Included by allocator_shim.cc for maximal inlining.
        'generic_allocators.cc',
        'win_allocator.cc',

        # We simply don't use these, but list them above so that IDE
        # users can view the full available source for reference, etc.
        '<(tcmalloc_dir)/src/addressmap-inl.h',
        '<(tcmalloc_dir)/src/base/atomicops-internals-linuxppc.h',
        '<(tcmalloc_dir)/src/base/atomicops-internals-macosx.h',
        '<(tcmalloc_dir)/src/base/atomicops-internals-x86-msvc.h',
        '<(tcmalloc_dir)/src/base/atomicops-internals-x86.cc',
        '<(tcmalloc_dir)/src/base/atomicops-internals-x86.h',
        '<(tcmalloc_dir)/src/base/atomicops.h',
        '<(tcmalloc_dir)/src/base/basictypes.h',
        '<(tcmalloc_dir)/src/base/commandlineflags.h',
        '<(tcmalloc_dir)/src/base/cycleclock.h',
        '<(tcmalloc_dir)/src/base/elfcore.h',
        '<(tcmalloc_dir)/src/base/googleinit.h',
        '<(tcmalloc_dir)/src/base/linux_syscall_support.h',
        '<(tcmalloc_dir)/src/base/simple_mutex.h',
        '<(tcmalloc_dir)/src/base/spinlock_linux-inl.h',
        '<(tcmalloc_dir)/src/base/spinlock_posix-inl.h',
        '<(tcmalloc_dir)/src/base/spinlock_win32-inl.h',
        '<(tcmalloc_dir)/src/base/stl_allocator.h',
        '<(tcmalloc_dir)/src/base/thread_annotations.h',
        '<(tcmalloc_dir)/src/getpc.h',
        '<(tcmalloc_dir)/src/google/heap-checker.h',
        '<(tcmalloc_dir)/src/google/heap-profiler.h',
        '<(tcmalloc_dir)/src/google/malloc_extension_c.h',
        '<(tcmalloc_dir)/src/google/malloc_hook.h',
        '<(tcmalloc_dir)/src/google/malloc_hook_c.h',
        '<(tcmalloc_dir)/src/google/profiler.h',
        '<(tcmalloc_dir)/src/google/stacktrace.h',
        '<(tcmalloc_dir)/src/memfs_malloc.cc',
        '<(tcmalloc_dir)/src/packed-cache-inl.h',
        '<(tcmalloc_dir)/src/page_heap_allocator.h',
        '<(tcmalloc_dir)/src/pagemap.h',
        '<(tcmalloc_dir)/src/stacktrace_config.h',
        '<(tcmalloc_dir)/src/stacktrace_generic-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_libunwind-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_powerpc-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_win32-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_with_context.cc',
        '<(tcmalloc_dir)/src/stacktrace_x86-inl.h',
        '<(tcmalloc_dir)/src/stacktrace_x86_64-inl.h',
        '<(tcmalloc_dir)/src/tcmalloc_guard.h',
        '<(tcmalloc_dir)/src/windows/config.h',
        '<(tcmalloc_dir)/src/windows/google/tcmalloc.h',
        '<(tcmalloc_dir)/src/windows/get_mangled_names.cc',
        '<(tcmalloc_dir)/src/windows/ia32_modrm_map.cc',
        '<(tcmalloc_dir)/src/windows/ia32_opcode_map.cc',
        '<(tcmalloc_dir)/src/windows/mingw.h',
        '<(tcmalloc_dir)/src/windows/mini_disassembler.cc',
        '<(tcmalloc_dir)/src/windows/mini_disassembler.h',
        '<(tcmalloc_dir)/src/windows/mini_disassembler_types.h',
        '<(tcmalloc_dir)/src/windows/override_functions.cc',
        '<(tcmalloc_dir)/src/windows/patch_functions.cc',
        '<(tcmalloc_dir)/src/windows/preamble_patcher.cc',
        '<(tcmalloc_dir)/src/windows/preamble_patcher.h',
        '<(tcmalloc_dir)/src/windows/preamble_patcher_with_stub.cc',
      ],
      'dependencies': [
        '../third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
      ],
      'msvs_settings': {
        # TODO(sgk):  merge this with build/common.gypi settings
        'VCLibrarianTool': {
          'AdditionalOptions': ['/ignore:4006,4221'],
          'AdditionalLibraryDirectories':
            ['<(DEPTH)/third_party/platformsdk_win7/files/Lib'],
        },
        'VCLinkerTool': {
          'AdditionalOptions': ['/ignore:4006'],
        },
      },
      'configurations': {
        'Debug_Base': {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'RuntimeLibrary': '0',
            },
          },
        },
      },
      'conditions': [
        ['OS=="win"', {
          'defines': [
            ['PERFTOOLS_DLL_DECL', '']
          ],
          'dependencies': [
            'libcmt',
          ],
          'include_dirs': [
            '<(tcmalloc_dir)/src/windows',
          ],
          'sources!': [
            '<(tcmalloc_dir)/src/base/linuxthreads.cc',
            '<(tcmalloc_dir)/src/base/linuxthreads.h',
            '<(tcmalloc_dir)/src/base/vdso_support.cc',
            '<(tcmalloc_dir)/src/base/vdso_support.h',
            '<(tcmalloc_dir)/src/maybe_threads.cc',
            '<(tcmalloc_dir)/src/maybe_threads.h',
            '<(tcmalloc_dir)/src/symbolize.h',
            '<(tcmalloc_dir)/src/system-alloc.cc',
            '<(tcmalloc_dir)/src/system-alloc.h',

            # included by allocator_shim.cc
            '<(tcmalloc_dir)/src/tcmalloc.cc',

            # heap-profiler/checker/cpuprofiler
            '<(tcmalloc_dir)/src/base/thread_lister.c',
            '<(tcmalloc_dir)/src/base/thread_lister.h',
            '<(tcmalloc_dir)/src/heap-checker-bcad.cc',
            '<(tcmalloc_dir)/src/heap-checker.cc',
            '<(tcmalloc_dir)/src/heap-profiler.cc',
            '<(tcmalloc_dir)/src/memory_region_map.cc',
            '<(tcmalloc_dir)/src/memory_region_map.h',
            '<(tcmalloc_dir)/src/profiledata.cc',
            '<(tcmalloc_dir)/src/profiledata.h',
            '<(tcmalloc_dir)/src/profile-handler.cc',
            '<(tcmalloc_dir)/src/profile-handler.h',
            '<(tcmalloc_dir)/src/profiler.cc',

            # debugallocation
            '<(tcmalloc_dir)/src/debugallocation.cc',
          ],
        }],
        ['OS=="linux" or OS=="freebsd" or OS=="solaris"', {
          'sources!': [
            '<(tcmalloc_dir)/src/system-alloc.h',
            '<(tcmalloc_dir)/src/windows/port.cc',
            '<(tcmalloc_dir)/src/windows/port.h',

            # TODO(willchan): Support allocator shim later on.
            'allocator_shim.cc',

            # TODO(willchan): support jemalloc on other platforms
            # jemalloc files
            '<(jemalloc_dir)/jemalloc.c',
            '<(jemalloc_dir)/jemalloc.h',
            '<(jemalloc_dir)/ql.h',
            '<(jemalloc_dir)/qr.h',
            '<(jemalloc_dir)/rb.h',

          ],
          'cflags!': [
            '-fvisibility=hidden',
          ],
          'link_settings': {
            'ldflags': [
              # Don't let linker rip this symbol out, otherwise the heap&cpu
              # profilers will not initialize properly on startup.
              '-Wl,-uIsHeapProfilerRunning,-uProfilerStart',
              # Do the same for heap leak checker.
              '-Wl,-u_Z21InitialMallocHook_NewPKvj,-u_Z22InitialMallocHook_MMapPKvS0_jiiix,-u_Z22InitialMallocHook_SbrkPKvi',
              '-Wl,-u_Z21InitialMallocHook_NewPKvm,-u_Z22InitialMallocHook_MMapPKvS0_miiil,-u_Z22InitialMallocHook_SbrkPKvl',
              '-Wl,-u_ZN15HeapLeakChecker12IgnoreObjectEPKv,-u_ZN15HeapLeakChecker14UnIgnoreObjectEPKv',
          ]},
        }],
        [ 'linux_use_debugallocation==1', {
          'sources!': [
            # debugallocation.cc #includes tcmalloc.cc,
            # so only one of them should be used.
            '<(tcmalloc_dir)/src/tcmalloc.cc',
          ],
          'cflags': [
            '-DTCMALLOC_FOR_DEBUGALLOCATION',
          ],
        }, { # linux_use_debugallocation != 1
          'sources!': [
            '<(tcmalloc_dir)/src/debugallocation.cc',
          ],
        }],
        [ 'linux_keep_shadow_stacks==1', {
          'sources': [
            '<(tcmalloc_dir)/src/linux_shadow_stacks.cc',
            '<(tcmalloc_dir)/src/linux_shadow_stacks.h',
            '<(tcmalloc_dir)/src/stacktrace_shadow-inl.h',
          ],
          'cflags': [
            '-finstrument-functions',
            '-DKEEP_SHADOW_STACKS',
          ],
        }],
        [ 'linux_use_heapchecker==0', {
          # Do not compile and link the heapchecker source.
          'sources!': [
            '<(tcmalloc_dir)/src/heap-checker-bcad.cc',
            '<(tcmalloc_dir)/src/heap-checker.cc',
          ],
          # Disable the heap checker in tcmalloc.
          'cflags': [
            '-DNO_HEAP_CHECK',
          ],
        }],
      ],
    },
    {
      'target_name': 'allocator_unittests',
      'type': 'executable',
      'dependencies': [
        'allocator',
        '../../testing/gtest.gyp:gtest',
      ],
      'include_dirs': [
        '.',
        '<(tcmalloc_dir)/src/base',
        '<(tcmalloc_dir)/src',
        '../..',
      ],
      'msvs_guid': 'E99DA267-BE90-4F45-1294-6919DB2C9999',
      'sources': [
        'unittest_utils.cc',
        'allocator_unittests.cc',
      ],
    },
  ],
  'conditions': [
    ['OS=="win"', {
      'targets': [
        {
          'target_name': 'libcmt',
          'type': 'none',
          'actions': [
            {
              'action_name': 'libcmt',
              'inputs': [
                'prep_libc.sh',
              ],
              'outputs': [
                '<(SHARED_INTERMEDIATE_DIR)/allocator/libcmt.lib',
              ],
              'action': [
                './prep_libc.sh',
                '$(VCInstallDir)lib',
                '<(SHARED_INTERMEDIATE_DIR)/allocator',
              ],
            },
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
