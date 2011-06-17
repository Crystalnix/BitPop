// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Creates an output stream based on the ALSA PCM interface.
//
// On device write failure, the stream will move itself to an invalid state.
// No more data will be pulled from the data source, or written to the device.
// All calls to public API functions will either no-op themselves, or return an
// error if possible.  Specifically, If the stream is in an error state, Open()
// will return false, and Start() will call OnError() immediately on the
// provided callback.
//
// TODO(ajwong): The OnClose() and OnError() calling needing fixing.
//
// If the stream is successfully opened, Close() must be called before the
// stream is deleted as Close() is responsible for ensuring resource cleanup
// occurs.
//
// This object's thread-safety is a little tricky.  This object's public API
// can only be called from the thread that created the object.  Calling the
// public APIs in any method that may cause concurrent execution will result in
// a race condition.  When modifying the code in this class, please read the
// threading assumptions at the top of the implementation file to avoid
// introducing race conditions between tasks posted to the internal
// message_loop, and the thread calling the public APIs.
//
// TODO(sergeyu): AlsaPcmOutputStream is always created and used from the
// audio thread (i.e. |client_thread_loop_| and |message_loop_| always point
// to the same thread), so it doesn't need to be thread-safe anymore.
//
// TODO(sergeyu): Remove refcounter from AlsaPcmOutputStream and use
// ScopedRunnableMethodFactory to create tasks.

#ifndef MEDIA_AUDIO_LINUX_ALSA_OUTPUT_H_
#define MEDIA_AUDIO_LINUX_ALSA_OUTPUT_H_

#include <alsa/asoundlib.h>

#include <string>

#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "media/audio/audio_io.h"
#include "media/audio/audio_parameters.h"

namespace media {
class SeekableBuffer;
};  // namespace media

class AlsaWrapper;
class AudioManagerLinux;
class MessageLoop;

