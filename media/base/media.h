// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains code that should be used for initializing, or querying the state
// of the media library as a whole.

#ifndef MEDIA_BASE_MEDIA_H_
#define MEDIA_BASE_MEDIA_H_

class FilePath;

namespace media {

// Attempts to initialize the media library (loading DLLs, DSOs, etc.).
//
// If |module_dir| is the emptry string, then the system default library paths
// are searched for the dynamic libraries.  If a |module_dir| is provided, then
// only the specified |module_dir| will be searched for the dynamic libraries.
//
// If multiple initializations are attempted with different |module_dir|s
// specified then the first one to succeed remains effective for the lifetime
// of the process.
//
// Returns true if everything was successfully initialized, false otherwise.
bool InitializeMediaLibrary(const FilePath& module_dir);

// Use this if you need to check whether the media library is initialized
// for the this process, without actually trying to initialize it.
bool IsMediaLibraryInitialized();

// Attempts to initialize OpenMAX library.
//
// Returns true if OpenMAX was successfully initialized and loaded.
bool InitializeOpenMaxLibrary(const FilePath& module_dir);

}  // namespace media

#endif  // MEDIA_BASE_MEDIA_H_
