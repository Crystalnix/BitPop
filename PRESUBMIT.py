# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Top-level presubmit script for Chromium.

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details about the presubmit API built into gcl.
"""


import re
import sys


_EXCLUDED_PATHS = (
    r"^breakpad[\\\/].*",
    r"^native_client_sdk[\\\/].*",
    r"^net[\\\/]tools[\\\/]spdyshark[\\\/].*",
    r"^skia[\\\/].*",
    r"^v8[\\\/].*",
    r".*MakeFile$",
    r".+_autogen\.h$",
)


_TEST_ONLY_WARNING = (
    'You might be calling functions intended only for testing from\n'
    'production code.  It is OK to ignore this warning if you know what\n'
    'you are doing, as the heuristics used to detect the situation are\n'
    'not perfect.  The commit queue will not block on this warning.\n'
    'Email joi@chromium.org if you have questions.')


_BANNED_OBJC_FUNCTIONS = (
    (
      'addTrackingRect:',
      (
       'The use of -[NSView addTrackingRect:owner:userData:assumeInside:] is'
       'prohibited. Please use CrTrackingArea instead.',
       'http://dev.chromium.org/developers/coding-style/cocoa-dos-and-donts',
      ),
      False,
    ),
    (
      'NSTrackingArea',
      (
       'The use of NSTrackingAreas is prohibited. Please use CrTrackingArea',
       'instead.',
       'http://dev.chromium.org/developers/coding-style/cocoa-dos-and-donts',
      ),
      False,
    ),
    (
      'convertPointFromBase:',
      (
       'The use of -[NSView convertPointFromBase:] is almost certainly wrong.',
       'Please use |convertPoint:(point) fromView:nil| instead.',
       'http://dev.chromium.org/developers/coding-style/cocoa-dos-and-donts',
      ),
      True,
    ),
    (
      'convertPointToBase:',
      (
       'The use of -[NSView convertPointToBase:] is almost certainly wrong.',
       'Please use |convertPoint:(point) toView:nil| instead.',
       'http://dev.chromium.org/developers/coding-style/cocoa-dos-and-donts',
      ),
      True,
    ),
    (
      'convertRectFromBase:',
      (
       'The use of -[NSView convertRectFromBase:] is almost certainly wrong.',
       'Please use |convertRect:(point) fromView:nil| instead.',
       'http://dev.chromium.org/developers/coding-style/cocoa-dos-and-donts',
      ),
      True,
    ),
    (
      'convertRectToBase:',
      (
       'The use of -[NSView convertRectToBase:] is almost certainly wrong.',
       'Please use |convertRect:(point) toView:nil| instead.',
       'http://dev.chromium.org/developers/coding-style/cocoa-dos-and-donts',
      ),
      True,
    ),
    (
      'convertSizeFromBase:',
      (
       'The use of -[NSView convertSizeFromBase:] is almost certainly wrong.',
       'Please use |convertSize:(point) fromView:nil| instead.',
       'http://dev.chromium.org/developers/coding-style/cocoa-dos-and-donts',
      ),
      True,
    ),
    (
      'convertSizeToBase:',
      (
       'The use of -[NSView convertSizeToBase:] is almost certainly wrong.',
       'Please use |convertSize:(point) toView:nil| instead.',
       'http://dev.chromium.org/developers/coding-style/cocoa-dos-and-donts',
      ),
      True,
    ),
)


_BANNED_CPP_FUNCTIONS = (
    # Make sure that gtest's FRIEND_TEST() macro is not used; the
    # FRIEND_TEST_ALL_PREFIXES() macro from base/gtest_prod_util.h should be
    # used instead since that allows for FLAKY_, FAILS_ and DISABLED_ prefixes.
    (
      'FRIEND_TEST(',
      (
       'Chromium code should not use gtest\'s FRIEND_TEST() macro. Include',
       'base/gtest_prod_util.h and use FRIEND_TEST_ALL_PREFIXES() instead.',
      ),
      False,
    ),
    (
      'ScopedAllowIO',
      (
       'New code should not use ScopedAllowIO. Post a task to the blocking',
       'pool or the FILE thread instead.',
      ),
      True,
    ),
    (
      'FilePathWatcher::Delegate',
      (
       'New code should not use FilePathWatcher::Delegate. Use the callback',
       'interface instead.',
      ),
      False,
    ),
    (
      'browser::FindLastActiveWithProfile',
      (
       'This function is deprecated and we\'re working on removing it. Pass',
       'more context to get a Browser*, like a WebContents, window, or session',
       'id. Talk to ben@ or jam@ for more information.',
      ),
      True,
    ),
    (
      'browser::FindBrowserWithProfile',
      (
       'This function is deprecated and we\'re working on removing it. Pass',
       'more context to get a Browser*, like a WebContents, window, or session',
       'id. Talk to ben@ or jam@ for more information.',
      ),
      True,
    ),
    (
      'browser::FindAnyBrowser',
      (
       'This function is deprecated and we\'re working on removing it. Pass',
       'more context to get a Browser*, like a WebContents, window, or session',
       'id. Talk to ben@ or jam@ for more information.',
      ),
      True,
    ),
    (
      'browser::FindOrCreateTabbedBrowser',
      (
       'This function is deprecated and we\'re working on removing it. Pass',
       'more context to get a Browser*, like a WebContents, window, or session',
       'id. Talk to ben@ or jam@ for more information.',
      ),
      True,
    ),
    (
      'browser::FindTabbedBrowser',
      (
       'This function is deprecated and we\'re working on removing it. Pass',
       'more context to get a Browser*, like a WebContents, window, or session',
       'id. Talk to ben@ or jam@ for more information.',
      ),
      True,
    ),
)



def _CheckNoProductionCodeUsingTestOnlyFunctions(input_api, output_api):
  """Attempts to prevent use of functions intended only for testing in
  non-testing code. For now this is just a best-effort implementation
  that ignores header files and may have some false positives. A
  better implementation would probably need a proper C++ parser.
  """
  # We only scan .cc files and the like, as the declaration of
  # for-testing functions in header files are hard to distinguish from
  # calls to such functions without a proper C++ parser.
  platform_specifiers = r'(_(android|chromeos|gtk|mac|posix|win))?'
  source_extensions = r'\.(cc|cpp|cxx|mm)$'
  file_inclusion_pattern = r'.+%s' % source_extensions
  file_exclusion_patterns = (
      r'.*[/\\](test_|mock_).+%s' % source_extensions,
      r'.+_test_(base|support|util)%s' % source_extensions,
      r'.+_(api|browser|perf|unit|ui)?test%s%s' % (platform_specifiers,
                                                   source_extensions),
      r'.+profile_sync_service_harness%s' % source_extensions,
      )
  path_exclusion_patterns = (
      r'.*[/\\](test|tool(s)?)[/\\].*',
      # At request of folks maintaining this folder.
      r'chrome[/\\]browser[/\\]automation[/\\].*',
      )

  base_function_pattern = r'ForTest(ing)?|for_test(ing)?'
  inclusion_pattern = input_api.re.compile(r'(%s)\s*\(' % base_function_pattern)
  exclusion_pattern = input_api.re.compile(
    r'::[A-Za-z0-9_]+(%s)|(%s)[^;]+\{' % (
      base_function_pattern, base_function_pattern))

  def FilterFile(affected_file):
    black_list = (file_exclusion_patterns + path_exclusion_patterns +
                  _EXCLUDED_PATHS + input_api.DEFAULT_BLACK_LIST)
    return input_api.FilterSourceFile(
      affected_file,
      white_list=(file_inclusion_pattern, ),
      black_list=black_list)

  problems = []
  for f in input_api.AffectedSourceFiles(FilterFile):
    local_path = f.LocalPath()
    lines = input_api.ReadFile(f).splitlines()
    line_number = 0
    for line in lines:
      if (inclusion_pattern.search(line) and
          not exclusion_pattern.search(line)):
        problems.append(
          '%s:%d\n    %s' % (local_path, line_number, line.strip()))
      line_number += 1

  if problems:
    if not input_api.is_committing:
      return [output_api.PresubmitPromptWarning(_TEST_ONLY_WARNING, problems)]
    else:
      # We don't warn on commit, to avoid stopping commits going through CQ.
      return [output_api.PresubmitNotifyResult(_TEST_ONLY_WARNING, problems)]
  else:
    return []


def _CheckNoIOStreamInHeaders(input_api, output_api):
  """Checks to make sure no .h files include <iostream>."""
  files = []
  pattern = input_api.re.compile(r'^#include\s*<iostream>',
                                 input_api.re.MULTILINE)
  for f in input_api.AffectedSourceFiles(input_api.FilterSourceFile):
    if not f.LocalPath().endswith('.h'):
      continue
    contents = input_api.ReadFile(f)
    if pattern.search(contents):
      files.append(f)

  if len(files):
    return [ output_api.PresubmitError(
        'Do not #include <iostream> in header files, since it inserts static '
        'initialization into every file including the header. Instead, '
        '#include <ostream>. See http://crbug.com/94794',
        files) ]
  return []


def _CheckNoUNIT_TESTInSourceFiles(input_api, output_api):
  """Checks to make sure no source files use UNIT_TEST"""
  problems = []
  for f in input_api.AffectedFiles():
    if (not f.LocalPath().endswith(('.cc', '.mm'))):
      continue

    for line_num, line in f.ChangedContents():
      if 'UNIT_TEST' in line:
        problems.append('    %s:%d' % (f.LocalPath(), line_num))

  if not problems:
    return []
  return [output_api.PresubmitPromptWarning('UNIT_TEST is only for headers.\n' +
      '\n'.join(problems))]


def _CheckNoNewWStrings(input_api, output_api):
  """Checks to make sure we don't introduce use of wstrings."""
  problems = []
  for f in input_api.AffectedFiles():
    if (not f.LocalPath().endswith(('.cc', '.h')) or
        f.LocalPath().endswith('test.cc')):
      continue

    allowWString = False
    for line_num, line in f.ChangedContents():
      if 'presubmit: allow wstring' in line:
        allowWString = True
      elif not allowWString and 'wstring' in line:
        problems.append('    %s:%d' % (f.LocalPath(), line_num))
        allowWString = False
      else:
        allowWString = False

  if not problems:
    return []
  return [output_api.PresubmitPromptWarning('New code should not use wstrings.'
      '  If you are calling a cross-platform API that accepts a wstring, '
      'fix the API.\n' +
      '\n'.join(problems))]


