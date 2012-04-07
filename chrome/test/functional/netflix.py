#!/usr/bin/env python
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import time

import pyauto_functional
import pyauto


class NetflixTestHelper():
  """Helper functions for Netflix tests.

  For sample usage, look at class NetflixTest.
  """

  # Netflix player states.
  IS_PLAYING = '4'

  TITLE_HOMEPAGE = 'http://movies.netflix.com/WiHome'
  SIGNOUT_PAGE = 'https://account.netflix.com/Logout'
  # 30 Rock.
  VIDEO_URL = 'http://movies.netflix.com/WiPlayer?' + \
              'movieid=70136124&trkid=2361637&t=30+Rock'
  _pyauto = None

  def __init__(self, pyauto):
    self._pyauto = pyauto

  def _IsNetflixPluginEnabled(self):
    """Determine Netflix plugin availability and its state."""
    return [x for x in self._pyauto.GetPluginsInfo().Plugins() \
            if x['name'] == 'Netflix' and x['enabled']]

  def _LoginToNetflix(self):
    """Login to Netflix."""
    credentials = self._pyauto.GetPrivateInfo()['test_netflix_acct']
    board_name = self._pyauto.ChromeOSBoard()
    assert credentials.get(board_name), \
           'No netflix credentials for %s.' % board_name
    self._pyauto.NavigateToURL(credentials['login_url'])
    login_js = """
        document.getElementById('email').value='%s';
        document.getElementById('password').value='%s';
        window.domAutomationController.send('ok');
    """ % (credentials[board_name], credentials['password'])
    self._pyauto.assertEqual(self._pyauto.ExecuteJavascript(login_js), 'ok',
        msg='Failed to set login credentials.')
    self._pyauto.assertTrue(self._pyauto.SubmitForm('login-form'),
        msg='Login to Netflix failed. We think this is an authetication '
            'problem from the Netflix side. Sometimes we also see this while '
            'login in manually.')

  def _GetVideoDroppedFrames(self, tab_index=0, windex=0):
    """Returns total Netflix video dropped frames."""
    js = """
        var frames = nrdp.video.droppedFrames;
        window.domAutomationController.send(frames + '');
    """
    return int(self._pyauto.ExecuteJavascript(js, tab_index=tab_index,
                                              windex=windex))

  def _GetVideoFrames(self, tab_index=0, windex=0):
    """Returns Netflix video total frames."""
    js = """
        var frames = nrdp.video.totalFrames;
        window.domAutomationController.send(frames + '');
    """
    return int(self._pyauto.ExecuteJavascript(js, tab_index=tab_index,
                                              windex=windex))

  def _HandleInfobars(self):
    """Manage infobars, come up during the test.

    We expect password and Netflix infobars. Processing only Netflix infobar,
    since to start a vidoe, pressing the OK button is a must. We can keep other
    infobars open."""
    self._pyauto.WaitForInfobarCount(2)
    tab_info = self._pyauto.GetBrowserInfo()['windows'][0]['tabs'][0]
    infobars = tab_info['infobars']
    index = 0
    netflix_infobar_status = False
    for infobar in infobars:
       if infobar['buttons'][0] == 'OK':
         self._pyauto.PerformActionOnInfobar('accept', infobar_index=index)
         netflix_infobar_status = True
       index = index + 1
    self._pyauto.assertTrue(netflix_infobar_status,
                            msg='Netflix infobar did not show up')

  def _CurrentPlaybackTime(self):
    """Returns the current playback time in seconds."""
    time = self._pyauto.ExecuteJavascript("""
        time = nrdp.video.currentTime;
        window.domAutomationController.send(time + '');
    """)
    return int(float(time))

  def _SignOut(self):
    """Sing out from Netflix Login."""
    self._pyauto.NavigateToURL(self.SIGNOUT_PAGE)

  def _LoginAndStartPlaying(self):
    """Login and start playing the video."""
    self._pyauto.assertTrue(self._pyauto._IsNetflixPluginEnabled(),
                            msg='Netflix plugin is disabled or not available.')
    self._pyauto._LoginToNetflix()
    self._pyauto.assertTrue(self._pyauto.WaitUntil(
        lambda:self._pyauto.GetActiveTabURL().spec(),
        expect_retval=self.TITLE_HOMEPAGE),
        msg='Login to Netflix failed.')
    self._pyauto.NavigateToURL(self.VIDEO_URL)
    self._pyauto._HandleInfobars()
    self._pyauto.assertTrue(self._pyauto.WaitUntil(
        lambda: self._pyauto.ExecuteJavascript("""
            player_status = nrdp.video.readyState;
            window.domAutomationController.send(player_status + '');
        """), expect_retval=self.IS_PLAYING),
        msg='Player did not start playing the title.')

class NetflixTest(pyauto.PyUITest, NetflixTestHelper):
  """Test case for Netflix player."""

  def __init__(self, methodName='runTest', **kwargs):
    pyauto.PyUITest.__init__(self, methodName, **kwargs)
    NetflixTestHelper.__init__(self, self)

  def tearDown(self):
    self._SignOut()
    pyauto.PyUITest.tearDown(self)

  def testPlayerLoadsAndPlays(self):
    """Test that Netflix player loads and plays the title."""
    self._LoginAndStartPlaying()

  def testPlaying(self):
    """Test that title playing progresses."""
    self._LoginAndStartPlaying()
    title_length =  self.ExecuteJavascript("""
        time = nrdp.video.duration;
        window.domAutomationController.send(time + '');
    """)
    title_length = int(float(title_length))
    prev_time = 0
    current_time = 0
    count = 0
    while current_time < title_length:
      # We want to test playing only for ten seconds.
      count = count + 1
      if count == 10:
        break
      current_time = self._CurrentPlaybackTime()
      self.assertTrue(prev_time <= current_time,
          msg='Prev playing time %s is greater than current time %s.'
          % (prev_time, current_time))
      prev_time = current_time
      # play video for some time
      time.sleep(1)
    # crosbug.com/22037
    # In case player doesn't start playing at all, above while loop may
    # still pass. So re-verifying and assuming that player did play something
    # during last 10 seconds.
    self.assertTrue(current_time > 0,
        msg='Netflix player didnot start playing.')


if __name__ == '__main__':
  pyauto_functional.Main()
