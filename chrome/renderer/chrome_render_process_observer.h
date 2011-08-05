// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_CHROME_RENDER_PROCESS_OBSERVER_H_
#define CHROME_RENDERER_CHROME_RENDER_PROCESS_OBSERVER_H_
#pragma once

#include <string>

#include "base/compiler_specific.h"
#include "content/renderer/render_process_observer.h"

class GURL;
struct ContentSettings;

// This class filters the incoming control messages (i.e. ones not destined for
// a RenderView) for Chrome specific messages that the content layer doesn't
// happen.  If a few messages are related, they should probably have their own
// observer.
class ChromeRenderProcessObserver : public RenderProcessObserver {
 public:
  ChromeRenderProcessObserver();
  virtual ~ChromeRenderProcessObserver();

  static bool is_incognito_process() { return is_incognito_process_; }

 private:
  // RenderProcessObserver implementation.
  virtual bool OnControlMessageReceived(const IPC::Message& message) OVERRIDE;

  void OnSetIsIncognitoProcess(bool is_incognito_process);
  void OnSetContentSettingsForCurrentURL(
      const GURL& url, const ContentSettings& content_settings);
  void OnSetCacheCapacities(size_t min_dead_capacity,
                            size_t max_dead_capacity,
                            size_t capacity);
  void OnClearCache();
  void OnGetCacheResourceStats();
  void OnSetFieldTrialGroup(const std::string& fiel_trial_name,
                            const std::string& group_name);
  void OnGetRendererTcmalloc();
  void OnGetV8HeapStats();
  void OnPurgeMemory();

  static bool is_incognito_process_;

  DISALLOW_COPY_AND_ASSIGN(ChromeRenderProcessObserver);
};

#endif  // CHROME_RENDERER_CHROME_RENDER_PROCESS_OBSERVER_H_
