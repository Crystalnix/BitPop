// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This webpage shows layout of YV12 and other YUV formats
// http://www.fourcc.org/yuv.php
// The actual conversion is best described here
// http://en.wikipedia.org/wiki/YUV
// An article on optimizing YUV conversion using tables instead of multiplies
// http://lestourtereaux.free.fr/papers/data/yuvrgb.pdf
//
// YV12 is a full plane of Y and a half height, half width chroma planes
// YV16 is a full plane of Y and a full height, half width chroma planes
//
// ARGB pixel format is output, which on little endian is stored as BGRA.
// The alpha is set to 255, allowing the application to use RGBA or RGB32.

#include "media/base/yuv_convert.h"

#include "build/build_config.h"
#include "media/base/cpu_features.h"
#include "media/base/yuv_convert_internal.h"
#include "media/base/yuv_row.h"

#if USE_MMX
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <mmintrin.h>
#endif
#endif

#if USE_SSE2
#include <emmintrin.h>
#endif

namespace media {

// 16.16 fixed point arithmetic
const int kFractionBits = 16;
const int kFractionMax = 1 << kFractionBits;
const int kFractionMask = ((1 << kFractionBits) - 1);

// Convert a frame of YUV to 32 bit ARGB.
void ConvertYUVToRGB32(const uint8* y_buf,
                       const uint8* u_buf,
                       const uint8* v_buf,
                       uint8* rgb_buf,
                       int width,
                       int height,
                       int y_pitch,
                       int uv_pitch,
                       int rgb_pitch,
                       YUVType yuv_type) {
  unsigned int y_shift = yuv_type;
  for (int y = 0; y < height; ++y) {
    uint8* rgb_row = rgb_buf + y * rgb_pitch;
    const uint8* y_ptr = y_buf + y * y_pitch;
    const uint8* u_ptr = u_buf + (y >> y_shift) * uv_pitch;
    const uint8* v_ptr = v_buf + (y >> y_shift) * uv_pitch;

    FastConvertYUVToRGB32Row(y_ptr,
                             u_ptr,
                             v_ptr,
                             rgb_row,
                             width);
  }

  // MMX used for FastConvertYUVToRGB32Row requires emms instruction.
  EMMS();
}

#if USE_SSE2
// FilterRows combines two rows of the image using linear interpolation.
// SSE2 version does 16 pixels at a time

static void FilterRows(uint8* ybuf, const uint8* y0_ptr, const uint8* y1_ptr,
                       int source_width, int source_y_fraction) {
  __m128i zero = _mm_setzero_si128();
  __m128i y1_fraction = _mm_set1_epi16(source_y_fraction);
  __m128i y0_fraction = _mm_set1_epi16(256 - source_y_fraction);

  const __m128i* y0_ptr128 = reinterpret_cast<const __m128i*>(y0_ptr);
  const __m128i* y1_ptr128 = reinterpret_cast<const __m128i*>(y1_ptr);
  __m128i* dest128 = reinterpret_cast<__m128i*>(ybuf);
  __m128i* end128 = reinterpret_cast<__m128i*>(ybuf + source_width);

  do {
    __m128i y0 = _mm_loadu_si128(y0_ptr128);
    __m128i y1 = _mm_loadu_si128(y1_ptr128);
    __m128i y2 = _mm_unpackhi_epi8(y0, zero);
    __m128i y3 = _mm_unpackhi_epi8(y1, zero);
    y0 = _mm_unpacklo_epi8(y0, zero);
    y1 = _mm_unpacklo_epi8(y1, zero);
    y0 = _mm_mullo_epi16(y0, y0_fraction);
    y1 = _mm_mullo_epi16(y1, y1_fraction);
    y2 = _mm_mullo_epi16(y2, y0_fraction);
    y3 = _mm_mullo_epi16(y3, y1_fraction);
    y0 = _mm_add_epi16(y0, y1);
    y2 = _mm_add_epi16(y2, y3);
    y0 = _mm_srli_epi16(y0, 8);
    y2 = _mm_srli_epi16(y2, 8);
    y0 = _mm_packus_epi16(y0, y2);
    *dest128++ = y0;
    ++y0_ptr128;
    ++y1_ptr128;
  } while (dest128 < end128);
}
#elif USE_MMX
// MMX version does 8 pixels at a time
static void FilterRows(uint8* ybuf, const uint8* y0_ptr, const uint8* y1_ptr,
                       int source_width, int source_y_fraction) {
  __m64 zero = _mm_setzero_si64();
  __m64 y1_fraction = _mm_set1_pi16(source_y_fraction);
  __m64 y0_fraction = _mm_set1_pi16(256 - source_y_fraction);

  const __m64* y0_ptr64 = reinterpret_cast<const __m64*>(y0_ptr);
  const __m64* y1_ptr64 = reinterpret_cast<const __m64*>(y1_ptr);
  __m64* dest64 = reinterpret_cast<__m64*>(ybuf);
  __m64* end64 = reinterpret_cast<__m64*>(ybuf + source_width);

  do {
    __m64 y0 = *y0_ptr64++;
    __m64 y1 = *y1_ptr64++;
    __m64 y2 = _mm_unpackhi_pi8(y0, zero);
    __m64 y3 = _mm_unpackhi_pi8(y1, zero);
    y0 = _mm_unpacklo_pi8(y0, zero);
    y1 = _mm_unpacklo_pi8(y1, zero);
    y0 = _mm_mullo_pi16(y0, y0_fraction);
    y1 = _mm_mullo_pi16(y1, y1_fraction);
    y2 = _mm_mullo_pi16(y2, y0_fraction);
    y3 = _mm_mullo_pi16(y3, y1_fraction);
    y0 = _mm_add_pi16(y0, y1);
    y2 = _mm_add_pi16(y2, y3);
    y0 = _mm_srli_pi16(y0, 8);
    y2 = _mm_srli_pi16(y2, 8);
    y0 = _mm_packs_pu16(y0, y2);
    *dest64++ = y0;
  } while (dest64 < end64);
}
#else  // no MMX or SSE2
// C version does 8 at a time to mimic MMX code
static void FilterRows(uint8* ybuf, const uint8* y0_ptr, const uint8* y1_ptr,
                       int source_width, int source_y_fraction) {
  int y1_fraction = source_y_fraction;
  int y0_fraction = 256 - y1_fraction;
  uint8* end = ybuf + source_width;
  do {
    ybuf[0] = (y0_ptr[0] * y0_fraction + y1_ptr[0] * y1_fraction) >> 8;
    ybuf[1] = (y0_ptr[1] * y0_fraction + y1_ptr[1] * y1_fraction) >> 8;
    ybuf[2] = (y0_ptr[2] * y0_fraction + y1_ptr[2] * y1_fraction) >> 8;
    ybuf[3] = (y0_ptr[3] * y0_fraction + y1_ptr[3] * y1_fraction) >> 8;
    ybuf[4] = (y0_ptr[4] * y0_fraction + y1_ptr[4] * y1_fraction) >> 8;
    ybuf[5] = (y0_ptr[5] * y0_fraction + y1_ptr[5] * y1_fraction) >> 8;
    ybuf[6] = (y0_ptr[6] * y0_fraction + y1_ptr[6] * y1_fraction) >> 8;
    ybuf[7] = (y0_ptr[7] * y0_fraction + y1_ptr[7] * y1_fraction) >> 8;
    y0_ptr += 8;
    y1_ptr += 8;
    ybuf += 8;
  } while (ybuf < end);
}
#endif


// Scale a frame of YUV to 32 bit ARGB.
void ScaleYUVToRGB32(const uint8* y_buf,
                     const uint8* u_buf,
                     const uint8* v_buf,
                     uint8* rgb_buf,
                     int source_width,
                     int source_height,
                     int width,
                     int height,
                     int y_pitch,
                     int uv_pitch,
                     int rgb_pitch,
                     YUVType yuv_type,
                     Rotate view_rotate,
                     ScaleFilter filter) {
  // 4096 allows 3 buffers to fit in 12k.
  // Helps performance on CPU with 16K L1 cache.
  // Large enough for 3830x2160 and 30" displays which are 2560x1600.
  const int kFilterBufferSize = 4096;
  // Disable filtering if the screen is too big (to avoid buffer overflows).
  // This should never happen to regular users: they don't have monitors
  // wider than 4096 pixels.
  // TODO(fbarchard): Allow rotated videos to filter.
  if (source_width > kFilterBufferSize || view_rotate)
    filter = FILTER_NONE;

  unsigned int y_shift = yuv_type;
  // Diagram showing origin and direction of source sampling.
  // ->0   4<-
  // 7       3
  //
  // 6       5
  // ->1   2<-
  // Rotations that start at right side of image.
  if ((view_rotate == ROTATE_180) ||
      (view_rotate == ROTATE_270) ||
      (view_rotate == MIRROR_ROTATE_0) ||
      (view_rotate == MIRROR_ROTATE_90)) {
    y_buf += source_width - 1;
    u_buf += source_width / 2 - 1;
    v_buf += source_width / 2 - 1;
    source_width = -source_width;
  }
  // Rotations that start at bottom of image.
  if ((view_rotate == ROTATE_90) ||
      (view_rotate == ROTATE_180) ||
      (view_rotate == MIRROR_ROTATE_90) ||
      (view_rotate == MIRROR_ROTATE_180)) {
    y_buf += (source_height - 1) * y_pitch;
    u_buf += ((source_height >> y_shift) - 1) * uv_pitch;
    v_buf += ((source_height >> y_shift) - 1) * uv_pitch;
    source_height = -source_height;
  }

  // Handle zero sized destination.
  if (width == 0 || height == 0)
    return;
  int source_dx = source_width * kFractionMax / width;
  int source_dy = source_height * kFractionMax / height;
  int source_dx_uv = source_dx;

  if ((view_rotate == ROTATE_90) ||
      (view_rotate == ROTATE_270)) {
    int tmp = height;
    height = width;
    width = tmp;
    tmp = source_height;
    source_height = source_width;
    source_width = tmp;
    int original_dx = source_dx;
    int original_dy = source_dy;
    source_dx = ((original_dy >> kFractionBits) * y_pitch) << kFractionBits;
    source_dx_uv = ((original_dy >> kFractionBits) * uv_pitch) << kFractionBits;
    source_dy = original_dx;
    if (view_rotate == ROTATE_90) {
      y_pitch = -1;
      uv_pitch = -1;
      source_height = -source_height;
    } else {
      y_pitch = 1;
      uv_pitch = 1;
    }
  }

  // Need padding because FilterRows() will write 1 to 16 extra pixels
  // after the end for SSE2 version.
  uint8 yuvbuf[16 + kFilterBufferSize * 3 + 16];
  uint8* ybuf =
      reinterpret_cast<uint8*>(reinterpret_cast<uintptr_t>(yuvbuf + 15) & ~15);
  uint8* ubuf = ybuf + kFilterBufferSize;
  uint8* vbuf = ubuf + kFilterBufferSize;
  // TODO(fbarchard): Fixed point math is off by 1 on negatives.
  int yscale_fixed = (source_height << kFractionBits) / height;

  // TODO(fbarchard): Split this into separate function for better efficiency.
  for (int y = 0; y < height; ++y) {
    uint8* dest_pixel = rgb_buf + y * rgb_pitch;
    int source_y_subpixel = (y * yscale_fixed);
    if (yscale_fixed >= (kFractionMax * 2)) {
      source_y_subpixel += kFractionMax / 2;  // For 1/2 or less, center filter.
    }
    int source_y = source_y_subpixel >> kFractionBits;

    const uint8* y0_ptr = y_buf + source_y * y_pitch;
    const uint8* y1_ptr = y0_ptr + y_pitch;

    const uint8* u0_ptr = u_buf + (source_y >> y_shift) * uv_pitch;
    const uint8* u1_ptr = u0_ptr + uv_pitch;
    const uint8* v0_ptr = v_buf + (source_y >> y_shift) * uv_pitch;
    const uint8* v1_ptr = v0_ptr + uv_pitch;

    // vertical scaler uses 16.8 fixed point
    int source_y_fraction = (source_y_subpixel & kFractionMask) >> 8;
    int source_uv_fraction =
        ((source_y_subpixel >> y_shift) & kFractionMask) >> 8;

    const uint8* y_ptr = y0_ptr;
    const uint8* u_ptr = u0_ptr;
    const uint8* v_ptr = v0_ptr;
    // Apply vertical filtering if necessary.
    // TODO(fbarchard): Remove memcpy when not necessary.
    if (filter & media::FILTER_BILINEAR_V) {
      if (yscale_fixed != kFractionMax &&
          source_y_fraction && ((source_y + 1) < source_height)) {
        FilterRows(ybuf, y0_ptr, y1_ptr, source_width, source_y_fraction);
      } else {
        memcpy(ybuf, y0_ptr, source_width);
      }
      y_ptr = ybuf;
      ybuf[source_width] = ybuf[source_width-1];
      int uv_source_width = (source_width + 1) / 2;
      if (yscale_fixed != kFractionMax &&
          source_uv_fraction &&
          (((source_y >> y_shift) + 1) < (source_height >> y_shift))) {
        FilterRows(ubuf, u0_ptr, u1_ptr, uv_source_width, source_uv_fraction);
        FilterRows(vbuf, v0_ptr, v1_ptr, uv_source_width, source_uv_fraction);
      } else {
        memcpy(ubuf, u0_ptr, uv_source_width);
        memcpy(vbuf, v0_ptr, uv_source_width);
      }
      u_ptr = ubuf;
      v_ptr = vbuf;
      ubuf[uv_source_width] = ubuf[uv_source_width - 1];
      vbuf[uv_source_width] = vbuf[uv_source_width - 1];
    }
    if (source_dx == kFractionMax) {  // Not scaled
      FastConvertYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                               dest_pixel, width);
    } else {
      if (filter & FILTER_BILINEAR_H) {
        LinearScaleYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                                 dest_pixel, width, source_dx);
    } else {
// Specialized scalers and rotation.
#if USE_MMX && defined(_MSC_VER)
        if (width == (source_width * 2)) {
          DoubleYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                              dest_pixel, width);
        } else if ((source_dx & kFractionMask) == 0) {
          // Scaling by integer scale factor. ie half.
          ConvertYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                               dest_pixel, width,
                               source_dx >> kFractionBits);
        } else if (source_dx_uv == source_dx) {  // Not rotated.
          ScaleYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                             dest_pixel, width, source_dx);
        } else {
          RotateConvertYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                                     dest_pixel, width,
                                     source_dx >> kFractionBits,
                                     source_dx_uv >> kFractionBits);
        }
