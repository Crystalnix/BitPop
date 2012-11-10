// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_EVENT_UTILS_H_
#define CHROME_BROWSER_UI_VIEWS_EVENT_UTILS_H_

namespace views {
class Event;
}

namespace event_utils {

// Returns true if the specified event may have a
// WindowOptionDisposition.
bool IsPossibleDispositionEvent(const views::Event& event);

}  // namespace event_utils

#endif  // CHROME_BROWSER_UI_VIEWS_EVENT_UTILS_H_