def _CheckNoDEPSGIT(input_api, output_api):
  """Make sure .DEPS.git is never modified manually."""
  if any(f.LocalPath().endswith('.DEPS.git') for f in
      input_api.AffectedFiles()):
    return [output_api.PresubmitError(
      'Never commit changes to .DEPS.git. This file is maintained by an\n'
      'automated system based on what\'s in DEPS and your changes will be\n'
      'overwritten.\n'
      'See http://code.google.com/p/chromium/wiki/UsingNewGit#Rolling_DEPS\n'
      'for more information')]
  return []


def _CheckNoBannedFunctions(input_api, output_api):
  """Make sure that banned functions are not used."""
  warnings = []
  errors = []

  file_filter = lambda f: f.LocalPath().endswith(('.mm', '.m', '.h'))
  for f in input_api.AffectedFiles(file_filter=file_filter):
    for line_num, line in f.ChangedContents():
      for func_name, message, error in _BANNED_OBJC_FUNCTIONS:
        if func_name in line:
          problems = warnings;
          if error:
            problems = errors;
          problems.append('    %s:%d:' % (f.LocalPath(), line_num))
          for message_line in message:
            problems.append('      %s' % message_line)

  file_filter = lambda f: f.LocalPath().endswith(('.cc', '.mm', '.h'))
  for f in input_api.AffectedFiles(file_filter=file_filter):
    for line_num, line in f.ChangedContents():
      for func_name, message, error in _BANNED_CPP_FUNCTIONS:
        if func_name in line:
          problems = warnings;
          if error:
            problems = errors;
          problems.append('    %s:%d:' % (f.LocalPath(), line_num))
          for message_line in message:
            problems.append('      %s' % message_line)

  result = []
  if (warnings):
    result.append(output_api.PresubmitPromptWarning(
        'Banned functions were used.\n' + '\n'.join(warnings)))
  if (errors):
    result.append(output_api.PresubmitError(
        'Banned functions were used.\n' + '\n'.join(errors)))
  return result


