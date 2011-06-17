// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_GL_GL_BINDINGS_SKIA_IN_PROCESS_H_
#define UI_GFX_GL_GL_BINDINGS_SKIA_IN_PROCESS_H_
#pragma once

namespace gfx {

// The GPU back-end for skia requires pointers to GL functions. This function
// binds skia-gpu to the in-process GL
void BindSkiaToInProcessGL();

}

#endif  // UI_GFX_GL_GL_BINDINGS_SKIA_IN_PROCESS_H_

