#!/usr/bin/python
# Copyright (c) 2010 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# memcheck_analyze.py

''' Given a valgrind XML file, parses errors and uniques them.'''

import gdb_helper

import logging
import optparse
import os
import re
import subprocess
import sys
import time
from xml.dom.minidom import parse
from xml.parsers.expat import ExpatError

import common

# Global symbol table (yuck)
TheAddressTable = None

# These are functions (using C++ mangled names) that we look for in stack
# traces. We don't show stack frames while pretty printing when they are below
# any of the following:
_TOP_OF_STACK_POINTS = [
  # Don't show our testing framework.
  "testing::Test::Run()",
  "_ZN7testing4Test3RunEv",
  # Also don't show the internals of libc/pthread.
  "start_thread"
]

def getTextOf(top_node, name):
  ''' Returns all text in all DOM nodes with a certain |name| that are children
  of |top_node|.
  '''

  text = ""
  for nodes_named in top_node.getElementsByTagName(name):
    text += "".join([node.data for node in nodes_named.childNodes
                     if node.nodeType == node.TEXT_NODE])
  return text

def getCDATAOf(top_node, name):
  ''' Returns all CDATA in all DOM nodes with a certain |name| that are children
  of |top_node|.
  '''

  text = ""
  for nodes_named in top_node.getElementsByTagName(name):
    text += "".join([node.data for node in nodes_named.childNodes
                     if node.nodeType == node.CDATA_SECTION_NODE])
  if (text == ""):
    return None
  return text

def shortenFilePath(source_dir, directory):
  '''Returns a string with the string prefix |source_dir| removed from
  |directory|.'''
  prefixes_to_cut = ["build/src/", "valgrind/coregrind/"]

  if source_dir:
    prefixes_to_cut.append(source_dir)

  for p in prefixes_to_cut:
    index = directory.rfind(p)
    if index != -1:
      directory = directory[index + len(p):]

  return directory

# Constants that give real names to the abbreviations in valgrind XML output.
INSTRUCTION_POINTER = "ip"
OBJECT_FILE = "obj"
FUNCTION_NAME = "fn"
SRC_FILE_DIR = "dir"
SRC_FILE_NAME = "file"
SRC_LINE = "line"

def gatherFrames(node, source_dir):
  frames = []
  for frame in node.getElementsByTagName("frame"):
    frame_dict = {
      INSTRUCTION_POINTER : getTextOf(frame, INSTRUCTION_POINTER),
      OBJECT_FILE         : getTextOf(frame, OBJECT_FILE),
      FUNCTION_NAME       : getTextOf(frame, FUNCTION_NAME),
      SRC_FILE_DIR        : shortenFilePath(
          source_dir, getTextOf(frame, SRC_FILE_DIR)),
      SRC_FILE_NAME       : getTextOf(frame, SRC_FILE_NAME),
      SRC_LINE            : getTextOf(frame, SRC_LINE)
    }
    frames += [frame_dict]
    if frame_dict[FUNCTION_NAME] in _TOP_OF_STACK_POINTS:
      break
    global TheAddressTable
    if TheAddressTable != None and frame_dict[SRC_LINE] == "":
      # Try using gdb
      TheAddressTable.Add(frame_dict[OBJECT_FILE],
                          frame_dict[INSTRUCTION_POINTER])
  return frames

