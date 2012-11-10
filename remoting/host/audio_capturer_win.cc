// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <mmsystem.h>

#include <stdlib.h>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/timer.h"
#include "base/win/scoped_co_mem.h"
#include "base/win/scoped_com_initializer.h"
#include "base/win/scoped_comptr.h"
#include "remoting/host/audio_capturer.h"
#include "remoting/proto/audio.pb.h"

namespace {
const int kChannels = 2;
const int kBitsPerSample = 16;
const int kBitsPerByte = 8;
// Conversion factor from 100ns to 1ms.
const int kHnsToMs = 10000;

// Tolerance for catching packets of silence. If all samples have absolute
// value less than this threshold, the packet will be counted as a packet of
// silence. A value of 2 was chosen, because Windows can give samples of 1 and
// -1, even when no audio is playing.
const int kSilenceThreshold = 2;
}  // namespace

namespace remoting {

class AudioCapturerWin : public AudioCapturer {
 public:
  AudioCapturerWin();
  virtual ~AudioCapturerWin();

  // AudioCapturer interface.
  virtual bool Start(const PacketCapturedCallback& callback) OVERRIDE;
  virtual void Stop() OVERRIDE;
  virtual bool IsRunning() OVERRIDE;

 private:
  // Receives all packets from the audio capture endpoint buffer and pushes them
  // to the network.
  void DoCapture();

  static bool IsPacketOfSilence(const AudioPacket* packet);

  PacketCapturedCallback callback_;

  AudioPacket::SamplingRate sampling_rate_;

  scoped_ptr<base::RepeatingTimer<AudioCapturerWin> > capture_timer_;
  base::TimeDelta audio_device_period_;

  base::win::ScopedCoMem<WAVEFORMATEX> wave_format_ex_;
  base::win::ScopedComPtr<IAudioCaptureClient> audio_capture_client_;
  base::win::ScopedComPtr<IAudioClient> audio_client_;
  base::win::ScopedComPtr<IMMDevice> mm_device_;
  scoped_ptr<base::win::ScopedCOMInitializer> com_initializer_;

  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(AudioCapturerWin);
};

AudioCapturerWin::AudioCapturerWin()
    : sampling_rate_(AudioPacket::SAMPLING_RATE_INVALID) {
      thread_checker_.DetachFromThread();
}

AudioCapturerWin::~AudioCapturerWin() {
}

bool AudioCapturerWin::Start(const PacketCapturedCallback& callback) {
  DCHECK(!audio_capture_client_.get());
  DCHECK(!audio_client_.get());
  DCHECK(!mm_device_.get());
  DCHECK(static_cast<PWAVEFORMATEX>(wave_format_ex_) == NULL);
  DCHECK(thread_checker_.CalledOnValidThread());

  callback_ = callback;

  // Initialize the capture timer.
  capture_timer_.reset(new base::RepeatingTimer<AudioCapturerWin>());

  HRESULT hr = S_OK;
  com_initializer_.reset(new base::win::ScopedCOMInitializer());

  base::win::ScopedComPtr<IMMDeviceEnumerator> mm_device_enumerator;
  hr = mm_device_enumerator.CreateInstance(__uuidof(MMDeviceEnumerator));
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to create IMMDeviceEnumerator. Error " << hr;
    return false;
  }

