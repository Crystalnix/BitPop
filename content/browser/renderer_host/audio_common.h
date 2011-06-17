// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_AUDIO_COMMON_H_
#define CONTENT_BROWSER_RENDERER_HOST_AUDIO_COMMON_H_
#pragma once

#include "base/basictypes.h"

struct AudioParameters;

// Calculates a safe hardware buffer size (in number of samples) given a set
// of audio parameters.
uint32 SelectSamplesPerPacket(const AudioParameters& params);

#endif  // CONTENT_BROWSER_RENDERER_HOST_AUDIO_COMMON_H_