class AlsaPcmOutputStream :
    public AudioOutputStream,
    public base::RefCountedThreadSafe<AlsaPcmOutputStream> {
 public:
  // String for the generic "default" ALSA device that has the highest
  // compatibility and chance of working.
  static const char kDefaultDevice[];

  // Pass this to the AlsaPcmOutputStream if you want to attempt auto-selection
  // of the audio device.
  static const char kAutoSelectDevice[];

  // Prefix for device names to enable ALSA library resampling.
  static const char kPlugPrefix[];

  // The minimum latency that is accepted by the device.
  static const uint32 kMinLatencyMicros;

  // Create a PCM Output stream for the ALSA device identified by
  // |device_name|.  The AlsaPcmOutputStream uses |wrapper| to communicate with
  // the alsa libraries, allowing for dependency injection during testing.  All
  // requesting of data, and writing to the alsa device will be done on
  // |message_loop|.
  //
  // If unsure of what to use for |device_name|, use |kAutoSelectDevice|.
  AlsaPcmOutputStream(const std::string& device_name,
                      AudioParameters params,
                      AlsaWrapper* wrapper,
                      AudioManagerLinux* manager,
                      MessageLoop* message_loop);

  // Implementation of AudioOutputStream.
  virtual bool Open();
  virtual void Close();
  virtual void Start(AudioSourceCallback* callback);
  virtual void Stop();
  virtual void SetVolume(double volume);
  virtual void GetVolume(double* volume);

 private:
  friend class base::RefCountedThreadSafe<AlsaPcmOutputStream>;
  friend class AlsaPcmOutputStreamTest;
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest,
                           AutoSelectDevice_DeviceSelect);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest,
                           AutoSelectDevice_FallbackDevices);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, AutoSelectDevice_HintFail);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, BufferPacket);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, BufferPacket_Negative);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, BufferPacket_StopStream);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, BufferPacket_Underrun);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, BufferPacket_FullBuffer);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, ConstructedState);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, LatencyFloor);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, OpenClose);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, PcmOpenFailed);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, PcmSetParamsFailed);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, ScheduleNextWrite);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest,
                           ScheduleNextWrite_StopStream);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, StartStop);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, WritePacket_FinishedPacket);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, WritePacket_NormalPacket);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, WritePacket_StopStream);
  FRIEND_TEST_ALL_PREFIXES(AlsaPcmOutputStreamTest, WritePacket_WriteFails);

  virtual ~AlsaPcmOutputStream();

  // Flags indicating the state of the stream.
  enum InternalState {
    kInError = 0,
    kCreated,
    kIsOpened,
    kIsPlaying,
    kIsStopped,
    kIsClosed
  };
  friend std::ostream& operator<<(std::ostream& os, InternalState);

  // Various tasks that complete actions started in the public API.
  void OpenTask();
  void StartTask();
  void CloseTask();

  // Functions to get another packet from the data source and write it into the
  // ALSA device.
  void BufferPacket(bool* source_exhausted);
  void WritePacket();
  void WriteTask();
  void ScheduleNextWrite(bool source_exhausted);

  // Utility functions for talking with the ALSA API.
  static uint32 FramesToMicros(uint32 frames, uint32 sample_rate);
  static uint32 FramesToMillis(uint32 frames, uint32 sample_rate);
  std::string FindDeviceForChannels(uint32 channels);
  snd_pcm_sframes_t GetAvailableFrames();
  snd_pcm_sframes_t GetCurrentDelay();

  // Attempts to find the best matching linux audio device for the given number
  // of channels.  This function will set |device_name_| and |should_downmix_|.
  snd_pcm_t* AutoSelectDevice(uint32 latency);

  // Thread-asserting accessors for member variables.
  AudioManagerLinux* manager();

  // Struct holding all mutable the data that must be shared by the
  // message_loop() and the thread that created the object.
  class SharedData {
   public:
    explicit SharedData(MessageLoop* state_transition_loop);

    // Functions to safeguard state transitions and ensure that transitions are
    // only allowed occuring on the thread that created the object.  All
    // changes to the object state should go through these functions.
    bool CanTransitionTo(InternalState to);
    bool CanTransitionTo_Locked(InternalState to);
    InternalState TransitionTo(InternalState to);
    InternalState state();

    float volume();
    void set_volume(float v);

    // API for Proxying calls to the AudioSourceCallback provided during
    // Start().  These APIs are threadsafe.
    //
    // TODO(ajwong): This is necessary because the ownership semantics for the
    // |source_callback_| object are incorrect in AudioRenderHost. The callback
    // is passed into the output stream, but ownership is not transfered which
    // requires a synchronization on access of the |source_callback_| to avoid
    // using a deleted callback.
    uint32 OnMoreData(AudioOutputStream* stream, uint8* dest,
                      uint32 max_size, AudioBuffersState buffers_state);
    void OnError(AudioOutputStream* stream, int code);

    // Changes the AudioSourceCallback to proxy calls to.  Pass in NULL to
    // release ownership of the currently registered callback.
    void set_source_callback(AudioSourceCallback* callback);

   private:
    base::Lock lock_;

    InternalState state_;
    float volume_;  // Volume level from 0.0 to 1.0.

    AudioSourceCallback* source_callback_;

    MessageLoop* const state_transition_loop_;

    DISALLOW_COPY_AND_ASSIGN(SharedData);
  } shared_data_;

  // Configuration constants from the constructor.  Referenceable by all threads
  // since they are constants.
  const std::string requested_device_name_;
  const snd_pcm_format_t pcm_format_;
  const uint32 channels_;
  const uint32 sample_rate_;
  const uint32 bytes_per_sample_;
  const uint32 bytes_per_frame_;

  // Device configuration data. Populated after OpenTask() completes.
  std::string device_name_;
  bool should_downmix_;
  uint32 packet_size_;
  uint32 micros_per_packet_;
  uint32 latency_micros_;
  uint32 bytes_per_output_frame_;
  uint32 alsa_buffer_frames_;

  // Flag indicating the code should stop reading from the data source or
  // writing to the ALSA device.  This is set because the device has entered
  // an unrecoverable error state, or the ClosedTask() has executed.
  bool stop_stream_;

  // Wrapper class to invoke all the ALSA functions.
  AlsaWrapper* wrapper_;

  // Audio manager that created us.  Used to report that we've been closed.
  // This should only be used on the |client_thread_loop_|.  Access via
  // the manager() function.
  AudioManagerLinux* manager_;

  // Handle to the actual PCM playback device.
  snd_pcm_t* playback_handle_;

  scoped_ptr<media::SeekableBuffer> buffer_;
  uint32 frames_per_packet_;

  // Used to check which message loop is allowed to call the public APIs.
  MessageLoop* client_thread_loop_;

  // The message loop responsible for querying the data source, and writing to
  // the output device.
  MessageLoop* message_loop_;

  DISALLOW_COPY_AND_ASSIGN(AlsaPcmOutputStream);
};

#endif  // MEDIA_AUDIO_LINUX_ALSA_OUTPUT_H_
