// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_PAGE_LOAD_HISTOGRAMS_H_
#define CHROME_RENDERER_PAGE_LOAD_HISTOGRAMS_H_

#include "base/basictypes.h"
#include "content/renderer/render_view_observer.h"

class NavigationState;
class RendererHistogramSnapshots;

class PageLoadHistograms : public RenderViewObserver {
 public:
  PageLoadHistograms(RenderView* render_view,
                     RendererHistogramSnapshots* histogram_snapshots);

 private:
  // RenderViewObserver implementation.
  virtual void FrameWillClose(WebKit::WebFrame* frame);
  virtual void LogCrossFramePropertyAccess(
      WebKit::WebFrame* frame,
      WebKit::WebFrame* target,
      bool cross_origin,
      const WebKit::WebString& property_name,
      unsigned long long event_id);
  virtual bool OnMessageReceived(const IPC::Message& message);

  // Dump all page load histograms appropriate for the given frame.
  //
  // This method will only dump once-per-instance, so it is safe to call
  // multiple times.
  //
  // The time points we keep are
  //    request: time document was requested by user
  //    start: time load of document started
  //    commit: time load of document started
  //    finish_document: main document loaded, before onload()
  //    finish_all_loads: after onload() and all resources are loaded
  //    first_paint: first paint performed
  //    first_paint_after_load: first paint performed after load is finished
  //    begin: request if it was user requested, start otherwise
  //
  // It's possible for the request time not to be set, if a client
  // redirect had been done (the user never requested the page)
  // Also, it's possible to load a page without ever laying it out
  // so first_paint and first_paint_after_load can be 0.
  void Dump(WebKit::WebFrame* frame);

  void ResetCrossFramePropertyAccess();

  void LogPageLoadTime(const NavigationState* state,
                       const WebKit::WebDataSource* ds) const;

  // Site isolation metric counts.
  // These are per-page-load counts, reset to 0 after they are dumped.
  int cross_origin_access_count_;
  int same_origin_access_count_;

  RendererHistogramSnapshots* histogram_snapshots_;

  DISALLOW_COPY_AND_ASSIGN(PageLoadHistograms);
};

#endif  // CHROME_RENDERER_PAGE_LOAD_HISTOGRAMS_H_
