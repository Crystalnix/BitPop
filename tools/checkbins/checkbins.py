#!/usr/bin/python
# Copyright (c) 2010 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Makes sure that all EXE and DLL files in the provided directory were built
correctly.

Currently this tool will check that binaries were built with /NXCOMPAT and
/DYNAMICBASE set.
"""

import os
import optparse
import sys

# Find /third_party/pefile based on current directory and script path.
sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..',
                             'third_party', 'pefile'))
import pefile

PE_FILE_EXTENSIONS = ['.exe', '.dll']
DYNAMICBASE_FLAG = 0x0040
NXCOMPAT_FLAG = 0x0100

# Please do not add your file here without confirming that it indeed doesn't
# require /NXCOMPAT and /DYNAMICBASE.  Contact cpu@chromium.org or your local
# Windows guru for advice.
EXCLUDED_FILES = ['chrome_frame_mini_installer.exe',
                  'mini_installer.exe',
                  'wow_helper.exe']

def IsPEFile(path):
  return (os.path.isfile(path) and
          os.path.splitext(path)[1].lower() in PE_FILE_EXTENSIONS and
          os.path.basename(path) not in EXCLUDED_FILES)

def main(options, args):
  directory = args[0]
  pe_total = 0
  pe_passed = 0

  for file in os.listdir(directory):
    path = os.path.abspath(os.path.join(directory, file))
    if not IsPEFile(path):
      continue
    pe = pefile.PE(path, fast_load=True)
    pe_total = pe_total + 1
    success = True

    # Check for /DYNAMICBASE.
    if pe.OPTIONAL_HEADER.DllCharacteristics & DYNAMICBASE_FLAG:
      if options.verbose:
        print "Checking %s for /DYNAMICBASE... PASS" % path
    else:
      success = False
      print "Checking %s for /DYNAMICBASE... FAIL" % path

    # Check for /NXCOMPAT.
    if pe.OPTIONAL_HEADER.DllCharacteristics & NXCOMPAT_FLAG:
      if options.verbose:
        print "Checking %s for /NXCOMPAT... PASS" % path
    else:
      success = False
      print "Checking %s for /NXCOMPAT... FAIL" % path

    # Update tally.
    if success:
      pe_passed = pe_passed + 1

  print "Result: %d files found, %d files passed" % (pe_total, pe_passed)
  if pe_passed != pe_total:
    sys.exit(1)

if __name__ == '__main__':
  usage = "Usage: %prog [options] DIRECTORY"
  option_parser = optparse.OptionParser(usage=usage)
  option_parser.add_option("-v", "--verbose", action="store_true",
                           default=False, help="Print debug logging")
  options, args = option_parser.parse_args()
  if not args:
    option_parser.print_help()
    sys.exit(0)
  main(options, args)