def _CheckNoPragmaOnce(input_api, output_api):
  """Make sure that banned functions are not used."""
  files = []
  pattern = input_api.re.compile(r'^#pragma\s+once',
                                 input_api.re.MULTILINE)
  for f in input_api.AffectedSourceFiles(input_api.FilterSourceFile):
    if not f.LocalPath().endswith('.h'):
      continue
    contents = input_api.ReadFile(f)
    if pattern.search(contents):
      files.append(f)

  if files:
    return [output_api.PresubmitError(
        'Do not use #pragma once in header files.\n'
        'See http://www.chromium.org/developers/coding-style#TOC-File-headers',
        files)]
  return []


def _CheckUnwantedDependencies(input_api, output_api):
  """Runs checkdeps on #include statements added in this
  change. Breaking - rules is an error, breaking ! rules is a
  warning.
  """
  # We need to wait until we have an input_api object and use this
  # roundabout construct to import checkdeps because this file is
  # eval-ed and thus doesn't have __file__.
  original_sys_path = sys.path
  try:
    sys.path = sys.path + [input_api.os_path.join(
        input_api.PresubmitLocalPath(), 'tools', 'checkdeps')]
    import checkdeps
    from cpp_checker import CppChecker
    from rules import Rule
  finally:
    # Restore sys.path to what it was before.
    sys.path = original_sys_path

  added_includes = []
  for f in input_api.AffectedFiles():
    if not CppChecker.IsCppFile(f.LocalPath()):
      continue

    changed_lines = [line for line_num, line in f.ChangedContents()]
    added_includes.append([f.LocalPath(), changed_lines])

  deps_checker = checkdeps.DepsChecker()

  error_descriptions = []
  warning_descriptions = []
  for path, rule_type, rule_description in deps_checker.CheckAddedCppIncludes(
      added_includes):
    description_with_path = '%s\n    %s' % (path, rule_description)
    if rule_type == Rule.DISALLOW:
      error_descriptions.append(description_with_path)
    else:
      warning_descriptions.append(description_with_path)

  results = []
  if error_descriptions:
    results.append(output_api.PresubmitError(
        'You added one or more #includes that violate checkdeps rules.',
        error_descriptions))
  if warning_descriptions:
    results.append(output_api.PresubmitPromptWarning(
        'You added one or more #includes of files that are temporarily\n'
        'allowed but being removed. Can you avoid introducing the\n'
        '#include? See relevant DEPS file(s) for details and contacts.',
        warning_descriptions))
  return results


