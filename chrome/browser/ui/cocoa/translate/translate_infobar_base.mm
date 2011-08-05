// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>
#import "chrome/browser/ui/cocoa/translate/translate_infobar_base.h"

#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/sys_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/translate/translate_infobar_delegate.h"
#import "chrome/browser/ui/cocoa/hover_close_button.h"
#include "chrome/browser/ui/cocoa/infobars/infobar.h"
#import "chrome/browser/ui/cocoa/infobars/infobar_container_controller.h"
#import "chrome/browser/ui/cocoa/infobars/infobar_controller.h"
#import "chrome/browser/ui/cocoa/infobars/infobar_gradient_view.h"
#include "chrome/browser/ui/cocoa/translate/after_translate_infobar_controller.h"
#import "chrome/browser/ui/cocoa/translate/before_translate_infobar_controller.h"
#include "chrome/browser/ui/cocoa/translate/translate_message_infobar_controller.h"
#include "grit/generated_resources.h"
#include "third_party/GTM/AppKit/GTMUILocalizerAndLayoutTweaker.h"
#include "ui/base/l10n/l10n_util.h"

using TranslateInfoBarUtilities::MoveControl;
using TranslateInfoBarUtilities::VerticallyCenterView;
using TranslateInfoBarUtilities::VerifyControlOrderAndSpacing;
using TranslateInfoBarUtilities::CreateLabel;
using TranslateInfoBarUtilities::AddMenuItem;

#pragma mark TranslateInfoBarUtilities helper functions.

namespace TranslateInfoBarUtilities {

// Move the |toMove| view |spacing| pixels before/after the |anchor| view.
// |after| signifies the side of |anchor| on which to place |toMove|.
void MoveControl(NSView* anchor, NSView* toMove, int spacing, bool after) {
  NSRect anchorFrame = [anchor frame];
  NSRect toMoveFrame = [toMove frame];

  // At the time of this writing, OS X doesn't natively support BiDi UIs, but
  // it doesn't hurt to be forward looking.
  bool toRight = after;

  if (toRight) {
    toMoveFrame.origin.x = NSMaxX(anchorFrame) + spacing;
  } else {
    // Place toMove to theleft of anchor.
    toMoveFrame.origin.x = NSMinX(anchorFrame) -
        spacing - NSWidth(toMoveFrame);
  }
  [toMove setFrame:toMoveFrame];
}

// Check that the control |before| is ordered visually before the |after|
// control.
// Also, check that there is space between them.
bool VerifyControlOrderAndSpacing(id before, id after) {
  NSRect beforeFrame = [before frame];
  NSRect afterFrame = [after frame];
  return NSMinX(afterFrame) >= NSMaxX(beforeFrame);
}

// Vertically center |toMove| in its container.
void VerticallyCenterView(NSView* toMove) {
  NSRect superViewFrame = [[toMove superview] frame];
  NSRect viewFrame = [toMove frame];
  // If the superview is the infobar view, then subtract out the anti-spoof
  // height so that the content is centered in the content area of the infobar,
  // rather than in the total height (which includes the bulge).
  CGFloat superHeight = NSHeight(superViewFrame);
  if ([[toMove superview] isKindOfClass:[InfoBarGradientView class]])
    superHeight = infobars::kBaseHeight;
  viewFrame.origin.y =
      floor((superHeight - NSHeight(viewFrame)) / 2.0);
  [toMove setFrame:viewFrame];
}

// Creates a label control in the style we need for the translate infobar's
// labels within |bounds|.
NSTextField* CreateLabel(NSRect bounds) {
  NSTextField* ret = [[NSTextField alloc] initWithFrame:bounds];
  [ret setEditable:NO];
  [ret setDrawsBackground:NO];
  [ret setBordered:NO];
  return ret;
}

// Adds an item with the specified properties to |menu|.
void AddMenuItem(NSMenu *menu, id target, SEL selector, NSString* title,
    int tag, bool enabled, bool checked) {
  if (tag == -1) {
    [menu addItem:[NSMenuItem separatorItem]];
  } else {
    NSMenuItem* item = [[[NSMenuItem alloc]
      initWithTitle:title
             action:selector
      keyEquivalent:@""] autorelease];
    [item setTag:tag];
    [menu addItem:item];
    [item setTarget:target];
    if (checked)
      [item setState:NSOnState];
    if (!enabled)
      [item setEnabled:NO];
  }
}

}  // namespace TranslateInfoBarUtilities

