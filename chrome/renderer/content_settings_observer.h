// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_CONTENT_SETTINGS_OBSERVER_H_
#define CHROME_RENDERER_CONTENT_SETTINGS_OBSERVER_H_
#pragma once

#include <map>

#include "chrome/common/content_settings.h"
#include "content/renderer/render_view_observer.h"
#include "content/renderer/render_view_observer_tracker.h"

class GURL;

// Handles blocking content per content settings for each RenderView.
class ContentSettingsObserver
    : public RenderViewObserver,
      public RenderViewObserverTracker<ContentSettingsObserver> {
 public:
  explicit ContentSettingsObserver(RenderView* render_view);
  virtual ~ContentSettingsObserver();

  // Sets the content settings that back allowScripts(), allowImages(), and
  // allowPlugins().
  void SetContentSettings(const ContentSettings& settings);

  // Returns the setting for the given type.
  ContentSetting GetContentSetting(ContentSettingsType type);

  // Sends an IPC notification that the specified content type was blocked.
  // If the content type requires it, |resource_identifier| names the specific
  // resource that was blocked (the plugin path in the case of plugins),
  // otherwise it's the empty string.
  void DidBlockContentType(ContentSettingsType settings_type,
                           const std::string& resource_identifier);

 private:
  // RenderViewObserver implementation.
  virtual bool OnMessageReceived(const IPC::Message& message);
  virtual void DidCommitProvisionalLoad(WebKit::WebFrame* frame,
                                        bool is_new_navigation);
  virtual bool AllowImages(WebKit::WebFrame* frame, bool enabled_per_settings);
  virtual bool AllowPlugins(WebKit::WebFrame* frame, bool enabled_per_settings);
  virtual bool AllowScript(WebKit::WebFrame* frame, bool enabled_per_settings);
  virtual void DidNotAllowPlugins(WebKit::WebFrame* frame);
  virtual void DidNotAllowScript(WebKit::WebFrame* frame);

  // Message handlers.
  void OnSetContentSettingsForLoadingURL(
      const GURL& url,
      const ContentSettings& content_settings);

  // Helper method that returns if the user wants to block content of type
  // |content_type|.
  bool AllowContentType(ContentSettingsType settings_type);

  // Resets the |content_blocked_| array.
  void ClearBlockedContentSettings();

  typedef std::map<GURL, ContentSettings> HostContentSettings;
  HostContentSettings host_content_settings_;

  // Stores if loading of images, scripts, and plugins is allowed.
  ContentSettings current_content_settings_;

  // Stores if images, scripts, and plugins have actually been blocked.
  bool content_blocked_[CONTENT_SETTINGS_NUM_TYPES];

  DISALLOW_COPY_AND_ASSIGN(ContentSettingsObserver);
};

#endif  // CHROME_RENDERER_CONTENT_SETTINGS_OBSERVER_H_