class ValgrindError:
  ''' Takes a <DOM Element: error> node and reads all the data from it. A
  ValgrindError is immutable and is hashed on its pretty printed output.
  '''

  def __init__(self, source_dir, error_node, commandline):
    ''' Copies all the relevant information out of the DOM and into object
    properties.

    Args:
      error_node: The <error></error> DOM node we're extracting from.
      source_dir: Prefix that should be stripped from the <dir> node.
      commandline: The command that was run under valgrind
    '''

    # Valgrind errors contain one <what><stack> pair, plus an optional
    # <auxwhat><stack> pair, plus an optional <origin><what><stack></origin>,
    # plus (since 3.5.0) a <suppression></suppression> pair.
    # (Origin is nicely enclosed; too bad the other two aren't.)
    # The most common way to see all three in one report is
    # a syscall with a parameter that points to uninitialized memory, e.g.
    # Format:
    # <error>
    #   <unique>0x6d</unique>
    #   <tid>1</tid>
    #   <kind>SyscallParam</kind>
    #   <what>Syscall param write(buf) points to uninitialised byte(s)</what>
    #   <stack>
    #     <frame>
    #     ...
    #     </frame>
    #   </stack>
    #   <auxwhat>Address 0x5c9af4f is 7 bytes inside a block of ...</auxwhat>
    #   <stack>
    #     <frame>
    #     ...
    #     </frame>
    #   </stack>
    #   <origin>
    #   <what>Uninitialised value was created by a heap allocation</what>
    #   <stack>
    #     <frame>
    #     ...
    #     </frame>
    #   </stack>
    #   </origin>
    #   <suppression>
    #     <sname>insert_a_suppression_name_here</sname>
    #     <skind>Memcheck:Param</skind>
    #     <skaux>write(buf)</skaux>
    #     <sframe> <fun>__write_nocancel</fun> </sframe>
    #     ...
    #     <sframe> <fun>main</fun> </sframe>
    #     <rawtext>
    # <![CDATA[
    # {
    #    <insert_a_suppression_name_here>
    #    Memcheck:Param
    #    write(buf)
    #    fun:__write_nocancel
    #    ...
    #    fun:main
    # }
    # ]]>
    #     </rawtext>
    #   </suppression>
    # </error>
    #
    # Each frame looks like this:
    #  <frame>
    #    <ip>0x83751BC</ip>
    #    <obj>/data/dkegel/chrome-build/src/out/Release/base_unittests</obj>
    #    <fn>_ZN7testing8internal12TestInfoImpl7RunTestEPNS_8TestInfoE</fn>
    #    <dir>/data/dkegel/chrome-build/src/testing/gtest/src</dir>
    #    <file>gtest-internal-inl.h</file>
    #    <line>655</line>
    #  </frame>
    # although the dir, file, and line elements are missing if there is
    # no debug info.

    self._kind = getTextOf(error_node, "kind")
    self._backtraces = []
    self._suppression = None
    self._commandline = commandline

    # Iterate through the nodes, parsing <what|auxwhat><stack> pairs.
    description = None
    for node in error_node.childNodes:
      if node.localName == "what" or node.localName == "auxwhat":
        description = "".join([n.data for n in node.childNodes
                              if n.nodeType == n.TEXT_NODE])
      elif node.localName == "xwhat":
        description = getTextOf(node, "text")
      elif node.localName == "stack":
        self._backtraces.append([description, gatherFrames(node, source_dir)])
        description = None
      elif node.localName == "origin":
        description = getTextOf(node, "what")
        stack = node.getElementsByTagName("stack")[0]
        frames = gatherFrames(stack, source_dir)
        self._backtraces.append([description, frames])
        description = None
        stack = None
        frames = None
      elif node.localName == "suppression":
        self._suppression = getCDATAOf(node, "rawtext");

  def __str__(self):
    ''' Pretty print the type and backtrace(s) of this specific error,
        including suppression (which is just a mangled backtrace).'''
    output = ""
    if (self._commandline):
      output += self._commandline + "\n"

    output += self._kind + "\n"
    for backtrace in self._backtraces:
      output += backtrace[0] + "\n"
      filter = subprocess.Popen("c++filt -n", stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,
                                shell=True,
                                close_fds=True)
      buf = ""
      for frame in backtrace[1]:
        buf +=  (frame[FUNCTION_NAME] or frame[INSTRUCTION_POINTER]) + "\n"
      (stdoutbuf, stderrbuf) = filter.communicate(buf.encode('latin-1'))
      demangled_names = stdoutbuf.split("\n")

      i = 0
      for frame in backtrace[1]:
        output += ("  " + demangled_names[i])
        i = i + 1

        global TheAddressTable
        if TheAddressTable != None and frame[SRC_FILE_DIR] == "":
           # Try using gdb
           foo = TheAddressTable.GetFileLine(frame[OBJECT_FILE],
                                             frame[INSTRUCTION_POINTER])
           if foo[0] != None:
             output += (" (" + foo[0] + ":" + foo[1] + ")")
        elif frame[SRC_FILE_DIR] != "":
          output += (" (" + frame[SRC_FILE_DIR] + "/" + frame[SRC_FILE_NAME] +
                     ":" + frame[SRC_LINE] + ")")
        else:
          output += " (" + frame[OBJECT_FILE] + ")"
        output += "\n"

    assert self._suppression != None, "Your Valgrind doesn't generate " \
                                      "suppressions - is it too old?"

    output += "Suppression (error hash=#%016X#):" % \
        (self.__hash__() & 0xffffffffffffffff)
    # Widen suppression slightly to make portable between mac and linux
    supp = self._suppression;
    supp = supp.replace("fun:_Znwj", "fun:_Znw*")
    supp = supp.replace("fun:_Znwm", "fun:_Znw*")
    # Split into lines so we can enforce length limits
    supplines = supp.split("\n")

    # Truncate at line 26 (VG_MAX_SUPP_CALLERS plus 2 for name and type)
    # or at the first 'boring' caller.
    # (https://bugs.kde.org/show_bug.cgi?id=199468 proposes raising
    # VG_MAX_SUPP_CALLERS, but we're probably fine with it as is.)
    # TODO(dkegel): add more boring callers
    newlen = 26;
    for boring_caller in ["   fun:_ZN11MessageLoop3RunEv",
                          "   fun:_ZN7testing4Test3RunEv"]:
      try:
        newlen = min(newlen, supplines.index(boring_caller))
      except ValueError:
        pass
    if (len(supplines) > newlen):
      supplines = supplines[0:newlen]
      supplines.append("}")

    output += "\n".join(supplines) + "\n"

    return output

  def UniqueString(self):
    ''' String to use for object identity. Don't print this, use str(obj)
    instead.'''
    rep = self._kind + " "
    for backtrace in self._backtraces:
      for frame in backtrace[1]:
        rep += frame[FUNCTION_NAME]

        if frame[SRC_FILE_DIR] != "":
          rep += frame[SRC_FILE_DIR] + "/" + frame[SRC_FILE_NAME]
        else:
          rep += frame[OBJECT_FILE]

    return rep

  def __hash__(self):
    return hash(self.UniqueString())
  def __eq__(self, rhs):
    return self.UniqueString() == rhs

