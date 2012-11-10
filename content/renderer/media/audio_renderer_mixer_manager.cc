// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/audio_renderer_mixer_manager.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "content/renderer/media/audio_device_factory.h"
#include "media/base/audio_renderer_mixer.h"
#include "media/base/audio_renderer_mixer_input.h"

namespace content {

AudioRendererMixerManager::AudioRendererMixerManager(int hardware_sample_rate,
                                                     int hardware_buffer_size)
    : hardware_sample_rate_(hardware_sample_rate),
      hardware_buffer_size_(hardware_buffer_size) {
}

AudioRendererMixerManager::~AudioRendererMixerManager() {
  DCHECK(mixers_.empty());
}

media::AudioRendererMixerInput* AudioRendererMixerManager::CreateInput() {
  return new media::AudioRendererMixerInput(
      base::Bind(
          &AudioRendererMixerManager::GetMixer, base::Unretained(this)),
      base::Bind(
          &AudioRendererMixerManager::RemoveMixer, base::Unretained(this)));
}

media::AudioRendererMixer* AudioRendererMixerManager::GetMixer(
    const media::AudioParameters& params) {
  base::AutoLock auto_lock(mixers_lock_);

  AudioRendererMixerMap::iterator it = mixers_.find(params);
  if (it != mixers_.end()) {
    it->second.ref_count++;
    return it->second.mixer;
  }

  // Create output parameters based on the audio hardware configuration for
  // passing on to the output sink.  Force to 16-bit output for now since we
  // know that works well for WebAudio and WebRTC.
  media::AudioParameters output_params(
      media::AudioParameters::AUDIO_PCM_LOW_LATENCY, params.channel_layout(),
      hardware_sample_rate_, 16, hardware_buffer_size_);

  media::AudioRendererMixer* mixer = new media::AudioRendererMixer(
      params, output_params, AudioDeviceFactory::NewOutputDevice());

  AudioRendererMixerReference mixer_reference = { mixer, 1 };
  mixers_[params] = mixer_reference;
  return mixer;
}

void AudioRendererMixerManager::RemoveMixer(
    const media::AudioParameters& params) {
  base::AutoLock auto_lock(mixers_lock_);

  AudioRendererMixerMap::iterator it = mixers_.find(params);
  DCHECK(it != mixers_.end());

  // Only remove the mixer if AudioRendererMixerManager is the last owner.
  it->second.ref_count--;
  if (it->second.ref_count == 0) {
    delete it->second.mixer;
    mixers_.erase(it);
  }
}

}  // namespace content