// TranslateInfoBarDelegate views specific method:
InfoBar* TranslateInfoBarDelegate::CreateInfoBar(TabContentsWrapper* owner) {
  TranslateInfoBarControllerBase* infobar_controller = NULL;
  switch (type_) {
    case BEFORE_TRANSLATE:
      infobar_controller =
          [[BeforeTranslateInfobarController alloc] initWithDelegate:this];
      break;
    case AFTER_TRANSLATE:
      infobar_controller =
          [[AfterTranslateInfobarController alloc] initWithDelegate:this];
      break;
    case TRANSLATING:
    case TRANSLATION_ERROR:
      infobar_controller =
          [[TranslateMessageInfobarController alloc] initWithDelegate:this];
      break;
    default:
      NOTREACHED();
  }
  return new InfoBar(infobar_controller);
}

@implementation TranslateInfoBarControllerBase (FrameChangeObserver)

// Triggered when the frame changes.  This will figure out what size and
// visibility the options popup should be.
- (void)didChangeFrame:(NSNotification*)notification {
  [self adjustOptionsButtonSizeAndVisibilityForView:
      [[self visibleControls] lastObject]];
}

@end


@interface TranslateInfoBarControllerBase (Private)

// Removes all controls so that layout can add in only the controls
// required.
- (void)clearAllControls;

// Create all the various controls we need for the toolbar.
- (void)constructViews;

// Reloads text for all labels for the current state.
- (void)loadLabelText:(TranslateErrors::Type)error;

// Set the infobar background gradient.
- (void)setInfoBarGradientColor;

// Main function to update the toolbar graphic state and data model after
// the state has changed.
// Controls are moved around as needed and visibility changed to match the
// current state.
- (void)updateState;

// Called when the source or target language selection changes in a menu.
// |newLanguageIdx| is the index of the newly selected item in the appropriate
// menu.
- (void)sourceLanguageModified:(NSInteger)newLanguageIdx;
- (void)targetLanguageModified:(NSInteger)newLanguageIdx;

// Completely rebuild "from" and "to" language menus from the data model.
- (void)populateLanguageMenus;

@end

#pragma mark TranslateInfoBarController class

@implementation TranslateInfoBarControllerBase

- (id)initWithDelegate:(InfoBarDelegate*)delegate {
  if ((self = [super initWithDelegate:delegate])) {
      originalLanguageMenuModel_.reset(
          new LanguagesMenuModel([self delegate],
                                 LanguagesMenuModel::ORIGINAL));

      targetLanguageMenuModel_.reset(
          new LanguagesMenuModel([self delegate],
                                 LanguagesMenuModel::TARGET));
  }
  return self;
}

- (TranslateInfoBarDelegate*)delegate {
  return reinterpret_cast<TranslateInfoBarDelegate*>(delegate_);
}

- (void)constructViews {
  // Using a zero or very large frame causes GTMUILocalizerAndLayoutTweaker
  // to not resize the view properly so we take the bounds of the first label
  // which is contained in the nib.
  NSRect bogusFrame = [label_ frame];
  label1_.reset(CreateLabel(bogusFrame));
  label2_.reset(CreateLabel(bogusFrame));
  label3_.reset(CreateLabel(bogusFrame));

  optionsPopUp_.reset([[NSPopUpButton alloc] initWithFrame:bogusFrame
                                                 pullsDown:YES]);
  fromLanguagePopUp_.reset([[NSPopUpButton alloc] initWithFrame:bogusFrame
                                                      pullsDown:NO]);
  toLanguagePopUp_.reset([[NSPopUpButton alloc] initWithFrame:bogusFrame
                                                     pullsDown:NO]);
  showOriginalButton_.reset([[NSButton alloc] init]);
  translateMessageButton_.reset([[NSButton alloc] init]);
}