def find_and_truncate(f):
  f.seek(0)
  while True:
    line = f.readline()
    if line == "":
      return False
    if '</valgrindoutput>' in line:
      # valgrind often has garbage after </valgrindoutput> upon crash
      f.truncate()
      return True

class MemcheckAnalyzer:
  ''' Given a set of Valgrind XML files, parse all the errors out of them,
  unique them and output the results.'''

  SANITY_TEST_SUPPRESSIONS = {
      "Memcheck sanity test 01 (memory leak).": 1,
      "Memcheck sanity test 02 (malloc/read left).": 1,
      "Memcheck sanity test 03 (malloc/read right).": 1,
      "Memcheck sanity test 04 (malloc/write left).": 1,
      "Memcheck sanity test 05 (malloc/write right).": 1,
      "Memcheck sanity test 06 (new/read left).": 1,
      "Memcheck sanity test 07 (new/read right).": 1,
      "Memcheck sanity test 08 (new/write left).": 1,
      "Memcheck sanity test 09 (new/write right).": 1,
      "Memcheck sanity test 10 (write after free).": 1,
      "Memcheck sanity test 11 (write after delete).": 1,
      "Memcheck sanity test 12 (array deleted without []).": 1,
      "Memcheck sanity test 13 (single element deleted with []).": 1,
      "Memcheck sanity test 14 (malloc/read uninit).": 1,
      "Memcheck sanity test 15 (new/read uninit).": 1,
  }

  # Max time to wait for memcheck logs to complete.
  LOG_COMPLETION_TIMEOUT = 180.0

  def __init__(self, source_dir, show_all_leaks=False, use_gdb=False):
    '''Create a parser for Memcheck logs.

    Args:
      source_dir: Path to top of source tree for this build
      show_all_leaks: Whether to show even less important leaks
      use_gdb: Whether to use gdb to resolve source filenames and line numbers
               in the report stacktraces
    '''
    self._source_dir = source_dir
    self._show_all_leaks = show_all_leaks
    self._use_gdb = use_gdb

    # Contains the set of unique errors
    self._errors = set()

    # Contains the time when the we started analyzing the first log file.
    # This variable is used to skip incomplete logs after some timeout.
    self._analyze_start_time = None


  def Report(self, files, check_sanity=False):
    '''Reads in a set of files and prints Memcheck report.

    Args:
      files: A list of filenames.
      check_sanity: if true, search for SANITY_TEST_SUPPRESSIONS
    '''
    # Beyond the detailed errors parsed by ValgrindError above,
    # the xml file contain records describing suppressions that were used:
    # <suppcounts>
    #  <pair>
    #    <count>28</count>
    #    <name>pango_font_leak_todo</name>
    #  </pair>
    #  <pair>
    #    <count>378</count>
    #    <name>bug_13243</name>
    #  </pair>
    # </suppcounts
    # Collect these and print them at the end.
    #
    # With our patch for https://bugs.kde.org/show_bug.cgi?id=205000 in,
    # the file also includes records of the form
    # <load_obj><obj>/usr/lib/libgcc_s.1.dylib</obj><ip>0x27000</ip></load_obj>
    # giving the filename and load address of each binary that was mapped
    # into the process.

    global TheAddressTable
    if self._use_gdb:
      TheAddressTable = gdb_helper.AddressTable()
    else:
      TheAddressTable = None
    cur_report_errors = set()
    suppcounts = {}
    badfiles = set()

    if self._analyze_start_time == None:
      self._analyze_start_time = time.time()
    start_time = self._analyze_start_time

    parse_failed = False
    for file in files:
      # Wait up to three minutes for valgrind to finish writing all files,
      # but after that, just skip incomplete files and warn.
      f = open(file, "r+")
      pid = re.match(".*\.([0-9]+)$", file)
      if pid:
        pid = pid.groups()[0]
      found = False
      running = True
      firstrun = True
      origsize = os.path.getsize(file)
      while (running and not found and
             (firstrun or
              ((time.time() - start_time) < self.LOG_COMPLETION_TIMEOUT))):
        firstrun = False
        f.seek(0)
        if pid:
          # Make sure the process is still running so we don't wait for
          # 3 minutes if it was killed. See http://crbug.com/17453
          ps_out = subprocess.Popen("ps p %s" % pid, shell=True,
                                    stdout=subprocess.PIPE).stdout
          if len(ps_out.readlines()) < 2:
            running = False
        found = find_and_truncate(f)
        if not running and not found:
          logging.warn("Valgrind process PID = %s is not running but "
                       "its XML log has not been finished correctly." % pid)
        if running and not found:
          time.sleep(1)
      f.close()
      if not found:
        badfiles.add(file)
      else:
        newsize = os.path.getsize(file)
        if origsize > newsize+1:
          logging.warn(str(origsize - newsize) +
                       " bytes of junk were after </valgrindoutput> in %s!" %
                       file)
        try:
          parsed_file = parse(file);
        except ExpatError, e:
          parse_failed = True
          logging.warn("could not parse %s: %s" % (file, e))
          lineno = e.lineno - 1
          context_lines = 5
          context_start = max(0, lineno - context_lines)
          context_end = lineno + context_lines + 1
          context_file = open(file, "r")
          for i in range(0, context_start):
            context_file.readline()
          for i in range(context_start, context_end):
            context_data = context_file.readline().rstrip()
            if i != lineno:
              logging.warn("  %s" % context_data)
            else:
              logging.warn("> %s" % context_data)
          context_file.close()
          continue
        if TheAddressTable != None:
          load_objs = parsed_file.getElementsByTagName("load_obj")
          for load_obj in load_objs:
            obj = getTextOf(load_obj, "obj")
            ip = getTextOf(load_obj, "ip")
            TheAddressTable.AddBinaryAt(obj, ip)

        commandline = None
        preamble = parsed_file.getElementsByTagName("preamble")[0];
        for node in preamble.getElementsByTagName("line"):
          if node.localName == "line":
            for x in node.childNodes:
              if x.nodeType == node.TEXT_NODE and "Command" in x.data:
                commandline = x.data
                break

        raw_errors = parsed_file.getElementsByTagName("error")
        for raw_error in raw_errors:
          # Ignore "possible" leaks for now by default.
          if (self._show_all_leaks or
              getTextOf(raw_error, "kind") != "Leak_PossiblyLost"):
            error = ValgrindError(self._source_dir, raw_error, commandline)
            if error not in cur_report_errors:
              # We haven't seen such errors doing this report yet...
              if error in self._errors:
                # ... but we saw it in earlier reports, e.g. previous UI test
                cur_report_errors.add("This error was already printed in "
                                      "some other test, see 'hash=#%016X#'" % \
                                      (error.__hash__() & 0xffffffffffffffff))
              else:
                # ... and we haven't seen it in other tests as well
                self._errors.add(error)
                cur_report_errors.add(error)

        suppcountlist = parsed_file.getElementsByTagName("suppcounts")
        if len(suppcountlist) > 0:
          suppcountlist = suppcountlist[0]
          for node in suppcountlist.getElementsByTagName("pair"):
            count = getTextOf(node, "count");
            name = getTextOf(node, "name");
            if name in suppcounts:
              suppcounts[name] += int(count)
            else:
              suppcounts[name] = int(count)

    if len(badfiles) > 0:
      logging.warn("valgrind didn't finish writing %d files?!" % len(badfiles))
      for file in badfiles:
        logging.warn("Last 20 lines of %s :" % file)
        os.system("tail -n 20 '%s' 1>&2" % file)

    if parse_failed:
      logging.error("FAIL! Couldn't parse Valgrind output file")
      return -2

    is_sane = False
    print "-----------------------------------------------------"
    print "Suppressions used:"
    print "  count name"

    remaining_sanity_supp = MemcheckAnalyzer.SANITY_TEST_SUPPRESSIONS
    for (name, count) in sorted(suppcounts.items(),
                                key=lambda (k,v): (v,k)):
      print "%7d %s" % (count, name)
      if name in remaining_sanity_supp and remaining_sanity_supp[name] == count:
        del remaining_sanity_supp[name]
    if len(remaining_sanity_supp) == 0:
      is_sane = True
    print "-----------------------------------------------------"
    sys.stdout.flush()

    retcode = 0
    if cur_report_errors:
      logging.error("FAIL! There were %s errors: " % len(cur_report_errors))

      if TheAddressTable != None:
        TheAddressTable.ResolveAll()

      for error in cur_report_errors:
        logging.error(error)

      retcode = -1

    # Report tool's insanity even if there were errors.
    if check_sanity and not is_sane:
      logging.error("FAIL! Sanity check failed!")
      logging.info("The following test errors were not handled: ")
      for (name, count) in sorted(remaining_sanity_supp.items(),
                                  key=lambda (k,v): (v,k)):
        logging.info("%7d %s" % (count, name))
      retcode = -3

    if retcode != 0:
      return retcode

    logging.info("PASS! No errors found!")
    return 0

def _main():
  '''For testing only. The MemcheckAnalyzer class should be imported instead.'''
  retcode = 0
  parser = optparse.OptionParser("usage: %prog [options] <files to analyze>")
  parser.add_option("", "--source_dir",
                    help="path to top of source tree for this build"
                    "(used to normalize source paths in baseline)")

  (options, args) = parser.parse_args()
  if len(args) == 0:
    parser.error("no filename specified")
  filenames = args

  analyzer = MemcheckAnalyzer(options.source_dir, use_gdb=True)
  retcode = analyzer.Report(filenames)

  sys.exit(retcode)

if __name__ == "__main__":
  _main()
