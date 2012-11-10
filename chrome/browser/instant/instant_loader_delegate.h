// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_INSTANT_INSTANT_LOADER_DELEGATE_H_
#define CHROME_BROWSER_INSTANT_INSTANT_LOADER_DELEGATE_H_

#include "base/string16.h"
#include "chrome/common/instant_types.h"

class GURL;

namespace gfx {
class Rect;
}

class InstantLoader;

// InstantLoader's delegate. This interface is implemented by InstantController.
class InstantLoaderDelegate {
 public:
  // Invoked when the status (either http_status_ok or ready) has changed.
  virtual void InstantStatusChanged(InstantLoader* loader) = 0;

  // Invoked when the loader has suggested text.
  virtual void SetSuggestedTextFor(
      InstantLoader* loader,
      const string16& text,
      InstantCompleteBehavior behavior) = 0;

  // Returns the bounds of instant.
  virtual gfx::Rect GetInstantBounds() = 0;

  // Returns true if instant should be committed on mouse up or at the end of a
  // touch-gesture.
  virtual bool ShouldCommitInstantOnPointerRelease() = 0;

  // Invoked when the the loader should be committed.
  virtual void CommitInstantLoader(InstantLoader* loader) = 0;

  // Invoked if the loader was created with the intention that the site supports
  // instant, but it turned out the site doesn't support instant.
  virtual void InstantLoaderDoesntSupportInstant(InstantLoader* loader) = 0;

  // Adds the specified url to the set of urls instant won't prefetch for.
  virtual void AddToBlacklist(InstantLoader* loader, const GURL& url) = 0;

  // Invoked if the loader swaps to a different WebContents.
  virtual void SwappedTabContents(InstantLoader* loader) = 0;

  // Invoked when the webcontents created by the loader is focused.
  virtual void InstantLoaderContentsFocused() = 0;

 protected:
  virtual ~InstantLoaderDelegate() {}
};

#endif  // CHROME_BROWSER_INSTANT_INSTANT_LOADER_DELEGATE_H_
