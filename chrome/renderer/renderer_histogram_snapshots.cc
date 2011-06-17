// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/renderer_histogram_snapshots.h"

#include <ctype.h>

#include "base/logging.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "chrome/common/render_messages.h"
#include "content/renderer/render_thread.h"

// TODO(raman): Before renderer shuts down send final snapshot lists.

using base::Histogram;
using base::StatisticsRecorder;

RendererHistogramSnapshots::RendererHistogramSnapshots()
    : ALLOW_THIS_IN_INITIALIZER_LIST(
          renderer_histogram_snapshots_factory_(this)) {
}

RendererHistogramSnapshots::~RendererHistogramSnapshots() {
}

// Send data quickly!
void RendererHistogramSnapshots::SendHistograms(int sequence_number) {
  RenderThread::current()->message_loop()->PostTask(FROM_HERE,
      renderer_histogram_snapshots_factory_.NewRunnableMethod(
          &RendererHistogramSnapshots::UploadAllHistrograms, sequence_number));
}

bool RendererHistogramSnapshots::OnControlMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(RendererHistogramSnapshots, message)
    IPC_MESSAGE_HANDLER(ViewMsg_GetRendererHistograms, OnGetRendererHistograms)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void RendererHistogramSnapshots::OnGetRendererHistograms(int sequence_number) {
  SendHistograms(sequence_number);
}

void RendererHistogramSnapshots::UploadAllHistrograms(int sequence_number) {
  DCHECK_EQ(0u, pickled_histograms_.size());

  // Push snapshots into our pickled_histograms_ vector.
  TransmitAllHistograms(Histogram::kIPCSerializationSourceFlag, false);

  // Send the sequence number and list of pickled histograms over synchronous
  // IPC, so we can clear pickled_histograms_ afterwards.
  RenderThread::current()->Send(
      new ViewHostMsg_RendererHistograms(
          sequence_number, pickled_histograms_));

  pickled_histograms_.clear();
}

void RendererHistogramSnapshots::TransmitHistogramDelta(
      const base::Histogram& histogram,
      const base::Histogram::SampleSet& snapshot) {
  DCHECK_NE(0, snapshot.TotalCount());
  snapshot.CheckSize(histogram);

  std::string histogram_info =
      Histogram::SerializeHistogramInfo(histogram, snapshot);
  pickled_histograms_.push_back(histogram_info);
}

void RendererHistogramSnapshots::InconsistencyDetected(int problem) {
  UMA_HISTOGRAM_ENUMERATION("Histogram.InconsistenciesRenderer",
                            problem, Histogram::NEVER_EXCEEDED_VALUE);
}

void RendererHistogramSnapshots::UniqueInconsistencyDetected(int problem) {
  UMA_HISTOGRAM_ENUMERATION("Histogram.InconsistenciesRendererUnique",
                            problem, Histogram::NEVER_EXCEEDED_VALUE);
}

void RendererHistogramSnapshots::SnapshotProblemResolved(int amount) {
  UMA_HISTOGRAM_COUNTS("Histogram.InconsistentSnapshotRenderer",
                       std::abs(amount));
}

