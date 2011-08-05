// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/mac/audio_output_mac.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "media/audio/audio_util.h"
#include "media/audio/mac/audio_manager_mac.h"

// A custom data structure to store information an AudioQueue buffer.
struct AudioQueueUserData {
  AudioQueueUserData() : empty_buffer(false) {}
  bool empty_buffer;
};

// Overview of operation:
// 1) An object of PCMQueueOutAudioOutputStream is created by the AudioManager
// factory: audio_man->MakeAudioStream(). This just fills some structure.
// 2) Next some thread will call Open(), at that point the underliying OS
// queue is created and the audio buffers allocated.
// 3) Then some thread will call Start(source) At this point the source will be
// called to fill the initial buffers in the context of that same thread.
// Then the OS queue is started which will create its own thread which
// periodically will call the source for more data as buffers are being
// consumed.
// 4) At some point some thread will call Stop(), which we handle by directly
// stoping the OS queue.
// 5) One more callback to the source could be delivered in in the context of
// the queue's own thread. Data, if any will be discared.
// 6) The same thread that called stop will call Close() where we cleanup
// and notifiy the audio manager, which likley will destroy this object.

#if !defined(MAC_OS_X_VERSION_10_6) || \
    MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_6
enum {
  kAudioQueueErr_EnqueueDuringReset = -66632
};
#endif

PCMQueueOutAudioOutputStream::PCMQueueOutAudioOutputStream(
    AudioManagerMac* manager, AudioParameters params)
    : audio_queue_(NULL),
      source_(NULL),
      manager_(manager),
      packet_size_(params.GetPacketSize()),
      silence_bytes_(0),
      volume_(1),
      pending_bytes_(0),
      num_source_channels_(params.channels),
      source_layout_(params.channel_layout),
      num_core_channels_(0),
      should_swizzle_(false),
      should_down_mix_(false) {
  // We must have a manager.
  DCHECK(manager_);
  // A frame is one sample across all channels. In interleaved audio the per
  // frame fields identify the set of n |channels|. In uncompressed audio, a
  // packet is always one frame.
  format_.mSampleRate = params.sample_rate;
  format_.mFormatID = kAudioFormatLinearPCM;
  format_.mFormatFlags = kLinearPCMFormatFlagIsPacked;
  format_.mBitsPerChannel = params.bits_per_sample;
  format_.mChannelsPerFrame = params.channels;
  format_.mFramesPerPacket = 1;
  format_.mBytesPerPacket = (format_.mBitsPerChannel * params.channels) / 8;
  format_.mBytesPerFrame = format_.mBytesPerPacket;

  memset(core_channel_orderings_, 0, sizeof(core_channel_orderings_));
  memset(channel_remap_, 0, sizeof(channel_remap_));

  if (params.bits_per_sample > 8) {
    format_.mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
  }

  // Silence buffer has a duration of 6ms to simulate the behavior of Windows.
  // This value is choosen by experiments and macs cannot keep up with
  // anything less than 6ms.
  silence_bytes_ = format_.mBytesPerFrame * params.sample_rate * 6 / 1000;
}

PCMQueueOutAudioOutputStream::~PCMQueueOutAudioOutputStream() {
}

void PCMQueueOutAudioOutputStream::HandleError(OSStatus err) {
  // source_ can be set to NULL from another thread. We need to cache its
  // pointer while we operate here. Note that does not mean that the source
  // has been destroyed.
  AudioSourceCallback* source = source_;
  if (source)
    source->OnError(this, static_cast<int>(err));
  NOTREACHED() << "error code " << err;
}

