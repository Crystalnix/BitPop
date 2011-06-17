#!/usr/bin/python
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This module is a simple qa tool that installs extensions and tests whether the
browser crashes while visiting a list of urls.

Usage: python extensions.py -v

Note: This assumes that there is a directory of extensions called
'extensions-tool' and that there is a file of newline-separated urls to visit
called 'urls.txt' in the data directory.
"""

import glob
import logging
import os
import sys

import pyauto_functional # must be imported before pyauto
import pyauto


class ExtensionsTest(pyauto.PyUITest):
  """Test of extensions."""

  def Debug(self):
    """Test method for experimentation.

    This method is not run automatically.
    """
    while True:
      raw_input('Interact with the browser and hit <enter> to dump history.. ')
      print '*' * 20
      extensions = self.GetExtensionsInfo()
      import pprint
      pp = pprint.PrettyPrinter(indent=2)
      pp.pprint(extensions)

  def _GetInstalledExtensionIds(self):
    return [extension['id'] for extension in self.GetExtensionsInfo()]

  def _ReturnCrashingExtensions(self, extensions, group_size, top_urls):
    """Install the given extensions in groups of group_size and return the
       group of extensions that crashes (if any).

    Args:
      extensions: A list of extensions to install.
      group_size: The number of extensions to install at one time.
      top_urls: The list of top urls to visit.

    Returns:
      The extensions in the crashing group or None if there is no crash.
    """
    curr_extension = 0
    num_extensions = len(extensions)
    self.RestartBrowser()
    orig_extension_ids = self._GetInstalledExtensionIds()

    while curr_extension < num_extensions:
      logging.debug('New group of %d extensions.' % group_size)
      group_end = curr_extension + group_size
      for extension in extensions[curr_extension:group_end]:
        logging.debug('Installing extension: %s' % extension)
        self.InstallExtension(pyauto.FilePath(extension), False)

      for url in top_urls:
        self.NavigateToURL(url)

      def _LogAndReturnCrashing():
        crashing_extensions = extensions[curr_extension:group_end]
        logging.debug('Crashing extensions: %s' % crashing_extensions)
        return crashing_extensions

      # If the browser has crashed, return the extensions in the failing group.
      try:
        num_browser_windows = self.GetBrowserWindowCount()
      except:
        return _LogAndReturnCrashing()
      else:
        if not num_browser_windows:
          return _LogAndReturnCrashing()
        else:
          # Uninstall all extensions that aren't installed by default.
          new_extension_ids = [id for id in self._GetInstalledExtensionIds()
                               if id not in orig_extension_ids]
          for extension_id in new_extension_ids:
            self.UninstallExtensionById(extension_id)

      curr_extension = group_end

    # None of the extensions crashed.
    return None

  def testExtensionCrashes(self):
    """Add top extensions; confirm browser stays up when visiting top urls"""
    # TODO: provide a way in pyauto to pass args to a test - take these as args
    extensions_dir = os.path.join(self.DataDir(), 'extensions-tool')
    urls_file = os.path.join(self.DataDir(), 'urls.txt')

    assert os.path.exists(extensions_dir), \
           'The dir "%s" must exist' % os.path.abspath(extensions_dir)
    assert os.path.exists(urls_file), \
           'The file "%s" must exist' % os.path.abspath(urls_file)

    num_urls_to_visit = 100
    extensions_group_size = 20

    top_urls = [l.rstrip() for l in
                open(urls_file).readlines()[:num_urls_to_visit]]

    failed_extensions = glob.glob(os.path.join(extensions_dir, '*.crx'))
    group_size = extensions_group_size

    while(group_size and failed_extensions):
      failed_extensions = self._ReturnCrashingExtensions(
          failed_extensions, group_size, top_urls)
      group_size = group_size // 2

    self.assertFalse(failed_extensions,
                     'Extension(s) in failing group: %s' % failed_extensions)

  def testGetExtensionPermissions(self):
    """Ensures we can retrieve the host/api permissions for an extension.

    This test assumes that the 'Bookmark Manager' extension exists in a fresh
    profile.
    """
    extensions_info = self.GetExtensionsInfo()
    bm_exts = [x for x in extensions_info if x['name'] == 'Bookmark Manager']
    self.assertTrue(bm_exts, msg='Could not find info for the Bookmark '
                                 'Manager extension.')
    ext = bm_exts[0]

    permissions_host = ext['host_permissions']
    self.assertTrue(len(permissions_host) == 2 and
                    'chrome://favicon/*' in permissions_host and
                    'chrome://resources/*' in permissions_host,
                    msg='Unexpected host permissions information.')

    permissions_api = ext['api_permissions']
    self.assertTrue(len(permissions_api) == 3 and
                    'bookmarks' in permissions_api and
                    'experimental' in permissions_api and
                    'tabs' in permissions_api,
                    msg='Unexpected host permissions information.')

  def testUpdateExtensionsNow(self):
    """Ensures that the "Update Extensions Now" functionality works properly."""
    # To verify that the "Update Extensions Now" functionality works, we first
    # ensure that the only app present in the NTP is the "Web Store" app.  Then,
    # we update the extensions, and verify that afterward, the two promo apps
    # are installed in addition to the "Web Store" app.
    app_info = self.GetNTPApps()
    self.assertTrue(len(app_info) == 1 and 'name' in app_info[0] and
                    app_info[0]['name'] == 'Chrome Web Store',
                    msg='Web Store not present in NTP in fresh profile.')
    self.UpdateExtensionsNow()
    app_info = self.GetNTPApps()
    self.assertEqual(len(app_info), 3,
                     msg='Error verifying promo apps after extension update.')


if __name__ == '__main__':
  pyauto_functional.Main()
