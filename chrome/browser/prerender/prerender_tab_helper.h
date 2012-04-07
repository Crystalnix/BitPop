// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRERENDER_PRERENDER_TAB_HELPER_H_
#define CHROME_BROWSER_PRERENDER_PRERENDER_TAB_HELPER_H_

#include "base/time.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "googleurl/src/gurl.h"

class TabContentsWrapper;

namespace prerender {

class PrerenderManager;

// PrerenderTabHelper is responsible for recording perceived pageload times
// to compare PLT's with prerendering enabled and disabled.
class PrerenderTabHelper : public content::WebContentsObserver {
 public:
  explicit PrerenderTabHelper(TabContentsWrapper* tab);
  virtual ~PrerenderTabHelper();

  // content::WebContentsObserver implementation.
  virtual void ProvisionalChangeToMainFrameUrl(
      const GURL& url,
      const GURL& opener_url) OVERRIDE;
  virtual void DidStopLoading() OVERRIDE;
  virtual void DidStartProvisionalLoadForFrame(
      int64 frame_id,
      bool is_main_frame,
      const GURL& validated_url,
      bool is_error_page,
      RenderViewHost* render_view_host) OVERRIDE;

  // Called when this prerendered TabContents has just been swapped in.
  void PrerenderSwappedIn();

  void UpdateTargetURL(int32 page_id, const GURL& url);

 private:
  // The data we store for a hover (time the hover occurred & URL).
  class HoverData;

  // Retrieves the PrerenderManager, or NULL, if none was found.
  PrerenderManager* MaybeGetPrerenderManager() const;

  // Checks with the PrerenderManager if the specified URL has been preloaded,
  // and if so, swap the RenderViewHost with the preload into this TabContents
  // object. |opener_url| denotes the window.opener url that is set for this
  // tab and is empty if there is no opener set.
  bool MaybeUsePrerenderedPage(const GURL& url, const GURL& opener_url);

  // Returns whether the TabContents being observed is currently prerendering.
  bool IsPrerendering();

  // Records histogram information for the current hover, based on whether
  // it was used or not.  Will not do anything if there is no current hover.
  // Also resets the hover to no hover.
  void MaybeLogCurrentHover(bool was_used);

  bool IsTopSite(const GURL& url);

  // TabContentsWrapper we're created for.
  TabContentsWrapper* tab_;

  // System time at which the current load was started for the purpose of
  // the perceived page load time (PPLT).
  base::TimeTicks pplt_load_start_;

  // Information about the last hover for each hover threshold.
  scoped_array<HoverData> last_hovers_;

  // Information about the current hover independent of thresholds.
  GURL current_hover_url_;
  base::TimeTicks current_hover_time_;

  // Current URL being loaded.
  GURL url_;

  DISALLOW_COPY_AND_ASSIGN(PrerenderTabHelper);
};

}  // namespace prerender

#endif  // CHROME_BROWSER_PRERENDER_PRERENDER_TAB_HELPER_H_
