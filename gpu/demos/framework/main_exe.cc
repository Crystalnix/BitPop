// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "gpu/demos/framework/window.h"
#include "ui/gfx/gl/gl_context.h"

#if defined(OS_LINUX)
#include <gtk/gtk.h>
#endif  // OS_LINUX

namespace {
static const int kWindowWidth = 512;
static const int kWindowHeight = 512;
}  // namespace.

int main(int argc, char** argv) {
#if defined(OS_LINUX)
  gtk_init(&argc, &argv);
#endif  // OS_LINUX

  // AtExitManager is used by singleton classes to delete themselves when
  // the program terminates.
  base::AtExitManager at_exit_manager_;

  CommandLine::Init(argc, argv);

  gfx::GLContext::InitializeOneOff();

  gpu::demos::Window window;
  CHECK(window.Init(kWindowWidth, kWindowHeight));

  window.MainLoop();
  return EXIT_SUCCESS;
}

