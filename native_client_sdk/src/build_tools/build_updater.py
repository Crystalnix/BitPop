#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build script to generate a new sdk_tools bundle.

This script packages the files necessary to generate the SDK updater -- the
tool users run to download new bundles, update existing bundles, etc.
"""

import buildbot_common
import build_utils
import cStringIO
import optparse
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SDK_SRC_DIR = os.path.dirname(SCRIPT_DIR)
SDK_DIR = os.path.dirname(SDK_SRC_DIR)
SRC_DIR = os.path.dirname(SDK_DIR)
NACL_DIR = os.path.join(SRC_DIR, 'native_client')
CYGTAR = os.path.join(NACL_DIR, 'build', 'cygtar.py')

sys.path.append(os.path.join(SDK_SRC_DIR, 'tools'))

import oshelpers


UPDATER_FILES = [
  # launch scripts
  ('build_tools/naclsdk', 'nacl_sdk/naclsdk'),
  ('build_tools/naclsdk.bat', 'nacl_sdk/naclsdk.bat'),

  # base manifest
  ('build_tools/json/naclsdk_manifest0.json',
      'nacl_sdk/sdk_cache/naclsdk_manifest2.json'),

  # SDK tools
  ('build_tools/sdk_tools/cacerts.txt', 'nacl_sdk/sdk_tools/cacerts.txt'),
  ('build_tools/sdk_tools/sdk_update.py', 'nacl_sdk/sdk_tools/sdk_update.py'),
  ('build_tools/sdk_tools/sdk_update_common.py',
      'nacl_sdk/sdk_tools/sdk_update_common.py'),
  ('build_tools/sdk_tools/sdk_update_main.py',
      'nacl_sdk/sdk_tools/sdk_update_main.py'),
  ('build_tools/manifest_util.py', 'nacl_sdk/sdk_tools/manifest_util.py'),
  ('build_tools/sdk_tools/third_party/__init__.py',
      'nacl_sdk/sdk_tools/third_party/__init__.py'),
  ('build_tools/sdk_tools/third_party/fancy_urllib/__init__.py',
      'nacl_sdk/sdk_tools/third_party/fancy_urllib/__init__.py'),
  ('build_tools/sdk_tools/third_party/fancy_urllib/README',
      'nacl_sdk/sdk_tools/third_party/fancy_urllib/README'),
  ('LICENSE', 'nacl_sdk/sdk_tools/LICENSE'),
  (CYGTAR, 'nacl_sdk/sdk_tools/cygtar.py'),
]


def MakeUpdaterFilesAbsolute(out_dir):
  """Return the result of changing all relative paths in UPDATER_FILES to
  absolute paths.

  Args:
    out_dir: The output directory.
  Returns:
    A list of 2-tuples. The first element in each tuple is the source path and
    the second is the destination path.
  """
  assert os.path.isabs(out_dir)

  result = []
  for in_file, out_file in UPDATER_FILES:
    if not os.path.isabs(in_file):
      in_file = os.path.join(SDK_SRC_DIR, in_file)
    out_file = os.path.join(out_dir, out_file)
    result.append((in_file, out_file))
  return result


def CopyFiles(files):
  """Given a list of 2-tuples (source, dest), copy each source file to a dest
  file.

  Args:
    files: A list of 2-tuples."""
  for in_file, out_file in files:
    buildbot_common.MakeDir(os.path.dirname(out_file))
    buildbot_common.CopyFile(in_file, out_file)


def UpdateRevisionNumber(out_dir, revision_number):
  """Update the sdk_tools bundle to have the given revision number.

  This function finds all occurrences of the string "{REVISION}" in
  sdk_update_main.py and replaces them with |revision_number|. The only
  observable effect of this change should be that running:

    naclsdk -v

  will contain the new |revision_number|.

  Args:
    out_dir: The output directory containing the scripts to update.
    revision_number: The revision number as an integer, or None to use the
        current Chrome revision (as retrieved through svn/git).
  """
  if revision_number is None:
    revision_number = build_utils.ChromeRevision()

  SDK_UPDATE_MAIN = os.path.join(out_dir,
      'nacl_sdk/sdk_tools/sdk_update_main.py')

  file = open(SDK_UPDATE_MAIN, 'r').read().replace(
      '{REVISION}', str(revision_number))
  open(SDK_UPDATE_MAIN, 'w').write(file)


def BuildUpdater(out_dir, revision_number=None):
  """Build naclsdk.zip and sdk_tools.tgz in |out_dir|.

  Args:
    out_dir: The output directory.
    revision_number: The revision number of this updater, as an integer. Or
        None, to use the current Chrome revision."""
  buildbot_common.BuildStep('Create Updater')

  out_dir = os.path.abspath(out_dir)

  # Build SDK directory
  buildbot_common.RemoveDir(os.path.join(out_dir, 'nacl_sdk'))

  updater_files = MakeUpdaterFilesAbsolute(out_dir)
  out_files = [out_file for in_file, out_file in updater_files]

  CopyFiles(updater_files)
  UpdateRevisionNumber(out_dir, revision_number)

  # Make zip
  buildbot_common.RemoveFile(os.path.join(out_dir, 'nacl_sdk.zip'))
  buildbot_common.Run([sys.executable, oshelpers.__file__, 'zip',
                      'nacl_sdk.zip'] + out_files,
                      cwd=out_dir)

  # Tar of all files under nacl_sdk/sdk_tools
  sdktoolsdir = os.path.join(out_dir, 'nacl_sdk/sdk_tools')
  tarname = os.path.join(out_dir, 'sdk_tools.tgz')
  files_to_tar = [os.path.relpath(out_file, sdktoolsdir)
      for out_file in out_files if out_file.startswith(sdktoolsdir)]
  buildbot_common.RemoveFile(tarname)
  buildbot_common.Run([sys.executable, CYGTAR, '-C',
      os.path.join(out_dir, sdktoolsdir), '-czf', tarname] + files_to_tar)
  sys.stdout.write('\n')


def main(args):
  parser = optparse.OptionParser()
  parser.add_option('-o', '--out', help='output directory',
      dest='out_dir', default='out')
  options, args = parser.parse_args(args[1:])

  BuildUpdater(options.out_dir)


if __name__ == '__main__':
  sys.exit(main(sys.argv))
