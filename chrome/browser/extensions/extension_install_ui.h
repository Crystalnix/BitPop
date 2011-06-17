// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_INSTALL_UI_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_INSTALL_UI_H_
#pragma once

#include <string>
#include <vector>

#include "base/string16.h"
#include "chrome/browser/extensions/image_loading_tracker.h"
#include "chrome/common/extensions/url_pattern.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/native_widget_types.h"

class Extension;
class MessageLoop;
class Profile;
class InfoBarDelegate;
class TabContents;

// Displays all the UI around extension installation and uninstallation.
class ExtensionInstallUI : public ImageLoadingTracker::Observer {
 public:
  enum PromptType {
    UNSET_PROMPT_TYPE = -1,
    INSTALL_PROMPT = 0,
    RE_ENABLE_PROMPT,
    NUM_PROMPT_TYPES
  };

  // A mapping from PromptType to message ID for various dialog content.
  static const int kTitleIds[NUM_PROMPT_TYPES];
  static const int kHeadingIds[NUM_PROMPT_TYPES];
  static const int kButtonIds[NUM_PROMPT_TYPES];
  static const int kWarningIds[NUM_PROMPT_TYPES];

  class Delegate {
   public:
    // We call this method to signal that the installation should continue.
    virtual void InstallUIProceed() = 0;

    // We call this method to signal that the installation should stop.
    virtual void InstallUIAbort() = 0;

   protected:
    virtual ~Delegate() {}
  };

  explicit ExtensionInstallUI(Profile* profile);
  virtual ~ExtensionInstallUI();

  // This is called by the installer to verify whether the installation should
  // proceed. This is declared virtual for testing.
  //
  // We *MUST* eventually call either Proceed() or Abort() on |delegate|.
  virtual void ConfirmInstall(Delegate* delegate, const Extension* extension);

  // This is called by the app handler launcher to verify whether the app
  // should be re-enabled. This is declared virtual for testing.
  //
  // We *MUST* eventually call either Proceed() or Abort() on |delegate|.
  virtual void ConfirmReEnable(Delegate* delegate, const Extension* extension);

  // Installation was successful. This is declared virtual for testing.
  virtual void OnInstallSuccess(const Extension* extension, SkBitmap* icon);

  // Installation failed. This is declared virtual for testing.
  virtual void OnInstallFailure(const std::string& error);

  // ImageLoadingTracker::Observer:
  virtual void OnImageLoaded(
      SkBitmap* image, const ExtensionResource& resource, int index);

  // Show an infobar for a newly-installed theme.  previous_theme_id
  // should be empty if the previous theme was the system/default
  // theme.
  //
  // TODO(akalin): Find a better home for this (and
  // GetNewThemeInstalledInfoBarDelegate()).
  static void ShowThemeInfoBar(
      const std::string& previous_theme_id, bool previous_use_system_theme,
      const Extension* new_theme, Profile* profile);

 private:
  // Sets the icon that will be used in any UI. If |icon| is NULL, or contains
  // an empty bitmap, then a default icon will be used instead.
  void SetIcon(SkBitmap* icon);

  // Starts the process of showing a confirmation UI, which is split into two.
  // 1) Set off a 'load icon' task.
  // 2) Handle the load icon response and show the UI (OnImageLoaded).
  void ShowConfirmation(PromptType prompt_type);

  // Returns the delegate to control the browser's info bar. This is
  // within its own function due to its platform-specific nature.
  static InfoBarDelegate* GetNewThemeInstalledInfoBarDelegate(
      TabContents* tab_contents,
      const Extension* new_theme,
      const std::string& previous_theme_id,
      bool previous_use_system_theme);

  Profile* profile_;
  MessageLoop* ui_loop_;

  // Used to undo theme installation.
  std::string previous_theme_id_;
  bool previous_use_system_theme_;

  // The extensions installation icon.
  SkBitmap icon_;

  // The extension we are showing the UI for.
  const Extension* extension_;

  // The delegate we will call Proceed/Abort on after confirmation UI.
  Delegate* delegate_;

  // The type of prompt we are going to show.
  PromptType prompt_type_;

  // Keeps track of extension images being loaded on the File thread for the
  // purpose of showing the install UI.
  ImageLoadingTracker tracker_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_INSTALL_UI_H_
