// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_PRINTING_EXPORT_H_
#define PRINTING_PRINTING_EXPORT_H_
#pragma once

#if defined(COMPONENT_BUILD)
#if defined(WIN32)

#if defined(PRINTING_IMPLEMENTATION)
#define PRINTING_EXPORT __declspec(dllexport)
#else
#define PRINTING_EXPORT __declspec(dllimport)
#endif  // defined(PRINTING_IMPLEMENTATION)

#else  // defined(WIN32)
#define PRINTING_EXPORT __attribute__((visibility("default")))
#endif

#else  // defined(COMPONENT_BUILD)
#define PRINTING_EXPORT
#endif

#endif  // PRINTING_PRINTING_EXPORT_H_
