// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_GL_GL_EXPORT_H_
#define UI_GFX_GL_GL_EXPORT_H_
#pragma once

#if defined(COMPONENT_BUILD)
#if defined(WIN32)

#if defined(GL_IMPLEMENTATION)
#define GL_EXPORT __declspec(dllexport)
#else
#define GL_EXPORT __declspec(dllimport)
#endif  // defined(GL_IMPLEMENTATION)

#else  // defined(WIN32)
#define GL_EXPORT __attribute__((visibility("default")))
#endif

#else  // defined(COMPONENT_BUILD)
#define GL_EXPORT
#endif

#endif  // UI_GFX_GL_GL_EXPORT_H_