bool PCMQueueOutAudioOutputStream::Open() {
  // Get the default device id.
  AudioObjectID device_id = 0;
  AudioObjectPropertyAddress property_address = {
      kAudioHardwarePropertyDefaultOutputDevice,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMaster
  };
  UInt32 device_id_size = sizeof(device_id);
  OSStatus err = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                            &property_address, 0, NULL,
                                            &device_id_size, &device_id);
  if (err != noErr) {
    HandleError(err);
    return false;
  }
  // Get the size of the channel layout.
  UInt32 core_layout_size;
  // TODO(annacc): AudioDeviceGetPropertyInfo() is deprecated, but its
  // replacement, AudioObjectGetPropertyDataSize(), doesn't work yet with
  // kAudioDevicePropertyPreferredChannelLayout.
  err = AudioDeviceGetPropertyInfo(device_id, 0, false,
                                   kAudioDevicePropertyPreferredChannelLayout,
                                   &core_layout_size, NULL);
  if (err != noErr) {
    HandleError(err);
    return false;
  }
  // Get the device's channel layout.  This layout may vary in sized based on
  // the number of channels.  Use |core_layout_size| to allocate memory.
  scoped_ptr_malloc<AudioChannelLayout> core_channel_layout;
  core_channel_layout.reset(
      reinterpret_cast<AudioChannelLayout*>(malloc(core_layout_size)));
  memset(core_channel_layout.get(), 0, core_layout_size);
  // TODO(annacc): AudioDeviceGetProperty() is deprecated, but its
  // replacement, AudioObjectGetPropertyData(), doesn't work yet with
  // kAudioDevicePropertyPreferredChannelLayout.
  err = AudioDeviceGetProperty(device_id, 0, false,
                               kAudioDevicePropertyPreferredChannelLayout,
                               &core_layout_size, core_channel_layout.get());
  if (err != noErr) {
    HandleError(err);
    return false;
  }

  num_core_channels_ =
      static_cast<int>(core_channel_layout->mNumberChannelDescriptions);
  if (num_core_channels_ == 2 &&
      ChannelLayoutToChannelCount(source_layout_) > 2) {
    should_down_mix_ = true;
    format_.mChannelsPerFrame = num_core_channels_;
    format_.mBytesPerFrame = (format_.mBitsPerChannel >> 3) *
        format_.mChannelsPerFrame;
    format_.mBytesPerPacket = format_.mBytesPerFrame * format_.mFramesPerPacket;
  } else {
    should_down_mix_ = false;
  }
  // Create the actual queue object and let the OS use its own thread to
  // run its CFRunLoop.
  err = AudioQueueNewOutput(&format_, RenderCallback, this, NULL,
                                     kCFRunLoopCommonModes, 0, &audio_queue_);
  if (err != noErr) {
    HandleError(err);
    return false;
  }
  // Allocate the hardware-managed buffers.
  for (uint32 ix = 0; ix != kNumBuffers; ++ix) {
    err = AudioQueueAllocateBuffer(audio_queue_, packet_size_, &buffer_[ix]);
    if (err != noErr) {
      HandleError(err);
      return false;
    }
    // Allocate memory for user data.
    buffer_[ix]->mUserData = new AudioQueueUserData();
  }
  // Set initial volume here.
  err = AudioQueueSetParameter(audio_queue_, kAudioQueueParam_Volume, 1.0);
  if (err != noErr) {
    HandleError(err);
    return false;
  }

  // Capture channel layout in a format we can use.
  for (int i = 0; i < CHANNELS_MAX; ++i)
    core_channel_orderings_[i] = kEmptyChannel;

  bool all_channels_unknown = true;
  for (int i = 0; i < num_core_channels_; ++i) {
    AudioChannelLabel label =
        core_channel_layout->mChannelDescriptions[i].mChannelLabel;
    if (label == kAudioChannelLabel_Unknown) {
      continue;
    }
    all_channels_unknown = false;
    switch (label) {
      case kAudioChannelLabel_Left:
        core_channel_orderings_[LEFT] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][LEFT];
        break;
      case kAudioChannelLabel_Right:
        core_channel_orderings_[RIGHT] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][RIGHT];
        break;
      case kAudioChannelLabel_Center:
        core_channel_orderings_[CENTER] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][CENTER];
        break;
      case kAudioChannelLabel_LFEScreen:
        core_channel_orderings_[LFE] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][LFE];
        break;
      case kAudioChannelLabel_LeftSurround:
        core_channel_orderings_[SIDE_LEFT] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][SIDE_LEFT];
        break;
      case kAudioChannelLabel_RightSurround:
        core_channel_orderings_[SIDE_RIGHT] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][SIDE_RIGHT];
        break;
      case kAudioChannelLabel_LeftCenter:
        core_channel_orderings_[LEFT_OF_CENTER] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][LEFT_OF_CENTER];
        break;
      case kAudioChannelLabel_RightCenter:
        core_channel_orderings_[RIGHT_OF_CENTER] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][RIGHT_OF_CENTER];
        break;
      case kAudioChannelLabel_CenterSurround:
        core_channel_orderings_[BACK_CENTER] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][BACK_CENTER];
        break;
      case kAudioChannelLabel_RearSurroundLeft:
        core_channel_orderings_[BACK_LEFT] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][BACK_LEFT];
        break;
      case kAudioChannelLabel_RearSurroundRight:
        core_channel_orderings_[BACK_RIGHT] = i;
        channel_remap_[i] = kChannelOrderings[source_layout_][BACK_RIGHT];
        break;
      default:
        DLOG(WARNING) << "Channel label not supported";
        channel_remap_[i] = kEmptyChannel;
        break;
    }
  }

  if (all_channels_unknown) {
    return true;
  }

  // Check if we need to adjust the layout.
  // If the device has a BACK_LEFT and no SIDE_LEFT and the source has
  // a SIDE_LEFT but no BACK_LEFT, then move (and preserve the channel).
  // e.g. CHANNEL_LAYOUT_5POINT1 -> CHANNEL_LAYOUT_5POINT1_BACK
  CheckForAdjustedLayout(SIDE_LEFT, BACK_LEFT);
  // Same for SIDE_RIGHT -> BACK_RIGHT.
  CheckForAdjustedLayout(SIDE_RIGHT, BACK_RIGHT);
  // Move BACK_LEFT to SIDE_LEFT.
  // e.g. CHANNEL_LAYOUT_5POINT1_BACK -> CHANNEL_LAYOUT_5POINT1
  CheckForAdjustedLayout(BACK_LEFT, SIDE_LEFT);
  // Same for BACK_RIGHT -> SIDE_RIGHT.
  CheckForAdjustedLayout(BACK_RIGHT, SIDE_RIGHT);
  // Move SIDE_LEFT to LEFT_OF_CENTER.
  // e.g. CHANNEL_LAYOUT_7POINT1 -> CHANNEL_LAYOUT_7POINT1_WIDE
  CheckForAdjustedLayout(SIDE_LEFT, LEFT_OF_CENTER);
  // Same for SIDE_RIGHT -> RIGHT_OF_CENTER.
  CheckForAdjustedLayout(SIDE_RIGHT, RIGHT_OF_CENTER);
  // Move LEFT_OF_CENTER to SIDE_LEFT.
  // e.g. CHANNEL_LAYOUT_7POINT1_WIDE -> CHANNEL_LAYOUT_7POINT1
  CheckForAdjustedLayout(LEFT_OF_CENTER, SIDE_LEFT);
  // Same for RIGHT_OF_CENTER -> SIDE_RIGHT.
  CheckForAdjustedLayout(RIGHT_OF_CENTER, SIDE_RIGHT);
  // For MONO -> STEREO, move audio to LEFT and RIGHT if applicable.
  CheckForAdjustedLayout(CENTER, LEFT);
  CheckForAdjustedLayout(CENTER, RIGHT);

  // Check if we will need to swizzle from source to device layout (maybe not!).
  should_swizzle_ = false;
  for (int i = 0; i < num_core_channels_; ++i) {
    if (kChannelOrderings[source_layout_][i] != core_channel_orderings_[i]) {
      should_swizzle_ = true;
      break;
    }
  }

  return true;
}

