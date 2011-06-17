// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_RESULT_CODES_H_
#define CONTENT_COMMON_RESULT_CODES_H_
#pragma once

#include "base/process_util.h"

// This file consolidates all the return codes for the browser and renderer
// process. The return code is the value that:
// a) is returned by main() or winmain(), or
// b) specified in the call for ExitProcess() or TerminateProcess(), or
// c) the exception value that causes a process to terminate.
//
// It is advisable to not use negative numbers because the Windows API returns
// it as an unsigned long and the exception values have high numbers. For
// example EXCEPTION_ACCESS_VIOLATION value is 0xC0000005.

class ResultCodes {
 public:
  enum ExitCode {
    NORMAL_EXIT = 0,            // Process terminated normally.
    KILLED = 1,                 // Process was killed by user or system.
    HUNG = 2,                   // Process hung.
    INVALID_CMDLINE_URL,        // An invalid command line url was given.
    SBOX_INIT_FAILED,           // The sandbox could not be initialized.
    GOOGLE_UPDATE_INIT_FAILED,  // The Google Update client stub init failed.
    GOOGLE_UPDATE_LAUNCH_FAILED,// Google Update could not launch chrome DLL.
    BAD_PROCESS_TYPE,           // The process is of an unknown type.
    MISSING_PATH,               // An critical chrome path is missing.
    MISSING_DATA,               // A critical chrome file is missing.
    SHELL_INTEGRATION_FAILED,   // Failed to make Chrome default browser.
    MACHINE_LEVEL_INSTALL_EXISTS, // Machine level install exists
    UNINSTALL_DELETE_FILE_ERROR,// Error while deleting shortcuts.
    UNINSTALL_CHROME_ALIVE,     // Uninstall detected another chrome instance.
    UNINSTALL_NO_SURVEY,        // Do not launch survey after uninstall.
    UNINSTALL_USER_CANCEL,      // The user changed her mind.
    UNINSTALL_DELETE_PROFILE,   // Delete profile as well during uninstall.
    UNSUPPORTED_PARAM,          // Command line parameter is not supported.
    KILLED_BAD_MESSAGE,         // A bad message caused the process termination.
    IMPORTER_HUNG,              // Browser import hung and was killed.
    RESPAWN_FAILED,             // Trying to restart the browser we crashed.

    NORMAL_EXIT_EXP1,           // The EXP1, EXP2, EXP3, EXP4 are generic codes
    NORMAL_EXIT_EXP2,           // used to communicate some simple outcome back
    NORMAL_EXIT_EXP3,           // to the process that launched us. This is
    NORMAL_EXIT_EXP4,           // used for experiments and the actual meaning
                                // depends on the experiment.

    NORMAL_EXIT_CANCEL,         // For experiments this return code means that
                                // the user canceled causes the did_run "dr"
                                // signal to be reset so this chrome run does
                                // not count as active chrome usage.

    PROFILE_IN_USE,             // The profile was in use on another host.

    UNINSTALL_EXTENSION_ERROR,  // Failed to silently uninstall an extension.
    PACK_EXTENSION_ERROR,       // Failed to pack an extension via the cmd line.

    EXIT_LAST_CODE              // Last return code (keep it last).
  };
};

#endif  // CONTENT_COMMON_RESULT_CODES_H_
