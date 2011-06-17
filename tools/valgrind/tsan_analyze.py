#!/usr/bin/python
# Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# tsan_analyze.py

''' Given a ThreadSanitizer output file, parses errors and uniques them.'''

import gdb_helper

import common
import logging
import optparse
import os
import re
import subprocess
import sys
import time

# Global symbol table (ugh)
TheAddressTable = None

class _StackTraceLine(object):
  def __init__(self, line, address, binary):
    self.raw_line_ = line
    self.address = address
    self.binary = binary
  def __str__(self):
    global TheAddressTable
    file, line = TheAddressTable.GetFileLine(self.binary, self.address)
    if (file is None) or (line is None):
      return self.raw_line_
    else:
      return self.raw_line_.replace(self.binary, '%s:%s' % (file, line))

class TsanAnalyzer(object):
  ''' Given a set of ThreadSanitizer output files, parse all the errors out of
  them, unique them and output the results.'''

  LOAD_LIB_RE = re.compile('--[0-9]+-- ([^(:]*) \((0x[0-9a-f]+)\)')
  TSAN_LINE_RE = re.compile('==[0-9]+==\s*[#0-9]+\s*'
                            '([0-9A-Fa-fx]+):'
                            '(?:[^ ]* )*'
                            '([^ :\n]+)'
                            '')
  TSAN_WARNING_LINE_RE = re.compile('==[0-9]+==\s*[#0-9]+\s*'
                                    '(?:[^ ]* )*'
                                    '([^ :\n]+)')

  THREAD_CREATION_STR = ("INFO: T.* "
      "(has been created by T.* at this point|is program's main thread)")

  SANITY_TEST_SUPPRESSION = "ThreadSanitizer sanity test"
  TSAN_RACE_DESCRIPTION = "Possible data race"
  TSAN_WARNING_DESCRIPTION =  ("Unlocking a non-locked lock"
      "|accessing an invalid lock"
      "|which did not acquire this lock")
  RACE_VERIFIER_LINE = "Confirmed a race|unexpected race"
  TSAN_ASSERTION = "Assertion failed: "

  def __init__(self, source_dir, use_gdb=False):
    '''Reads in a set of files.

    Args:
      source_dir: Path to top of source tree for this build
    '''

    self._use_gdb = use_gdb

  def ReadLine(self):
    self.line_ = self.cur_fd_.readline()
    self.stack_trace_line_ = None
    if not self._use_gdb:
      return
    global TheAddressTable
    match = TsanAnalyzer.LOAD_LIB_RE.match(self.line_)
    if match:
      binary, ip = match.groups()
      TheAddressTable.AddBinaryAt(binary, ip)
      return
    match = TsanAnalyzer.TSAN_LINE_RE.match(self.line_)
    if match:
      address, binary_name = match.groups()
      stack_trace_line = _StackTraceLine(self.line_, address, binary_name)
      TheAddressTable.Add(stack_trace_line.binary, stack_trace_line.address)
      self.stack_trace_line_ = stack_trace_line

  def ReadSection(self):
    result = [self.line_]
    if re.search("{{{", self.line_):
      while not re.search('}}}', self.line_):
        self.ReadLine()
        if self.stack_trace_line_ is None:
          result.append(self.line_)
        else:
          result.append(self.stack_trace_line_)
    return result

  def ReadTillTheEnd(self):
    result = [self.line_]
    while self.line_:
      self.ReadLine()
      result.append(self.line_)
    return result

  def ParseReportFile(self, filename):
    '''Parses a report file and returns a list of ThreadSanitizer reports.


    Args:
      filename: report filename.
    Returns:
      list of (list of (str iff self._use_gdb, _StackTraceLine otherwise)).
    '''
    ret = []
    self.cur_fd_ = open(filename, 'r')

    while True:
      # Read ThreadSanitizer reports.
      self.ReadLine()
      if not self.line_:
        break

      tmp = []
      while re.search(TsanAnalyzer.RACE_VERIFIER_LINE, self.line_):
        tmp.append(self.line_)
        self.ReadLine()
      while re.search(TsanAnalyzer.THREAD_CREATION_STR, self.line_):
        tmp.extend(self.ReadSection())
        self.ReadLine()
      if re.search(TsanAnalyzer.TSAN_RACE_DESCRIPTION, self.line_):
        tmp.extend(self.ReadSection())
        ret.append(tmp)
      if (re.search(TsanAnalyzer.TSAN_WARNING_DESCRIPTION, self.line_) and
          not common.IsWindows()): # workaround for http://crbug.com/53198
        tmp.extend(self.ReadSection())
        ret.append(tmp)
      if re.search(TsanAnalyzer.TSAN_ASSERTION, self.line_):
        tmp.extend(self.ReadTillTheEnd())
        ret.append(tmp)
        break

      match = re.search(" used_suppression:\s+([0-9]+)\s(.*)", self.line_)
      if match:
        count, supp_name = match.groups()
        count = int(count)
        if supp_name in self.used_suppressions:
          self.used_suppressions[supp_name] += count
        else:
          self.used_suppressions[supp_name] = count
    self.cur_fd_.close()
    return ret

  def GetReports(self, files):
    '''Extracts reports from a set of files.

    Reads a set of files and returns a list of all discovered
    ThreadSanitizer race reports. As a side effect, populates
    self.used_suppressions with appropriate info.
    '''

    global TheAddressTable
    if self._use_gdb:
      TheAddressTable = gdb_helper.AddressTable()
    else:
      TheAddressTable = None
    reports = []
    self.used_suppressions = {}
    for file in files:
      reports.extend(self.ParseReportFile(file))
    if self._use_gdb:
      TheAddressTable.ResolveAll()
      # Make each line of each report a string.
      reports = map(lambda(x): map(str, x), reports)
    return [''.join(report_lines) for report_lines in reports]

  def Report(self, files, check_sanity=False):
    '''Reads in a set of files and prints ThreadSanitizer report.

    Args:
      files: A list of filenames.
      check_sanity: if true, search for SANITY_TEST_SUPPRESSIONS
    '''

    reports = self.GetReports(files)

    is_sane = False
    print "-----------------------------------------------------"
    print "Suppressions used:"
    print "  count name"
    for item in sorted(self.used_suppressions.items(), key=lambda (k,v): (v,k)):
      print "%7s %s" % (item[1], item[0])
      if item[0].startswith(TsanAnalyzer.SANITY_TEST_SUPPRESSION):
        is_sane = True
    print "-----------------------------------------------------"
    sys.stdout.flush()

    retcode = 0
    if reports:
      logging.error("FAIL! Found %i report(s)" % len(reports))
      for report in reports:
        logging.error('\n' + report)
      retcode = -1

    # Report tool's insanity even if there were errors.
    if check_sanity and not is_sane:
      logging.error("FAIL! Sanity check failed!")
      retcode = -3

    if retcode != 0:
      return retcode
    logging.info("PASS: No reports found")
    return 0

if __name__ == '__main__':
  '''For testing only. The TsanAnalyzer class should be imported instead.'''
  retcode = 0
  parser = optparse.OptionParser("usage: %prog [options] <files to analyze>")
  parser.add_option("", "--source_dir",
                    help="path to top of source tree for this build"
                    "(used to normalize source paths in baseline)")

  (options, args) = parser.parse_args()
  if not args:
    parser.error("no filename specified")
  filenames = args

  analyzer = TsanAnalyzer(options.source_dir, use_gdb=True)
  retcode = analyzer.Report(filenames)

  sys.exit(retcode)
