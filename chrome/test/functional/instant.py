#!/usr/bin/python

# Copyright (c) 2010 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import cgi
import os

import pyauto_functional  # Must be imported before pyauto
import pyauto


class InstantSettingsTest(pyauto.PyUITest):
  """Test Chrome Instant settings."""

  def testEnableDisableInstant(self):
    """Test to verify default Chrome Instant setting.
    Check if the setting can be enabled and disabled."""
    self.assertFalse(self.GetPrefsInfo().Prefs(pyauto.kInstantEnabled),
                     msg='Instant is enabled by default.')
    # Enable instant.
    self.SetPrefs(pyauto.kInstantEnabled, True)
    self.assertTrue(self.GetPrefsInfo().Prefs(pyauto.kInstantEnabled),
                    msg='Instant is not enabled.')
    self.SetOmniboxText('google.com')
    self.assertTrue(self.WaitUntil(
        lambda: self.GetInstantInfo().get('current') and not
        self.GetInstantInfo().get('loading')))
    title = self.GetInstantInfo()['title']
    self.assertEqual('Google', title, msg='Instant did not load.')
    # Disable Instant.
    self.SetPrefs(pyauto.kInstantEnabled, False)
    self.assertFalse(self.GetInstantInfo()['enabled'],
                     msg='Instant is not disabled.')


