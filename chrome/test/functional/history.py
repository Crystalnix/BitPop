#!/usr/bin/python
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import time

import pyauto_functional  # Must be imported before pyauto
import pyauto
import test_utils


class HistoryTest(pyauto.PyUITest):
  """TestCase for History."""

  def testBasic(self):
    url = self.GetFileURLForDataPath('title2.html')
    title = 'Title Of Awesomeness'
    self.NavigateToURL(url)

    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertEqual(title, history[0]['title'])
    self.assertEqual(url, history[0]['url'])

  def Debug(self):
    """Test method for experimentation.

    This method will not run automatically.
    """
    while True:
      raw_input('Interact with the browser and hit <enter> to dump history.. ')
      print '*' * 20
      history = self.GetHistoryInfo().History()
      import pprint
      pp = pprint.PrettyPrinter(indent=2)
      pp.pprint(history)

  def testHistoryPersists(self):
    """Verify that history persists after session restart."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    url = self.GetFileURLForDataPath('title2.html')
    title = 'Title Of Awesomeness'
    self.NavigateToURL(url)
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertEqual(title, history[0]['title'])
    self.assertEqual(url, history[0]['url'])
    self.RestartBrowser(clear_profile=False)
    # Verify that history persists.
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertEqual(title, history[0]['title'])
    self.assertEqual(url, history[0]['url'])

  def testInvalidURLNoHistory(self):
    """Invalid URLs should not go in history."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    urls = [ self.GetFileURLForPath('some_non-existing_path'),
             self.GetFileURLForPath('another_non-existing_path'),
           ]
    for url in urls:
      if not url.startswith('file://'):
        logging.warn('Using %s. Might depend on how dns failures are handled'
                     'on the network' % url)
      self.NavigateToURL(url)
    self.assertEqual(0, len(self.GetHistoryInfo().History()))

  def testNewTabNoHistory(self):
    """New tab page - chrome://newtab/ should not show up in history."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    self.AppendTab(pyauto.GURL('chrome://newtab/'))
    self.assertEqual(0, len(self.GetHistoryInfo().History()))

  def testIncognitoNoHistory(self):
    """Incognito browsing should not show up in history."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    url = self.GetFileURLForDataPath('title2.html')
    self.RunCommand(pyauto.IDC_NEW_INCOGNITO_WINDOW)
    self.NavigateToURL(url, 1, 0)
    self.assertEqual(0, len(self.GetHistoryInfo().History()))

  def testStarredBookmarkInHistory(self):
    """Verify "starred" URLs in history."""
    url = self.GetFileURLForDataPath('title2.html')
    title = 'Title Of Awesomeness'
    self.NavigateToURL(url)

    # Should not be starred in history yet.
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertFalse(history[0]['starred'])

    # Bookmark the URL.
    bookmarks = self.GetBookmarkModel()
    bar_id = bookmarks.BookmarkBar()['id']
    self.AddBookmarkURL(bar_id, 0, title, url)

    # Should be starred now.
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertTrue(history[0]['starred'])

    # Remove bookmark.
    bookmarks = self.GetBookmarkModel()
    node = bookmarks.FindByTitle(title)
    self.assertTrue(node)
    id = node[0]['id']
    self.RemoveBookmark(id)

    # Should not be starred anymore.
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertFalse(history[0]['starred'])

  def testNavigateMultiTimes(self):
    """Multiple navigations to the same url should have a single history."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    url = self.GetFileURLForDataPath('title2.html')
    for i in range(5):
      self.NavigateToURL(url)
    self.assertEqual(1, len(self.GetHistoryInfo().History()))

  def testMultiTabsWindowsHistory(self):
    """Verify history with multiple windows and tabs."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    urls = []
    for name in ['title2.html', 'title1.html', 'title3.html', 'simple.html']:
       urls.append(self.GetFileURLForDataPath(name))
    num_urls = len(urls)
    assert num_urls == 4, 'Need 4 urls'

    self.NavigateToURL(urls[0], 0, 0)        # window 0, tab 0
    self.OpenNewBrowserWindow(True)
    self.AppendTab(pyauto.GURL(urls[1]), 0)  # window 0, tab 1
    self.AppendTab(pyauto.GURL(urls[2]), 1)  # window 1
    self.AppendTab(pyauto.GURL(urls[3]), 1)  # window 1

    history = self.GetHistoryInfo().History()
    self.assertEqual(num_urls, len(history))
    # The history should be ordered most recent first.
    for i in range(num_urls):
      self.assertEqual(urls[-1 - i], history[i]['url'])

  def testDownloadNoHistory(self):
    """Downloaded URLs should not show up in history."""
    zip_file = 'a_zip_file.zip'
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    test_utils.DownloadFileFromDownloadsDataDir(self, zip_file)
    test_utils.RemoveDownloadedTestFile(self, zip_file)
    # We shouldn't have any history
    history = self.GetHistoryInfo().History()
    self.assertEqual(0, len(history))

  def testRedirectHistory(self):
    """HTTP meta-refresh redirects should have separate history entries."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    file_url = self.GetFileURLForDataPath('History', 'redirector.html')
    landing_url = self.GetFileURLForDataPath('History', 'landing.html')
    tab = self.GetBrowserWindow(0).GetTab(0)
    tab.NavigateToURLBlockUntilNavigationsComplete(pyauto.GURL(file_url), 2)
    self.assertEqual(landing_url, self.GetActiveTabURL().spec())
    # We should have two history items
    history = self.GetHistoryInfo().History()
    self.assertEqual(2, len(history))
    self.assertEqual(landing_url, history[0]['url'])

  def testForge(self):
    """Brief test of forging history items.

    Note the history system can tweak values (e.g. lower-case a URL or
    append an '/' on it) so be careful with exact comparison.
    """
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    # Minimal interface
    self.AddHistoryItem({'url': 'http://ZOINKS'})
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertTrue('zoinks' in history[0]['url'])  # yes it gets lower-cased.
    # Python's time might be slightly off (~10 ms) from Chrome's time (on win).
    # time.time() on win counts in 1ms steps whereas it's 1us on linux.
    # So give the new history item some time separation, so that we can rely
    # on the history ordering.
    def _GetTimeLaterThan(tm):
      y = time.time()
      if y - tm < 0.5:  # 0.5s should be an acceptable separation
        return 0.5 + y
    new_time = _GetTimeLaterThan(history[0]['time'])
    # Full interface (specify both title and url)
    self.AddHistoryItem({'title': 'Google',
                         'url': 'http://www.google.com',
                         'time': new_time})
    # Expect a second item
    history = self.GetHistoryInfo().History()
    self.assertEqual(2, len(history))
    # And make sure our forged item is there.
    self.assertEqual('Google', history[0]['title'])
    self.assertTrue('google.com' in history[0]['url'])
    self.assertTrue(abs(new_time - history[0]['time']) < 1.0)

  def testHttpsHistory(self):
    """Verify a site using https protocol shows up within history."""
    https_url = 'https://encrypted.google.com/'
    url_title = 'Google'
    self.NavigateToURL(https_url)
    history = self.GetHistoryInfo().History()
    self.assertEqual(len(history), 1)
    self.assertEqual(url_title, history[0]['title'])
    self.assertEqual(https_url, history[0]['url'])

  def testFtpHistory(self):
    """Verify a site using ftp protocol shows up within history."""
    ftp_url = 'ftp://ftp.kernel.org/'
    ftp_title = 'Index of /'
    self.NavigateToURL(ftp_url)
    history = self.GetHistoryInfo().History()
    self.assertEqual(len(history), 1)
    self.assertEqual(ftp_title, history[0]['title'])
    self.assertEqual(ftp_url, history[0]['url'])


if __name__ == '__main__':
  pyauto_functional.Main()
