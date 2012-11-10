// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Movie class for WTL forms to call to control the media pipeline.

#ifndef MEDIA_TOOLS_PLAYER_WTL_MOVIE_H_
#define MEDIA_TOOLS_PLAYER_WTL_MOVIE_H_

#include "media/tools/player_wtl/player_wtl.h"

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "media/base/message_loop_factory.h"

template <typename T> struct DefaultSingletonTraits;

namespace media {

class AudioManager;
class Pipeline;
class VideoRendererBase;

class Movie {
 public:
  // Returns the singleton instance.
  static Movie* GetInstance();

  // Open a movie.
  bool Open(const wchar_t* url, VideoRendererBase* video_renderer);

  // Set playback rate.
  void Play(float rate);

  // Set playback rate.
  float GetPlayRate();

  // Get movie duration in seconds.
  float GetDuration();

  // Get current movie position in seconds.
  float GetPosition();

  // Set current movie position in seconds.
  void SetPosition(float position);

  // Set playback pause.
  void SetPause(bool pause);

  // Get playback pause state.
  bool GetPause();

  // Set buffer to render into.
  void SetFrameBuffer(HBITMAP hbmp, HWND hwnd);

  // Close movie.
  void Close();

  // Query if movie is currently open.
  bool IsOpen();

  // Enable/Disable audio.
  void SetAudioEnable(bool enable_audio);

  // Get Enable/Disable audio state.
  bool GetAudioEnable();

  // Enable/Disable draw.
  void SetDrawEnable(bool enable_draw);

  // Get Enable/Disable draw state.
  bool GetDrawEnable();

  // Enable/Disable dump yuv file.
  void SetDumpYuvFileEnable(bool enable_dump_yuv_file);

  // Get Enable/Disable dump yuv file state.
  bool GetDumpYuvFileEnable();

 private:
  // Only allow Singleton to create and delete Movie.
  friend struct DefaultSingletonTraits<Movie>;
  Movie();
  virtual ~Movie();

  scoped_refptr<Pipeline> pipeline_;
  scoped_ptr<media::MessageLoopFactory> message_loop_factory_;
  scoped_ptr<AudioManager> audio_manager_;

  bool enable_audio_;
  bool enable_draw_;
  bool enable_dump_yuv_file_;
  bool enable_pause_;
  int max_threads_;
  float play_rate_;
  HBITMAP movie_dib_;
  HWND movie_hwnd_;

  DISALLOW_COPY_AND_ASSIGN(Movie);
};

}  // namespace media

#endif  // MEDIA_TOOLS_PLAYER_WTL_MOVIE_H_
