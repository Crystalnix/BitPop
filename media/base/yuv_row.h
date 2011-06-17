// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// yuv_row internal functions to handle YUV conversion and scaling to RGB.
// These functions are used from both yuv_convert.cc and yuv_scale.cc.

// TODO(fbarchard): Write function that can handle rotation and scaling.

#ifndef MEDIA_BASE_YUV_ROW_H_
#define MEDIA_BASE_YUV_ROW_H_

#include "base/basictypes.h"

extern "C" {
// Can only do 1x.
// This is the second fastest of the scalers.
void FastConvertYUVToRGB32Row(const uint8* y_buf,
                              const uint8* u_buf,
                              const uint8* v_buf,
                              uint8* rgb_buf,
                              int width);

// Can do 1x, half size or any scale down by an integer amount.
// Step can be negative (mirroring, rotate 180).
// This is the third fastest of the scalers.
void ConvertYUVToRGB32Row(const uint8* y_buf,
                          const uint8* u_buf,
                          const uint8* v_buf,
                          uint8* rgb_buf,
                          int width,
                          int step);

// Rotate is like Convert, but applies different step to Y versus U and V.
// This allows rotation by 90 or 270, by stepping by stride.
// This is the forth fastest of the scalers.
void RotateConvertYUVToRGB32Row(const uint8* y_buf,
                                const uint8* u_buf,
                                const uint8* v_buf,
                                uint8* rgb_buf,
                                int width,
                                int ystep,
                                int uvstep);

// Doubler does 4 pixels at a time.  Each pixel is replicated.
// This is the fastest of the scalers.
void DoubleYUVToRGB32Row(const uint8* y_buf,
                         const uint8* u_buf,
                         const uint8* v_buf,
                         uint8* rgb_buf,
                         int width);

// Handles arbitrary scaling up or down.
// Mirroring is supported, but not 90 or 270 degree rotation.
// Chroma is under sampled every 2 pixels for performance.
void ScaleYUVToRGB32Row(const uint8* y_buf,
                        const uint8* u_buf,
                        const uint8* v_buf,
                        uint8* rgb_buf,
                        int width,
                        int source_dx);

// Handles arbitrary scaling up or down with bilinear filtering.
// Mirroring is supported, but not 90 or 270 degree rotation.
// Chroma is under sampled every 2 pixels for performance.
// This is the slowest of the scalers.
void LinearScaleYUVToRGB32Row(const uint8* y_buf,
                              const uint8* u_buf,
                              const uint8* v_buf,
                              uint8* rgb_buf,
                              int width,
                              int source_dx);

void FastConvertRGB32ToYUVRow(const uint8* rgb_buf_1,
                              const uint8* rgb_buf_2,
                              uint8* y_buf_1,
                              uint8* y_buf_2,
                              uint8* u_buf,
                              uint8* v_buf,
                              int width);

#if defined(_MSC_VER)
#define SIMD_ALIGNED(var) __declspec(align(16)) var
#else
#define SIMD_ALIGNED(var) var __attribute__((aligned(16)))
#endif
extern SIMD_ALIGNED(int16 kCoefficientsRgbY[768][4]);
extern SIMD_ALIGNED(int16 kCoefficientsYuvR[768][4]);

// Method to force C version.
//#define USE_MMX 0
//#define USE_SSE2 0

#if !defined(USE_MMX)
// Windows, Mac and Linux/BSD use MMX
#if defined(__MMX__) || defined(_MSC_VER)
#define USE_MMX 1
#else
#define USE_MMX 0
#endif
#endif

#if !defined(USE_SSE2)
#if defined(__SSE2__) || defined(ARCH_CPU_X86_64) || _M_IX86_FP==2
#define USE_SSE2 1
#else
#define USE_SSE2 0
#endif
#endif

// x64 uses MMX2 (SSE) so emms is not required.
// Warning C4799: function has no EMMS instruction.
// EMMS() is slow and should be called by the calling function once per image.
#if USE_MMX && !defined(ARCH_CPU_X86_64)
#if defined(_MSC_VER)
#define EMMS() __asm emms
#pragma warning(disable: 4799)
#else
#define EMMS() asm("emms")
#endif
#else
#define EMMS()
#endif

}  // extern "C"

#endif  // MEDIA_BASE_YUV_ROW_H_
