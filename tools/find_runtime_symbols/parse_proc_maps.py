#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re
import sys


_MAPS_PATTERN = re.compile(
    '^([a-f0-9]+)-([a-f0-9]+)\s+(.)(.)(.)(.)\s+([a-f0-9]+)\s+(\S+):(\S+)\s+'
    '(\d+)\s+(\S+)$', re.IGNORECASE)


class ProcMapsEntry(object):
  """A class representing one line in /proc/.../maps."""

  def __init__(
      self, begin, end, readable, writable, executable, private, offset,
      major, minor, inode, name):
    self.begin = begin
    self.end = end
    self.readable = readable
    self.writable = writable
    self.executable = executable
    self.private = private
    self.offset = offset
    self.major = major
    self.minor = minor
    self.inode = inode
    self.name = name


class ProcMaps(object):
  """A class representing contents in /proc/.../maps."""

  def __init__(self):
    self._sorted_indexes = []
    self._dictionary = {}
    self._sorted = True

  def append(self, entry):
    if self._sorted_indexes and self._sorted_indexes[-1] > entry.begin:
      self._sorted = False
    self._sorted_indexes.append(entry.begin)
    self._dictionary[entry.begin] = entry

  def iter(self, condition):
    if not self._sorted:
      self._sorted_indexes.sort()
      self._sorted = True
    for index in self._sorted_indexes:
      if not condition or condition(self._dictionary[index]):
        yield self._dictionary[index]

  def __iter__(self):
    if not self._sorted:
      self._sorted_indexes.sort()
      self._sorted = True
    for index in self._sorted_indexes:
      yield self._dictionary[index]


def parse_proc_maps(f):
  table = ProcMaps()
  for line in f:
    matched = _MAPS_PATTERN.match(line)
    if matched:
      table.append(ProcMapsEntry(
          int(matched.group(1), 16),  # begin
          int(matched.group(2), 16),  # end
          matched.group(3),           # readable
          matched.group(4),           # writable
          matched.group(5),           # executable
          matched.group(6),           # private
          int(matched.group(7), 16),  # offset
          matched.group(8),           # major
          matched.group(9),           # minor
          int(matched.group(10), 10), # inode
          matched.group(11)           # name
          ))

  return table


def main():
  if len(sys.argv) < 2:
    sys.stderr.write("""Usage:
%s /path/to/maps
""" % sys.argv[0])
    return 1

  with open(sys.argv[1], mode='r') as f:
    maps = parse_proc_maps(f)

  for entry in maps:
    print "%016x-%016x +%06x %s" % (
        entry.begin, entry.end, entry.offset, entry.name)

  return 0


if __name__ == '__main__':
  sys.exit(main())
