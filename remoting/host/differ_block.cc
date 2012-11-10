// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/differ_block.h"

#include "base/cpu.h"
#include "build/build_config.h"
#include "remoting/host/differ_block_internal.h"

namespace remoting {

int BlockDifference_C(const uint8* image1, const uint8* image2, int stride) {
  int width_bytes = kBlockSize * kBytesPerPixel;

  for (int y = 0; y < kBlockSize; y++) {
    if (memcmp(image1, image2, width_bytes) != 0)
      return 1;
    image1 += stride;
    image2 += stride;
  }
  return 0;
}

int BlockDifference(const uint8* image1, const uint8* image2, int stride) {
  static int (*diff_proc)(const uint8*, const uint8*, int) = NULL;

  if (!diff_proc) {
#if defined(ARCH_CPU_ARM_FAMILY)
    // For ARM processors, always use C version.
    // TODO(hclam): Implement a NEON version.
    diff_proc = &BlockDifference_C;
#else
    base::CPU cpu;
    // For x86 processors, check if SSE2 is supported.
    if (cpu.has_sse2() && kBlockSize == 32)
      diff_proc = &BlockDifference_SSE2_W32;
    else if (cpu.has_sse2() && kBlockSize == 16)
      diff_proc = &BlockDifference_SSE2_W16;
    else
      diff_proc = &BlockDifference_C;
#endif
  }

  return diff_proc(image1, image2, stride);
}

}  // namespace remoting