def _CommonChecks(input_api, output_api):
  """Checks common to both upload and commit."""
  results = []
  results.extend(input_api.canned_checks.PanProjectChecks(
      input_api, output_api, excluded_paths=_EXCLUDED_PATHS))
  results.extend(_CheckAuthorizedAuthor(input_api, output_api))
  results.extend(
    _CheckNoProductionCodeUsingTestOnlyFunctions(input_api, output_api))
  results.extend(_CheckNoIOStreamInHeaders(input_api, output_api))
  results.extend(_CheckNoUNIT_TESTInSourceFiles(input_api, output_api))
  results.extend(_CheckNoNewWStrings(input_api, output_api))
  results.extend(_CheckNoDEPSGIT(input_api, output_api))
  results.extend(_CheckNoBannedFunctions(input_api, output_api))
  results.extend(_CheckNoPragmaOnce(input_api, output_api))
  results.extend(_CheckUnwantedDependencies(input_api, output_api))
  return results


def _CheckSubversionConfig(input_api, output_api):
  """Verifies the subversion config file is correctly setup.

  Checks that autoprops are enabled, returns an error otherwise.
  """
  join = input_api.os_path.join
  if input_api.platform == 'win32':
    appdata = input_api.environ.get('APPDATA', '')
    if not appdata:
      return [output_api.PresubmitError('%APPDATA% is not configured.')]
    path = join(appdata, 'Subversion', 'config')
  else:
    home = input_api.environ.get('HOME', '')
    if not home:
      return [output_api.PresubmitError('$HOME is not configured.')]
    path = join(home, '.subversion', 'config')

  error_msg = (
      'Please look at http://dev.chromium.org/developers/coding-style to\n'
      'configure your subversion configuration file. This enables automatic\n'
      'properties to simplify the project maintenance.\n'
      'Pro-tip: just download and install\n'
      'http://src.chromium.org/viewvc/chrome/trunk/tools/build/slave/config\n')

  try:
    lines = open(path, 'r').read().splitlines()
    # Make sure auto-props is enabled and check for 2 Chromium standard
    # auto-prop.
    if (not '*.cc = svn:eol-style=LF' in lines or
        not '*.pdf = svn:mime-type=application/pdf' in lines or
        not 'enable-auto-props = yes' in lines):
      return [
          output_api.PresubmitNotifyResult(
              'It looks like you have not configured your subversion config '
              'file or it is not up-to-date.\n' + error_msg)
      ]
  except (OSError, IOError):
    return [
        output_api.PresubmitNotifyResult(
            'Can\'t find your subversion config file.\n' + error_msg)
    ]
  return []


