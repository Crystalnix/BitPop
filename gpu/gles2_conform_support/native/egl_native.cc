// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#if defined(GLES2_CONFORM_SUPPORT_ONLY)
#include "gpu/gles2_conform_support/gtf/gtf_stubs.h"
#else
#include "third_party/gles2_conform/GTF_ES/glsl/GTF/Source/eglNative.h"
#endif
}

GTFbool GTFNativeCreatePixmap(EGLNativeDisplayType nativeDisplay,
                              EGLDisplay eglDisplay, EGLConfig eglConfig,
                              const char *title, int width, int height,
                              EGLNativePixmapType *pNativePixmap) {
  return GTFtrue;
}

void GTFNativeDestroyPixmap(EGLNativeDisplayType nativeDisplay,
                            EGLNativePixmapType nativePixmap) {
}
