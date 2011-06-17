#!/usr/bin/env python
#
# $Id: _bsd.py 664 2010-10-09 16:14:34Z g.rodola $
#

import unittest
import subprocess
import time
import re
import sys

import psutil

from test_psutil import reap_children, get_test_subprocess
from _posix import ps


def sysctl(cmdline):
    """Expects a sysctl command with an argument and parse the result
    returning only the value of interest.
    """
    p = subprocess.Popen(cmdline, shell=1, stdout=subprocess.PIPE)
    result = p.communicate()[0].strip().split()[1]
    if sys.version_info >= (3,):
        result = str(result, sys.stdout.encoding)
    try:
        return int(result)
    except ValueError:
        return result

def parse_sysctl_vmtotal(output):
    """Parse sysctl vm.vmtotal output returning total and free memory
    values.
    """
    line = output.split('\n')[4]  # our line of interest
    mobj = re.match(r'Virtual\s+Memory.*Total:\s+(\d+)K,\s+Active\s+(\d+)K.*', line)
    total, active = mobj.groups()
    # values are represented in kilo bytes
    total = int(total) * 1024
    active = int(active) * 1024
    free = total - active
    return total, free


class BSDSpecificTestCase(unittest.TestCase):

    def setUp(self):
        self.pid = get_test_subprocess().pid

    def tearDown(self):
        reap_children()

    def test_TOTAL_PHYMEM(self):
        sysctl_hwphymem = sysctl('sysctl hw.physmem')
        self.assertEqual(sysctl_hwphymem, psutil.TOTAL_PHYMEM)

    def test_avail_phymem(self):
        # This test is not particularly accurate and may fail if the OS is
        # consuming memory for other applications.
        # We just want to test that the difference between psutil result
        # and sysctl's is not too high.
        _sum = sum((sysctl("sysctl vm.stats.vm.v_inactive_count"),
                    sysctl("sysctl vm.stats.vm.v_cache_count"),
                    sysctl("sysctl vm.stats.vm.v_free_count")
                   ))
        _pagesize = sysctl("sysctl hw.pagesize")
        sysctl_avail_phymem = _sum * _pagesize
        psutil_avail_phymem =  psutil.avail_phymem()
        difference = abs(psutil_avail_phymem - sysctl_avail_phymem)
        # On my system both sysctl and psutil report the same values.
        # Let's use a tollerance of 0.5 MB and consider the test as failed
        # if we go over it.
        if difference > (0.5 * 2**20):
            self.fail("sysctl=%s; psutil=%s; difference=%s;" %(
                      sysctl_avail_phymem, psutil_avail_phymem, difference))

    def test_total_virtmem(self):
        # This test is not particularly accurate and may fail if the OS is
        # consuming memory for other applications.
        # We just want to test that the difference between psutil result
        # and sysctl's is not too high.
        p = subprocess.Popen("sysctl vm.vmtotal", shell=1, stdout=subprocess.PIPE)
        result = p.communicate()[0].strip()
        if sys.version_info >= (3,):
            result = str(result, sys.stdout.encoding)
        sysctl_total_virtmem, _ = parse_sysctl_vmtotal(result)
        psutil_total_virtmem = psutil.total_virtmem()
        difference = abs(sysctl_total_virtmem - psutil_total_virtmem)

        # On my system I get a difference of 4657152 bytes, probably because
        # the system is consuming memory for this same test.
        # Assuming psutil is right, let's use a tollerance of 10 MB and consider
        # the test as failed if we go over it.
        if difference > (10 * 2**20):
            self.fail("sysctl=%s; psutil=%s; difference=%s;" %(
                       sysctl_total_virtmem, psutil_total_virtmem, difference)
                      )

    def test_avail_virtmem(self):
        # This test is not particularly accurate and may fail if the OS is
        # consuming memory for other applications.
        # We just want to test that the difference between psutil result
        # and sysctl's is not too high.
        p = subprocess.Popen("sysctl vm.vmtotal", shell=1, stdout=subprocess.PIPE)
        result = p.communicate()[0].strip()
        if sys.version_info >= (3,):
            result = str(result, sys.stdout.encoding)
        _, sysctl_avail_virtmem = parse_sysctl_vmtotal(result)
        psutil_avail_virtmem = psutil.avail_virtmem()
        difference = abs(sysctl_avail_virtmem - psutil_avail_virtmem)
        # let's assume the test is failed if difference is > 0.5 MB
        if difference > (0.5 * 2**20):
            self.fail("sysctl=%s; psutil=%s; difference=%s;" %(
                       sysctl_avail_virtmem, psutil_avail_virtmem, difference))

    def test_process_create_time(self):
        cmdline = "ps -o lstart -p %s" %self.pid
        p = subprocess.Popen(cmdline, shell=1, stdout=subprocess.PIPE)
        output = p.communicate()[0]
        if sys.version_info >= (3,):
            output = str(output, sys.stdout.encoding)
        start_ps = output.replace('STARTED', '').strip()
        start_psutil = psutil.Process(self.pid).create_time
        start_psutil = time.strftime("%a %b %e %H:%M:%S %Y",
                                     time.localtime(start_psutil))
        self.assertEqual(start_ps, start_psutil)


if __name__ == '__main__':
    test_suite = unittest.TestSuite()
    test_suite.addTest(unittest.makeSuite(BSDSpecificTestCase))
    unittest.TextTestRunner(verbosity=2).run(test_suite)




