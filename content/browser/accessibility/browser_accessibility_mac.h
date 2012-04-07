// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_MAC_H_
#define CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_MAC_H_
#pragma once

#include <map>
#include <utility>
#include <vector>

#include "base/memory/scoped_nsobject.h"
#include "content/browser/accessibility/browser_accessibility.h"

@class BrowserAccessibilityCocoa;

class BrowserAccessibilityMac : public BrowserAccessibility {
 public:
  // Implementation of BrowserAccessibility.
  virtual void PreInitialize() OVERRIDE;
  virtual void NativeReleaseReference() OVERRIDE;

  // Overrides from BrowserAccessibility.
  virtual void DetachTree(std::vector<BrowserAccessibility*>* nodes) OVERRIDE;

  // The BrowserAccessibilityCocoa associated with us.
  BrowserAccessibilityCocoa* native_view() const {
    return browser_accessibility_cocoa_;
  }

 private:
  // This gives BrowserAccessibility::Create access to the class constructor.
  friend class BrowserAccessibility;

  BrowserAccessibilityMac();

  // Allows access to the BrowserAccessibilityCocoa which wraps this.
  // BrowserAccessibility.
  // We own this object until our manager calls ReleaseReference;
  // thereafter, the cocoa object owns us.
  BrowserAccessibilityCocoa* browser_accessibility_cocoa_;
  DISALLOW_COPY_AND_ASSIGN(BrowserAccessibilityMac);
};

#endif // CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_MAC_H_
