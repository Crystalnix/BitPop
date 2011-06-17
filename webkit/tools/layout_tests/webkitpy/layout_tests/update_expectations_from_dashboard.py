#!/usr/bin/env python
# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to read in updates in JSON form from the layout test dashboard
and apply them to test_expectations.txt.

Usage:
1. Go to http://src.chromium.org/viewvc/chrome/trunk/src/webkit/tools/
       layout_tests/flakiness_dashboard.html#expectationsUpdate=true
2. Copy-paste that JSON into a local file.
3. python update_expectations_from_dashboard.py path/to/local/file
"""

import copy
import logging
import os
import sys
import re
from expectations_line import ExpectationsLine

#
# Find the WebKit python directories and add them to the PYTHONPATH
#
try:
    f = __file__
except NameError:
    f = sys.argv[0]

this_file = os.path.abspath(f)
base_dir = this_file[0:this_file.find('webkit'+ os.sep + 'tools')]
webkitpy_dir = os.path.join(base_dir, 'third_party', 'WebKit', 'Tools',
                            'Scripts')
sys.path.append(webkitpy_dir)

#
# Now import the python packages we need from WebKit
#
import webkitpy.thirdparty.simplejson as simplejson

from webkitpy.layout_tests.layout_package import test_expectations
import webkitpy.layout_tests.port as port

def get_port():
    class Options:
        def __init__(self):
            self.chromium = True

    return port.get(None, Options())

def update_expectations(port, expectations, updates):
    expectations = ExpectationsUpdater(port, expectations, None, 'WIN',
        False, False, True)
    return expectations.update_based_on_json(updates)


class OptionsAndExpectationsHolder(object):
    """Container for a list of options and a list of expectations for a given
    test."""

    def __init__(self, options, expectations):
        self.options = options
        self.expectations = expectations


class BuildInfo(OptionsAndExpectationsHolder):
    """Container for a list of options and expectations for a given test as
    well as a map from build_type (e.g. debug/release) to a list of platforms
    (e.g. ["win", "linux"]).
    """

    def __init__(self, options, expectations, build_info):
        OptionsAndExpectationsHolder.__init__(self, options, expectations)
        self.build_info = build_info


class ExpectationsUpdater(test_expectations.TestExpectationsFile):
    """Class to update test_expectations.txt based on updates in the following
    form:
    {"test1.html": {
      "WIN RELEASE": {"missing": "FAIL TIMEOUT", "extra": "CRASH"}}
      "WIN DEBUG": {"missing": "FAIL TIMEOUT"}}
     "test2.html": ...
    }
    """

    def _get_build_types_and_platforms(self, options):
        """Splits up the options list into three lists: platforms,
        build_types and other_options."""
        platforms = []
        build_types = []
        other_options = []
        for option in options:
            if option in self._port.test_platform_names():
                platforms.append(option)
            elif option in self.BUILD_TYPES:
                build_types.append(option)
            else:
                other_options.append(option)

        if not len(build_types):
            build_types = self.BUILD_TYPES

        if not len(platforms):
            # If there are no platforms specified, use the most generic version
            # of each platform name so we don't have to dedup them later.
            platforms = self._port.test_base_platform_names()

        return (platforms, build_types, other_options)

    def _apply_updates_to_results(self, test, results, update_json,
        expectations, other_options):
        """Applies the updates from the JSON to the existing results in
        test_expectations.
        Args:
          test: The test to update.
          results: The results object to update.
          update_json: The parsed JSON object with the updates.
          expectations: The existing expectatons for this test.
          other_options: The existing modifiers for this test
              excluding platforms and build_types.
        """
        updates = update_json[test]
        for build_info in updates:
            platform, build_type = build_info.lower().split(' ')

            # If the platform/build_type is not currently listed for the test,
            # skip it as this platform/build_type may be listed in another
            # line.
            if platform not in results or build_type not in results[platform]:
                continue

            these_results = results[platform][build_type]
            these_updates = updates[build_info]
            these_expectations = these_results.expectations
            these_options = these_results.options

            self._apply_extra_updates(these_updates, these_options,
                                      these_expectations)
            self._apply_missing_updates(test, these_updates, these_options,
                                        these_expectations)

    def _apply_extra_updates(self, updates, options, expectations):
        """Remove extraneous expectations/options in the updates object to
        the given options/expectations lists.
        """
        if "extra" not in updates:
            return

        items = updates["extra"].lower().split(' ')
        for item in items:
            if item in self.EXPECTATIONS:
                if item in expectations:
                    expectations.remove(item)
            else:
                if item in options:
                    options.remove(item)

    def _apply_missing_updates(self, test, updates, options, expectations):
        """Apply an addition expectations/options in the updates object to
        the given options/expectations lists.
        """
        if "missing" not in updates:
            return

        items = updates["missing"].lower().split(' ')
        for item in items:
            if item == 'other':
                continue

            # Don't add TIMEOUT to SLOW tests. Automating that is too
            # complicated instead, print out tests that need manual attention.
            if ((item == "timeout" and
                 ("slow" in options or "slow" in items)) or
                (item == "slow" and
                 ("timeout" in expectations or "timeout" in items))):
                logging.info("NEEDS MANUAL ATTENTION: %s may need "
                             "to be marked TIMEOUT or SLOW." % test)
            elif item in self.EXPECTATIONS:
                if item not in expectations:
                    expectations.append(item)
                    if ("fail" in expectations and
                        (item == "image+text" or item == "image" or
                         item == "text")):
                        expectations.remove("fail")
            else:
                if item not in options:
                    options.append(item)

    def _append_platform(self, item, build_type, platform):
        """Appends the give build_type and platform to the BuildInfo item.
        """
        build_info = item.build_info
        if build_type not in build_info:
            build_info[build_type] = []
        build_info[build_type].append(platform)

    def _get_updates_deduped_by_matching_options_and_expectations(self,
                                                                  results):
        """Converts the results, which is
        results[platforms][build_type] = OptionsAndExpectationsHolder
        to BuildInfo objects, which dedupes platform/build_types that
        have the same expectations and options.
        """
        updates = []
        for platform in results:
            for build_type in results[platform]:
                options = results[platform][build_type].options
                expectations = results[platform][build_type].expectations

                found_match = False
                for update in updates:
                    if (update.options == options and
                        update.expectations == expectations):
                        self._append_platform(update, build_type, platform)
                        found_match = True
                        break

                if found_match:
                    continue

                update = BuildInfo(options, expectations, {})
                self._append_platform(update, build_type, platform)
                updates.append(update)

        return self._round_up_flaky_updates(updates)

    def _has_majority_build_configurations(self, candidate, candidate2):
        """Returns true if the candidate BuildInfo represents all build
        configurations except the single one listed in candidate2.
        For example, if a test is FAIL TIMEOUT on all bots except WIN-Release,
        where it is just FAIL. Or if a test is FAIL TIMEOUT on MAC-Release,
        Mac-Debug and Linux-Release, but only FAIL on Linux-Debug.
        """
        build_types = self.BUILD_TYPES[:]
        build_info = candidate.build_info
        if "release" not in build_info or "debug" not in build_info:
            return None

        release_set = set(build_info["release"])
        debug_set = set(build_info["debug"])
        if len(release_set - debug_set) is 1:
            full_set = release_set
            partial_set = debug_set
            needed_build_type = "debug"
        elif len(debug_set - release_set) is 1:
            full_set = debug_set
            partial_set = release_set
            needed_build_type = "release"
        else:
            return None

        build_info2 = candidate2.build_info
        if needed_build_type not in build_info2:
            return None

        build_type = None
        for this_build_type in build_info2:
            # Can only work if this candidate has one build_type.
            if build_type:
                return None
            build_type = this_build_type

        if set(build_info2[needed_build_type]) == full_set - partial_set:
            return full_set
        else:
            return None

    def _round_up_flaky_updates(self, updates):
        """Consolidates the updates into one update if 5/6 results are
        flaky and the is a subset of the flaky results 6th just not
        happening to flake or 3/4 results are flaky and the 4th has a
        subset of the flaky results.
        """
        if len(updates) is not 2:
            return updates

        item1, item2 = updates
        candidate = None
        candidate_platforms = self._has_majority_build_configurations(item1,
                                                                      item2)
        if candidate_platforms:
            candidate = item1
        else:
            candidate_platforms = self._has_majority_build_configurations(
                item1, item2)
            if candidate_platforms:
                candidate = item2

        if candidate:
            options1 = set(item1.options)
            options2 = set(item2.options)
            expectations1 = set(item1.expectations)
            if not len(expectations1):
                expectations1.add("pass")
            expectations2 = set(item2.expectations)
            if not len(expectations2):
                expectations2.add("pass")

            options_union = options1 | options2
            expectations_union = expectations1 | expectations2
            # If the options and expectations are equal to their respective
            # unions then we can round up to include the 6th platform.
            if (candidate == item1 and options1 == options_union and
                expectations1 == expectations_union and len(expectations2) or
                candidate == item2 and options2 == options_union and
                expectations2 == expectations_union and len(expectations1)):
                for build_type in self.BUILD_TYPES:
                    candidate.build_info[build_type] = list(
                        candidate_platforms)
                updates = [candidate]
        return updates

    def _merge(self, expectations):
        result = []
        for expectation in expectations:
            if not len(result):
                result.append(expectation)
            elif result[-1].can_merge(expectation):
                result[-1].merge(expectation)
            else:
                result.append(expectation)

        return result

    def update_based_on_json(self, update_json):
        """Updates the expectations based on the update_json, which is of the
        following form:
        {"1.html": {
          "WIN DEBUG": {"extra": "FAIL", "missing", "PASS"},
          "WIN RELEASE": {"extra": "FAIL"}
        }}
        """
        unused = copy.deepcopy(update_json)
        output = []

        comment_lines = []
        removed_test_on_previous_line = False
        lineno = 0
        for line in self._get_iterable_expectations(self._expectations):
            lineno += 1
            test, options, expectations = self.parse_expectations_line(line,
                                                                       lineno)

            # If there are no updates for this test, then output the line
            # unmodified.
            if (test not in update_json):
                if test:
                    self._write_completed_lines(output, comment_lines, line)
                else:
                    if removed_test_on_previous_line:
                        removed_test_on_previous_line = False
                        comment_lines = []
                    comment_lines.append(line)
                continue

            platforms, build_types, other_options = \
                self._get_build_types_and_platforms(options)

            updates = update_json[test]
            has_updates_for_this_line = False
            for build_info in updates:
                platform, build_type = build_info.lower().split(' ')
                if platform in platforms and build_type in build_types:
                    has_updates_for_this_line = True
                    del(unused[test][build_info])

            # If the updates for this test don't apply for the platforms /
            # build-types listed in this line, then output the line unmodified.
            if not has_updates_for_this_line:
                self._write_completed_lines(output, comment_lines, line)
                continue

            results = {}
            for platform in platforms:
                results[platform] = {}
                for build_type in build_types:
                    results[platform][build_type] = \
                        OptionsAndExpectationsHolder(other_options[:],
                                                     expectations[:])

            self._apply_updates_to_results(test, results, update_json,
                                           expectations, other_options)

            deduped_updates = \
                self._get_updates_deduped_by_matching_options_and_expectations(
                    results)
            removed_test_on_previous_line = not self._write_updates(output,
                comment_lines, test, deduped_updates)
        # Append any comment/whitespace lines at the end of test_expectations.
        output.extend(comment_lines)

        new_expectations = []
        for test in unused:
            for build_info in unused[test]:
                if 'missing' in unused[test][build_info]:
                    new_expectations.append(ExpectationsLine("BUG_AUTO %s : %s = %s\n" % (build_info, test, unused[test][build_info]['missing'])))

        new_expectations = self._merge(self._merge(new_expectations))

        if len(new_expectations):
            output += ["\n"]
            output += [str(x) + "\n" for x in new_expectations]

        return "".join(output)

    def _write_updates(self, output, comment_lines, test, updates):
        """Writes the updates to the output.
        Args:
          output: List to append updates to.
          comment_lines: Comments that come before this test that should be
              prepending iff any tests lines are written out.
          test: The test being updating.
          updates: List of BuildInfo instances that represent the final values
              for this test line..
        """
        wrote_any_lines = False
        for update in updates:
            options = update.options
            expectations = update.expectations

            has_meaningful_modifier = False
            for option in options:
                if option in self.MODIFIERS:
                    has_meaningful_modifier = True
                    break

            has_non_pass_expectation = False
            for expectation in expectations:
                if expectation != "pass":
                    has_non_pass_expectation = True
                    break

            # If this test is only left with platform, build_type, bug number
            # and a PASS or no expectation, then we can exclude it from
            # test_expectations.
            if not has_meaningful_modifier and not has_non_pass_expectation:
                continue

            if not has_non_pass_expectation:
                expectations = ["pass"]

            missing_build_types = list(self.BUILD_TYPES)
            sentinal = None
            for build_type in update.build_info:
                if not sentinal:
                    sentinal = update.build_info[build_type]
                # Remove build_types where the list of platforms is equal.
                if sentinal == update.build_info[build_type]:
                    missing_build_types.remove(build_type)

            has_all_build_types = not len(missing_build_types)
            if has_all_build_types:
                self._write_line(output, comment_lines, update, options,
                                 build_type, expectations, test,
                                 has_all_build_types)
                wrote_any_lines = True
            else:
                for build_type in update.build_info:
                    self._write_line(output, comment_lines, update, options,
                                     build_type, expectations, test,
                                     has_all_build_types)
                    wrote_any_lines = True

        return wrote_any_lines

    def _write_completed_lines(self, output, comment_lines, test_line=None):
        """Writes the comment_lines and test_line to the output and empties
        out the comment_lines."""
        output.extend(comment_lines)
        del comment_lines[:]
        if test_line:
            output.append(test_line)

    def _get_platform(self, platforms):
        """Returns the platform to use. If all platforms are listed, then
        return the empty string as that's what we want to list in
        test_expectations.txt.

        Args:
          platforms: List of lower-case platform names.
        """
        platforms.sort()
        if platforms == list(self._port.test_base_platform_names()):
            return ""
        else:
            return " ".join(platforms)

    def _write_line(self, output, comment_lines, update, options, build_type,
        expectations, test, exclude_build_type):
        """Writes a test_expectations.txt line.
        Args:
          output: List to append new lines to.
          comment_lines: List of lines to prepend before the new line.
          update: The update object.
        """
        line = options[:]

        platforms = self._get_platform(update.build_info[build_type])
        if platforms:
            line.append(platforms)
        if not exclude_build_type:
            line.append(build_type)

        line = [x.upper() for x in line]
        expectations = [x.upper() for x in expectations]

        line = line + [":", test, "="] + expectations
        self._write_completed_lines(output, comment_lines,
                                    " ".join(line) + "\n")

def main():
    logging.basicConfig(level=logging.INFO,
        format='%(message)s')

    if len(sys.argv) != 2:
        usage()
        sys.exit(1)
    updates = simplejson.load(open(sys.argv[1]))

    port_obj = get_port()
    path_to_expectations = port_obj.path_to_test_expectations_file()

    old_expectations = open(path_to_expectations).read()
    new_expectations = update_expectations(port_obj, old_expectations, updates)
    open(path_to_expectations, 'w').write(new_expectations)

def usage():
    print "usage: %s file_with_json_expectations_diff" % sys.argv[0]

if '__main__' == __name__:
    main()