void PCMQueueOutAudioOutputStream::Close() {
  // It is valid to call Close() before calling Open(), thus audio_queue_
  // might be NULL.
  if (audio_queue_) {
    OSStatus err = 0;
    for (uint32 ix = 0; ix != kNumBuffers; ++ix) {
      if (buffer_[ix]) {
        // Free user data.
        delete static_cast<AudioQueueUserData*>(buffer_[ix]->mUserData);
        // Free AudioQueue buffer.
        err = AudioQueueFreeBuffer(audio_queue_, buffer_[ix]);
        if (err != noErr) {
          HandleError(err);
          break;
        }
      }
    }
    err = AudioQueueDispose(audio_queue_, true);
    if (err != noErr)
      HandleError(err);
  }
  // Inform the audio manager that we have been closed. This can cause our
  // destruction.
  manager_->ReleaseOutputStream(this);
}

void PCMQueueOutAudioOutputStream::Stop() {
  // We request a synchronous stop, so the next call can take some time. In
  // the windows implementation we block here as well.
  source_ = NULL;
  // We set the source to null to signal to the data queueing thread it can stop
  // queueing data, however at most one callback might still be in flight which
  // could attempt to enqueue right after the next call. Rather that trying to
  // use a lock we rely on the internal Mac queue lock so the enqueue might
  // succeed or might fail but it won't crash or leave the queue itself in an
  // inconsistent state.
  OSStatus err = AudioQueueStop(audio_queue_, true);
  if (err != noErr)
    HandleError(err);
}

