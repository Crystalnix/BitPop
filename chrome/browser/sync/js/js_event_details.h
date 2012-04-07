// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_JS_JS_EVENT_DETAILS_H_
#define CHROME_BROWSER_SYNC_JS_JS_EVENT_DETAILS_H_
#pragma once

// See README.js for design comments.

#include <string>

#include "base/values.h"
#include "chrome/browser/sync/util/immutable.h"

namespace browser_sync {

// A thin wrapper around Immutable<DictionaryValue>.  Used for passing
// around event details to different threads.
class JsEventDetails {
 public:
  // Uses an empty dictionary.
  JsEventDetails();

  // Takes over the data in |details|, leaving |details| empty.
  explicit JsEventDetails(DictionaryValue* details);

  ~JsEventDetails();

  const DictionaryValue& Get() const;

  std::string ToString() const;

  // Copy constructor and assignment operator welcome.

 private:
  typedef Immutable<DictionaryValue, HasSwapMemFnByPtr<DictionaryValue> >
      ImmutableDictionaryValue;

  ImmutableDictionaryValue details_;
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_JS_JS_EVENT_DETAILS_H_
