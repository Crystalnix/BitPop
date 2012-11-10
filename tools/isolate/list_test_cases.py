#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""List all the test cases for a google test.

See more info at http://code.google.com/p/googletest/.
"""

import optparse
import sys

import run_test_cases


def main():
  """CLI frontend to validate arguments."""
  parser = optparse.OptionParser(
      usage='%prog <options> [gtest]')
  parser.add_option(
      '-d', '--disabled',
      action='store_true',
      help='Include DISABLED_ tests')
  parser.add_option(
      '-f', '--fails',
      action='store_true',
      help='Include FAILS_ tests')
  parser.add_option(
      '-F', '--flaky',
      action='store_true',
      help='Include FLAKY_ tests')
  parser.add_option(
      '-i', '--index',
      type='int',
      help='Shard index to run')
  parser.add_option(
      '-s', '--shards',
      type='int',
      help='Total number of shards to calculate from the --index to run')
  options, args = parser.parse_args()
  if len(args) != 1:
    parser.error('Please provide the executable to run')

  if bool(options.shards) != bool(options.index is not None):
    parser.error('Use both --index X --shards Y or none of them')

  try:
    tests = run_test_cases.list_test_cases(
        args[0],
        options.index,
        options.shards,
        options.disabled,
        options.fails,
        options.flaky)
    for test in tests:
      print test
  except run_test_cases.Failure, e:
    print e.args[0]
    return e.args[1]
  return 0


if __name__ == '__main__':
  sys.exit(main())
