#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for checkdeps.
"""

import os
import unittest


import checkdeps
import results


class CheckDepsTest(unittest.TestCase):

  def setUp(self):
    self.deps_checker = checkdeps.DepsChecker(being_tested=True)

  def testRegularCheckDepsRun(self):
    self.deps_checker.CheckDirectory(
        os.path.join(self.deps_checker.base_directory,
                     'tools/checkdeps/testdata'))
    problems = self.deps_checker.results_formatter.GetResults()
    self.failUnlessEqual(3, len(problems))

    def VerifySubstringsInProblems(key_path, substrings_in_sequence):
      found = False
      key_path = os.path.normpath(key_path)
      for problem in problems:
        index = problem.find(key_path)
        if index != -1:
          for substring in substrings_in_sequence:
            index = problem.find(substring, index + 1)
            self.failUnless(index != -1)
          found = True
          break
      if not found:
        self.fail('Found no problem for file %s' % key_path)

    VerifySubstringsInProblems('testdata/allowed/test.h',
                               ['-tools/checkdeps/testdata/disallowed',
                                '-third_party/explicitly_disallowed',
                                'Because of no rule applying'])
    VerifySubstringsInProblems('testdata/disallowed/test.h',
                               ['-third_party/explicitly_disallowed',
                                'Because of no rule applying',
                                'Because of no rule applying'])
    VerifySubstringsInProblems('disallowed/allowed/test.h',
                               ['-third_party/explicitly_disallowed',
                                'Because of no rule applying',
                                'Because of no rule applying'])

  def testTempRulesGenerator(self):
    self.deps_checker.results_formatter = results.TemporaryRulesFormatter()
    self.deps_checker.CheckDirectory(
        os.path.join(self.deps_checker.base_directory,
                     'tools/checkdeps/testdata/allowed'))
    temp_rules = self.deps_checker.results_formatter.GetResults()
    expected = [u'  "!third_party/explicitly_disallowed/bad.h",',
                u'  "!third_party/no_rule/bad.h",',
                u'  "!tools/checkdeps/testdata/disallowed/bad.h",']
    self.failUnlessEqual(expected, temp_rules)

  def testCheckAddedIncludesAllGood(self):
    problems = self.deps_checker.CheckAddedCppIncludes(
      [['tools/checkdeps/testdata/allowed/test.cc',
        ['#include "tools/checkdeps/testdata/allowed/good.h"',
         '#include "tools/checkdeps/testdata/disallowed/allowed/good.h"']
      ]])
    self.failIf(problems)

  def testCheckAddedIncludesManyGarbageLines(self):
    garbage_lines = ["My name is Sam%d\n" % num for num in range(50)]
    problems = self.deps_checker.CheckAddedCppIncludes(
      [['tools/checkdeps/testdata/allowed/test.cc', garbage_lines]])
    self.failIf(problems)

  def testCheckAddedIncludesNoRule(self):
    problems = self.deps_checker.CheckAddedCppIncludes(
      [['tools/checkdeps/testdata/allowed/test.cc',
        ['#include "no_rule_for_this/nogood.h"']
      ]])
    self.failUnless(problems)

  def testCheckAddedIncludesSkippedDirectory(self):
    problems = self.deps_checker.CheckAddedCppIncludes(
      [['tools/checkdeps/testdata/disallowed/allowed/skipped/test.cc',
        ['#include "whatever/whocares.h"']
      ]])
    self.failIf(problems)

  def testCheckAddedIncludesTempAllowed(self):
    problems = self.deps_checker.CheckAddedCppIncludes(
      [['tools/checkdeps/testdata/allowed/test.cc',
        ['#include "tools/checkdeps/testdata/disallowed/temporarily_allowed.h"']
      ]])
    self.failUnless(problems)


if __name__ == '__main__':
  unittest.main()
