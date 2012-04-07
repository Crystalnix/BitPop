// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/html_dialog_window_controller.h"

#include "base/logging.h"
#include "base/memory/scoped_nsobject.h"
#include "base/property_bag.h"
#include "base/sys_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#import "chrome/browser/ui/browser_dialogs.h"
#import "chrome/browser/ui/cocoa/browser_command_executor.h"
#import "chrome/browser/ui/cocoa/chrome_event_processing_window.h"
#include "chrome/browser/ui/dialog_style.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/webui/html_dialog_controller.h"
#include "chrome/browser/ui/webui/html_dialog_tab_contents_delegate.h"
#include "chrome/browser/ui/webui/html_dialog_ui.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/gfx/size.h"

using content::WebContents;
using content::WebUIMessageHandler;

// Thin bridge that routes notifications to
// HtmlDialogWindowController's member variables.
class HtmlDialogWindowDelegateBridge : public HtmlDialogUIDelegate,
                                       public HtmlDialogTabContentsDelegate {
public:
  // All parameters must be non-NULL/non-nil.
  HtmlDialogWindowDelegateBridge(HtmlDialogWindowController* controller,
                                 Profile* profile,
                                 Browser* browser,
                                 HtmlDialogUIDelegate* delegate);

  virtual ~HtmlDialogWindowDelegateBridge();

  // Called when the window is directly closed, e.g. from the close
  // button or from an accelerator.
  void WindowControllerClosed();

  // HtmlDialogUIDelegate declarations.
  virtual ui::ModalType GetDialogModalType() const OVERRIDE;
  virtual string16 GetDialogTitle() const OVERRIDE;
  virtual GURL GetDialogContentURL() const OVERRIDE;
  virtual void GetWebUIMessageHandlers(
      std::vector<WebUIMessageHandler*>* handlers) const OVERRIDE;
  virtual void GetDialogSize(gfx::Size* size) const OVERRIDE;
  virtual std::string GetDialogArgs() const OVERRIDE;
  virtual void OnDialogClosed(const std::string& json_retval) OVERRIDE;
  virtual void OnCloseContents(WebContents* source,
                               bool* out_close_dialog) OVERRIDE;
  virtual bool ShouldShowDialogTitle() const OVERRIDE { return true; }

  // HtmlDialogTabContentsDelegate declarations.
  virtual void MoveContents(WebContents* source, const gfx::Rect& pos);
  virtual void HandleKeyboardEvent(const NativeWebKeyboardEvent& event);
  virtual void CloseContents(WebContents* source) OVERRIDE;
  virtual content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params) OVERRIDE;
  virtual void AddNewContents(content::WebContents* source,
                              content::WebContents* new_contents,
                              WindowOpenDisposition disposition,
                              const gfx::Rect& initial_pos,
                              bool user_gesture) OVERRIDE;
  virtual void LoadingStateChanged(content::WebContents* source) OVERRIDE;

private:
  HtmlDialogWindowController* controller_;  // weak
  HtmlDialogUIDelegate* delegate_;  // weak, owned by controller_
  HtmlDialogController* dialog_controller_;

  // Calls delegate_'s OnDialogClosed() exactly once, nulling it out
  // afterwards so that no other HtmlDialogUIDelegate calls are sent
  // to it.  Returns whether or not the OnDialogClosed() was actually
  // called on the delegate.
  bool DelegateOnDialogClosed(const std::string& json_retval);

  DISALLOW_COPY_AND_ASSIGN(HtmlDialogWindowDelegateBridge);
};

// ChromeEventProcessingWindow expects its controller to implement the
// BrowserCommandExecutor protocol.
@interface HtmlDialogWindowController (InternalAPI) <BrowserCommandExecutor>

// BrowserCommandExecutor methods.
- (void)executeCommand:(int)command;

@end

namespace browser {

gfx::NativeWindow ShowHtmlDialog(gfx::NativeWindow parent,
                                 Profile* profile,
                                 Browser* browser,
                                 HtmlDialogUIDelegate* delegate,
                                 DialogStyle style) {
  return [HtmlDialogWindowController showHtmlDialog:delegate
      profile:profile
      browser:browser];
}

void CloseHtmlDialog(gfx::NativeWindow window) {
  [window performClose:nil];
}

}  // namespace html_dialog_window_controller

HtmlDialogWindowDelegateBridge::HtmlDialogWindowDelegateBridge(
    HtmlDialogWindowController* controller,
    Profile* profile,
    Browser* browser,
    HtmlDialogUIDelegate* delegate)
    : HtmlDialogTabContentsDelegate(profile),
      controller_(controller),
      delegate_(delegate),
      dialog_controller_(new HtmlDialogController(this, profile, browser)) {
  DCHECK(controller_);
  DCHECK(delegate_);
}