  // Get the audio endpoint.
  hr = mm_device_enumerator->GetDefaultAudioEndpoint(eRender,
                                                     eConsole,
                                                     mm_device_.Receive());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to get IMMDevice. Error " << hr;
    return false;
  }

  // Get an audio client.
  hr = mm_device_->Activate(__uuidof(IAudioClient),
                            CLSCTX_ALL,
                            NULL,
                            audio_client_.ReceiveVoid());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to get an IAudioClient. Error " << hr;
    return false;
  }

  REFERENCE_TIME device_period;
  hr = audio_client_->GetDevicePeriod(&device_period, NULL);
  if (FAILED(hr)) {
    LOG(ERROR) << "IAudioClient::GetDevicePeriod failed. Error " << hr;
    return false;
  }
  audio_device_period_ = base::TimeDelta::FromMilliseconds(
      device_period / kChannels / kHnsToMs);

  // Get the wave format.
  hr = audio_client_->GetMixFormat(&wave_format_ex_);
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to get WAVEFORMATEX. Error " << hr;
    return false;
  }

  // Set the wave format
  switch (wave_format_ex_->wFormatTag) {
    case WAVE_FORMAT_IEEE_FLOAT:
      // Intentional fall-through.
    case WAVE_FORMAT_PCM:
      if (!AudioCapturer::IsValidSampleRate(wave_format_ex_->nSamplesPerSec)) {
        LOG(ERROR) << "Host sampling rate is neither 44.1 kHz nor 48 kHz.";
        return false;
      }
      sampling_rate_ = static_cast<AudioPacket::SamplingRate>(
          wave_format_ex_->nSamplesPerSec);

      wave_format_ex_->wFormatTag = WAVE_FORMAT_PCM;
      wave_format_ex_->nChannels = kChannels;
      wave_format_ex_->wBitsPerSample = kBitsPerSample;
      wave_format_ex_->nBlockAlign = kChannels * kBitsPerSample / kBitsPerByte;
      wave_format_ex_->nAvgBytesPerSec =
          sampling_rate_ * kChannels * kBitsPerSample / kBitsPerByte;
      break;
    case WAVE_FORMAT_EXTENSIBLE: {
      PWAVEFORMATEXTENSIBLE wave_format_extensible =
          reinterpret_cast<WAVEFORMATEXTENSIBLE*>(
          static_cast<WAVEFORMATEX*>(wave_format_ex_));
      if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT,
                      wave_format_extensible->SubFormat)) {
        if (!AudioCapturer::IsValidSampleRate(
                wave_format_extensible->Format.nSamplesPerSec)) {
          LOG(ERROR) << "Host sampling rate is neither 44.1 kHz nor 48 kHz.";
          return false;
        }
        sampling_rate_ = static_cast<AudioPacket::SamplingRate>(
            wave_format_extensible->Format.nSamplesPerSec);

        wave_format_extensible->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        wave_format_extensible->Samples.wValidBitsPerSample = kBitsPerSample;

        wave_format_extensible->Format.nChannels = kChannels;
        wave_format_extensible->Format.nSamplesPerSec = sampling_rate_;
        wave_format_extensible->Format.wBitsPerSample = kBitsPerSample;
        wave_format_extensible->Format.nBlockAlign =
            kChannels * kBitsPerSample / kBitsPerByte;
        wave_format_extensible->Format.nAvgBytesPerSec =
            sampling_rate_ * kChannels * kBitsPerSample / kBitsPerByte;
      } else {
        LOG(ERROR) << "Failed to force 16-bit samples";
        return false;
      }
      break;
    }
    default:
      LOG(ERROR) << "Failed to force 16-bit PCM";
      return false;
  }

  // Initialize the IAudioClient.
  hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 0,
                                 0,
                                 wave_format_ex_,
                                 NULL);
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to initialize IAudioClient. Error " << hr;
    return false;
  }

  // Get an IAudioCaptureClient.
  hr = audio_client_->GetService(__uuidof(IAudioCaptureClient),
                                 audio_capture_client_.ReceiveVoid());
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to get an IAudioCaptureClient. Error " << hr;
    return false;
  }

  // Start the IAudioClient.
  hr = audio_client_->Start();
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to start IAudioClient. Error " << hr;
    return false;
  }

  // Start capturing.
  capture_timer_->Start(FROM_HERE,
                        audio_device_period_,
                        this,
                        &AudioCapturerWin::DoCapture);
  return true;
}

void AudioCapturerWin::Stop() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(IsRunning());

  capture_timer_.reset();
  mm_device_.Release();
  audio_client_.Release();
  audio_capture_client_.Release();
  wave_format_ex_.Reset(NULL);
  com_initializer_.reset();

  thread_checker_.DetachFromThread();
}

bool AudioCapturerWin::IsRunning() {
  DCHECK(thread_checker_.CalledOnValidThread());
  return capture_timer_.get() != NULL;
}

void AudioCapturerWin::DoCapture() {
  DCHECK(AudioCapturer::IsValidSampleRate(sampling_rate_));
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(IsRunning());

  // Fetch all packets from the audio capture endpoint buffer.
  while (true) {
    UINT32 next_packet_size;
    HRESULT hr = audio_capture_client_->GetNextPacketSize(&next_packet_size);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to GetNextPacketSize. Error " << hr;
      return;
    }

    if (next_packet_size <= 0) {
      return;
    }

    BYTE* data;
    UINT32 frames;
    DWORD flags;
    hr = audio_capture_client_->GetBuffer(
        &data, &frames, &flags, NULL, NULL);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to GetBuffer. Error " << hr;
      return;
    }

    scoped_ptr<AudioPacket> packet = scoped_ptr<AudioPacket>(new AudioPacket());
    packet->set_data(data, frames * wave_format_ex_->nBlockAlign);
    packet->set_sampling_rate(sampling_rate_);
    packet->set_bytes_per_sample(
        static_cast<AudioPacket::BytesPerSample>(sizeof(int16)));
    packet->set_encoding(AudioPacket::ENCODING_RAW);

    if (!IsPacketOfSilence(packet.get()))
      callback_.Run(packet.Pass());

    hr = audio_capture_client_->ReleaseBuffer(frames);
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to ReleaseBuffer. Error " << hr;
      return;
    }
  }
}

// Detects whether there is audio playing in a packet of samples.
// Windows can give nonzero samples, even when there is no audio playing, so
// extremely low amplitude samples are counted as silence.
bool AudioCapturerWin::IsPacketOfSilence(const AudioPacket* packet) {
  DCHECK_EQ(static_cast<AudioPacket::BytesPerSample>(sizeof(int16)),
            packet->bytes_per_sample());
  const int16* data = reinterpret_cast<const int16*>(packet->data().data());
  int number_of_samples = packet->data().size() * kBitsPerByte / kBitsPerSample;

  for (int i = 0; i < number_of_samples; i++) {
    if (abs(data[i]) > kSilenceThreshold)
      return false;
  }
  return true;
}

scoped_ptr<AudioCapturer> AudioCapturer::Create() {
  return scoped_ptr<AudioCapturer>(new AudioCapturerWin());
}

}  // namespace remoting