void PCMQueueOutAudioOutputStream::SetVolume(double volume) {
  if (!audio_queue_)
    return;
  volume_ = static_cast<float>(volume);
  OSStatus err = AudioQueueSetParameter(audio_queue_,
                                        kAudioQueueParam_Volume,
                                        volume);
  if (err != noErr) {
    HandleError(err);
  }
}

void PCMQueueOutAudioOutputStream::GetVolume(double* volume) {
  if (!audio_queue_)
    return;
  *volume = volume_;
}

template<class Format>
void PCMQueueOutAudioOutputStream::SwizzleLayout(Format* b, uint32 filled) {
  Format src_format[num_source_channels_];
  int filled_channels = (num_core_channels_ < num_source_channels_) ?
                        num_core_channels_ : num_source_channels_;
  for (uint32 i = 0; i < filled; i += sizeof(src_format),
      b += num_source_channels_) {
    // TODO(fbarchard): This could be further optimized with pshufb.
    memcpy(src_format, b, sizeof(src_format));
    for (int ch = 0; ch < filled_channels; ++ch) {
      if (channel_remap_[ch] != kEmptyChannel &&
          channel_remap_[ch] <= CHANNELS_MAX) {
        b[ch] = src_format[channel_remap_[ch]];
      } else {
        b[ch] = 0;
      }
    }
  }
}

bool PCMQueueOutAudioOutputStream::CheckForAdjustedLayout(
    Channels input_channel,
    Channels output_channel) {
  if (core_channel_orderings_[output_channel] > kEmptyChannel &&
      core_channel_orderings_[input_channel] == kEmptyChannel &&
      kChannelOrderings[source_layout_][input_channel] > kEmptyChannel &&
      kChannelOrderings[source_layout_][output_channel] == kEmptyChannel) {
    channel_remap_[core_channel_orderings_[output_channel]] =
        kChannelOrderings[source_layout_][input_channel];
    return true;
  }
  return false;
}

