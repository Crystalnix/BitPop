// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPC_IPC_DESCRIPTORS_H_
#define IPC_IPC_DESCRIPTORS_H_
#pragma once

// This is a list of global descriptor keys to be used with the
// base::GlobalDescriptors object (see base/global_descriptors_posix.h)
enum {
  kPrimaryIPCChannel = 0,
};

#endif  // IPC_IPC_DESCRIPTORS_H_
