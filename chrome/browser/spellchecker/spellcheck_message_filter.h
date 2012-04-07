// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_MESSAGE_FILTER_H_
#define CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_MESSAGE_FILTER_H_

#include "content/public/browser/browser_message_filter.h"

// A message filter implementation that receives spell checker requests from
// SpellCheckProvider.
class SpellCheckMessageFilter : public content::BrowserMessageFilter {
 public:
  explicit SpellCheckMessageFilter(int render_process_id);
  virtual ~SpellCheckMessageFilter();

  // content::BrowserMessageFilter implementation.
  virtual void OverrideThreadForMessage(
      const IPC::Message& message,
      content::BrowserThread::ID* thread) OVERRIDE;
  virtual bool OnMessageReceived(const IPC::Message& message,
                                 bool* message_was_ok) OVERRIDE;

 private:
  void OnSpellCheckerRequestDictionary();
  void OnNotifyChecked(const string16& word, bool misspelled);

  int render_process_id_;
};

#endif  // CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_MESSAGE_FILTER_H_
