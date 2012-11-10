#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import optparse
import os
import sys
import tarfile

import buildbot_common

SDK_BUILD_DIR = buildbot_common.SCRIPT_DIR
MONO_BUILD_DIR = os.path.join(SDK_BUILD_DIR, 'mono_build')
MONO_DIR = os.path.join(MONO_BUILD_DIR, 'nacl-mono')


def main(args):
  parser = optparse.OptionParser()
  parser.add_option('--arch',
                    help='Target architecture',
                    dest='arch',
                    default='x86-32')
  parser.add_option('--sdk-revision',
                    help='SDK Revision'
                         ' (default=buildbot revision)',
                    dest='sdk_revision',
                    default=None)
  parser.add_option('--sdk-url',
                    help='SDK Download URL',
                    dest='sdk_url',
                    default=None)
  parser.add_option('--install-dir',
                    help='Install Directory',
                    dest='install_dir',
                    default='naclmono')
  (options, args) = parser.parse_args(args[1:])

  assert sys.platform.find('linux') != -1

  buildbot_revision = os.environ.get('BUILDBOT_REVISION', '')

  build_prefix = options.arch + ' '

  buildbot_common.BuildStep(build_prefix + 'Clean Old SDK')
  buildbot_common.MakeDir(MONO_BUILD_DIR)
  buildbot_common.RemoveDir(os.path.join(MONO_BUILD_DIR, 'pepper_*'))

  buildbot_common.BuildStep(build_prefix + 'Setup New SDK')
  sdk_dir = None
  sdk_revision = options.sdk_revision
  sdk_url = options.sdk_url
  if not sdk_url:
    if not sdk_revision:
      assert buildbot_revision
      sdk_revision = buildbot_revision.split(':')[0]
    sdk_url = 'gs://nativeclient-mirror/nacl/nacl_sdk/'\
              'trunk.%s/naclsdk_linux.bz2' % sdk_revision

  sdk_url = sdk_url.replace('https://commondatastorage.googleapis.com/',
                            'gs://')

  sdk_file = sdk_url.split('/')[-1]

  buildbot_common.Run([buildbot_common.GetGsutil(), 'cp', sdk_url, sdk_file],
                      cwd=MONO_BUILD_DIR)
  tar_file = None
  try:
    tar_file = tarfile.open(os.path.join(MONO_BUILD_DIR, sdk_file))
    pepper_dir = os.path.commonprefix(tar_file.getnames())
    tar_file.extractall(path=MONO_BUILD_DIR)
    sdk_dir = os.path.join(MONO_BUILD_DIR, pepper_dir)
  finally:
    if tar_file:
      tar_file.close()

  assert sdk_dir

  buildbot_common.BuildStep(build_prefix + 'Checkout Mono')
  # TODO(elijahtaylor): Get git URL from master/trigger to make this
  # more flexible for building from upstream and release branches.
  git_url = 'git://github.com/elijahtaylor/mono.git'
  git_rev = 'HEAD'
  if buildbot_revision:
    git_rev = buildbot_revision.split(':')[1]
  if not os.path.exists(MONO_DIR):
    buildbot_common.MakeDir(MONO_DIR)
    buildbot_common.Run(['git', 'clone', git_url, MONO_DIR])
  else:
    buildbot_common.Run(['git', 'fetch'], cwd=MONO_DIR)
  if git_rev:
    buildbot_common.Run(['git', 'checkout', git_rev], cwd=MONO_DIR)

  arch_to_bitsize = {'x86-32': '32',
                     'x86-64': '64'}
  arch_to_output_folder = {'x86-32': 'runtime-build',
                           'x86-64': 'runtime64-build'}

  buildbot_common.BuildStep(build_prefix + 'Configure Mono')
  os.environ['NACL_SDK_ROOT'] = sdk_dir
  os.environ['TARGET_BITSIZE'] = arch_to_bitsize[options.arch]
  buildbot_common.Run(['./autogen.sh'], cwd=MONO_DIR)
  buildbot_common.Run(['make', 'distclean'], cwd=MONO_DIR)

  buildbot_common.BuildStep(build_prefix + 'Build and Install Mono')
  nacl_interp_script = os.path.join(SDK_BUILD_DIR, 'nacl_interp_loader_mono.sh')
  os.environ['NACL_INTERP_LOADER'] = nacl_interp_script
  buildbot_common.Run(['./nacl-mono-runtime.sh',
                      MONO_DIR, # Mono directory with 'configure'
                      arch_to_output_folder[options.arch], # Build dir
                      options.install_dir],
                      cwd=SDK_BUILD_DIR)

  buildbot_common.BuildStep(build_prefix + 'Test Mono')
  buildbot_common.Run(['make', 'check', '-j8'],
      cwd=os.path.join(SDK_BUILD_DIR, arch_to_output_folder[options.arch]))

  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv))
