#!/usr/bin/python

# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


class MediaTestEnvNames:
  """Class that contains all environment names used in media tests.

  Since PyAuto does not support commandline arguments, we have to rely on
  environment variables. The following are the names of the environment
  variables that are used in chrome/src/test/functional/media_test_runner.py
  and media tests (subclasses of MediaTestBase in
  chrome/src/test/functional/media_test_base.py)
  """
  # PLAYER_HTML is a HTML file that contains media tag and other
  # JavaScript code for running the test.
  # Use this to indicate its URL.
  PLAYER_HTML_URL_ENV_NAME = 'PLAYER_HTML_URL'

  # Display the result output in compact form (e.g., "local", "remote").
  PLAYER_HTML_URL_NICKNAME_ENV_NAME = 'PLAYER_HTML_URL_NICKNAME'

  # Use this when you want to add extra information in the result output.
  EXTRA_NICKNAME_ENV_NAME = 'EXTRA_NICKNAME'

  # Use this when you do not want to report the first result output.
  # First result includes time to start up the browser.
  REMOVE_FIRST_RESULT_ENV_NAME = 'REMOVE_FIRST_RESULT'

  # Add t=Data() parameter in query string to disable media cache.
  ADD_T_PARAMETER_ENV_NAME = 'ADD_T_PARAMETER'

  # Print out only playback time information.
  PRINT_ONLY_TIME_ENV_NAME = 'PRINT_ONLY_TIME'

  # Define the number of tries.
  N_RUNS_ENV_NAME = 'N_RUNS'

  # Define the tag name in HTML (either video or audio).
  MEDIA_TAG_ENV_NAME = 'HTML_TAG'

  # Define the media file name.
  MEDIA_FILENAME_ENV_NAME = 'MEDIA_FILENAME'

  # Define the media file nickname that is used for display.
  MEDIA_FILENAME_NICKNAME_ENV_NAME = 'MEDIA_FILENAME_NICKNAME'

  # Define the interval for the measurement.
  MEASURE_INTERVAL_ENV_NAME = 'MEASURE_INTERVALS'
