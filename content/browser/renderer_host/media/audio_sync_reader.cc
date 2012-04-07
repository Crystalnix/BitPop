// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/media/audio_sync_reader.h"

#include <algorithm>

#include "base/process_util.h"
#include "base/shared_memory.h"
#include "base/threading/platform_thread.h"
#include "media/audio/audio_buffers_state.h"
#include "media/audio/audio_util.h"

const int kMinIntervalBetweenReadCallsInMs = 10;

AudioSyncReader::AudioSyncReader(base::SharedMemory* shared_memory)
    : shared_memory_(shared_memory) {
}

AudioSyncReader::~AudioSyncReader() {
}

bool AudioSyncReader::DataReady() {
  return !media::IsUnknownDataSize(
      shared_memory_,
      media::PacketSizeSizeInBytes(shared_memory_->created_size()));
}

// media::AudioOutputController::SyncReader implementations.
void AudioSyncReader::UpdatePendingBytes(uint32 bytes) {
  if (bytes != static_cast<uint32>(media::AudioOutputController::kPauseMark)) {
    // Store unknown length of data into buffer, so we later
    // can find out if data became available.
    media::SetUnknownDataSize(
        shared_memory_,
        media::PacketSizeSizeInBytes(shared_memory_->created_size()));
  }
  base::AutoLock auto_lock(lock_);
  if (socket_.get()) {
    socket_->Send(&bytes, sizeof(bytes));
  }
}

uint32 AudioSyncReader::Read(void* data, uint32 size) {
  uint32 max_size = media::PacketSizeSizeInBytes(
      shared_memory_->created_size());

#if defined(OS_WIN)
  // HACK: yield if reader is called too often.
  // Problem is lack of synchronization between host and renderer. We cannot be
  // sure if renderer already filled the buffer, and due to all the plugins we
  // cannot change the API, so we yield if previous call was too recent.
  // Optimization: if renderer is "new" one that writes length of data we can
  // stop yielding the moment length is written -- not ideal solution,
  // but better than nothing.
  while (!DataReady() &&
         ((base::Time::Now() - previous_call_time_).InMilliseconds() <
          kMinIntervalBetweenReadCallsInMs)) {
    base::PlatformThread::YieldCurrentThread();
  }
  previous_call_time_ = base::Time::Now();
#endif

  uint32 read_size = std::min(media::GetActualDataSizeInBytes(shared_memory_,
                                                              max_size),
                              size);

  // Get the data from the buffer.
  memcpy(data, shared_memory_->memory(), read_size);

  // If amount read was less than requested, then zero out the remainder.
  if (read_size < size)
    memset(static_cast<char*>(data) + read_size, 0, size - read_size);

  // Zero out the entire buffer.
  memset(shared_memory_->memory(), 0, max_size);

  // Store unknown length of data into buffer, in case renderer does not store
  // the length itself. It also helps in decision if we need to yield.
  media::SetUnknownDataSize(shared_memory_, max_size);

  return read_size;
}

void AudioSyncReader::Close() {
  base::AutoLock auto_lock(lock_);
  if (socket_.get()) {
    socket_->Close();
    socket_.reset(NULL);
  }
}

bool AudioSyncReader::Init() {
  socket_.reset(new base::SyncSocket());
  foreign_socket_.reset(new base::SyncSocket());
  return base::SyncSocket::CreatePair(socket_.get(), foreign_socket_.get());
}

#if defined(OS_WIN)
bool AudioSyncReader::PrepareForeignSocketHandle(
    base::ProcessHandle process_handle,
    base::SyncSocket::Handle* foreign_handle) {
  ::DuplicateHandle(GetCurrentProcess(), foreign_socket_->handle(),
                    process_handle, foreign_handle,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
  if (*foreign_handle != 0)
    return true;
  return false;
}
#else
bool AudioSyncReader::PrepareForeignSocketHandle(
    base::ProcessHandle process_handle,
    base::FileDescriptor* foreign_handle) {
  foreign_handle->fd = foreign_socket_->handle();
  foreign_handle->auto_close = false;
  if (foreign_handle->fd != -1)
    return true;
  return false;
}
#endif
