// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_RENDERER_WEBAUDIODEVICE_IMPL_H_
#define CONTENT_RENDERER_RENDERER_WEBAUDIODEVICE_IMPL_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "content/renderer/audio_device.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebAudioDevice.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebVector.h"

class RendererWebAudioDeviceImpl : public WebKit::WebAudioDevice,
                                   public AudioDevice::RenderCallback {
 public:
  RendererWebAudioDeviceImpl(size_t buffer_size,
                             int channels,
                             double sample_rate,
                             WebKit::WebAudioDevice::RenderCallback* callback);
  virtual ~RendererWebAudioDeviceImpl();

  // WebKit::WebAudioDevice implementation.
  virtual void start();
  virtual void stop();
  virtual double sampleRate();

  // AudioDevice::RenderCallback implementation.
  virtual void Render(const std::vector<float*>& audio_data,
                      size_t number_of_frames,
                      size_t audio_delay_milliseconds);

 private:
  scoped_refptr<AudioDevice> audio_device_;

  // Weak reference to the callback into WebKit code.
  WebKit::WebAudioDevice::RenderCallback* client_callback_;

  DISALLOW_COPY_AND_ASSIGN(RendererWebAudioDeviceImpl);
};

#endif  // CONTENT_RENDERER_RENDERER_WEBAUDIODEVICE_IMPL_H_
