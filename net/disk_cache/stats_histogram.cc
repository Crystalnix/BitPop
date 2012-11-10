// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/stats_histogram.h"

#include "base/debug/leak_annotations.h"
#include "base/logging.h"
#include "base/metrics/statistics_recorder.h"
#include "net/disk_cache/stats.h"

namespace disk_cache {

using base::Histogram;
using base::StatisticsRecorder;

// Static.
const Stats* StatsHistogram::stats_ = NULL;

StatsHistogram::~StatsHistogram() {
  // Only cleanup what we set.
  if (init_)
    stats_ = NULL;
}

StatsHistogram* StatsHistogram::FactoryGet(const std::string& name) {
  Sample minimum = 1;
  Sample maximum = disk_cache::Stats::kDataSizesLength - 1;
  size_t bucket_count = disk_cache::Stats::kDataSizesLength;

  Histogram* histogram = StatisticsRecorder::FindHistogram(name);
  if (!histogram) {
    // To avoid racy destruction at shutdown, the following will be leaked.
    StatsHistogram* stats_histogram =
        new StatsHistogram(name, minimum, maximum, bucket_count);
    stats_histogram->SetFlags(kUmaTargetedHistogramFlag);
    histogram = StatisticsRecorder::RegisterOrDeleteDuplicate(stats_histogram);
  }

  DCHECK(HISTOGRAM == histogram->histogram_type());
  DCHECK(histogram->HasConstructionArguments(minimum, maximum, bucket_count));

  // We're preparing for an otherwise unsafe upcast by ensuring we have the
  // proper class type.
  StatsHistogram* return_histogram = static_cast<StatsHistogram*>(histogram);
  return return_histogram;
}

bool StatsHistogram::Init(const Stats* stats) {
  DCHECK(stats);
  if (stats_)
    return false;

  // We support statistics report for only one cache.
  init_ = true;
  stats_ = stats;
  return true;
}

Histogram::Sample StatsHistogram::ranges(size_t i) const {
  DCHECK(stats_);
  return stats_->GetBucketRange(i);
}

size_t StatsHistogram::bucket_count() const {
  return disk_cache::Stats::kDataSizesLength;
}

void StatsHistogram::SnapshotSample(SampleSet* sample) const {
  DCHECK(stats_);
  StatsSamples my_sample;
  stats_->Snapshot(&my_sample);

  *sample = my_sample;

  // Only report UMA data once.
  StatsHistogram* mutable_me = const_cast<StatsHistogram*>(this);
  mutable_me->ClearFlags(kUmaTargetedHistogramFlag);
}

Histogram::Inconsistencies StatsHistogram::FindCorruption(
    const SampleSet& snapshot) const {
  return NO_INCONSISTENCIES;  // This class won't monitor inconsistencies.
}

}  // namespace disk_cache