- (void)sourceLanguageModified:(NSInteger)newLanguageIdx {
  size_t newLanguageIdxSizeT = static_cast<size_t>(newLanguageIdx);
  DCHECK_NE(TranslateInfoBarDelegate::kNoIndex, newLanguageIdxSizeT);
  if (newLanguageIdxSizeT == [self delegate]->original_language_index())
    return;
  [self delegate]->SetOriginalLanguage(newLanguageIdxSizeT);
  int commandId = IDC_TRANSLATE_ORIGINAL_LANGUAGE_BASE + newLanguageIdx;
  int newMenuIdx = [fromLanguagePopUp_ indexOfItemWithTag:commandId];
  [fromLanguagePopUp_ selectItemAtIndex:newMenuIdx];
}

- (void)targetLanguageModified:(NSInteger)newLanguageIdx {
  size_t newLanguageIdxSizeT = static_cast<size_t>(newLanguageIdx);
  DCHECK_NE(TranslateInfoBarDelegate::kNoIndex, newLanguageIdxSizeT);
  if (newLanguageIdxSizeT == [self delegate]->target_language_index())
    return;
  [self delegate]->SetTargetLanguage(newLanguageIdxSizeT);
  int commandId = IDC_TRANSLATE_TARGET_LANGUAGE_BASE + newLanguageIdx;
  int newMenuIdx = [toLanguagePopUp_ indexOfItemWithTag:commandId];
  [toLanguagePopUp_ selectItemAtIndex:newMenuIdx];
}

- (void)loadLabelText {
  // Do nothing by default, should be implemented by subclasses.
}

- (void)updateState {
  [self loadLabelText];
  [self clearAllControls];
  [self showVisibleControls:[self visibleControls]];
  [optionsPopUp_ setHidden:![self shouldShowOptionsPopUp]];
  [self layout];
  [self adjustOptionsButtonSizeAndVisibilityForView:
      [[self visibleControls] lastObject]];
}

- (void)setInfoBarGradientColor {
  NSColor* startingColor = [NSColor colorWithCalibratedWhite:0.93 alpha:1.0];
  NSColor* endingColor = [NSColor colorWithCalibratedWhite:0.85 alpha:1.0];
  NSGradient* translateInfoBarGradient =
      [[[NSGradient alloc] initWithStartingColor:startingColor
                                     endingColor:endingColor] autorelease];

  [infoBarView_ setGradient:translateInfoBarGradient];
  [infoBarView_
      setStrokeColor:[NSColor colorWithCalibratedWhite:0.75 alpha:1.0]];
}

- (void)removeOkCancelButtons {
  // Removing okButton_ & cancelButton_ from the view may cause them
  // to be released and since we can still access them from other areas
  // in the code later, we need them to be nil when this happens.
  [okButton_ removeFromSuperview];
  okButton_ = nil;
  [cancelButton_ removeFromSuperview];
  cancelButton_ = nil;
}

- (void)clearAllControls {
  // Step 1: remove all controls from the infobar so we have a clean slate.
  NSArray *allControls = [self allControls];

  for (NSControl* control in allControls) {
    if ([control superview])
      [control removeFromSuperview];
  }
}

- (void)showVisibleControls:(NSArray*)visibleControls {
  NSRect optionsFrame = [optionsPopUp_ frame];
  for (NSControl* control in visibleControls) {
    [GTMUILocalizerAndLayoutTweaker sizeToFitView:control];
    [control setAutoresizingMask:NSViewMaxXMargin];

    // Need to check if a view is already attached since |label1_| is always
    // parented and we don't want to add it again.
    if (![control superview])
      [infoBarView_ addSubview:control];

    if ([control isKindOfClass:[NSButton class]])
      VerticallyCenterView(control);

    // Make "from" and "to" language popup menus the same size as the options
    // menu.
    // We don't autosize since some languages names are really long causing
    // the toolbar to overflow.
    if ([control isKindOfClass:[NSPopUpButton class]])
      [control setFrame:optionsFrame];
  }
}

- (void)layout {

}

- (NSArray*)visibleControls {
  return [NSArray array];
}

