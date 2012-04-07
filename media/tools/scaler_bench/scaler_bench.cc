// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This tool can be used to measure performace of video frame scaling
// code. It times performance of the scaler with and without filtering.
// It also measures performance of the Skia scaler for comparison.

#include <iostream>
#include <vector>

#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/string_number_conversions.h"
#include "base/time.h"
#include "media/base/video_frame.h"
#include "media/base/yuv_convert.h"
#include "third_party/skia/include/core/SkCanvas.h"

using base::TimeDelta;
using base::TimeTicks;
using media::VideoFrame;

static int source_width = 1280;
static int source_height = 720;
static int dest_width = 1366;
static int dest_height = 768;
static int num_frames = 500;
static int num_buffers = 50;

static double BenchmarkSkia() {
  std::vector<scoped_refptr<VideoFrame> > source_frames;
  ScopedVector<SkBitmap> dest_frames;
  for (int i = 0; i < num_buffers; i++) {
    source_frames.push_back(
        VideoFrame::CreateBlackFrame(source_width, source_height));

    SkBitmap* bitmap = new SkBitmap();
    bitmap->setConfig(SkBitmap::kARGB_8888_Config,
                      dest_width, dest_height);
    bitmap->allocPixels();
    dest_frames.push_back(bitmap);
  }

  SkBitmap bitmap;
  bitmap.setConfig(SkBitmap::kARGB_8888_Config, source_width, source_height);
  bitmap.allocPixels();

  TimeTicks start = TimeTicks::HighResNow();
  for (int i = 0; i < num_frames; i++) {
    scoped_refptr<VideoFrame> source_frame = source_frames[i % num_buffers];
    SkBitmap* dest_bitmap = dest_frames[i % num_buffers];

    bitmap.lockPixels();
    media::ConvertYUVToRGB32(source_frame->data(VideoFrame::kYPlane),
                             source_frame->data(VideoFrame::kUPlane),
                             source_frame->data(VideoFrame::kVPlane),
                             static_cast<uint8*>(bitmap.getPixels()),
                             source_width,
                             source_height,
                             source_frame->stride(VideoFrame::kYPlane),
                             source_frame->stride(VideoFrame::kUPlane),
                             bitmap.rowBytes(),
                             media::YV12);
    bitmap.unlockPixels();

    SkCanvas canvas(*dest_bitmap);
    SkRect rect;
    rect.set(0, 0, SkIntToScalar(dest_width),
             SkIntToScalar(dest_height));
    canvas.clipRect(rect);
    SkMatrix matrix;
    matrix.reset();
    matrix.preScale(SkIntToScalar(dest_width) /
                    SkIntToScalar(source_width),
                    SkIntToScalar(dest_height) /
                    SkIntToScalar(source_height));
    SkPaint paint;
    paint.setFlags(SkPaint::kFilterBitmap_Flag);
    canvas.drawBitmapMatrix(bitmap, matrix, &paint);
  }
  TimeTicks end = TimeTicks::HighResNow();
  return static_cast<double>((end - start).InMilliseconds()) / num_frames;
}

static double BenchmarkRGBToYUV() {
  int rgb_stride = source_width * 4;
  scoped_array<uint8> rgb_frame(new uint8[rgb_stride * source_height]);

  int y_stride = source_width;
  int uv_stride = source_width / 2;
  scoped_array<uint8> y_plane(new uint8[y_stride * source_height]);
  scoped_array<uint8> u_plane(new uint8[uv_stride * source_height / 2]);
  scoped_array<uint8> v_plane(new uint8[uv_stride * source_height / 2]);

  TimeTicks start = TimeTicks::HighResNow();

  for (int i = 0; i < num_frames; ++i) {
    media::ConvertRGB32ToYUV(rgb_frame.get(),
                             y_plane.get(),
                             u_plane.get(),
                             v_plane.get(),
                             source_width,
                             source_height,
                             rgb_stride,
                             y_stride,
                             uv_stride);
  }

  TimeTicks end = TimeTicks::HighResNow();
  return static_cast<double>((end - start).InMilliseconds()) / num_frames;
}

static double BenchmarkFilter(media::ScaleFilter filter) {
  std::vector<scoped_refptr<VideoFrame> > source_frames;
  std::vector<scoped_refptr<VideoFrame> > dest_frames;

  for (int i = 0; i < num_buffers; i++) {
    source_frames.push_back(
        VideoFrame::CreateBlackFrame(source_width, source_height));

    dest_frames.push_back(
        VideoFrame::CreateFrame(VideoFrame::RGB32,
                                dest_width,
                                dest_height,
                                TimeDelta::FromSeconds(0),
                                TimeDelta::FromSeconds(0)));
  }

  TimeTicks start = TimeTicks::HighResNow();
  for (int i = 0; i < num_frames; i++) {
    scoped_refptr<VideoFrame> source_frame = source_frames[i % num_buffers];
    scoped_refptr<VideoFrame> dest_frame = dest_frames[i % num_buffers];

    media::ScaleYUVToRGB32(source_frame->data(VideoFrame::kYPlane),
                           source_frame->data(VideoFrame::kUPlane),
                           source_frame->data(VideoFrame::kVPlane),
                           dest_frame->data(0),
                           source_width,
                           source_height,
                           dest_width,
                           dest_height,
                           source_frame->stride(VideoFrame::kYPlane),
                           source_frame->stride(VideoFrame::kUPlane),
                           dest_frame->stride(0),
                           media::YV12,
                           media::ROTATE_0,
                           filter);
  }
  TimeTicks end = TimeTicks::HighResNow();
  return static_cast<double>((end - start).InMilliseconds()) / num_frames;
}

