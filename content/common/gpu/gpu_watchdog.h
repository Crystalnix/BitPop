// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_GPU_WATCHDOG_H_
#define CONTENT_COMMON_GPU_GPU_WATCHDOG_H_

// Interface for objects that monitor the a GPUProcessor's progress. The
// GPUProcessor will regularly invoke CheckArmed.
class GpuWatchdog {
 public:
  GpuWatchdog() {
  }

  virtual ~GpuWatchdog() {
  };

  virtual void CheckArmed() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(GpuWatchdog);
};

#endif  // CONTENT_COMMON_GPU_GPU_WATCHDOG_H_
