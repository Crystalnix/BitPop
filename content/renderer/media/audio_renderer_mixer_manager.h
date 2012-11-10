// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_AUDIO_RENDERER_MIXER_MANAGER_H_
#define CONTENT_RENDERER_MEDIA_AUDIO_RENDERER_MIXER_MANAGER_H_

#include <map>

#include "base/synchronization/lock.h"
#include "content/common/content_export.h"
#include "media/audio/audio_parameters.h"

namespace media {
class AudioRendererMixer;
class AudioRendererMixerInput;
}

namespace content {

// Manages sharing of an AudioRendererMixer among AudioRendererMixerInputs based
// on their AudioParameters configuration.  Inputs with the same AudioParameters
// configuration will share a mixer while a new AudioRendererMixer will be
// lazily created if one with the exact AudioParameters does not exist.
//
// There should only be one instance of AudioRendererMixerManager per render
// thread.
//
// TODO(dalecurtis): Right now we require AudioParameters to be an exact match
// when we should be able to ignore bits per channel since we're only dealing
// with floats.  However, bits per channel is currently used to interleave the
// audio data by AudioOutputDevice::AudioThreadCallback::Process for consumption
// via the shared memory.  See http://crbug.com/114700.
class CONTENT_EXPORT AudioRendererMixerManager {
 public:
  // Construct an instance using the given audio hardware configuration.
  AudioRendererMixerManager(int hardware_sample_rate, int hardware_buffer_size);
  ~AudioRendererMixerManager();

  // Creates an AudioRendererMixerInput with the proper callbacks necessary to
  // retrieve an AudioRendererMixer instance from AudioRendererMixerManager.
  // Caller must ensure AudioRendererMixerManager outlives the returned input.
  media::AudioRendererMixerInput* CreateInput();

 private:
  friend class AudioRendererMixerManagerTest;

  // Returns a mixer instance based on AudioParameters; an existing one if one
  // with the provided AudioParameters exists or a new one if not.
  media::AudioRendererMixer* GetMixer(const media::AudioParameters& params);

  // Remove a mixer instance given a mixer if the only other reference is held
  // by AudioRendererMixerManager.  Every AudioRendererMixer owner must call
  // this method when it's done with a mixer.
  void RemoveMixer(const media::AudioParameters& params);

  // Map of AudioParameters to <AudioRendererMixer, Count>.  Count allows
  // AudioRendererMixerManager to keep track explicitly (v.s. RefCounted which
  // is implicit) of the number of outstanding AudioRendererMixers.
  struct AudioRendererMixerReference {
    media::AudioRendererMixer* mixer;
    int ref_count;
  };
  typedef std::map<media::AudioParameters, AudioRendererMixerReference,
                   media::AudioParameters::Compare> AudioRendererMixerMap;
  AudioRendererMixerMap mixers_;
  base::Lock mixers_lock_;

  // Audio hardware configuration.  Used to construct output AudioParameters for
  // each AudioRendererMixer instance.
  int hardware_sample_rate_;
  int hardware_buffer_size_;

  DISALLOW_COPY_AND_ASSIGN(AudioRendererMixerManager);
};

}  // namespace content

#endif  // CONTENT_RENDERER_MEDIA_AUDIO_RENDERER_MIXER_MANAGER_H_
