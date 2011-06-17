#!/usr/bin/env python
#
# $Id: setup.py 748 2010-10-29 12:51:58Z g.rodola $
#

import sys
import os
import shutil
from distutils.core import setup, Extension

# Hack for Python 3 to tell distutils to run 2to3 against the files
# copied in the build directory before installing.
# Reference: http://osdir.com/ml/python.python-3000.cvs/2008-03/msg00127.html
try:
    from distutils.command.build_py import build_py_2to3 as build_py
except ImportError:
    from distutils.command.build_py import build_py


# Windows
if sys.platform.lower().startswith("win"):

    def get_winver():
        maj,min = sys.getwindowsversion()[0:2]
        return '0x0%s' % ((maj * 100) + min)

    extensions = Extension('_psutil_mswindows',
                           sources=['psutil/_psutil_mswindows.c',
                                    'psutil/arch/mswindows/process_info.c',
                                    'psutil/arch/mswindows/process_handles.c',
                                    'psutil/arch/mswindows/security.c'],
                           define_macros=[('_WIN32_WINNT', get_winver()),
                                          ('_AVAIL_WINVER_', get_winver())],
                           libraries=["psapi", "kernel32", "advapi32", "shell32",
                                      "netapi32"]
                           )
# OS X
elif sys.platform.lower().startswith("darwin"):
    extensions = Extension('_psutil_osx',
                           sources = ['psutil/_psutil_osx.c',
                                      'psutil/arch/osx/process_info.c']
                           )
# FreeBSD
elif sys.platform.lower().startswith("freebsd"):
    extensions = Extension('_psutil_bsd',
                           sources = ['psutil/_psutil_bsd.c',
                                      'psutil/arch/bsd/process_info.c']
                           )
# Others
elif sys.platform.lower().startswith("linux"):
    extensions = None
else:
    raise NotImplementedError('platform %s is not supported' % sys.platform)


def main():
    setup_args = dict(
        name='psutil',
        version="0.2.0",
        description='A process utilities module for Python',
        long_description="""
psutil is a module providing convenience functions for managing processes in a
portable way by using Python.""",
        keywords=['psutil', 'ps', 'top', 'process', 'utility'],
        author='Giampaolo Rodola, Dave Daeschler, Jay Loden',
        author_email='psutil-dev@googlegroups.com',
        url='http://code.google.com/p/psutil/',
        platforms='Platform Independent',
        license='License :: OSI Approved :: BSD License',
        packages=['psutil'],
        cmdclass={'build_py':build_py},  # Python 3.X
        classifiers=[
              'Development Status :: 5 - Production/Stable',
              'Environment :: Console',
              'Operating System :: MacOS :: MacOS X',
              'Operating System :: Microsoft :: Windows :: Windows NT/2000',
              'Operating System :: POSIX :: Linux',
              'Operating System :: POSIX :: BSD :: FreeBSD',
              'Operating System :: OS Independent',
              'Programming Language :: C',
              'Programming Language :: Python',
              'Programming Language :: Python :: 2',
              'Programming Language :: Python :: 2.4',
              'Programming Language :: Python :: 2.5',
              'Programming Language :: Python :: 2.6',
              'Programming Language :: Python :: 2.7',
              'Programming Language :: Python :: 3',
              'Programming Language :: Python :: 3.0',
              'Programming Language :: Python :: 3.1',
              'Programming Language :: Python :: 3.2',
              'Topic :: System :: Monitoring',
              'Topic :: System :: Networking',
              'Topic :: System :: Benchmark',
              'Topic :: System :: Systems Administration',
              'Topic :: Utilities',
              'Topic :: Software Development :: Libraries :: Python Modules',
              'Intended Audience :: Developers',
              'Intended Audience :: System Administrators',
              'License :: OSI Approved :: MIT License',
              ],
        )
    if extensions is not None:
        setup_args["ext_modules"] = [extensions]

    setup(**setup_args)


if __name__ == '__main__':
    main()