class InstantTest(pyauto.PyUITest):
  """TestCase for Omnibox Instant feature."""

  def setUp(self):
    pyauto.PyUITest.setUp(self)
    self.SetPrefs(pyauto.kInstantEnabled, True)

  def _DoneLoading(self):
    info = self.GetInstantInfo()
    return info.get('current') and not info.get('loading')

  def _DoneLoadingGoogleQuery(self, query):
    """Wait for Omnibox Instant to load Google search result
       and verify location URL contains the specifed query.

       Args:
         query: Value of query parameter.
                E.g., http://www.google.com?q=hi so query is 'hi'.
    """
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    location = self.GetInstantInfo().get('location')
    if location is not None:
      q = cgi.parse_qs(location).get('q')
      if q is not None and query in q:
        return True
    return False

  def testInstantNavigation(self):
    """Test that instant navigates based on omnibox input."""
    self.SetOmniboxText('google.com')
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    location = self.GetInstantInfo()['location']
    self.assertTrue('google.com' in location,
                    msg='No google.com in %s' % location)

    self.SetOmniboxText('google.es')
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    location = self.GetInstantInfo()['location']
    self.assertTrue('google.es' in location,
                    msg='No google.es in %s' % location)

    # Initiate instant search (at default google.com).
    self.SetOmniboxText('chrome instant')
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    location = self.GetInstantInfo()['location']
    self.assertTrue('google.com' in location,
                    msg='No google.com in %s' % location)

  def testInstantCaseSensitivity(self):
    """Verify that Chrome Instant results case insensitive."""
    # Text in lowercase letters.
    self.SetOmniboxText('google')
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    lowercase_instant_info = self.GetInstantInfo()
    # Text in uppercase letters.
    self.SetOmniboxText('GOOGLE')
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    uppercase_instant_info = self.GetInstantInfo()
    # Check lowercase and uppercase text results are same.
    self.assertEquals(lowercase_instant_info, uppercase_instant_info,
        msg='Lowercase and Uppercase instant info doesn\'t match')
    # Text in mixed case letters.
    self.SetOmniboxText('GooGle')
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    mixedcase_instant_info = self.GetInstantInfo()
    # Check mixedcase and uppercase text results are same.
    self.assertEquals(mixedcase_instant_info, uppercase_instant_info,
        msg='Mixedcase and Uppercase instant info doesn\'t match')

  def testInstantWithSearchEngineOtherThanGoogle(self):
    """Verify that Instant is inactive for search engines other than Google."""
    # Check with Yahoo!.
    self.MakeSearchEngineDefault('yahoo.com')
    self.assertFalse(self.GetInstantInfo()['active'],
                     msg='Instant is active for Yahoo!')
    # Check with Bing.
    self.MakeSearchEngineDefault('bing.com')
    self.assertFalse(self.GetInstantInfo()['active'],
                     msg='Instant is active for Bing.')

  def testInstantDisabledInIncognito(self):
    """Test that instant is disabled in Incognito mode."""
    self.RunCommand(pyauto.IDC_NEW_INCOGNITO_WINDOW)
    self.SetOmniboxText('google.com', windex=1)
    self.assertFalse(self.GetInstantInfo()['active'],
                     'Instant enabled in Incognito mode.')

  def testInstantOverlayNotStoredInHistory(self):
    """Test that instant overlay page is not stored in history."""
    url = self.GetFileURLForPath(os.path.join(self.DataDir(), 'title2.html'))
    self.SetOmniboxText(url)
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    history = self.GetHistoryInfo().History()
    self.assertEqual(0, len(history))

  def testInstantDisabledForJavaScript(self):
    """Test that instant is disabled for javascript URLs."""
    self.SetOmniboxText('javascript:')
    self.assertFalse(self.GetInstantInfo()['active'],
                     'Instant enabled for javascript URL.')

  def testInstantDisablesPopupsOnPrefetch(self):
    """Test that instant disables popups when prefetching."""
    file_url = self.GetFileURLForPath(os.path.join(
        self.DataDir(), 'popup_blocker', 'popup-blocked-to-post-blank.html'))
    self.SetOmniboxText(file_url)
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    location = self.GetInstantInfo()['location']
    self.assertTrue(file_url in location,
                    msg='Prefetched page is not %s' % file_url)
    blocked_popups = self.GetBlockedPopupsInfo()
    self.assertEqual(0, len(blocked_popups),
                     msg='Unexpected popup in instant preview.')

  def testInstantLoadsFor100CharsLongQuery(self):
    """Test that instant loads for search query of 100 characters."""
    query = '#' * 100
    self.SetOmniboxText(query)
    self.assertTrue(self.WaitUntil(self._DoneLoadingGoogleQuery, args=[query]))

  def _BringUpInstant(self):
    """Helper function to bring up instant."""
    file_path = os.path.join(os.path.abspath(self.DataDir()),
                             'google', 'google.html')
    self.SetOmniboxText(self.GetFileURLForPath(file_path))
    self.assertTrue(self.WaitUntil(self._DoneLoading))
    self.assertTrue('google.html' in self.GetInstantInfo()['location'],
                    msg='No google.html in %s' %
                    self.GetInstantInfo()['location'])

  def testFindInCanDismissInstant(self):
    """Test that instant preview is dismissed by find-in-page."""
    self._BringUpInstant()
    self.OpenFindInPage()
    self.assertEqual(self.GetActiveTabTitle(), 'about:blank')

  def testNTPCanDismissInstant(self):
    """Test that instant preview is dismissed by adding new tab page."""
    self._BringUpInstant()
    self.AppendTab(pyauto.GURL('chrome://newtab'))
    self.GetBrowserWindow(0).GetTab(1).Close(True)
    self.assertEqual(self.GetActiveTabTitle(), 'about:blank')

  def testExtnPageCanDismissInstant(self):
    """Test that instant preview is dismissed by extension page."""
    self._BringUpInstant()
    self.AppendTab(pyauto.GURL('chrome://extensions'))
    self.GetBrowserWindow(0).GetTab(1).Close(True)
    self.assertEqual(self.GetActiveTabTitle(), 'about:blank')

  def testNewWindowCanDismissInstant(self):
    """Test that instant preview is dismissed by New Window."""
    self._BringUpInstant()
    self.OpenNewBrowserWindow(True)
    self.CloseBrowserWindow(1)
    self.assertEqual(self.GetActiveTabTitle(), 'about:blank')

  def _AssertInstantDoesNotDownloadFile(self, path):
    """Asserts instant does not download the specified file.

       Args:
         path: Path to file.
    """
    self.NavigateToURL('chrome://downloads')
    filepath = self.GetFileURLForDataPath(path)
    self.SetOmniboxText(filepath)
    self.WaitUntilOmniboxQueryDone()
    self.WaitForAllDownloadsToComplete()
    self.assertFalse(self.GetDownloadsInfo().Downloads(),
                     msg='Should not download: %s' % filepath)

  def testInstantDoesNotDownloadZipFile(self):
    """Test that instant does not download zip file."""
    self._AssertInstantDoesNotDownloadFile(os.path.join('zip', 'test.zip'))

  def testInstantDoesNotDownloadPDFFile(self):
    """Test that instant does not download PDF file."""
    self._AssertInstantDoesNotDownloadFile(os.path.join('printing',
                                           'cloud_print_unittest.pdf'))

  def _AssertInstantLoadsFile(self, path):
    """Asserts instant loads the specified file.

       Args:
         path: Path to file.
    """
    filepath = self.GetFileURLForDataPath(path)
    error = 'Failed to load: %s' % filepath
    self.SetOmniboxText(filepath)
    self.assertTrue(self.WaitUntil(self._DoneLoading), msg=error)
    self.assertEqual(self.GetInstantInfo()['location'], filepath, msg=error)

  def testInstantLoadsGIF(self):
    """Test that instant loads GIF file."""
    self._AssertInstantLoadsFile(os.path.join('animate1.gif'))

  def testInstantLoadsJPEG(self):
    """Test that instant loads JPEG file."""
    self._AssertInstantLoadsFile(os.path.join('gpu', 'webgl_teapot',
                                              'bump.jpg'))

  def testInstantLoadsPNG(self):
    """Test that instant loads PNG file."""
    self._AssertInstantLoadsFile(os.path.join('save_page', '1.png'))

  def testInstantLoadsSVG(self):
    """Test that instant loads SVG file."""
    self._AssertInstantLoadsFile(os.path.join('circle.svg'))


if __name__ == '__main__':
  pyauto_functional.Main()