static double BenchmarkScaleWithRect() {
  std::vector<scoped_refptr<VideoFrame> > source_frames;
  std::vector<scoped_refptr<VideoFrame> > dest_frames;

  for (int i = 0; i < num_buffers; i++) {
    source_frames.push_back(
        VideoFrame::CreateBlackFrame(source_width, source_height));

    dest_frames.push_back(
        VideoFrame::CreateFrame(VideoFrame::RGB32,
                                dest_width,
                                dest_height,
                                TimeDelta::FromSeconds(0),
                                TimeDelta::FromSeconds(0)));
  }

  TimeTicks start = TimeTicks::HighResNow();
  for (int i = 0; i < num_frames; i++) {
    scoped_refptr<VideoFrame> source_frame = source_frames[i % num_buffers];
    scoped_refptr<VideoFrame> dest_frame = dest_frames[i % num_buffers];

    media::ScaleYUVToRGB32WithRect(
        source_frame->data(VideoFrame::kYPlane),
        source_frame->data(VideoFrame::kUPlane),
        source_frame->data(VideoFrame::kVPlane),
        dest_frame->data(0),
        source_width,
        source_height,
        dest_width,
        dest_height,
        0, 0,
        dest_width,
        dest_height,
        source_frame->stride(VideoFrame::kYPlane),
        source_frame->stride(VideoFrame::kUPlane),
        dest_frame->stride(0));
  }
  TimeTicks end = TimeTicks::HighResNow();
  return static_cast<double>((end - start).InMilliseconds()) / num_frames;
}

int main(int argc, const char** argv) {
  CommandLine::Init(argc, argv);
  const CommandLine* cmd_line = CommandLine::ForCurrentProcess();

  if (!cmd_line->GetArgs().empty()) {
    std::cerr << "Usage: " << argv[0] << " [OPTIONS]\n"
              << "  --frames=N                      "
              << "Number of frames\n"
              << "  --buffers=N                     "
              << "Number of buffers\n"
              << "  --src-w=N                       "
              << "Width of the source image\n"
              << "  --src-h=N                       "
              << "Height of the source image\n"
              << "  --dest-w=N                      "
              << "Width of the destination image\n"
              << "  --dest-h=N                      "
              << "Height of the destination image\n"
              << std::endl;
    return 1;
  }

  std::string source_width_param(cmd_line->GetSwitchValueASCII("src-w"));
  if (!source_width_param.empty() &&
      !base::StringToInt(source_width_param, &source_width)) {
    source_width = 0;
  }

  std::string source_height_param(cmd_line->GetSwitchValueASCII("src-h"));
  if (!source_height_param.empty() &&
      !base::StringToInt(source_height_param, &source_height)) {
    source_height = 0;
  }

  std::string dest_width_param(cmd_line->GetSwitchValueASCII("dest-w"));
  if (!dest_width_param.empty() &&
      !base::StringToInt(dest_width_param, &dest_width)) {
    dest_width = 0;
  }

  std::string dest_height_param(cmd_line->GetSwitchValueASCII("dest-h"));
  if (!dest_height_param.empty() &&
      !base::StringToInt(dest_height_param, &dest_height)) {
    dest_height = 0;
  }

  std::string frames_param(cmd_line->GetSwitchValueASCII("frames"));
  if (!frames_param.empty() &&
      !base::StringToInt(frames_param, &num_frames)) {
    num_frames = 0;
  }

  std::string buffers_param(cmd_line->GetSwitchValueASCII("buffers"));
  if (!buffers_param.empty() &&
      !base::StringToInt(buffers_param, &num_buffers)) {
    num_buffers = 0;
  }

  std::cout << "Source image size: " << source_width
            << "x" << source_height << std::endl;
  std::cout << "Destination image size: " << dest_width
            << "x" << dest_height << std::endl;
  std::cout << "Number of frames: " << num_frames << std::endl;
  std::cout << "Number of buffers: " << num_buffers << std::endl;

  std::cout << "Skia: " << BenchmarkSkia()
            << "ms/frame" << std::endl;
  std::cout << "RGB To YUV: " << BenchmarkRGBToYUV()
            << "ms/frame" << std::endl;
  std::cout << "No filtering: " << BenchmarkFilter(media::FILTER_NONE)
            << "ms/frame" << std::endl;
  std::cout << "Bilinear Vertical: "
            << BenchmarkFilter(media::FILTER_BILINEAR_V)
            << "ms/frame" << std::endl;
  std::cout << "Bilinear Horizontal: "
            << BenchmarkFilter(media::FILTER_BILINEAR_H)
            << "ms/frame" << std::endl;
  std::cout << "Bilinear: " << BenchmarkFilter(media::FILTER_BILINEAR)
            << "ms/frame" << std::endl;
  std::cout << "Bilinear with rect: " << BenchmarkScaleWithRect()
            << "ms/frame" << std::endl;

  return 0;
}