- (void)rebuildOptionsMenu:(BOOL)hideTitle {
  if (![self shouldShowOptionsPopUp])
     return;

  // The options model doesn't know how to handle state transitions, so rebuild
  // it each time through here.
  optionsMenuModel_.reset(
              new OptionsMenuModel([self delegate]));

  [optionsPopUp_ removeAllItems];
  // Set title.
  NSString* optionsLabel = hideTitle ? @"" :
      l10n_util::GetNSString(IDS_TRANSLATE_INFOBAR_OPTIONS);
  [optionsPopUp_ addItemWithTitle:optionsLabel];

   // Populate options menu.
  NSMenu* optionsMenu = [optionsPopUp_ menu];
  [optionsMenu setAutoenablesItems:NO];
  for (int i = 0; i < optionsMenuModel_->GetItemCount(); ++i) {
    NSString* title = base::SysUTF16ToNSString(
        optionsMenuModel_->GetLabelAt(i));
    int cmd = optionsMenuModel_->GetCommandIdAt(i);
    bool checked = optionsMenuModel_->IsItemCheckedAt(i);
    bool enabled = optionsMenuModel_->IsEnabledAt(i);
    AddMenuItem(optionsMenu,
                self,
                @selector(optionsMenuChanged:),
                title,
                cmd,
                enabled,
                checked);
  }
}

- (BOOL)shouldShowOptionsPopUp {
  return YES;
}

- (void)populateLanguageMenus {
  NSMenu* originalLanguageMenu = [fromLanguagePopUp_ menu];
  [originalLanguageMenu setAutoenablesItems:NO];
  int selectedMenuIndex = 0;
  int selectedLangIndex =
      static_cast<int>([self delegate]->original_language_index());
  for (int i = 0; i < originalLanguageMenuModel_->GetItemCount(); ++i) {
    NSString* title = base::SysUTF16ToNSString(
        originalLanguageMenuModel_->GetLabelAt(i));
    int cmd = originalLanguageMenuModel_->GetCommandIdAt(i);
    bool checked = (cmd == selectedLangIndex);
    if (checked)
      selectedMenuIndex = i;
    bool enabled = originalLanguageMenuModel_->IsEnabledAt(i);
    cmd += IDC_TRANSLATE_ORIGINAL_LANGUAGE_BASE;
    AddMenuItem(originalLanguageMenu,
                self,
                @selector(languageMenuChanged:),
                title,
                cmd,
                enabled,
                checked);
  }
  [fromLanguagePopUp_ selectItemAtIndex:selectedMenuIndex];

  NSMenu* targetLanguageMenu = [toLanguagePopUp_ menu];
  [targetLanguageMenu setAutoenablesItems:NO];
  selectedLangIndex =
      static_cast<int>([self delegate]->target_language_index());
  for (int i = 0; i < targetLanguageMenuModel_->GetItemCount(); ++i) {
    NSString* title = base::SysUTF16ToNSString(
        targetLanguageMenuModel_->GetLabelAt(i));
    int cmd = targetLanguageMenuModel_->GetCommandIdAt(i);
    bool checked = (cmd == selectedLangIndex);
    if (checked)
      selectedMenuIndex = i;
    bool enabled = targetLanguageMenuModel_->IsEnabledAt(i);
    cmd += IDC_TRANSLATE_TARGET_LANGUAGE_BASE;
    AddMenuItem(targetLanguageMenu,
                self,
                @selector(languageMenuChanged:),
                title,
                cmd,
                enabled,
                checked);
  }
  [toLanguagePopUp_ selectItemAtIndex:selectedMenuIndex];
}