HtmlDialogWindowDelegateBridge::~HtmlDialogWindowDelegateBridge() {}

void HtmlDialogWindowDelegateBridge::WindowControllerClosed() {
  Detach();
  delete dialog_controller_;
  controller_ = nil;
  DelegateOnDialogClosed("");
}

bool HtmlDialogWindowDelegateBridge::DelegateOnDialogClosed(
    const std::string& json_retval) {
  if (delegate_) {
    HtmlDialogUIDelegate* real_delegate = delegate_;
    delegate_ = NULL;
    real_delegate->OnDialogClosed(json_retval);
    return true;
  }
  return false;
}

// HtmlDialogUIDelegate definitions.

// All of these functions check for NULL first since delegate_ is set
// to NULL when the window is closed.

ui::ModalType HtmlDialogWindowDelegateBridge::GetDialogModalType() const {
  // TODO(akalin): Support modal dialog boxes.
  if (delegate_ && delegate_->GetDialogModalType() != ui::MODAL_TYPE_NONE) {
    LOG(WARNING) << "Modal HTML dialogs are not supported yet";
  }
  return ui::MODAL_TYPE_NONE;
}

string16 HtmlDialogWindowDelegateBridge::GetDialogTitle() const {
  return delegate_ ? delegate_->GetDialogTitle() : string16();
}

GURL HtmlDialogWindowDelegateBridge::GetDialogContentURL() const {
  return delegate_ ? delegate_->GetDialogContentURL() : GURL();
}

void HtmlDialogWindowDelegateBridge::GetWebUIMessageHandlers(
    std::vector<WebUIMessageHandler*>* handlers) const {
  if (delegate_) {
    delegate_->GetWebUIMessageHandlers(handlers);
  } else {
    // TODO(akalin): Add this clause in the windows version.  Also
    // make sure that everything expects handlers to be non-NULL and
    // document it.
    handlers->clear();
  }
}

void HtmlDialogWindowDelegateBridge::GetDialogSize(gfx::Size* size) const {
  if (delegate_) {
    delegate_->GetDialogSize(size);
  } else {
    *size = gfx::Size();
  }
}

std::string HtmlDialogWindowDelegateBridge::GetDialogArgs() const {
  return delegate_ ? delegate_->GetDialogArgs() : "";
}

void HtmlDialogWindowDelegateBridge::OnDialogClosed(
    const std::string& json_retval) {
  Detach();
  // [controller_ close] should be called at most once, too.
  if (DelegateOnDialogClosed(json_retval)) {
    [controller_ close];
  }
  controller_ = nil;
}

void HtmlDialogWindowDelegateBridge::OnCloseContents(WebContents* source,
                                                     bool* out_close_dialog) {
  if (out_close_dialog)
    *out_close_dialog = true;
}

void HtmlDialogWindowDelegateBridge::CloseContents(WebContents* source) {
  bool close_dialog = false;
  OnCloseContents(source, &close_dialog);
  if (close_dialog)
    OnDialogClosed(std::string());
}

content::WebContents* HtmlDialogWindowDelegateBridge::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params) {
  content::WebContents* new_contents = NULL;
  if (delegate_ &&
      delegate_->HandleOpenURLFromTab(source, params, &new_contents)) {
    return new_contents;
  }
  return HtmlDialogTabContentsDelegate::OpenURLFromTab(source, params);
}

void HtmlDialogWindowDelegateBridge::AddNewContents(
    content::WebContents* source,
    content::WebContents* new_contents,
    WindowOpenDisposition disposition,
    const gfx::Rect& initial_pos,
    bool user_gesture) {
  if (delegate_ && delegate_->HandleAddNewContents(
          source, new_contents, disposition, initial_pos, user_gesture)) {
    return;
  }
  HtmlDialogTabContentsDelegate::AddNewContents(
      source, new_contents, disposition, initial_pos, user_gesture);
}

void HtmlDialogWindowDelegateBridge::LoadingStateChanged(
    content::WebContents* source) {
  if (delegate_)
    delegate_->OnLoadingStateChanged(source);
}

void HtmlDialogWindowDelegateBridge::MoveContents(WebContents* source,
                                                  const gfx::Rect& pos) {
  // TODO(akalin): Actually set the window bounds.
}

