// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_CHROME_PATHS_H__
#define CHROME_COMMON_CHROME_PATHS_H__
#pragma once

#include "build/build_config.h"

// This file declares path keys for the chrome module.  These can be used with
// the PathService to access various special directories and files.

namespace chrome {

enum {
  PATH_START = 1000,

  DIR_APP = PATH_START,         // Directory where dlls and data reside.
  DIR_LOGS,                     // Directory where logs should be written.
  DIR_USER_DATA,                // Directory where user data can be written.
  DIR_CRASH_DUMPS,              // Directory where crash dumps are written.
  DIR_USER_DESKTOP,             // Directory that correspond to the desktop.
  DIR_RESOURCES,                // Directory containing separate file resources
                                // used by Chrome at runtime.
  DIR_SHARED_RESOURCES,         // Directory containing js and css files used
                                // by WebUI and component extensions.
  DIR_INSPECTOR,                // Directory where web inspector is located.
  DIR_APP_DICTIONARIES,         // Directory where the global dictionaries are.
  DIR_USER_DOCUMENTS,           // Directory for a user's "My Documents".
  DIR_DEFAULT_DOWNLOADS_SAFE,   // Directory for a user's
                                // "My Documents/Downloads".
  DIR_DEFAULT_DOWNLOADS,        // Directory for a user's downloads.
  DIR_USER_DATA_TEMP,           // A temp directory within DIR_USER_DATA.  Use
                                // this when a temporary file or directory will
                                // be moved into the profile, to avoid issues
                                // moving across volumes.  See crbug.com/13044 .
                                // Getting this path does not create it.  Users
                                // should check that the path exists before
                                // using it.
  DIR_INTERNAL_PLUGINS,         // Directory where internal plugins reside.
  DIR_MEDIA_LIBS,               // Directory where the Media libraries reside.
#if defined(OS_POSIX) && !defined(OS_MACOSX)
  DIR_POLICY_FILES,             // Directory for system-wide read-only
                                // policy files that allow sys-admins
                                // to set policies for chrome. This directory
                                // contains subdirectories.
#endif
#if defined(OS_MACOSX)
  DIR_MANAGED_PREFS,            // Directory that stores the managed prefs plist
                                // files for the current user.
#endif
#if defined(OS_CHROMEOS) || defined(OS_MACOSX)
  DIR_USER_EXTERNAL_EXTENSIONS,  // Directory for per-user external extensions
                                 // on Chrome Mac.  On Chrome OS, this path is
                                 // used for OEM customization.
                                 // Getting this path does not create it.
#endif

  DIR_EXTERNAL_EXTENSIONS,      // Directory where installer places .crx files.

#if defined(OS_MACOSX)
  DIR_DEPRECATED_EXTERNAL_EXTENSIONS,  // Former home of external extensions.
                                       // We read from the old path for now,
                                       // to give users time to migrate.
#endif

  DIR_DEFAULT_APPS,             // Directory where installer places .crx files
                                // to be installed when chrome is first run.
  FILE_RESOURCE_MODULE,         // Full path and filename of the module that
                                // contains embedded resources (version,
                                // strings, images, etc.).
  FILE_LOCAL_STATE,             // Path and filename to the file in which
                                // machine/installation-specific state is saved.
  FILE_RECORDED_SCRIPT,         // Full path to the script.log file that
                                // contains recorded browser events for
                                // playback.
  FILE_FLASH_PLUGIN,            // Full path to the internal Flash plugin file.
  FILE_PEPPER_FLASH_PLUGIN,     // Full path to the pepper Flash plugin file.
  FILE_PDF_PLUGIN,              // Full path to the internal PDF plugin file.

#if defined(OS_POSIX) && !defined(OS_MACOSX)
  FILE_NACL_HELPER,             // Full path to Linux nacl_helper executable.
  FILE_NACL_HELPER_BOOTSTRAP,   // ... and nacl_helper_bootstrap executable.
#endif
  FILE_NACL_PLUGIN,             // Full path to the internal NaCl plugin file.
  FILE_PNACL_COMPONENT,         // Full path to the PNaCl component files.
  FILE_O3D_PLUGIN,              // Full path to the O3D Pepper plugin file.
  FILE_GTALK_PLUGIN,            // Full path to the GTalk Pepper plugin file.
  FILE_LIBAVCODEC,              // Full path to libavcodec media decoding
                                // library.
  FILE_LIBAVFORMAT,             // Full path to libavformat media parsing
                                // library.
  FILE_LIBAVUTIL,               // Full path to libavutil media utility library.
  FILE_RESOURCES_PACK,          // Full path to the .pak file containing
                                // binary data (e.g., html files and images
                                // used by interal pages).
#if defined(OS_CHROMEOS)
  FILE_CHROMEOS_API,            // Full path to chrome os api shared object.
#endif

  // Valid only in development environment; TODO(darin): move these
  DIR_GEN_TEST_DATA,            // Directory where generated test data resides.
  DIR_TEST_DATA,                // Directory where unit test data resides.
  DIR_TEST_TOOLS,               // Directory where unit test tools reside.
  DIR_LAYOUT_TESTS,             // Returns the LayoutTests path for layout
                                // tests. For the current git workflow, it
                                // returns
                                //   third_party/WebKit/LayoutTests
                                // On svn workflow (including build machines)
                                // and older git workflow, it returns
                                //   chrome/test/data/layout_tests/LayoutTests
                                // See, http://crbug.com/105104.

  PATH_END
};

// Call once to register the provider for the path keys defined above.
void RegisterPathProvider();

}  // namespace chrome

#endif  // CHROME_COMMON_CHROME_PATHS_H__