- (void)addAdditionalControls {
  using l10n_util::GetNSString;
  using l10n_util::GetNSStringWithFixup;

  // Get layout information from the NIB.
  NSRect okButtonFrame = [okButton_ frame];
  NSRect cancelButtonFrame = [cancelButton_ frame];
  spaceBetweenControls_ = NSMinX(cancelButtonFrame) - NSMaxX(okButtonFrame);

  // Set infobar background color.
  [self setInfoBarGradientColor];

  // Instantiate additional controls.
  [self constructViews];

  // Set ourselves as the delegate for the options menu so we can populate it
  // dynamically.
  [[optionsPopUp_ menu] setDelegate:self];

  // Replace label_ with label1_ so we get a consistent look between all the
  // labels we display in the translate view.
  [[label_ superview] replaceSubview:label_ with:label1_.get()];
  label_.reset(); // Now released.

  // Populate contextual menus.
  [self rebuildOptionsMenu:NO];
  [self populateLanguageMenus];

  // Set OK & Cancel text.
  [okButton_ setTitle:GetNSStringWithFixup(IDS_TRANSLATE_INFOBAR_ACCEPT)];
  [cancelButton_ setTitle:GetNSStringWithFixup(IDS_TRANSLATE_INFOBAR_DENY)];

  // Set up "Show original" and "Try again" buttons.
  [showOriginalButton_ setFrame:okButtonFrame];

  // Set each of the buttons and popups to the NSTexturedRoundedBezelStyle
  // (metal-looking) style.
  NSArray* allControls = [self allControls];
  for (NSControl* control in allControls) {
    if (![control isKindOfClass:[NSButton class]])
      continue;
    NSButton* button = (NSButton*)control;
    [button setBezelStyle:NSTexturedRoundedBezelStyle];
    if ([button isKindOfClass:[NSPopUpButton class]]) {
      [[button cell] setArrowPosition:NSPopUpArrowAtBottom];
    }
  }
  // The options button is handled differently than the rest as it floats
  // to the right.
  [optionsPopUp_ setBezelStyle:NSTexturedRoundedBezelStyle];
  [[optionsPopUp_ cell] setArrowPosition:NSPopUpArrowAtBottom];

  [showOriginalButton_ setTarget:self];
  [showOriginalButton_ setAction:@selector(showOriginal:)];
  [translateMessageButton_ setTarget:self];
  [translateMessageButton_ setAction:@selector(messageButtonPressed:)];

  [showOriginalButton_
      setTitle:GetNSStringWithFixup(IDS_TRANSLATE_INFOBAR_REVERT)];

  // Add and configure controls that are visible in all modes.
  [optionsPopUp_ setAutoresizingMask:NSViewMinXMargin];
  // Add "options" popup z-ordered below all other controls so when we
  // resize the toolbar it doesn't hide them.
  [infoBarView_ addSubview:optionsPopUp_
                positioned:NSWindowBelow
                relativeTo:nil];
  [GTMUILocalizerAndLayoutTweaker sizeToFitView:optionsPopUp_];
  MoveControl(closeButton_, optionsPopUp_, spaceBetweenControls_, false);
  VerticallyCenterView(optionsPopUp_);

  [infoBarView_ setPostsFrameChangedNotifications:YES];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(didChangeFrame:)
             name:NSViewFrameDidChangeNotification
           object:infoBarView_];
  // Show and place GUI elements.
  [self updateState];
}

- (void)infobarWillClose {
  [[optionsPopUp_ menu] cancelTracking];
  [super infobarWillClose];
}

- (void)adjustOptionsButtonSizeAndVisibilityForView:(NSView*)lastView {
  [optionsPopUp_ setHidden:NO];
  [self rebuildOptionsMenu:NO];
  [[optionsPopUp_ cell] setArrowPosition:NSPopUpArrowAtBottom];
  [optionsPopUp_ sizeToFit];

  MoveControl(closeButton_, optionsPopUp_, spaceBetweenControls_, false);
  if (!VerifyControlOrderAndSpacing(lastView, optionsPopUp_)) {
    [self rebuildOptionsMenu:YES];
    NSRect oldFrame = [optionsPopUp_ frame];
    oldFrame.size.width = NSHeight(oldFrame);
    [optionsPopUp_ setFrame:oldFrame];
    [[optionsPopUp_ cell] setArrowPosition:NSPopUpArrowAtCenter];
    MoveControl(closeButton_, optionsPopUp_, spaceBetweenControls_, false);
    if (!VerifyControlOrderAndSpacing(lastView, optionsPopUp_)) {
      [optionsPopUp_ setHidden:YES];
    }
  }
}

// Called when "Translate" button is clicked.
- (IBAction)ok:(id)sender {
  TranslateInfoBarDelegate* delegate = [self delegate];
  TranslateInfoBarDelegate::Type state = delegate->type();
  DCHECK(state == TranslateInfoBarDelegate::BEFORE_TRANSLATE ||
      state == TranslateInfoBarDelegate::TRANSLATION_ERROR);
  delegate->Translate();
  UMA_HISTOGRAM_COUNTS("Translate.Translate", 1);
}

// Called when someone clicks on the "Nope" button.
- (IBAction)cancel:(id)sender {
  DCHECK(
      [self delegate]->type() == TranslateInfoBarDelegate::BEFORE_TRANSLATE);
  [self delegate]->TranslationDeclined();
  UMA_HISTOGRAM_COUNTS("Translate.DeclineTranslate", 1);
  [super dismiss:nil];
}

