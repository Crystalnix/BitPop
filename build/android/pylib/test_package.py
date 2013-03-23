# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import logging
import re
import os

import constants
from perf_tests_helper import PrintPerfResult
from pylib import pexpect
from test_result import BaseTestResult, TestResults

from android_commands import errors


class TestPackage(object):
  """A helper base class for both APK and stand-alone executables.

  Args:
    adb: ADB interface the tests are using.
    device: Device to run the tests.
    test_suite: A specific test suite to run, empty to run all.
    timeout: Timeout for each test.
    cleanup_test_files: Whether or not to cleanup test files on device.
    tool: Name of the Valgrind tool.
    dump_debug_info: A debug_info object.
  """

  def __init__(self, adb, device, test_suite, timeout,
               cleanup_test_files, tool, dump_debug_info):
    self.adb = adb
    self.device = device
    self.test_suite_full = test_suite
    self.test_suite = os.path.splitext(test_suite)[0]
    self.test_suite_basename = self._GetTestSuiteBaseName()
    self.test_suite_dirname = os.path.dirname(
        self.test_suite.split(self.test_suite_basename)[0])
    self.cleanup_test_files = cleanup_test_files
    self.tool = tool
    if timeout == 0:
      timeout = 60
    # On a VM (e.g. chromium buildbots), this timeout is way too small.
    if os.environ.get('BUILDBOT_SLAVENAME'):
      timeout = timeout * 2
    self.timeout = timeout * self.tool.GetTimeoutScale()
    self.dump_debug_info = dump_debug_info

  def GetDisabledPrefixes(self):
    return ['DISABLED_', 'FLAKY_', 'FAILS_']

  def _ParseGTestListTests(self, all_tests):
    """Parses and filters the raw test lists.

    Args:
    all_tests: The raw test listing with the following format:

      IPCChannelTest.
        SendMessageInChannelConnected
      IPCSyncChannelTest.
        Simple
        DISABLED_SendWithTimeoutMixedOKAndTimeout

    Returns:
      A list of non-disabled tests. For the above raw listing:

      [IPCChannelTest.SendMessageInChannelConnected, IPCSyncChannelTest.Simple]
    """
    ret = []
    current = ''
    disabled_prefixes = self.GetDisabledPrefixes()
    for test in all_tests:
      if not test:
        continue
      if test[0] != ' ' and not test.endswith('.'):
        # Ignore any lines with unexpected format.
        continue
      if test[0] != ' ' and test.endswith('.'):
        current = test
        continue
      if 'YOU HAVE' in test:
        break
      test_name = test[2:]
      if not any([test_name.startswith(x) for x in disabled_prefixes]):
        ret += [current + test_name]
    return ret

  def PushDataAndPakFiles(self):
    external_storage = self.adb.GetExternalStorage()
    if (self.test_suite_basename == 'ui_unittests' or
        self.test_suite_basename == 'unit_tests'):
      self.adb.PushIfNeeded(
          self.test_suite_dirname + '/chrome.pak',
          external_storage + '/paks/chrome.pak')
      self.adb.PushIfNeeded(
          self.test_suite_dirname + '/locales/en-US.pak',
          external_storage + '/paks/en-US.pak')
    if self.test_suite_basename == 'unit_tests':
      self.adb.PushIfNeeded(
          self.test_suite_dirname + '/resources.pak',
          external_storage + '/paks/resources.pak')
      self.adb.PushIfNeeded(
          self.test_suite_dirname + '/chrome_100_percent.pak',
          external_storage + '/paks/chrome_100_percent.pak')
      self.adb.PushIfNeeded(self.test_suite_dirname + '/test_data',
                            external_storage + '/test_data')
    if self.test_suite_basename == 'content_unittests':
      self.adb.PushIfNeeded(
          self.test_suite_dirname + '/content_resources.pak',
          external_storage + '/paks/content_resources.pak')
    if self.test_suite_basename == 'breakpad_unittests':
      self.adb.PushIfNeeded(
          self.test_suite_dirname + '/linux_dumper_unittest_helper',
          constants.TEST_EXECUTABLE_DIR + '/linux_dumper_unittest_helper')

  def _WatchTestOutput(self, p):
    """Watches the test output.
    Args:
      p: the process generating output as created by pexpect.spawn.
    """
    ok_tests = []
    failed_tests = []
    crashed_tests = []
    timed_out = False
    overall_fail = False

    # Test case statuses.
    re_run = re.compile('\[ RUN      \] ?(.*)\r\n')
    re_fail = re.compile('\[  FAILED  \] ?(.*)\r\n')
    re_ok = re.compile('\[       OK \] ?(.*?) .*\r\n')

    # Test run statuses.
    re_passed = re.compile('\[  PASSED  \] ?(.*)\r\n')
    re_runner_fail = re.compile('\[ RUNNER_FAILED \] ?(.*)\r\n')
    # Signal handlers are installed before starting tests
    # to output the CRASHED marker when a crash happens.
    re_crash = re.compile('\[ CRASHED      \](.*)\r\n')

    try:
      while True:
        found = p.expect([re_run, re_passed, re_runner_fail],
                         timeout=self.timeout)
        if found == 1:  # re_passed
          break
        elif found == 2:  # re_runner_fail
          overall_fail = True
          break
        else:  # re_run
          if self.dump_debug_info:
            self.dump_debug_info.TakeScreenshot('_Test_Start_Run_')

          full_test_name = p.match.group(1).replace('\r', '')
          found = p.expect([re_ok, re_fail, re_crash], timeout=self.timeout)
          if found == 0:  # re_ok
            if full_test_name == p.match.group(1).replace('\r', ''):
              ok_tests += [BaseTestResult(full_test_name, p.before)]
          elif found == 2:  # re_crash
            crashed_tests += [BaseTestResult(full_test_name, p.before)]
            overall_fail = True
            break
          else:  # re_fail
            failed_tests += [BaseTestResult(full_test_name, p.before)]
    except pexpect.EOF:
      logging.error('Test terminated - EOF')
      raise errors.DeviceUnresponsiveError('Device may be offline')
    except pexpect.TIMEOUT:
      logging.error('Test terminated after %d second timeout.',
                    self.timeout)
      timed_out = True
    finally:
      p.close()

    ret_code = self._GetGTestReturnCode()
    if ret_code:
      logging.critical(
          'gtest exit code: %d\npexpect.before: %s\npexpect.after: %s',
          ret_code, p.before, p.after)
      overall_fail = True

    # Create TestResults and return
    return TestResults.FromRun(ok=ok_tests, failed=failed_tests,
                               crashed=crashed_tests, timed_out=timed_out,
                               overall_fail=overall_fail)