// Note to future hackers of this function: Do not add locks here because we
// call out to third party source that might do crazy things including adquire
// external locks or somehow re-enter here because its legal for them to call
// some audio functions.
void PCMQueueOutAudioOutputStream::RenderCallback(void* p_this,
                                                  AudioQueueRef queue,
                                                  AudioQueueBufferRef buffer) {
  PCMQueueOutAudioOutputStream* audio_stream =
      static_cast<PCMQueueOutAudioOutputStream*>(p_this);
  // Call the audio source to fill the free buffer with data. Not having a
  // source means that the queue has been closed. This is not an error.
  AudioSourceCallback* source = audio_stream->source_;
  if (!source)
    return;

  // Adjust the number of pending bytes by subtracting the amount played.
  if (!static_cast<AudioQueueUserData*>(buffer->mUserData)->empty_buffer)
    audio_stream->pending_bytes_ -= buffer->mAudioDataByteSize;
  uint32 capacity = buffer->mAudioDataBytesCapacity;
  // TODO(sergeyu): Specify correct hardware delay for AudioBuffersState.
  uint32 filled = source->OnMoreData(
      audio_stream, reinterpret_cast<uint8*>(buffer->mAudioData), capacity,
      AudioBuffersState(audio_stream->pending_bytes_, 0));

  // In order to keep the callback running, we need to provide a positive amount
  // of data to the audio queue. To simulate the behavior of Windows, we write
  // a buffer of silence.
  if (!filled) {
    CHECK(audio_stream->silence_bytes_ <= static_cast<int>(capacity));
    filled = audio_stream->silence_bytes_;

    // Assume unsigned audio.
    int silence_value = 128;
    if (audio_stream->format_.mBitsPerChannel > 8) {
      // When bits per channel is greater than 8, audio is signed.
      silence_value = 0;
    }

    memset(buffer->mAudioData, silence_value, filled);
    static_cast<AudioQueueUserData*>(buffer->mUserData)->empty_buffer = true;
  } else if (filled > capacity) {
    // User probably overran our buffer.
    audio_stream->HandleError(0);
    return;
  } else {
    static_cast<AudioQueueUserData*>(buffer->mUserData)->empty_buffer = false;
  }

  if (audio_stream->should_down_mix_) {
    // Downmixes the L, R, C channels to stereo.
    if (media::FoldChannels(buffer->mAudioData,
                            filled,
                            audio_stream->num_source_channels_,
                            audio_stream->format_.mBitsPerChannel >> 3,
                            audio_stream->volume_)) {
      filled = filled * 2 / audio_stream->num_source_channels_;
    } else {
      LOG(ERROR) << "Folding failed";
    }
  } else if (audio_stream->should_swizzle_) {
    // Handle channel order for surround sound audio.
    if (audio_stream->format_.mBitsPerChannel == 8) {
      audio_stream->SwizzleLayout(reinterpret_cast<uint8*>(buffer->mAudioData),
                                  filled);
    } else if (audio_stream->format_.mBitsPerChannel == 16) {
      audio_stream->SwizzleLayout(reinterpret_cast<int16*>(buffer->mAudioData),
                                  filled);
    } else if (audio_stream->format_.mBitsPerChannel == 32) {
      audio_stream->SwizzleLayout(reinterpret_cast<int32*>(buffer->mAudioData),
                                  filled);
    }
  }

  buffer->mAudioDataByteSize = filled;

  // Increment bytes by amount filled into audio buffer if this is not a
  // silence buffer.
  if (!static_cast<AudioQueueUserData*>(buffer->mUserData)->empty_buffer)
    audio_stream->pending_bytes_ += filled;
  if (NULL == queue)
    return;
  // Queue the audio data to the audio driver.
  OSStatus err = AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
  if (err != noErr) {
    if (err == kAudioQueueErr_EnqueueDuringReset) {
      // This is the error you get if you try to enqueue a buffer and the
      // queue has been closed. Not really a problem if indeed the queue
      // has been closed.
      if (!audio_stream->source_)
        return;
    }
    audio_stream->HandleError(err);
  }
}

void PCMQueueOutAudioOutputStream::Start(AudioSourceCallback* callback) {
  DCHECK(callback);
  OSStatus err = noErr;
  source_ = callback;
  pending_bytes_ = 0;
  // Ask the source to pre-fill all our buffers before playing.
  for (uint32 ix = 0; ix != kNumBuffers; ++ix) {
    buffer_[ix]->mAudioDataByteSize = 0;
    RenderCallback(this, NULL, buffer_[ix]);
  }

  // Queue the buffers to the audio driver, sounds starts now.
  for (uint32 ix = 0; ix != kNumBuffers; ++ix) {
    err = AudioQueueEnqueueBuffer(audio_queue_, buffer_[ix], 0, NULL);
    if (err != noErr) {
      HandleError(err);
      return;
    }
  }
  err  = AudioQueueStart(audio_queue_, NULL);
  if (err != noErr) {
    HandleError(err);
    return;
  }
}
