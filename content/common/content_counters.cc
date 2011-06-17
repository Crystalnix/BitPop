// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/content_counters.h"

#include "base/metrics/stats_counters.h"

namespace content {

using base::StatsCounterTimer;
using base::StatsRate;

// Note: We use the construct-on-first-use pattern here, because we don't
//       want to fight with any static initializer ordering problems later.
//       The downside of this is that the objects don't ever get cleaned up.
//       But they are small and this is okay.

// Note: Because these are constructed on-first-use, there is a slight
//       race condition - two threads could initialize the same counter.
//       If this happened, the stats table would still work just fine;
//       we'd leak the extraneous StatsCounter object once, and that
//       would be it.  But these are small objects, so this is ok.

StatsCounterTimer& Counters::chrome_main() {
  static StatsCounterTimer* ctr = new StatsCounterTimer("Chrome.Init");
  return *ctr;
}

StatsCounterTimer& Counters::renderer_main() {
  static StatsCounterTimer* ctr = new StatsCounterTimer("Chrome.RendererInit");
  return *ctr;
}

}  // namespace content