// A simplified version of BrowserWindowCocoa::HandleKeyboardEvent().
// We don't handle global keyboard shortcuts here, but that's fine since
// they're all browser-specific. (This may change in the future.)
void HtmlDialogWindowDelegateBridge::HandleKeyboardEvent(
    const NativeWebKeyboardEvent& event) {
  if (event.skip_in_browser || event.type == NativeWebKeyboardEvent::Char)
    return;

  // Close ourselves if the user hits Esc or Command-. .  The normal
  // way to do this is to implement (void)cancel:(int)sender, but
  // since we handle keyboard events ourselves we can't do that.
  //
  // According to experiments, hitting Esc works regardless of the
  // presence of other modifiers (as long as it's not an app-level
  // shortcut, e.g. Commmand-Esc for Front Row) but no other modifiers
  // can be present for Command-. to work.
  //
  // TODO(thakis): It would be nice to get cancel: to work somehow.
  // Bug: http://code.google.com/p/chromium/issues/detail?id=32828 .
  if (event.type == NativeWebKeyboardEvent::RawKeyDown &&
      ((event.windowsKeyCode == ui::VKEY_ESCAPE) ||
       (event.windowsKeyCode == ui::VKEY_OEM_PERIOD &&
        event.modifiers == NativeWebKeyboardEvent::MetaKey))) {
    [controller_ close];
    return;
  }

  ChromeEventProcessingWindow* event_window =
      static_cast<ChromeEventProcessingWindow*>([controller_ window]);
  DCHECK([event_window isKindOfClass:[ChromeEventProcessingWindow class]]);
  [event_window redispatchKeyEvent:event.os_event];
}

@implementation HtmlDialogWindowController (InternalAPI)

// This gets called whenever a chrome-specific keyboard shortcut is performed
// in the HTML dialog window.  We simply swallow all those events.
- (void)executeCommand:(int)command {}

@end

@implementation HtmlDialogWindowController

// NOTE(akalin): We'll probably have to add the parentWindow parameter back
// in once we implement modal dialogs.

+ (NSWindow*)showHtmlDialog:(HtmlDialogUIDelegate*)delegate
                    profile:(Profile*)profile
                    browser:(Browser*)browser {
  HtmlDialogWindowController* htmlDialogWindowController =
    [[HtmlDialogWindowController alloc] initWithDelegate:delegate
                                                 profile:profile
                                                 browser:browser];
  [htmlDialogWindowController loadDialogContents];
  [htmlDialogWindowController showWindow:nil];
  return [htmlDialogWindowController window];
}

- (id)initWithDelegate:(HtmlDialogUIDelegate*)delegate
               profile:(Profile*)profile
               browser:(Browser*)browser {
  DCHECK(delegate);
  DCHECK(profile);

  gfx::Size dialogSize;
  delegate->GetDialogSize(&dialogSize);
  NSRect dialogRect = NSMakeRect(0, 0, dialogSize.width(), dialogSize.height());
  NSUInteger style = NSTitledWindowMask | NSClosableWindowMask |
      NSResizableWindowMask;
  scoped_nsobject<ChromeEventProcessingWindow> window(
      [[ChromeEventProcessingWindow alloc]
           initWithContentRect:dialogRect
                     styleMask:style
                       backing:NSBackingStoreBuffered
                         defer:YES]);
  if (!window.get()) {
    return nil;
  }
  self = [super initWithWindow:window];
  if (!self) {
    return nil;
  }
  [window setWindowController:self];
  [window setDelegate:self];
  [window setTitle:base::SysUTF16ToNSString(delegate->GetDialogTitle())];
  [window setMinSize:dialogRect.size];
  [window center];
  delegate_.reset(
      new HtmlDialogWindowDelegateBridge(self, profile, browser, delegate));
  return self;
}

- (void)loadDialogContents {
  contentsWrapper_.reset(new TabContentsWrapper(WebContents::Create(
      delegate_->profile(), NULL, MSG_ROUTING_NONE, NULL, NULL)));
  [[self window]
      setContentView:contentsWrapper_->web_contents()->GetNativeView()];
  contentsWrapper_->web_contents()->SetDelegate(delegate_.get());

  // This must be done before loading the page; see the comments in
  // HtmlDialogUI.
  HtmlDialogUI::GetPropertyAccessor().SetProperty(
      contentsWrapper_->web_contents()->GetPropertyBag(), delegate_.get());

  contentsWrapper_->web_contents()->GetController().LoadURL(
      delegate_->GetDialogContentURL(),
      content::Referrer(),
      content::PAGE_TRANSITION_START_PAGE,
      std::string());

  // TODO(akalin): add accelerator for ESC to close the dialog box.
  //
  // TODO(akalin): Figure out why implementing (void)cancel:(id)sender
  // to do the above doesn't work.
}

- (void)windowWillClose:(NSNotification*)notification {
  delegate_->WindowControllerClosed();
  [self autorelease];
}

@end
