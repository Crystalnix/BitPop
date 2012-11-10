// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/base/util.h"

#include <math.h>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/time.h"
#include "media/base/video_frame.h"
#include "media/base/yuv_convert.h"
#include "third_party/skia/include/core/SkRegion.h"

using media::VideoFrame;

namespace remoting {

enum { kBytesPerPixelRGB32 = 4 };

// Do not write LOG messages in this routine since it is called from within
// our LOG message handler. Bad things will happen.
std::string GetTimestampString() {
  base::Time t = base::Time::NowFromSystemTime();
  base::Time::Exploded tex;
  t.LocalExplode(&tex);
  return StringPrintf("%02d%02d/%02d%02d%02d:",
                      tex.month, tex.day_of_month,
                      tex.hour, tex.minute, tex.second);
}

int CalculateRGBOffset(int x, int y, int stride) {
  return stride * y + kBytesPerPixelRGB32 * x;
}

int CalculateYOffset(int x, int y, int stride) {
  DCHECK(((x & 1) == 0) && ((y & 1) == 0));
  return stride * y + x;
}

int CalculateUVOffset(int x, int y, int stride) {
  DCHECK(((x & 1) == 0) && ((y & 1) == 0));
  return stride * y / 2 + x / 2;
}

void ConvertRGB32ToYUVWithRect(const uint8* rgb_plane,
                               uint8* y_plane,
                               uint8* u_plane,
                               uint8* v_plane,
                               int x,
                               int y,
                               int width,
                               int height,
                               int rgb_stride,
                               int y_stride,
                               int uv_stride) {
  int rgb_offset = CalculateRGBOffset(x, y, rgb_stride);
  int y_offset = CalculateYOffset(x, y, y_stride);
  int uv_offset = CalculateUVOffset(x, y, uv_stride);;

  media::ConvertRGB32ToYUV(rgb_plane + rgb_offset,
                           y_plane + y_offset,
                           u_plane + uv_offset,
                           v_plane + uv_offset,
                           width,
                           height,
                           rgb_stride,
                           y_stride,
                           uv_stride);
}

void ConvertAndScaleYUVToRGB32Rect(const uint8* source_yplane,
                                   const uint8* source_uplane,
                                   const uint8* source_vplane,
                                   int source_ystride,
                                   int source_uvstride,
                                   const SkISize& source_size,
                                   const SkIRect& source_buffer_rect,
                                   uint8* dest_buffer,
                                   int dest_stride,
                                   const SkISize& dest_size,
                                   const SkIRect& dest_buffer_rect,
                                   const SkIRect& dest_rect) {
  // N.B. It is caller's responsibility to check if strides are large enough. We
  // cannot do it here anyway.
  DCHECK(SkIRect::MakeSize(source_size).contains(source_buffer_rect));
  DCHECK(SkIRect::MakeSize(dest_size).contains(dest_buffer_rect));
  DCHECK(dest_buffer_rect.contains(dest_rect));
  DCHECK(ScaleRect(source_buffer_rect, source_size, dest_size).
             contains(dest_rect));

  // If the source and/or destination buffers don't start at (0, 0)
  // offset the pointers to pretend we have complete buffers.
  int y_offset = - CalculateYOffset(source_buffer_rect.x(),
                                    source_buffer_rect.y(),
                                    source_ystride);
  int uv_offset = - CalculateUVOffset(source_buffer_rect.x(),
                                      source_buffer_rect.y(),
                                      source_uvstride);
  int rgb_offset = - CalculateRGBOffset(dest_buffer_rect.x(),
                                        dest_buffer_rect.y(),
                                        dest_stride);

  // See if scaling is needed.
  if (source_size == dest_size) {
    // Calculate the inner rectangle that can be copied by the optimized
    // ConvertYUVToRGB32().
    SkIRect inner_rect =
        SkIRect::MakeLTRB(RoundToTwosMultiple(dest_rect.left() + 1),
                          RoundToTwosMultiple(dest_rect.top() + 1),
                          dest_rect.right(),
                          dest_rect.bottom());

    // Offset pointers to point to the top left corner of the inner rectangle.
    y_offset += CalculateYOffset(inner_rect.x(), inner_rect.y(),
                                 source_ystride);
    uv_offset += CalculateUVOffset(inner_rect.x(), inner_rect.y(),
                                   source_uvstride);
    rgb_offset += CalculateRGBOffset(inner_rect.x(), inner_rect.y(),
                                     dest_stride);

    media::ConvertYUVToRGB32(source_yplane + y_offset,
                             source_uplane + uv_offset,
                             source_vplane + uv_offset,
                             dest_buffer + rgb_offset,
                             inner_rect.width(),
                             inner_rect.height(),
                             source_ystride,
                             source_uvstride,
                             dest_stride,
                             media::YV12);

    // Now see if some pixels weren't copied due to alignment.
    if (dest_rect != inner_rect) {
      SkIRect outer_rect =
        SkIRect::MakeLTRB(RoundToTwosMultiple(dest_rect.left()),
                          RoundToTwosMultiple(dest_rect.top()),
                          dest_rect.right(),
                          dest_rect.bottom());

      SkIPoint offset = SkIPoint::Make(outer_rect.x() - inner_rect.x(),
                                       outer_rect.y() - inner_rect.y());

      // Offset the pointers to point to the top left corner of the outer
      // rectangle.
      y_offset += CalculateYOffset(offset.x(), offset.y(), source_ystride);
      uv_offset += CalculateUVOffset(offset.x(), offset.y(), source_uvstride);
      rgb_offset += CalculateRGBOffset(offset.x(), offset.y(), dest_stride);

      // Draw unaligned edges.
      SkRegion edges(dest_rect);
      edges.op(inner_rect, SkRegion::kDifference_Op);
      for (SkRegion::Iterator i(edges); !i.done(); i.next()) {
        SkIRect rect(i.rect());
        rect.offset(- outer_rect.left(), - outer_rect.top());
        media::ScaleYUVToRGB32WithRect(source_yplane + y_offset,
                                       source_uplane + uv_offset,
                                       source_vplane + uv_offset,
                                       dest_buffer + rgb_offset,
                                       source_size.width(),
                                       source_size.height(),
                                       dest_size.width(),
                                       dest_size.height(),
                                       rect.left(),
                                       rect.top(),
                                       rect.right(),
                                       rect.bottom(),
                                       source_ystride,
                                       source_uvstride,
                                       dest_stride);
      }
    }
  } else {
    media::ScaleYUVToRGB32WithRect(source_yplane + y_offset,
                                   source_uplane + uv_offset,
                                   source_vplane + uv_offset,
                                   dest_buffer + rgb_offset,
                                   source_size.width(),
                                   source_size.height(),
                                   dest_size.width(),
                                   dest_size.height(),
                                   dest_rect.left(),
                                   dest_rect.top(),
                                   dest_rect.right(),
                                   dest_rect.bottom(),
                                   source_ystride,
                                   source_uvstride,
                                   dest_stride);
  }
}

int RoundToTwosMultiple(int x) {
  return x & (~1);
}

SkIRect AlignRect(const SkIRect& rect) {
  int x = RoundToTwosMultiple(rect.left());
  int y = RoundToTwosMultiple(rect.top());
  int right = RoundToTwosMultiple(rect.right() + 1);
  int bottom = RoundToTwosMultiple(rect.bottom() + 1);
  return SkIRect::MakeLTRB(x, y, right, bottom);
}

SkIRect ScaleRect(const SkIRect& rect,
                  const SkISize& in_size,
                  const SkISize& out_size) {
  int left = (rect.left() * out_size.width()) / in_size.width();
  int top = (rect.top() * out_size.height()) / in_size.height();
  int right = (rect.right() * out_size.width() + in_size.width() - 1) /
      in_size.width();
  int bottom = (rect.bottom() * out_size.height() + in_size.height() - 1) /
      in_size.height();
  return SkIRect::MakeLTRB(left, top, right, bottom);
}

void CopyRect(const uint8* src_plane,
              int src_plane_stride,
              uint8* dest_plane,
              int dest_plane_stride,
              int bytes_per_pixel,
              const SkIRect& rect) {
  // Get the address of the starting point.
  const int src_y_offset = src_plane_stride * rect.top();
  const int dest_y_offset = dest_plane_stride * rect.top();
  const int x_offset = bytes_per_pixel * rect.left();
  src_plane += src_y_offset + x_offset;
  dest_plane += dest_y_offset + x_offset;

  // Copy pixels in the rectangle line by line.
  const int bytes_per_line = bytes_per_pixel * rect.width();
  const int height = rect.height();
  for (int i = 0 ; i < height; ++i) {
    memcpy(dest_plane, src_plane, bytes_per_line);
    src_plane += src_plane_stride;
    dest_plane += dest_plane_stride;
  }
}

void CopyRGB32Rect(const uint8* source_buffer,
                   int source_stride,
                   const SkIRect& source_buffer_rect,
                   uint8* dest_buffer,
                   int dest_stride,
                   const SkIRect& dest_buffer_rect,
                   const SkIRect& dest_rect) {
  DCHECK(dest_buffer_rect.contains(dest_rect));
  DCHECK(source_buffer_rect.contains(dest_rect));

  // Get the address of the starting point.
  int source_offset = CalculateRGBOffset(dest_rect.x() - source_buffer_rect.x(),
                                         dest_rect.y() - source_buffer_rect.y(),
                                         source_stride);
  int dest_offset = CalculateRGBOffset(dest_rect.x() - dest_buffer_rect.x(),
                                       dest_rect.y() - dest_buffer_rect.y(),
                                       source_stride);

  // Copy bits.
  CopyRect(source_buffer + source_offset,
           source_stride,
           dest_buffer + dest_offset,
           dest_stride,
           kBytesPerPixelRGB32,
           SkIRect::MakeWH(dest_rect.width(), dest_rect.height()));
}

std::string ReplaceLfByCrLf(const std::string& in) {
  std::string out;
  out.resize(2 * in.size());
  char* out_p_begin = &out[0];
  char* out_p = out_p_begin;
  const char* in_p_begin = &in[0];
  const char* in_p_end = &in[in.size()];
  for (const char* in_p = in_p_begin; in_p < in_p_end; ++in_p) {
    char c = *in_p;
    if (c == '\n') {
      *out_p++ = '\r';
    }
    *out_p++ = c;
  }
  out.resize(out_p - out_p_begin);
  return out;
}

std::string ReplaceCrLfByLf(const std::string& in) {
  std::string out;
  out.resize(in.size());
  char* out_p_begin = &out[0];
  char* out_p = out_p_begin;
  const char* in_p_begin = &in[0];
  const char* in_p_end = &in[in.size()];
  for (const char* in_p = in_p_begin; in_p < in_p_end; ++in_p) {
    char c = *in_p;
    if ((c == '\r') && (in_p + 1 < in_p_end) && (*(in_p + 1) == '\n')) {
      *out_p++ = '\n';
      ++in_p;
    } else {
      *out_p++ = c;
    }
  }
  out.resize(out_p - out_p_begin);
  return out;
}

}  // namespace remoting