def _CheckAuthorizedAuthor(input_api, output_api):
  """For non-googler/chromites committers, verify the author's email address is
  in AUTHORS.
  """
  # TODO(maruel): Add it to input_api?
  import fnmatch

  author = input_api.change.author_email
  if not author:
    input_api.logging.info('No author, skipping AUTHOR check')
    return []
  authors_path = input_api.os_path.join(
      input_api.PresubmitLocalPath(), 'AUTHORS')
  valid_authors = (
      input_api.re.match(r'[^#]+\s+\<(.+?)\>\s*$', line)
      for line in open(authors_path))
  valid_authors = [item.group(1).lower() for item in valid_authors if item]
  if input_api.verbose:
    print 'Valid authors are %s' % ', '.join(valid_authors)
  if not any(fnmatch.fnmatch(author.lower(), valid) for valid in valid_authors):
    return [output_api.PresubmitPromptWarning(
        ('%s is not in AUTHORS file. If you are a new contributor, please visit'
        '\n'
        'http://www.chromium.org/developers/contributing-code and read the '
        '"Legal" section\n'
        'If you are a chromite, verify the contributor signed the CLA.') %
        author)]
  return []


def CheckChangeOnUpload(input_api, output_api):
  results = []
  results.extend(_CommonChecks(input_api, output_api))
  return results


def CheckChangeOnCommit(input_api, output_api):
  results = []
  results.extend(_CommonChecks(input_api, output_api))
  # TODO(thestig) temporarily disabled, doesn't work in third_party/
  #results.extend(input_api.canned_checks.CheckSvnModifiedDirectories(
  #    input_api, output_api, sources))
  # Make sure the tree is 'open'.
  results.extend(input_api.canned_checks.CheckTreeIsOpen(
      input_api,
      output_api,
      json_url='http://chromium-status.appspot.com/current?format=json'))
  results.extend(input_api.canned_checks.CheckRietveldTryJobExecution(input_api,
      output_api, 'http://codereview.chromium.org',
      ('win_rel', 'linux_rel', 'mac_rel, win:compile'),
      'tryserver@chromium.org'))

  results.extend(input_api.canned_checks.CheckChangeHasBugField(
      input_api, output_api))
  results.extend(input_api.canned_checks.CheckChangeHasDescription(
      input_api, output_api))
  results.extend(_CheckSubversionConfig(input_api, output_api))
  return results


def GetPreferredTrySlaves(project, change):
  files = change.LocalPaths()

  if not files:
    return []

  if all(re.search('\.(m|mm)$|[/_]mac[/_.]', f) for f in files):
    return ['mac_rel']
  if all(re.search('[/_]win[/_.]', f) for f in files):
    return ['win_rel']
  if all(re.search('[/_]android[/_.]', f) for f in files):
    return ['android']

  trybots = ['win_rel', 'linux_rel', 'mac_rel', 'linux_clang:compile',
             'android']

  # Match things like path/aura/file.cc and path/file_aura.cc.
  # Same for chromeos.
  if any(re.search('[/_](aura|chromeos)', f) for f in files):
    trybots += ['linux_chromeos', 'linux_chromeos_clang:compile']

  return trybots
