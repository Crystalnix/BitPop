// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/fake_audio_output_stream.h"

#include "base/at_exit.h"
#include "base/logging.h"

bool FakeAudioOutputStream::has_created_fake_stream_ = false;
FakeAudioOutputStream* FakeAudioOutputStream::last_fake_stream_ = NULL;

// static
AudioOutputStream* FakeAudioOutputStream::MakeFakeStream(
    AudioParameters params) {
  if (!has_created_fake_stream_)
    base::AtExitManager::RegisterCallback(&DestroyLastFakeStream, NULL);
  has_created_fake_stream_ = true;

  FakeAudioOutputStream* new_stream = new FakeAudioOutputStream(params);

  if (last_fake_stream_) {
    DCHECK(last_fake_stream_->closed_);
    delete last_fake_stream_;
  }
  last_fake_stream_ = new_stream;

  return new_stream;
}

// static
FakeAudioOutputStream* FakeAudioOutputStream::GetLastFakeStream() {
  return last_fake_stream_;
}

bool FakeAudioOutputStream::Open() {
  if (packet_size_ < sizeof(int16))
    return false;
  buffer_.reset(new uint8[packet_size_]);
  return true;
}

void FakeAudioOutputStream::Start(AudioSourceCallback* callback)  {
  callback_ = callback;
  memset(buffer_.get(), 0, packet_size_);
  callback_->OnMoreData(this, buffer_.get(), packet_size_,
                        AudioBuffersState(0, 0));
}

void FakeAudioOutputStream::Stop() {
  callback_ = NULL;
}

void FakeAudioOutputStream::SetVolume(double volume) {
  volume_ = volume;
}

void FakeAudioOutputStream::GetVolume(double* volume) {
  *volume = volume_;
}

void FakeAudioOutputStream::Close() {
  closed_ = true;
}

FakeAudioOutputStream::FakeAudioOutputStream(AudioParameters params)
    : volume_(0),
      callback_(NULL),
      packet_size_(params.GetPacketSize()),
      closed_(false) {
}

FakeAudioOutputStream::~FakeAudioOutputStream() {}

// static
void FakeAudioOutputStream::DestroyLastFakeStream(void* param) {
  if (last_fake_stream_) {
    DCHECK(last_fake_stream_->closed_);
    delete last_fake_stream_;
  }
}
