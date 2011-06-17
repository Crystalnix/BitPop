// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/set_process_title.h"

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/string_util.h"
#include "build/build_config.h"

#if defined(OS_POSIX)
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#if defined(OS_LINUX)
#include <sys/prctl.h>

// Linux/glibc doesn't natively have setproctitle().
#include "content/common/set_process_title_linux.h"
#endif

#if defined(OS_POSIX) && !defined(OS_MACOSX)

void SetProcessTitleFromCommandLine(char** main_argv) {
  // Build a single string which consists of all the arguments separated
  // by spaces. We can't actually keep them separate due to the way the
  // setproctitle() function works.
  std::string title;
  bool have_argv0 = false;

#if defined(OS_LINUX)
  if (main_argv)
    setproctitle_init(main_argv);

  // In Linux we sometimes exec ourselves from /proc/self/exe, but this makes us
  // show up as "exe" in process listings. Read the symlink /proc/self/exe and
  // use the path it points at for our process title. Note that this is only for
  // display purposes and has no TOCTTOU security implications.
  FilePath target;
  FilePath self_exe("/proc/self/exe");
  if (file_util::ReadSymbolicLink(self_exe, &target)) {
    have_argv0 = true;
    title = target.value();
    // If the binary has since been deleted, Linux appends " (deleted)" to the
    // symlink target. Remove it, since this is not really part of our name.
    const std::string kDeletedSuffix = " (deleted)";
    if (EndsWith(title, kDeletedSuffix, true))
      title.resize(title.size() - kDeletedSuffix.size());
#if defined(PR_SET_NAME)
    // If PR_SET_NAME is available at compile time, we try using it. We ignore
    // any errors if the kernel does not support it at runtime though. When
    // available, this lets us set the short process name that shows when the
    // full command line is not being displayed in most process listings.
    prctl(PR_SET_NAME, FilePath(title).BaseName().value().c_str());
#endif
  }
#endif

  const CommandLine* command_line = CommandLine::ForCurrentProcess();
  for (size_t i = 1; i < command_line->argv().size(); ++i) {
    if (!title.empty())
      title += " ";
    title += command_line->argv()[i];
  }
  // Disable prepending argv[0] with '-' if we prepended it ourselves above.
  setproctitle(have_argv0 ? "-%s" : "%s", title.c_str());
}

#else

// All other systems (basically Windows & Mac) have no need or way to implement
// this function.
void SetProcessTitleFromCommandLine(char** /* main_argv */) {
}

#endif

