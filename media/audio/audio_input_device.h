// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Low-latency audio capturing class utilizing audio input stream provided
// by a server (browser) process by use of an IPC interface.
//
// Relationship of classes:
//
//  AudioInputController                 AudioInputDevice
//           ^                                  ^
//           |                                  |
//           v                  IPC             v
// AudioInputRendererHost  <---------> AudioInputIPCDelegate
//           ^                       (impl in AudioInputMessageFilter)
//           |
//           v
// AudioInputDeviceManager
//
// Transportation of audio samples from the browser to the render process
// is done by using shared memory in combination with a SyncSocket.
// The AudioInputDevice user registers an AudioInputDevice::CaptureCallback by
// calling Initialize().  The callback will be called with recorded audio from
// the underlying audio layers.
// The session ID is used by the AudioInputRendererHost to start the device
// referenced by this ID.
//
// State sequences:
//
// Sequence where session_id has not been set using SetDevice():
// ('<-' signifies callbacks, -> signifies calls made by AudioInputDevice)
// Start -> InitializeOnIOThread -> CreateStream ->
//       <- OnStreamCreated <-
//       -> StartOnIOThread -> PlayStream ->
//
// Sequence where session_id has been set using SetDevice():
// Start -> InitializeOnIOThread -> StartDevice ->
//       <- OnDeviceReady <-
//       -> CreateStream ->
//       <- OnStreamCreated <-
//       -> StartOnIOThread -> PlayStream ->
//
// AudioInputDevice::Capture => low latency audio transport on audio thread =>
//                               |
// Stop --> ShutDownOnIOThread ------>  CloseStream -> Close
//
// This class depends on two threads to function:
//
// 1. An IO thread.
//    This thread is used to asynchronously process Start/Stop etc operations
//    that are available via the public interface.  The public methods are
//    asynchronous and simply post a task to the IO thread to actually perform
//    the work.
// 2. Audio transport thread.
//    Responsible for calling the CaptureCallback and feed audio samples from
//    the server side audio layer using a socket and shared memory.
//
// Implementation notes:
// - The user must call Stop() before deleting the class instance.

#ifndef MEDIA_AUDIO_AUDIO_INPUT_DEVICE_H_
#define MEDIA_AUDIO_AUDIO_INPUT_DEVICE_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/shared_memory.h"
#include "media/audio/audio_device_thread.h"
#include "media/audio/audio_input_ipc.h"
#include "media/audio/audio_parameters.h"
#include "media/audio/scoped_loop_observer.h"
#include "media/base/media_export.h"

namespace media {

// TODO(henrika): This class is based on the AudioOutputDevice class and it has
// many components in common. Investigate potential for re-factoring.
// TODO(henrika): Add support for event handling (e.g. OnStateChanged,
// OnCaptureStopped etc.) and ensure that we can deliver these notifications
// to any clients using this class.
class MEDIA_EXPORT AudioInputDevice
    : NON_EXPORTED_BASE(public AudioInputIPCDelegate),
      NON_EXPORTED_BASE(public ScopedLoopObserver),
      public base::RefCountedThreadSafe<AudioInputDevice> {
 public:
  class MEDIA_EXPORT CaptureCallback {
   public:
    virtual void Capture(const std::vector<float*>& audio_data,
                         int number_of_frames,
                         int audio_delay_milliseconds,
                         double volume) = 0;
    virtual void OnCaptureError() = 0;
   protected:
    virtual ~CaptureCallback();
  };

  class MEDIA_EXPORT CaptureEventHandler {
   public:
    // Notification to the client that the device with the specific |device_id|
    // has been started.
    // This callback is triggered as a result of StartDevice().
    virtual void OnDeviceStarted(const std::string& device_id) = 0;

    // Notification to the client that the device has been stopped.
    virtual void OnDeviceStopped() = 0;

   protected:
    virtual ~CaptureEventHandler();
  };

  AudioInputDevice(AudioInputIPC* ipc,
                   const scoped_refptr<base::MessageLoopProxy>& io_loop);

  // Initializes the AudioInputDevice.  This method must be called before
  // any other methods can be used.
  void Initialize(const AudioParameters& params,
                  CaptureCallback* callback,
                  CaptureEventHandler* event_handler);

  // Specify the |session_id| to query which device to use.
  // Start() will use the second sequence if this method is called before.
  void SetDevice(int session_id);

  // Starts audio capturing.
  // TODO(henrika): add support for notification when recording has started.
  void Start();

  // Stops audio capturing.
  // TODO(henrika): add support for notification when recording has stopped.
  void Stop();

  // Sets the capture volume scaling, with range [0.0, 1.0] inclusive.
  // Returns |true| on success.
  void SetVolume(double volume);

  // Sets the Automatic Gain Control state to on or off.
  // This method must be called before Start(). It will not have any effect
  // if it is called while capturing has already started.
  void SetAutomaticGainControl(bool enabled);

 protected:
  // Methods called on IO thread ----------------------------------------------
  // AudioInputIPCDelegate implementation.
  virtual void OnStreamCreated(base::SharedMemoryHandle handle,
                               base::SyncSocket::Handle socket_handle,
                               int length) OVERRIDE;
  virtual void OnVolume(double volume) OVERRIDE;
  virtual void OnStateChanged(
      AudioInputIPCDelegate::State state) OVERRIDE;
  virtual void OnDeviceReady(const std::string& device_id) OVERRIDE;
  virtual void OnIPCClosed() OVERRIDE;

  friend class base::RefCountedThreadSafe<AudioInputDevice>;
  virtual ~AudioInputDevice();

 private:
  // Methods called on IO thread ----------------------------------------------
  // The following methods are tasks posted on the IO thread that needs to
  // be executed on that thread. They interact with AudioInputMessageFilter and
  // sends IPC messages on that thread.
  void InitializeOnIOThread();
  void SetSessionIdOnIOThread(int session_id);
  void StartOnIOThread();
  void ShutDownOnIOThread();
  void SetVolumeOnIOThread(double volume);
  void SetAutomaticGainControlOnIOThread(bool enabled);

  // MessageLoop::DestructionObserver implementation for the IO loop.
  // If the IO loop dies before we do, we shut down the audio thread from here.
  virtual void WillDestroyCurrentMessageLoop() OVERRIDE;

  AudioParameters audio_parameters_;

  CaptureCallback* callback_;
  CaptureEventHandler* event_handler_;

  AudioInputIPC* ipc_;

  // Our stream ID on the message filter. Only modified on the IO thread.
  int stream_id_;

  // The media session ID used to identify which input device to be started.
  // Only modified on the IO thread.
  int session_id_;

  // State variable used to indicate it is waiting for a OnDeviceReady()
  // callback. Only modified on the IO thread.
  bool pending_device_ready_;

  // Stores the Automatic Gain Control state. Default is false.
  // Only modified on the IO thread.
  bool agc_is_enabled_;

  // Our audio thread callback class.  See source file for details.
  class AudioThreadCallback;

  // In order to avoid a race between OnStreamCreated and Stop(), we use this
  // guard to control stopping and starting the audio thread.
  base::Lock audio_thread_lock_;
  AudioDeviceThread audio_thread_;
  scoped_ptr<AudioInputDevice::AudioThreadCallback> audio_callback_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(AudioInputDevice);
};

}  // namespace media

#endif  // MEDIA_AUDIO_AUDIO_INPUT_DEVICE_H_
