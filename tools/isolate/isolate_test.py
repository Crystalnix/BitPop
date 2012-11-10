#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os
import sys
import tempfile
import unittest

import isolate

ROOT_DIR = os.path.dirname(os.path.abspath(__file__))


class Isolate(unittest.TestCase):
  def setUp(self):
    # Everything should work even from another directory.
    os.chdir(os.path.dirname(ROOT_DIR))

  def test_load_isolate_empty(self):
    content = "{}"
    command, infiles, touched, read_only = isolate.load_isolate(content)
    self.assertEquals([], command)
    self.assertEquals([], infiles)
    self.assertEquals([], touched)
    self.assertEquals(None, read_only)

  def test_result_load_empty(self):
    values = {
    }
    expected = {
      'command': [],
      'files': {},
      'read_only': None,
      'relative_cwd': None,
    }
    self.assertEquals(expected, isolate.Result.load(values).flatten())

  def test_result_load(self):
    values = {
      'command': 'maybe',
      'files': {'foo': 42},
      'read_only': 2,
      'relative_cwd': None,
    }
    expected = {
      'command': 'maybe',
      'files': {'foo': 42},
      'read_only': 2,
      'relative_cwd': None,
    }
    self.assertEquals(expected, isolate.Result.load(values).flatten())

  def test_result_load_unexpected(self):
    values = {
      'foo': 'bar',
    }
    expected = (
      ("Found unexpected entry {'foo': 'bar'} while constructing an "
          "object Result"),
      {'foo': 'bar'},
      'Result')
    try:
      isolate.Result.load(values)
      self.fail()
    except ValueError, e:
      self.assertEquals(expected, e.args)

  def test_savedstate_load_empty(self):
    values = {
    }
    expected = {
      'isolate_file': None,
      'variables': {},
    }
    self.assertEquals(expected, isolate.SavedState.load(values).flatten())

  def test_savedstate_load(self):
    values = {
      'isolate_file': os.path.join(ROOT_DIR, 'maybe'),
      'variables': {'foo': 42},
    }
    expected = {
      'isolate_file': os.path.join(ROOT_DIR, 'maybe'),
      'variables': {'foo': 42},
    }
    self.assertEquals(expected, isolate.SavedState.load(values).flatten())

  def test_load_stale_result(self):
    directory = tempfile.mkdtemp(prefix='isolate_')
    try:
      isolate_file = os.path.join(
            ROOT_DIR, 'data', 'isolate', 'touch_root.isolate')
      class Options(object):
        result = os.path.join(directory, 'result')
        outdir = os.path.join(directory, '0utdir')
        isolate = isolate_file
        variables = {'foo': 'bar'}

      # Data to be loaded in the .result file. Do not create a .state file.
      input_data = {
        'command': ['python'],
        'files': {
          'foo': {
            "mode": 416,
            "sha-1": "invalid",
            "size": 538,
            "timestamp": 1335146921,
          },
          os.path.join('data', 'isolate', 'touch_root.py'): {
            "mode": 488,
            "sha-1": "invalid",
            "size": 538,
            "timestamp": 1335146921,
          },
        },
      }
      isolate.trace_inputs.write_json(Options.result, input_data, False)

      # A CompleteState object contains two parts:
      # - Result instance stored in complete_state.result, corresponding to the
      #   .result file, is what is read by run_test_from_archive.py.
      # - SavedState instance stored in compelte_state.saved_state,
      #   corresponding to the .state file, which is simply to aid the developer
      #   when re-running the same command multiple times and contain
      #   discardable information.
      complete_state = isolate.load_complete_state(Options, isolate.STATS_ONLY)
      actual_result = complete_state.result.flatten()
      actual_saved_state = complete_state.saved_state.flatten()

      expected_result = {
        'command': ['python', 'touch_root.py'],
        'files': {
          os.path.join(u'data', 'isolate', 'touch_root.py'): {
            'mode': 488,
            'size': self._size('data', 'isolate', 'touch_root.py'),
          },
          'isolate.py': {
            'mode': 488,
            'size': self._size('isolate.py'),
          },
        },
        'read_only': None,
        'relative_cwd': os.path.join('data', 'isolate'),
      }
      if sys.platform == 'win32':
        # 'mode' are not saved in windows.
        for values in expected_result['files'].itervalues():
          del values['mode']
      for item in actual_result['files'].itervalues():
        self.assertTrue(item.pop('timestamp'))
      self.assertEquals(expected_result, actual_result)

      expected_saved_state = {
        'isolate_file': isolate_file,
        'variables': {'foo': 'bar'},
      }
      self.assertEquals(expected_saved_state, actual_saved_state)
    finally:
      isolate.run_test_from_archive.rmtree(directory)

  @staticmethod
  def _size(*args):
    return os.stat(os.path.join(ROOT_DIR, *args)).st_size


if __name__ == '__main__':
  logging.basicConfig(
      level=logging.DEBUG if '-v' in sys.argv else logging.ERROR,
      format='%(levelname)5s %(filename)15s(%(lineno)3d): %(message)s')
  unittest.main()
