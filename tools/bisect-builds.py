#!/usr/bin/python
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Snapshot Build Bisect Tool

This script bisects a snapshot archive using binary search. It starts at
a bad revision (it will try to guess HEAD) and asks for a last known-good
revision. It will then binary search across this revision range by downloading,
unzipping, and opening Chromium for you. After testing the specific revision,
it will ask you whether it is good or bad before continuing the search.
"""

# Base URL to download snapshots from.
BUILD_BASE_URL = 'http://build.chromium.org/f/chromium/continuous/'

# The index file that lists all the builds. This lives in BUILD_BASE_URL.
BUILD_INDEX_FILE = 'all_builds.txt'

# The type (platform) of the build archive. This is what's passed in to the
# '-a/--archive' option.
BUILD_ARCHIVE_TYPE = ''

# The location of the builds. Format this with a (date, revision) tuple, which
# can be obtained through ParseIndexLine().
BUILD_ARCHIVE_URL = '/%s/%d/'

# Name of the build archive.
BUILD_ZIP_NAME = ''

# Directory name inside the archive.
BUILD_DIR_NAME = ''

# Name of the executable.
BUILD_EXE_NAME = ''

# URL to the ViewVC commit page.
BUILD_VIEWVC_URL = 'http://src.chromium.org/viewvc/chrome?view=rev&revision=%d'

# Changelogs URL
CHANGELOG_URL = 'http://build.chromium.org/f/chromium/' \
                'perf/dashboard/ui/changelog.html?url=/trunk/src&range=%d:%d'

###############################################################################

import math
import optparse
import os
import pipes
import re
import shutil
import sys
import tempfile
import urllib
import zipfile


def UnzipFilenameToDir(filename, dir):
  """Unzip |filename| to directory |dir|."""
  zf = zipfile.ZipFile(filename)
  # Make base.
  pushd = os.getcwd()
  try:
    if not os.path.isdir(dir):
      os.mkdir(dir)
    os.chdir(dir)
    # Extract files.
    for info in zf.infolist():
      name = info.filename
      if name.endswith('/'):  # dir
        if not os.path.isdir(name):
          os.makedirs(name)
      else:  # file
        dir = os.path.dirname(name)
        if not os.path.isdir(dir):
          os.makedirs(dir)
        out = open(name, 'wb')
        out.write(zf.read(name))
        out.close()
      # Set permissions. Permission info in external_attr is shifted 16 bits.
      os.chmod(name, info.external_attr >> 16L)
    os.chdir(pushd)
  except Exception, e:
    print >>sys.stderr, e
    sys.exit(1)


def SetArchiveVars(archive):
  """Set a bunch of global variables appropriate for the specified archive."""
  global BUILD_ARCHIVE_TYPE
  global BUILD_ZIP_NAME
  global BUILD_DIR_NAME
  global BUILD_EXE_NAME
  global BUILD_BASE_URL

  BUILD_ARCHIVE_TYPE = archive

  if BUILD_ARCHIVE_TYPE in ('linux', 'linux64', 'linux-chromiumos'):
    BUILD_ZIP_NAME = 'chrome-linux.zip'
    BUILD_DIR_NAME = 'chrome-linux'
    BUILD_EXE_NAME = 'chrome'
  elif BUILD_ARCHIVE_TYPE in ('mac'):
    BUILD_ZIP_NAME = 'chrome-mac.zip'
    BUILD_DIR_NAME = 'chrome-mac'
    BUILD_EXE_NAME = 'Chromium.app/Contents/MacOS/Chromium'
  elif BUILD_ARCHIVE_TYPE in ('win'):
    BUILD_ZIP_NAME = 'chrome-win32.zip'
    BUILD_DIR_NAME = 'chrome-win32'
    BUILD_EXE_NAME = 'chrome.exe'


def ParseDirectoryIndex(url):
  """Parses the all_builds.txt index file. The format of this file is:
    mac/2011-02-16/75130
    mac/2011-02-16/75218
    mac/2011-02-16/75226
    mac/2011-02-16/75234
    mac/2011-02-16/75184
  This function will return a list of DATE/REVISION strings for the platform
  specified by BUILD_ARCHIVE_TYPE.
  """
  handle = urllib.urlopen(url)
  dirindex = handle.readlines()
  handle.close()

  # Only return values for the specified platform. Include the trailing slash to
  # not confuse linux and linux64.
  archtype = BUILD_ARCHIVE_TYPE + '/'
  dirindex = filter(lambda l: l.startswith(archtype), dirindex)

  # Remove the newline separator and the platform token.
  dirindex = map(lambda l: l[len(archtype):].strip(), dirindex)
  dirindex.sort()
  return dirindex


def ParseIndexLine(iline):
  """Takes an index line returned by ParseDirectoryIndex() and returns a
  2-tuple of (date, revision). |date| is a string and |revision| is an int."""
  split = iline.split('/')
  assert(len(split) == 2)
  return (split[0], int(split[1]))


def GetRevision(iline):
    """Takes an index line, parses it, and returns the revision."""
    return ParseIndexLine(iline)[1]


def GetRevList(good, bad):
  """Gets the list of revision numbers between |good| and |bad|."""
  # Download the main revlist.
  revlist = ParseDirectoryIndex(BUILD_BASE_URL + BUILD_INDEX_FILE)
  revrange = range(good, bad)
  revlist = filter(lambda r: GetRevision(r) in revrange, revlist)
  revlist.sort()
  return revlist


def TryRevision(iline, profile, args):
  """Downloads revision from |iline|, unzips it, and opens it for the user to
  test. |profile| is the profile to use."""
  # Do this in a temp dir so we don't collide with user files.
  cwd = os.getcwd()
  tempdir = tempfile.mkdtemp(prefix='bisect_tmp')
  os.chdir(tempdir)

  # Download the file.
  download_url = BUILD_BASE_URL + BUILD_ARCHIVE_TYPE + \
      (BUILD_ARCHIVE_URL % ParseIndexLine(iline)) + BUILD_ZIP_NAME
  def _ReportHook(blocknum, blocksize, totalsize):
    size = blocknum * blocksize
    if totalsize == -1:  # Total size not known.
      progress = "Received %d bytes" % size
    else:
      size = min(totalsize, size)
      progress = "Received %d of %d bytes, %.2f%%" % (
          size, totalsize, 100.0 * size / totalsize)
    # Send a \r to let all progress messages use just one line of output.
    sys.stdout.write("\r" + progress)
    sys.stdout.flush()
  try:
    print 'Fetching ' + download_url
    urllib.urlretrieve(download_url, BUILD_ZIP_NAME, _ReportHook)
    print
  except Exception, e:
    print('Could not retrieve the download. Sorry.')
    sys.exit(-1)

  # Unzip the file.
  print 'Unzipping ...'
  UnzipFilenameToDir(BUILD_ZIP_NAME, os.curdir)

  # Tell the system to open the app.
  args = ['--user-data-dir=%s' % profile] + args
  flags = ' '.join(map(pipes.quote, args))
  exe = os.path.join(os.getcwd(), BUILD_DIR_NAME, BUILD_EXE_NAME)
  cmd = '%s %s' % (exe, flags)
  print 'Running %s' % cmd
  os.system(cmd)

  os.chdir(cwd)
  print 'Cleaning temp dir ...'
  try:
    shutil.rmtree(tempdir, True)
  except Exception, e:
    pass


def AskIsGoodBuild(iline):
  """Ask the user whether build from index line |iline| is good or bad."""
  # Loop until we get a response that we can parse.
  while True:
    response = raw_input('\nBuild %d is [(g)ood/(b)ad]: ' % GetRevision(iline))
    if response and response in ('g', 'b'):
      return response == 'g'

def main():
  usage = ('%prog [options] [-- chromium-options]\n'
           'Perform binary search on the snapshot builds.\n'
           '\n'
           'Tip: add "-- --no-first-run" to bypass the first run prompts.')
  parser = optparse.OptionParser(usage=usage)
  # Strangely, the default help output doesn't include the choice list.
  choices = ['mac', 'win', 'linux', 'linux64']
            # linux-chromiumos lacks a continuous archive http://crbug.com/78158
  parser.add_option('-a', '--archive',
                    choices = choices,
                    help = 'The buildbot archive to bisect [%s].' %
                           '|'.join(choices))
  parser.add_option('-b', '--bad', type = 'int',
                    help = 'The bad revision to bisect to.')
  parser.add_option('-g', '--good', type = 'int',
                    help = 'The last known good revision to bisect from.')
  parser.add_option('-p', '--profile', '--user-data-dir', type = 'str',
                    help = 'Profile to use; this will not reset every run. ' +
                    'Defaults to a clean profile.')
  (opts, args) = parser.parse_args()

  if opts.archive is None:
    print 'Error: missing required parameter: --archive'
    print
    parser.print_help()
    return 1

  if opts.bad and opts.good and (opts.good > opts.bad):
    print ('The good revision (%d) must precede the bad revision (%d).\n' %
           (opts.good, opts.bad))
    parser.print_help()
    return 1

  SetArchiveVars(opts.archive)

  # Pick a starting point, try to get HEAD for this.
  if opts.bad:
    bad_rev = opts.bad
  else:
    bad_rev = 0
    try:
      # Location of the latest build revision number
      BUILD_LATEST_URL = '%s/LATEST/REVISION' % (BUILD_BASE_URL)
      nh = urllib.urlopen(BUILD_LATEST_URL)
      latest = int(nh.read())
      nh.close()
      bad_rev = raw_input('Bad revision [HEAD:%d]: ' % latest)
      if (bad_rev == ''):
        bad_rev = latest
      bad_rev = int(bad_rev)
    except Exception, e:
      print('Could not determine latest revision. This could be bad...')
      bad_rev = int(raw_input('Bad revision: '))

  # Find out when we were good.
  if opts.good:
    good_rev = opts.good
  else:
    good_rev = 0
    try:
      good_rev = int(raw_input('Last known good [0]: '))
    except Exception, e:
      pass

  # Get a list of revisions to bisect across.
  revlist = GetRevList(good_rev, bad_rev)
  if len(revlist) < 2:  # Don't have enough builds to bisect
    print 'We don\'t have enough builds to bisect. revlist: %s' % revlist
    sys.exit(1)

  # If we don't have a |good_rev|, set it to be the first revision possible.
  if good_rev == 0:
    good_rev = revlist[0]

  # These are indexes of |revlist|.
  good = 0
  bad = len(revlist) - 1
  last_known_good_rev = revlist[good]

  # Binary search time!
  while good < bad:
    candidates = revlist[good:bad]
    num_poss = len(candidates)
    if num_poss > 10:
      print('%d candidates. %d tries left.' %
          (num_poss, round(math.log(num_poss, 2))))
    else:
      print('Candidates: %s' % map(GetRevision, revlist[good:bad]))

    # Cut the problem in half...
    test = int((bad - good) / 2) + good
    test_rev = revlist[test]

    # Let the user give this rev a spin (in her own profile, if she wants).
    profile = opts.profile
    if not profile:
      profile = 'profile'  # In a temp dir.
    TryRevision(test_rev, profile, args)
    if AskIsGoodBuild(test_rev):
      last_known_good_rev = revlist[good]
      good = test + 1
    else:
      bad = test

  # We're done. Let the user know the results in an official manner.
  bad_revision = GetRevision(revlist[bad])
  print('You are probably looking for build %d.' % bad_revision)
  print('CHANGELOG URL:')
  print(CHANGELOG_URL % (GetRevision(last_known_good_rev), bad_revision))
  print('Built at revision:')
  print(BUILD_VIEWVC_URL % bad_revision)

if __name__ == '__main__':
  sys.exit(main())
