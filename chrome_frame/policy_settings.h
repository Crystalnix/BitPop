// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_FRAME_POLICY_SETTINGS_H_
#define CHROME_FRAME_POLICY_SETTINGS_H_

#include <string>
#include <vector>
#include "base/memory/singleton.h"

#include "base/basictypes.h"

// A simple class that reads and caches policy settings for Chrome Frame.
// TODO(tommi): Support refreshing when new settings are pushed.
// TODO(tommi): Use Chrome's classes for this (and the notification service).
class PolicySettings {
 public:
  typedef enum RendererForUrl {
    RENDERER_NOT_SPECIFIED = -1,
    RENDER_IN_HOST,
    RENDER_IN_CHROME_FRAME,
  };

  static PolicySettings* GetInstance();

  RendererForUrl default_renderer() const {
    return default_renderer_;
  }

  RendererForUrl GetRendererForUrl(const wchar_t* url);

  RendererForUrl GetRendererForContentType(const wchar_t* content_type);

  // Returns the policy-configured Chrome app locale, or an empty string if none
  // is configured.
  const std::wstring& ApplicationLocale() const {
    return application_locale_;
  }

  // Helper functions for reading settings from the registry
  static void ReadUrlSettings(RendererForUrl* default_renderer,
      std::vector<std::wstring>* renderer_exclusion_list);
  static void ReadContentTypeSetting(
      std::vector<std::wstring>* content_type_list);
  static void ReadApplicationLocaleSetting(std::wstring* application_locale);

 protected:
  PolicySettings() : default_renderer_(RENDERER_NOT_SPECIFIED) {
    RefreshFromRegistry();
  }

  ~PolicySettings() {
  }

  // Protected for now since the class is not thread safe.
  void RefreshFromRegistry();

 protected:
  RendererForUrl default_renderer_;
  std::vector<std::wstring> renderer_exclusion_list_;
  std::vector<std::wstring> content_type_list_;
  std::wstring application_locale_;

 private:
  // This ensures no construction is possible outside of the class itself.
  friend struct DefaultSingletonTraits<PolicySettings>;
  DISALLOW_COPY_AND_ASSIGN(PolicySettings);
};

#endif  // CHROME_FRAME_POLICY_SETTINGS_H_
