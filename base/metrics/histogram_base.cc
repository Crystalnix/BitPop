// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/metrics/histogram_base.h"

namespace base {

const HistogramBase::Sample HistogramBase::kSampleType_MAX = INT_MAX;

HistogramBase::HistogramBase(const std::string& name)
    : histogram_name_(name) {}

HistogramBase::~HistogramBase() {}

}  // namespace base
