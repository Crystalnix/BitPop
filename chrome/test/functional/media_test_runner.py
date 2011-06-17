#!/usr/bin/python

# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A module to execute a subclass of MediaTastBase class.

This executes a media test class (a subclass of MediaTastBase class) with
different configuration (parameters) which are passed in the form of
environment variables (e.g., the number of runs). The location of the
subclass is passed as one of arguments. An example of invocation is
"./media_test_runner.py -p ./media_perf.py". In this example,
media_perf.py will be invoked using the default set of parameters.
The list of possible combinations of parameters are: T parameter
for media cache is set/non-set, Chrome flag is set/non-set, data element
in data source file (CSV file - its content is list form or its content is
in matrix form),
"""

import copy
import csv
import os
from optparse import OptionParser
import shlex
import sys
from subprocess import Popen

from media_test_env_names import MediaTestEnvNames
from media_test_matrix import MediaTestMatrix


def main():
  EXTRA_NICKNAMES = ['nocache', 'cache']
  # Disable/enable media_cache.
  CHROME_FLAGS = ['--chrome-flags=\'--media-cache-size=1\'', '']
  # The 't' parameter is passed to player.html to disable/enable the media
  # cache.
  ADD_T_PARAMETERS = ['Y', 'N']
  # Player.html should contain all the HTML and Javascript that is
  # necessary to run these tests.
  DEFAULT_PLAYER_HTML_URL = 'DEFAULT'
  DEFAULT_PLAYER_HTML_URL_NICKNAME = 'local'
  # Default base url nickname used to display the result in case it is not
  # specified by the environment variable.
  DEFAULT_PLAYER_HTML_URL_NICKNAME = 'local'
  PRINT_ONLY_TIME = 'Y'
  REMOVE_FIRST_RESULT = 'N'
  # The number of runs for each test. This is used to compute average values
  # from among all runs.
  DEFAULT_NUMBER_OF_RUNS = 3
  # The interval between measurement calls.
  DEFAULT_MEASURE_INTERVALS = 3
  DEFAULT_SUITE_NAME = 'MEDIA_TESTS'
  # This script is used to run the PYAUTO suite.
  pyauto_functional_script_name = os.path.join(os.path.dirname(__file__),
                                               'pyauto_functional.py')

  default_input_filename = os.path.join(os.pardir, 'data', 'media', 'csv',
                                        'media_list_data.csv')
  parser = OptionParser()
  # TODO(imasaki@chromium.org): add parameter verification.
  parser.add_option(
      '-i', '--input', dest='input_filename', default=default_input_filename,
      help='Data source file (file contents in list form) [defaults to "%s"]' %
      default_input_filename, metavar='FILE')
  parser.add_option(
      '-x', '--input_matrix', dest='input_matrix_filename',
      help='Data source file (file contents in matrix form)', metavar='FILE')
  parser.add_option('-t', '--input_matrix_testcase',
                    dest='input_matrix_testcase_name',
                    help='Run particular test in matrix')
  parser.add_option('-r', '--video_matrix_home_url',
                    default='',
                    dest='video_matrix_home_url',
                    help='Video Matrix home URL')
  parser.add_option('-p', '--test_prog_name', dest='test_prog_name',
                    help='Test main program name (not using suite)',
                    metavar='FILE')
  parser.add_option('-b', '--player_html_url', dest='player_html_url',
                    default='None',
                    help='Player.html URL [defaults to "%s"] ' %
                         DEFAULT_PLAYER_HTML_URL, metavar='FILE')
  parser.add_option('-u', '--player_html_url_nickname',
                    dest='player_html_url_nickname',
                    default=DEFAULT_PLAYER_HTML_URL_NICKNAME,
                    help='Player.html Nickname [defaults to "%s"]' %
                         DEFAULT_PLAYER_HTML_URL_NICKNAME)
  parser.add_option('-n', '--number_of_runs', dest='number_of_runs',
                    default=DEFAULT_NUMBER_OF_RUNS,
                    help='The number of runs [defaults to "%d"]' %
                         DEFAULT_NUMBER_OF_RUNS)
  parser.add_option('-m', '--measure_intervals', dest='measure_intervals',
                    default=DEFAULT_MEASURE_INTERVALS,
                    help='Interval for measurement data [defaults to "%d"]' %
                         DEFAULT_MEASURE_INTERVALS)
  parser.add_option('-o', '--test-one-combination', dest='one_combination',
                    default=True, # Currently default is True
                                  # since we want to test only 1 combination.
                    help='Run only one parameter combination')
  parser.add_option('-s', '--suite', dest='suite',
                    help='Suite file')
  options, args = parser.parse_args()
  if args:
    parser.print_help()
    sys.exit(1)

  test_data_list = []
  if options.input_matrix_filename is None:
    file = open(options.input_filename, 'rb')
    test_data_list = csv.reader(file)
    # First line contains headers that can be skipped.
    test_data_list.next()
  else:
    # Video_matrix_home_url requires "/" at the end.
    if not options.video_matrix_home_url.endswith('/'):
      options.video_matrix_home_url += '/'
    media_test_matrix = MediaTestMatrix()
    media_test_matrix.ReadData(options.input_matrix_filename)
    all_data_list = media_test_matrix.GenerateAllMediaInfosInCompactForm(
        True, options.video_matrix_home_url)
    if options.input_matrix_testcase_name is None:
      # Use all test cases.
      test_data_list = all_data_list
    else:
      # Choose particular video.
      media_info = MediaTestMatrix.LookForMediaInfoInCompactFormByNickName(
          all_data_list, options.input_matrix_testcase_name)
      if media_info is not None:
        test_data_list.append(media_info)
  for tag, filename, nickname in test_data_list:
      for j in range(len(CHROME_FLAGS)):
        for k in range(len(ADD_T_PARAMETERS)):
           parent_envs = copy.deepcopy(os.environ)
           if options.input_matrix_filename is None:
             filename = os.path.join(os.pardir, filename)
           envs = {
             MediaTestEnvNames.MEDIA_TAG_ENV_NAME: tag,
             MediaTestEnvNames.MEDIA_FILENAME_ENV_NAME: filename,
             MediaTestEnvNames.MEDIA_FILENAME_NICKNAME_ENV_NAME: nickname,
             MediaTestEnvNames.PLAYER_HTML_URL_ENV_NAME:
               options.player_html_url,
             MediaTestEnvNames.PLAYER_HTML_URL_NICKNAME_ENV_NAME:
               options.player_html_url_nickname,
             MediaTestEnvNames.EXTRA_NICKNAME_ENV_NAME:
               EXTRA_NICKNAMES[j],
             MediaTestEnvNames.ADD_T_PARAMETER_ENV_NAME: ADD_T_PARAMETERS[k],
             MediaTestEnvNames.PRINT_ONLY_TIME_ENV_NAME: PRINT_ONLY_TIME,
             MediaTestEnvNames.N_RUNS_ENV_NAME: str(options.number_of_runs),
             MediaTestEnvNames.REMOVE_FIRST_RESULT_ENV_NAME:
               REMOVE_FIRST_RESULT,
             MediaTestEnvNames.MEASURE_INTERVAL_ENV_NAME:
               str(options.measure_intervals),
           }
           envs.update(parent_envs)
           if options.suite is None and options.test_prog_name is not None:
             # Suite is not used - run test program directly.
             test_prog_name = options.test_prog_name
             suite_string = ''
           else:
             # Suite is used.
             # The test script names are in the PYAUTO_TEST file.
             test_prog_name = pyauto_functional_script_name
             if options.suite is None:
               suite_name = DEFAULT_SUITE_NAME
             else:
               suite_name = options.suite
             suite_string = ' --suite=%s' % suite_name
           test_prog_name = sys.executable + ' ' + test_prog_name
           cmd = test_prog_name + suite_string + ' ' + CHROME_FLAGS[j]
           proc = Popen(cmd, env=envs, shell=True)
           proc.communicate()
           if options.one_combination:
             sys.exit(0)


if __name__ == '__main__':
  main()