- (void)messageButtonPressed:(id)sender {
  [self delegate]->MessageInfoBarButtonPressed();
}

- (IBAction)showOriginal:(id)sender {
  [self delegate]->RevertTranslation();
}

// Called when any of the language drop down menus are changed.
- (void)languageMenuChanged:(id)item {
  if ([item respondsToSelector:@selector(tag)]) {
    int cmd = [item tag];
    if (cmd >= IDC_TRANSLATE_TARGET_LANGUAGE_BASE) {
      cmd -= IDC_TRANSLATE_TARGET_LANGUAGE_BASE;
      [self targetLanguageModified:cmd];
      return;
    } else if (cmd >= IDC_TRANSLATE_ORIGINAL_LANGUAGE_BASE) {
      cmd -= IDC_TRANSLATE_ORIGINAL_LANGUAGE_BASE;
      [self sourceLanguageModified:cmd];
      return;
    }
  }
  NOTREACHED() << "Language menu was changed with a bad language ID";
}

// Called when the options menu is changed.
- (void)optionsMenuChanged:(id)item {
  if ([item respondsToSelector:@selector(tag)]) {
    int cmd = [item tag];
    // Danger Will Robinson! : This call can release the infobar (e.g. invoking
    // "About Translate" can open a new tab).
    // Do not access member variables after this line!
    optionsMenuModel_->ExecuteCommand(cmd);
  } else {
    NOTREACHED();
  }
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

#pragma mark NSMenuDelegate

// Invoked by virtue of us being set as the delegate for the options menu.
- (void)menuNeedsUpdate:(NSMenu *)menu {
  [self adjustOptionsButtonSizeAndVisibilityForView:
      [[self visibleControls] lastObject]];
}

@end

@implementation TranslateInfoBarControllerBase (TestingAPI)

- (NSArray*)allControls {
  return [NSArray arrayWithObjects:label1_.get(),fromLanguagePopUp_.get(),
      label2_.get(), toLanguagePopUp_.get(), label3_.get(), okButton_,
      cancelButton_, showOriginalButton_.get(), translateMessageButton_.get(),
      nil];
}

- (NSMenu*)optionsMenu {
  return [optionsPopUp_ menu];
}

- (NSButton*)translateMessageButton {
  return translateMessageButton_.get();
}

- (bool)verifyLayout {
  // All the controls available to translate infobars, except the options popup.
  // The options popup is shown/hidden instead of actually removed.  This gets
  // checked in the subclasses.
  NSArray* allControls = [self allControls];
  NSArray* visibleControls = [self visibleControls];

  // Step 1: Make sure control visibility is what we expect.
  for (NSUInteger i = 0; i < [allControls count]; ++i) {
    id control = [allControls objectAtIndex:i];
    bool hasSuperView = [control superview];
    bool expectedVisibility = [visibleControls containsObject:control];

    if (expectedVisibility != hasSuperView) {
      NSString *title = @"";
      if ([control isKindOfClass:[NSPopUpButton class]]) {
        title = [[[control menu] itemAtIndex:0] title];
      }

      LOG(ERROR) <<
          "State: " << [self description] <<
          " Control @" << i << (hasSuperView ? " has" : " doesn't have") <<
          " a superview" << [[control description] UTF8String] <<
          " Title=" << [title UTF8String];
      return false;
    }
  }

  // Step 2: Check that controls are ordered correctly with no overlap.
  id previousControl = nil;
  for (NSUInteger i = 0; i < [visibleControls count]; ++i) {
    id control = [visibleControls objectAtIndex:i];
    // The options pop up doesn't lay out like the rest of the controls as
    // it floats to the right.  It has some known issues shown in
    // http://crbug.com/47941.
    if (control == optionsPopUp_.get())
      continue;
    if (previousControl &&
        !VerifyControlOrderAndSpacing(previousControl, control)) {
      NSString *title = @"";
      if ([control isKindOfClass:[NSPopUpButton class]]) {
        title = [[[control menu] itemAtIndex:0] title];
      }
      LOG(ERROR) <<
          "State: " << [self description] <<
          " Control @" << i << " not ordered correctly: " <<
          [[control description] UTF8String] <<[title UTF8String];
      return false;
    }
    previousControl = control;
  }

  return true;
}

@end // TranslateInfoBarControllerBase (TestingAPI)

