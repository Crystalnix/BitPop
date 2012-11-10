# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    # We have two principal targets: sandbox and sandbox_linux_unittests
    # All other targets are listed as dependencies.
    # FIXME(jln): for historial reasons, sandbox_linux is the setuid sandbox
    # and is its own target.
    {
      'target_name': 'sandbox',
      'type': 'none',
      'dependencies': [
        'suid_sandbox_client',
      ],
      'conditions': [
        # Only compile in the seccomp mode 1 code for the flag combination
        # where we support it.
        [ 'OS=="linux" and (target_arch=="ia32" or target_arch=="x64") '
          'and toolkit_views==0 and selinux==0', {
          'dependencies': [
            'linux/seccomp-legacy/seccomp.gyp:seccomp_sandbox',
          ],
        }],
        # Similarly, compile seccomp BPF when we support it
        [ 'OS=="linux" and (target_arch=="ia32" or target_arch=="x64")', {
          'type': 'static_library',
          'dependencies': [
            'seccomp_bpf',
          ],
        }],
      ],
    },
    {
      'target_name': 'sandbox_linux_unittests',
      'type': 'executable',
      'dependencies': [
        'sandbox',
        '../testing/gtest.gyp:gtest',
      ],
      'sources': [
        'tests/unit_tests.cc',
        'suid/client/setuid_sandbox_client_unittest.cc',
      ],
      'include_dirs': [
        '../..',
      ],
      'conditions': [
        [ 'OS=="linux" and (target_arch=="ia32" or target_arch=="x64")', {
          'sources': [
            'seccomp-bpf/sandbox_bpf_unittest.cc',
          ],
        }],
      ],
    },
    {
      'target_name': 'seccomp_bpf',
      'type': 'static_library',
      'sources': [
        'seccomp-bpf/sandbox_bpf.cc',
        'seccomp-bpf/sandbox_bpf.h',
        'seccomp-bpf/verifier.cc',
        'seccomp-bpf/verifier.h',
      ],
      'dependencies': [
        '../base/base.gyp:base',
      ],
      'include_dirs': [
        '../..',
      ],
    },
    {
      # The setuid sandbox, for Linux
      'target_name': 'chrome_sandbox',
      'type': 'executable',
      'sources': [
        'suid/common/sandbox.h',
        'suid/common/suid_unsafe_environment_variables.h',
        'suid/linux_util.c',
        'suid/linux_util.h',
        'suid/process_util.h',
        'suid/process_util_linux.c',
        'suid/sandbox.c',
      ],
      'cflags': [
        # For ULLONG_MAX
        '-std=gnu99',
      ],
      'include_dirs': [
        '../..',
      ],
    },
    {
      'target_name': 'libc_urandom_override',
      'type': 'static_library',
      'sources': [
        'services/libc_urandom_override.cc',
        'services/libc_urandom_override.h',
      ],
      'dependencies': [
        '../base/base.gyp:base',
      ],
      'include_dirs': [
        '..',
      ],
    },
    {
      'target_name': 'suid_sandbox_client',
      'type': 'static_library',
      'sources': [
        'suid/common/sandbox.h',
        'suid/common/suid_unsafe_environment_variables.h',
        'suid/client/setuid_sandbox_client.cc',
        'suid/client/setuid_sandbox_client.h',
      ],
      'dependencies': [
        '../base/base.gyp:base',
      ],
      'include_dirs': [
        '..',
      ],
    },

  ],
}
