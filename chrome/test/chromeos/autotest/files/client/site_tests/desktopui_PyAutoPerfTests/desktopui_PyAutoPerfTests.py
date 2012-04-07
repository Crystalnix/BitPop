# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import optparse
import os
import pwd
import re
import shutil
import subprocess

from autotest_lib.client.bin import utils
from autotest_lib.client.common_lib import error
from autotest_lib.client.cros import constants, chrome_test, cros_ui, login


class desktopui_PyAutoPerfTests(chrome_test.ChromeTestBase):
    """Wrapper for running Chrome's PyAuto-based performance tests.

    Performs all setup and fires off the CHROMEOS_PERF PyAuto suite.
    """
    _PERF_MARKER_PRE = '_PERF_PRE_'
    _PERF_MARKER_POST = '_PERF_POST_'
    _DEFAULT_NUM_ITERATIONS = 10  # Keep synced with perf.py.

    version = 1

    def initialize(self):
        chrome_test.ChromeTestBase.initialize(self)
        assert os.geteuid() == 0, 'Need superuser privileges'

        deps_dir = os.path.join(self.autodir, 'deps')
        subprocess.check_call(['chown', '-R', 'chronos', self.cr_source_dir])

        # Setup suid python binary which can enable Chrome testing interface.
        suid_python = os.path.join(self.test_binary_dir, 'suid-python')
        py_path = subprocess.Popen(['which', 'python'],
                                   stdout=subprocess.PIPE).communicate()[0]
        py_path = py_path.strip()
        assert os.path.exists(py_path), 'Could not find python'
        if os.path.islink(py_path):
            linkto = os.readlink(py_path)
            py_path = os.path.join(os.path.dirname(py_path), linkto)
        shutil.copy(py_path, suid_python)
        os.chown(suid_python, 0, 0)
        os.chmod(suid_python, 04755)

        # User chronos should own the current directory.
        chronos_id = pwd.getpwnam('chronos')
        os.chown(os.getcwd(), chronos_id.pw_uid, chronos_id.pw_gid)

        # Make sure Chrome minidumps are written locally.
        minidumps_file = '/mnt/stateful_partition/etc/enable_chromium_minidumps'
        if not os.path.exists(minidumps_file):
            open(minidumps_file, 'w').close()
            # Allow browser restart by its babysitter (session_manager).
            if os.path.exists(constants.DISABLE_BROWSER_RESTART_MAGIC_FILE):
                os.remove(constants.DISABLE_BROWSER_RESTART_MAGIC_FILE)
            cros_ui.nuke()
        assert os.path.exists(minidumps_file)

        # Setup /tmp/disable_chrome_restart.
        # Disallow further browser restart by its babysitter.
        if not os.path.exists(constants.DISABLE_BROWSER_RESTART_MAGIC_FILE):
            open(constants.DISABLE_BROWSER_RESTART_MAGIC_FILE, 'w').close()
        assert os.path.exists(constants.DISABLE_BROWSER_RESTART_MAGIC_FILE)

    def parse_args(self, args):
        """Parses input arguments to this autotest."""
        parser = optparse.OptionParser()
        parser.add_option('--iterations', dest='num_iterations', type='int',
                          default=self._DEFAULT_NUM_ITERATIONS,
                          help='Number of iterations for perf measurements. '
                               'Defaults to %default iterations.')
        parser.add_option('--max-timeouts', dest='max_timeouts', type='int',
                          default=0,
                          help='Maximum number of automation timeouts to '
                               'ignore before failing the test. Defaults to '
                               'the value given in perf.py.')
        parser.add_option('--suite', dest='suite', type='string',
                          default='CHROMEOS_PERF',
                          help='Name of the suite to run, as specified in the '
                               '"PYAUTO_TESTS" suite file. Defaults to '
                               '%default, which runs all perf tests.')
        # Preprocess the args to remove quotes before/after each one if they
        # exist.  This is necessary because arguments passed via
        # run_remote_tests.sh may be individually quoted, and those quotes must
        # be stripped before they are parsed.
        return parser.parse_args(map(lambda arg: arg.strip('\'\"'), args))

    def run_once(self, args=[]):
        """Runs the PyAuto performance tests."""
        if isinstance(args, str):
          args = args.split()
        options, test_args = self.parse_args(args)
        test_args = ' '.join(test_args)

        # Enable Chrome testing interface and login to a default account.
        deps_dir = os.path.join(self.autodir, 'deps')
        pyautolib_dir = os.path.join(self.cr_source_dir,
                                     'chrome', 'test', 'pyautolib')
        login_cmd = cros_ui.xcommand_as(
            'python %s chromeos_utils.ChromeosUtils.LoginToDefaultAccount '
            '-v --no-http-server' %
                os.path.join(pyautolib_dir, 'chromeos', 'chromeos_utils.py'))
        utils.system(login_cmd)

        # Run the PyAuto performance tests.
        print 'About to run the pyauto performance tests.'
        print 'Note: you will see two timestamps for each logging message.'
        print '      The outer timestamp occurs when the autotest dumps the '
        print '      pyauto output, which only occurs after all tests are '
        print '      complete. The inner timestamp is the time at which the '
        print '      message was logged by pyauto while the test was actually '
        print '      running.'
        functional_cmd = cros_ui.xcommand_as(
            '%s/chrome_test/test_src/chrome/test/functional/'
            'pyauto_functional.py --suite=%s %s' % (
                deps_dir, options.suite, test_args))
        environment = os.environ.copy()

        environment['NUM_ITERATIONS'] = str(options.num_iterations)
        self.write_perf_keyval({'iterations': options.num_iterations})

        if options.max_timeouts:
          environment['MAX_TIMEOUT_COUNT'] = str(options.max_timeouts)

        proc = subprocess.Popen(
            functional_cmd, shell=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, env=environment)
        output = proc.communicate()[0]
        print output  # Ensure pyauto test output is stored in autotest logs.

        # Output perf keyvals for any perf results recorded during the tests.
        re_compiled = re.compile('%s(.+)%s' % (self._PERF_MARKER_PRE,
                                               self._PERF_MARKER_POST))
        perf_lines = [line for line in output.split('\n')
                      if re_compiled.match(line)]
        if perf_lines:
            perf_dict = dict([eval(re_compiled.match(line).group(1))
                              for line in perf_lines])
            self.write_perf_keyval(perf_dict)

        # Fail the autotest if any pyauto tests failed.  This is done after
        # writing perf keyvals so that any computed results from passing tests
        # are still graphed.
        if proc.returncode != 0:
            raise error.TestFail(
                'Unexpected return code from pyauto_functional.py when running '
                'with the CHROMEOS_PERF suite: %d' % proc.returncode)