#else
        ScaleYUVToRGB32Row(y_ptr, u_ptr, v_ptr,
                           dest_pixel, width, source_dx);
#endif
      }
    }
  }
  // MMX used for FastConvertYUVToRGB32Row and FilterRows requires emms.
  EMMS();
}

void ConvertRGB32ToYUV(const uint8* rgbframe,
                       uint8* yplane,
                       uint8* uplane,
                       uint8* vplane,
                       int width,
                       int height,
                       int rgbstride,
                       int ystride,
                       int uvstride) {
  static void (*convert_proc)(const uint8*, uint8*, uint8*, uint8*,
                              int, int, int, int, int) = NULL;
  if (!convert_proc) {
#if defined(ARCH_CPU_ARM_FAMILY)
    // For ARM processors, always use C version.
    // TODO(hclam): Implement a NEON version.
    convert_proc = &ConvertRGB32ToYUV_C;
#else
    // For x86 processors, check if SSE2 is supported.
    if (hasSSE2())
      convert_proc = &ConvertRGB32ToYUV_SSE2;
    else
      convert_proc = &ConvertRGB32ToYUV_C;
#endif
  }

  convert_proc(rgbframe, yplane, uplane, vplane, width, height,
               rgbstride, ystride, uvstride);
}

void ConvertRGB24ToYUV(const uint8* rgbframe,
                       uint8* yplane,
                       uint8* uplane,
                       uint8* vplane,
                       int width,
                       int height,
                       int rgbstride,
                       int ystride,
                       int uvstride) {
  ConvertRGB24ToYUV_C(rgbframe, yplane, uplane, vplane, width, height,
                      rgbstride, ystride, uvstride);
}

void ConvertYUY2ToYUV(const uint8* src,
                      uint8* yplane,
                      uint8* uplane,
                      uint8* vplane,
                      int width,
                      int height) {
  ConvertYUY2ToYUV_C(src, yplane, uplane, vplane, width, height);
}
}  // namespace media
