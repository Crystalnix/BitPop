// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_EXTENSIONS_SHELL_WINDOW_COCOA_H_
#define CHROME_BROWSER_UI_COCOA_EXTENSIONS_SHELL_WINDOW_COCOA_H_

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_nsobject.h"
#include "chrome/browser/ui/extensions/shell_window.h"
#include "ui/gfx/rect.h"

class Profile;
class ShellWindowCocoa;

// A window controller for a minimal window to host a web app view. Passes
// Objective-C notifications to the C++ bridge.
@interface ShellWindowController : NSWindowController<NSWindowDelegate> {
 @private
  ShellWindowCocoa* shellWindow_; // Weak; owns self.
}

@property(assign, nonatomic) ShellWindowCocoa* shellWindow;

@end

// Cocoa bridge to ShellWindow.
class ShellWindowCocoa : public ShellWindow {
 public:
  ShellWindowCocoa(Profile* profile,
                   const extensions::Extension* extension,
                   const GURL& url,
                   const CreateParams& params);

  // BaseWindow implementation.
  virtual bool IsActive() const OVERRIDE;
  virtual bool IsMaximized() const OVERRIDE;
  virtual bool IsMinimized() const OVERRIDE;
  virtual bool IsFullscreen() const OVERRIDE;
  virtual gfx::NativeWindow GetNativeWindow() OVERRIDE;
  virtual gfx::Rect GetRestoredBounds() const OVERRIDE;
  virtual gfx::Rect GetBounds() const OVERRIDE;
  virtual void Show() OVERRIDE;
  virtual void ShowInactive() OVERRIDE;
  virtual void Close() OVERRIDE;
  virtual void Activate() OVERRIDE;
  virtual void Deactivate() OVERRIDE;
  virtual void Maximize() OVERRIDE;
  virtual void Minimize() OVERRIDE;
  virtual void Restore() OVERRIDE;
  virtual void SetBounds(const gfx::Rect& bounds) OVERRIDE;
  virtual void SetDraggableRegion(SkRegion* region) OVERRIDE;
  virtual void FlashFrame(bool flash) OVERRIDE;
  virtual bool IsAlwaysOnTop() const OVERRIDE;

  // Called when the window is about to be closed.
  void WindowWillClose();

  // Called when the window is focused.
  void WindowDidBecomeKey();

  // Called when the window is defocused.
  void WindowDidResignKey();

 protected:
  // ShellWindow implementation.
  virtual void SetFullscreen(bool fullscreen) OVERRIDE;
  virtual bool IsFullscreenOrPending() const OVERRIDE;

 private:
  virtual ~ShellWindowCocoa();

  NSWindow* window() const;

  void InstallView();
  void UninstallView();

  bool has_frame_;

  bool is_fullscreen_;
  NSRect restored_bounds_;

  scoped_nsobject<ShellWindowController> window_controller_;
  NSInteger attention_request_id_;  // identifier from requestUserAttention

  DISALLOW_COPY_AND_ASSIGN(ShellWindowCocoa);
};

#endif  // CHROME_BROWSER_UI_COCOA_EXTENSIONS_SHELL_WINDOW_COCOA_H_
