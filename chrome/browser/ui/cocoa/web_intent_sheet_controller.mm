// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/web_intent_sheet_controller.h"

#include "base/memory/scoped_nsobject.h"
#include "base/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/browser_list.h"
#import "chrome/browser/ui/cocoa/hover_close_button.h"
#import "chrome/browser/ui/cocoa/hyperlink_button_cell.h"
#import "chrome/browser/ui/cocoa/info_bubble_view.h"
#import "chrome/browser/ui/cocoa/info_bubble_window.h"
#include "chrome/browser/ui/cocoa/web_intent_picker_cocoa.h"
#include "chrome/browser/ui/intents/web_intent_picker_delegate.h"
#include "chrome/browser/ui/intents/web_intent_picker_model.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/generated_resources.h"
#include "grit/google_chrome_strings.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#import "third_party/GTM/AppKit/GTMUILocalizerAndLayoutTweaker.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/text/text_elider.h"
#include "ui/gfx/image/image.h"

using content::OpenURLParams;
using content::Referrer;

namespace {

// The width of the window, in view coordinates. The height will be
// determined by the content.
const CGFloat kWindowWidth = 400;

// The maximum width in view units of a suggested extension's title link.
const int kTitleLinkMaxWidth = 130;

// The width of a service button, in view coordinates.
const CGFloat kServiceButtonWidth = 300;

// Padding along on the X-axis between the window frame and content.
const CGFloat kFramePadding = 10;

// Spacing in between sections.
const CGFloat kVerticalSpacing = 10;

// Square size of the close button.
const CGFloat kCloseButtonSize = 16;

// Font size for picker header.
const CGFloat kHeaderFontSize = 14.5;

// Width of the text fields.
const CGFloat kTextWidth = kWindowWidth -
    (kFramePadding * 2 + kCloseButtonSize);

// Sets properties on the given |field| to act as title or description labels.
void ConfigureTextFieldAsLabel(NSTextField* field) {
  [field setEditable:NO];
  [field setSelectable:YES];
  [field setDrawsBackground:NO];
  [field setBezeled:NO];
}

NSButton* CreateHyperlinkButton(NSString* title, const NSRect& frame) {
  NSButton* button = [[NSButton alloc] initWithFrame:frame];
  scoped_nsobject<HyperlinkButtonCell> cell(
      [[HyperlinkButtonCell alloc] initTextCell:title]);
  [cell setControlSize:NSSmallControlSize];
  [button setCell:cell.get()];
  [button setButtonType:NSMomentaryPushInButton];
  [button setBezelStyle:NSRegularSquareBezelStyle];

  return button;
}

}  // namespace

// This simple NSView subclass is used as the single subview of the page info
// bubble's window's contentView. Drawing is flipped so that layout of the
// sections is easier. Apple recommends flipping the coordinate origin when
// doing a lot of text layout because it's more natural.
@interface WebIntentsContentView : NSView
@end
@implementation WebIntentsContentView
- (BOOL)isFlipped {
  return YES;
}

- (void)drawRect:(NSRect)rect {
  [[NSColor colorWithCalibratedWhite:1.0 alpha:1.0] set];
  NSRectFill(rect);
}
@end

// NSImageView subclassed to allow fading the alpha value of the image to
// indicate an inactive/disabled extension.
@interface DimmableImageView : NSImageView {
 @private
  CGFloat alpha;
}

- (void)setEnabled:(BOOL)enabled;

// NSView override
- (void)drawRect:(NSRect)rect;
@end

@implementation DimmableImageView
- (void)drawRect:(NSRect)rect {
  NSImage* image = [self image];

  NSRect sourceRect, destinationRect;
  sourceRect.origin = NSZeroPoint;
  sourceRect.size = [image size];

  destinationRect.origin = NSZeroPoint;
  destinationRect.size = [self frame].size;

  // If the source image is smaller than the destination, center it.
  if (destinationRect.size.width > sourceRect.size.width) {
    destinationRect.origin.x =
        (destinationRect.size.width - sourceRect.size.width) / 2.0;
    destinationRect.size.width = sourceRect.size.width;
  }
  if (destinationRect.size.height > sourceRect.size.height) {
    destinationRect.origin.y =
        (destinationRect.size.height - sourceRect.size.height) / 2.0;
    destinationRect.size.height = sourceRect.size.height;
  }

  [image drawInRect:destinationRect
           fromRect:sourceRect
          operation:NSCompositeSourceOver
           fraction:alpha];
}

- (void)setEnabled:(BOOL)enabled {
  if (enabled)
    alpha = 1.0;
  else
    alpha = 0.5;
  [self setNeedsDisplay:YES];
}
@end

// An NSView subclass to display ratings stars.
@interface RatingsView : NSView
  // Mark RatingsView as disabled/enabled.
- (void)setEnabled:(BOOL)enabled;
@end

@implementation RatingsView
- (void)setEnabled:(BOOL)enabled {
  for (DimmableImageView* imageView in [self subviews])
    [imageView setEnabled:enabled];
}
@end

// NSView for a single row in the suggestions view.
@interface SingleSuggestionView : NSView {
 @private
  scoped_nsobject<NSProgressIndicator> throbber_;
  scoped_nsobject<NSButton> cwsButton_;
  scoped_nsobject<RatingsView> ratingsWidget_;
  scoped_nsobject<NSButton> installButton_;
  scoped_nsobject<DimmableImageView> iconView_;
}

- (id)initWithExtension:
    (const WebIntentPickerModel::SuggestedExtension*)extension
              withIndex:(size_t)index
          forController:(WebIntentPickerSheetController*)controller;
- (void)startThrobber;
- (void)setEnabled:(BOOL)enabled;
- (void)stopThrobber;
- (NSInteger)tag;
@end

@implementation SingleSuggestionView
- (id)initWithExtension:
    (const WebIntentPickerModel::SuggestedExtension*)extension
              withIndex:(size_t)index
          forController:(WebIntentPickerSheetController*)controller {
  const CGFloat kMaxHeight = 34.0;
  const CGFloat kTitleX = 20.0;
  const CGFloat kMinAddButtonHeight = 24.0;
  const CGFloat kAddButtonX = 245;
  const CGFloat kAddButtonWidth = 128.0;

  // Build the main view
  NSRect contentFrame = NSMakeRect(0, 0, kWindowWidth, kMaxHeight);
  if (self = [super initWithFrame:contentFrame]) {
    NSMutableArray* subviews = [NSMutableArray array];

    // Add the extension icon.
    NSImage* icon = extension->icon.ToNSImage();
    NSRect imageFrame = NSZeroRect;

    iconView_.reset(
        [[DimmableImageView alloc] initWithFrame:imageFrame]);
    [iconView_ setImage:icon];
    [iconView_ setImageFrameStyle:NSImageFrameNone];
    [iconView_ setEnabled:YES];

    imageFrame.size = [icon size];
    imageFrame.size.height = std::min(NSHeight(imageFrame), kMaxHeight);
    imageFrame.origin.y += (kMaxHeight - NSHeight(imageFrame)) / 2.0;
    [iconView_ setFrame:imageFrame];
    [subviews addObject:iconView_];

    // Add the extension title.
    NSRect frame = NSMakeRect(kTitleX, 0, 0, 0);

    const string16 elidedTitle = ui::ElideText(
        extension->title, gfx::Font(), kTitleLinkMaxWidth, ui::ELIDE_AT_END);
    NSString* string = base::SysUTF16ToNSString(elidedTitle);
    cwsButton_.reset(CreateHyperlinkButton(string, frame));
    [cwsButton_ setAlignment:NSLeftTextAlignment];
    [cwsButton_ setTarget:controller];
    [cwsButton_ setAction:@selector(openExtensionLink:)];
    [cwsButton_ setTag:index];
    [cwsButton_ sizeToFit];

    frame = [cwsButton_ frame];
    frame.size.height = std::min([[cwsButton_ cell] cellSize].height,
                                 kMaxHeight);
    frame.origin.y = (kMaxHeight - NSHeight(frame)) / 2.0;
    [cwsButton_ setFrame:frame];
    [subviews addObject:cwsButton_];

    // Add the star rating
    CGFloat offsetX = frame.origin.x + NSWidth(frame) + kFramePadding;
    ratingsWidget_.reset(
        [SingleSuggestionView
            createStarWidgetWithRating:extension->average_rating]);
    frame = [ratingsWidget_ frame];
    frame.origin.y += (kMaxHeight - NSHeight(frame)) / 2.0;
    frame.origin.x = offsetX;
    [ratingsWidget_ setFrame: frame];
    [subviews addObject:ratingsWidget_];

    // Add an "add to chromium" button.
    frame = NSMakeRect(kAddButtonX, 0, kAddButtonWidth, 0);
    installButton_.reset([[NSButton alloc] initWithFrame:frame]);
    [installButton_ setAlignment:NSLeftTextAlignment];
    [installButton_ setButtonType:NSMomentaryPushInButton];
    [installButton_ setBezelStyle:NSRegularSquareBezelStyle];
    string = l10n_util::GetNSStringWithFixup(
        IDS_INTENT_PICKER_INSTALL_EXTENSION);
    [installButton_ setTitle:string];
    frame.size.height = std::min(kMinAddButtonHeight, kMaxHeight);
    frame.origin.y += (kMaxHeight - NSHeight(frame)) / 2.0;
    [installButton_ setFrame:frame];

    [installButton_ setTarget:controller];
    [installButton_ setAction:@selector(installExtension:)];
    [installButton_ setTag:index];
    [subviews addObject:installButton_];

    // Keep a throbber handy.
    frame.origin.x += (NSWidth(frame) - 16) / 2;
    frame.origin.y += (NSHeight(frame) - 16) /2;
    frame.size = NSMakeSize(16, 16);
    throbber_.reset(
        [[NSProgressIndicator alloc] initWithFrame:frame]);
    [throbber_ setHidden:YES];
    [throbber_ setStyle:NSProgressIndicatorSpinningStyle];
    [subviews addObject:throbber_];

    [self setSubviews:subviews];
  }
  return self;
}

+ (RatingsView*)createStarWidgetWithRating:(CGFloat)rating {
  const int kStarSpacing = 1;  // Spacing between stars in pixels.
  const CGFloat kStarSize = 16.0; // Size of the star in pixels.

  NSMutableArray* subviews = [NSMutableArray array];

  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  NSRect imageFrame = NSMakeRect(0, 0, kStarSize, kStarSize);

  for (int i = 0; i < 5; ++i) {
    NSImage* nsImage = rb.GetNativeImageNamed(
        WebIntentPicker::GetNthStarImageIdFromCWSRating(rating, i));

    scoped_nsobject<DimmableImageView> imageView(
        [[DimmableImageView alloc] initWithFrame:imageFrame]);
    [imageView setImage:nsImage];
    [imageView setImageFrameStyle:NSImageFrameNone];
    [imageView setFrame:imageFrame];
    [imageView setEnabled:YES];
    [subviews addObject:imageView];
    imageFrame.origin.x += kStarSize + kStarSpacing;
  }

  NSRect frame = NSMakeRect(0, 0, (kStarSize + kStarSpacing) * 5, kStarSize);
  RatingsView* widget = [[RatingsView alloc] initWithFrame:frame];
  [widget setSubviews: subviews];
  return widget;
}

- (NSInteger)tag {
  return [installButton_ tag];
}

- (void)startThrobber {
  [installButton_ setHidden:YES];
  [throbber_ setHidden:NO];
  [throbber_ startAnimation:self];
  [iconView_ setEnabled:YES];
  [ratingsWidget_ setEnabled:YES];
}

- (void)setEnabled:(BOOL) enabled {
  [installButton_ setEnabled:enabled];
  [cwsButton_ setEnabled:enabled];
  [iconView_ setEnabled:enabled];
  [ratingsWidget_ setEnabled:enabled];
}

- (void)stopThrobber {
  [throbber_ setHidden:YES];
  [throbber_ stopAnimation:self];
  [installButton_ setHidden:NO];
}

@end

@interface SuggestionView : NSView {
 @private
  // Used to forward button clicks. Weak reference.
  WebIntentPickerSheetController* controller_;
  scoped_nsobject<NSTextField> suggestionLabel_;
}

- (id)initWithModel:(WebIntentPickerModel*)model
      forController:(WebIntentPickerSheetController*)controller;
- (void)startThrobberForRow:(NSInteger)index;
- (void)stopThrobber;
@end

@implementation SuggestionView
- (id)initWithModel:(WebIntentPickerModel*)model
      forController:(WebIntentPickerSheetController*)controller {
  const CGFloat kYMargin = 16.0;
  size_t count = model->GetSuggestedExtensionCount();
  if (count == 0)
    return nil;

  NSMutableArray* subviews = [NSMutableArray array];

  NSRect textFrame = NSMakeRect(0, 0,
                         kTextWidth, 1);
  suggestionLabel_.reset([[NSTextField alloc] initWithFrame:textFrame]);
  ConfigureTextFieldAsLabel(suggestionLabel_);

  CGFloat offset = kYMargin;
  for (size_t i = count; i > 0; --i) {
    const WebIntentPickerModel::SuggestedExtension& ext =
        model->GetSuggestedExtensionAt(i - 1);
    scoped_nsobject<NSView> suggestView(
        [[SingleSuggestionView alloc] initWithExtension:&ext
                                              withIndex:i-1
                                          forController:controller]);
    offset += [self addStackedView:suggestView.get()
                        toSubviews:subviews
                          atOffset:offset];
  }

  [self updateSuggestionLabelForModel:model];
  offset += [self addStackedView:suggestionLabel_
                      toSubviews:subviews
                      atOffset:offset];

  offset += kYMargin;

  NSRect contentFrame = NSMakeRect(kFramePadding, 0, kWindowWidth, offset);
  if(self =  [super initWithFrame:contentFrame])
    [self setSubviews: subviews];

  controller_ = controller;
  return self;
}

- (void)updateSuggestionLabelForModel:(WebIntentPickerModel*)model {
  DCHECK(suggestionLabel_.get());
  string16 labelText = model->GetSuggestionsLinkText();

  if (labelText.empty()) {
    [suggestionLabel_ setHidden:TRUE];
  } else {
    NSRect textFrame = [suggestionLabel_ frame];

    [suggestionLabel_ setHidden:FALSE];
    [suggestionLabel_ setStringValue:base::SysUTF16ToNSString(labelText)];
     textFrame.size.height +=
         [GTMUILocalizerAndLayoutTweaker sizeToFitFixedWidthTextField:
               suggestionLabel_];
     [suggestionLabel_ setFrame: textFrame];
  }
}

- (void)startThrobberForRow:(NSInteger)index {
  for (SingleSuggestionView* row in [self subviews]) {
    if ([row isMemberOfClass:[SingleSuggestionView class]]) {
      [row setEnabled:NO];
      if ([row tag] == index) {
        [row startThrobber];
      }
    }
  }
}

- (void)stopThrobber {
  for (SingleSuggestionView* row in [self subviews]) {
    if ([row isMemberOfClass:[SingleSuggestionView class]]) {
      [row stopThrobber];
      [row setEnabled:YES];
    }
  }
}

- (IBAction)installExtension:(id)sender {
  [controller_ installExtension:sender];
}


- (CGFloat)addStackedView:(NSView*)view
               toSubviews:(NSMutableArray*)subviews
                 atOffset:(CGFloat)offset {
  if (view == nil)
    return 0.0;

  NSPoint frameOrigin = [view frame].origin;
  frameOrigin.y = offset;
  [view setFrameOrigin:frameOrigin];
  [subviews addObject:view];

  return NSHeight([view frame]);
}
@end

@implementation WebIntentPickerSheetController;

- (id)initWithPicker:(WebIntentPickerCocoa*)picker {
  // Use an arbitrary height because it will reflect the size of the content.
  NSRect contentRect = NSMakeRect(0, 0, kWindowWidth, kVerticalSpacing);

  // |window| is retained by the ConstrainedWindowMacDelegateCustomSheet when
  // the sheet is initialized.
  scoped_nsobject<NSWindow> window(
      [[NSWindow alloc] initWithContentRect:contentRect
                                  styleMask:NSTitledWindowMask
                                    backing:NSBackingStoreBuffered
                                      defer:YES]);
  if ((self = [super initWithWindow:window.get()])) {
    picker_ = picker;
    if (picker)
      model_ = picker->model();
    intentButtons_.reset([[NSMutableArray alloc] init]);

    inlineDispositionTitleField_.reset([[NSTextField alloc] init]);
    ConfigureTextFieldAsLabel(inlineDispositionTitleField_);

    flipView_.reset([[WebIntentsContentView alloc] init]);
    [flipView_ setAutoresizingMask:NSViewMinYMargin];
    [[[self window] contentView] setSubviews:
        [NSArray arrayWithObject:flipView_]];

    [self performLayoutWithModel:model_];
    [[self window] makeFirstResponder:self];
  }
  return self;
}

// Handle default OSX dialog cancel mechanisms. (Cmd-.)
- (void)cancelOperation:(id)sender {
  if (picker_)
    picker_->OnCancelled();
  [self closeSheet];
}

- (void)chooseAnotherService:(id)sender {
  if (picker_)
    picker_->OnChooseAnotherService();
}

// Handle keyDown events, specifically ESC.
- (void)keyDown:(NSEvent*)event {
  // Check for escape key.
  if ([[event charactersIgnoringModifiers] isEqualToString:@"\e"])
    [self cancelOperation:self];
  else
    [super keyDown:event];
}

- (void)sheetDidEnd:(NSWindow*)sheet
         returnCode:(int)returnCode
        contextInfo:(void*)contextInfo {
  if (picker_)
    picker_->OnSheetDidEnd(sheet);
}

- (void)setInlineDispositionTabContents:(TabContents*)tabContents {
  contents_ = tabContents;
}

- (void)setInlineDispositionFrameSize:(NSSize)inlineContentSize {
  DCHECK(contents_);

  NSView* webContentView = contents_->web_contents()->GetNativeView();

  // Compute container size to fit all elements, including padding.
  NSSize containerSize = inlineContentSize;
  containerSize.height += [webContentView frame].origin.y + kFramePadding;
  containerSize.width += 2 * kFramePadding;

  // Ensure minimum container width.
  containerSize.width = std::max(kWindowWidth, containerSize.width);

  // Resize web contents.
  [webContentView setFrameSize:inlineContentSize];

  // Position close button.
  NSRect buttonFrame = [closeButton_ frame];
  buttonFrame.origin.x = containerSize.width - kFramePadding - kCloseButtonSize;
  [closeButton_ setFrame:buttonFrame];

  [self setContainerSize:containerSize];
}

- (void)setContainerSize:(NSSize)containerSize {
  // Resize container views
  NSRect frame = NSMakeRect(0, 0, 0, 0);
  frame.size = containerSize;
  [[[self window] contentView] setFrame:frame];
  [flipView_ setFrame:frame];

  // Resize and reposition dialog window.
  frame.size = [[[self window] contentView] convertSize:containerSize
                                                 toView:nil];
  frame = [[self window] frameRectForContentRect:frame];

  // Readjust window position to keep top in place and center horizontally.
  NSRect windowFrame = [[self window] frame];
  windowFrame.origin.x -= (NSWidth(frame) - NSWidth(windowFrame)) / 2.0;
  windowFrame.origin.y -= (NSHeight(frame) - NSHeight(windowFrame));
  windowFrame.size = frame.size;
  [[self window] setFrame:windowFrame display:YES animate:NO];
}

// Pop up a new tab with the Chrome Web Store.
- (IBAction)showChromeWebStore:(id)sender {
  DCHECK(picker_);
  picker_->OnSuggestionsLinkClicked();
}

// A picker button has been pressed - invoke corresponding service.
- (IBAction)invokeService:(id)sender {
  DCHECK(picker_);
  picker_->OnServiceChosen([sender tag]);
}

- (IBAction)openExtensionLink:(id)sender {
  DCHECK(model_);
  DCHECK(picker_);
  const WebIntentPickerModel::SuggestedExtension& extension =
      model_->GetSuggestedExtensionAt([sender tag]);

  picker_->OnExtensionLinkClicked(UTF16ToUTF8(extension.id));
}

- (IBAction)installExtension:(id)sender {
  DCHECK(model_);
  DCHECK(picker_);
  const WebIntentPickerModel::SuggestedExtension& extension =
      model_->GetSuggestedExtensionAt([sender tag]);
  if (picker_) {
    [suggestionView_ startThrobberForRow:[sender tag]];
    [closeButton_ setEnabled:NO];
    [self setIntentButtonsEnabled:NO];
    picker_->OnExtensionInstallRequested(UTF16ToUTF8(extension.id));
  }
}

- (void)setIntentButtonsEnabled:(BOOL)enabled {
  for (NSButton* button in intentButtons_.get()) {
    [button setEnabled:enabled];
  }
}

- (CGFloat)addStackedView:(NSView*)view
               toSubviews:(NSMutableArray*)subviews
                 atOffset:(CGFloat)offset {
  if (view == nil)
    return 0.0;

  NSPoint frameOrigin = [view frame].origin;
  frameOrigin.y = offset;
  [view setFrameOrigin:frameOrigin];
  [subviews addObject:view];

  return NSHeight([view frame]);
}

// Adds a link to the Chrome Web Store, to obtain further intent handlers.
// Returns the y position delta for the next offset.
- (CGFloat)addCwsButtonToSubviews:(NSMutableArray*)subviews
                         atOffset:(CGFloat)offset {
  NSRect frame = NSMakeRect(kFramePadding, offset, 100, 10);
  NSString* string =
      l10n_util::GetNSStringWithFixup(IDS_FIND_MORE_INTENT_HANDLER_MESSAGE);
  scoped_nsobject<NSButton> button(CreateHyperlinkButton(string,frame));
  [button setTarget:self];
  [button setAction:@selector(showChromeWebStore:)];
  [subviews addObject:button.get()];

  // Call size-to-fit to fixup for the localized string.
  [GTMUILocalizerAndLayoutTweaker sizeToFitView:button.get()];

  return NSHeight([button frame]);
}

- (void)addCloseButtonToSubviews:(NSMutableArray*)subviews  {
  if (!closeButton_.get()) {
    NSRect buttonFrame = NSMakeRect(
        kFramePadding + kTextWidth, kFramePadding,
        kCloseButtonSize, kCloseButtonSize);
    closeButton_.reset(
        [[HoverCloseButton alloc] initWithFrame:buttonFrame]);
    [closeButton_ setTarget:self];
    [closeButton_ setAction:@selector(cancelOperation:)];
  }
  [subviews addObject:closeButton_];
}

// Adds a header (icon and explanatory text) to picker bubble.
// Returns the y position delta for the next offset.
- (CGFloat)addHeaderToSubviews:(NSMutableArray*)subviews
                      atOffset:(CGFloat)offset {
  // Create a new text field if we don't have one yet.
  // TODO(groby): This should not be necessary since the controller sends this
  // string.
  if (!actionTextField_.get()) {
    NSString* nsString =
        l10n_util::GetNSStringWithFixup(IDS_CHOOSE_INTENT_HANDLER_MESSAGE);
    [self setActionString:nsString];
  }

  NSRect textFrame = [actionTextField_ frame];
  textFrame.origin.y = offset;

  [actionTextField_ setFrame:textFrame];
  [subviews addObject:actionTextField_];

  return NSHeight([actionTextField_ frame]);
}

- (CGFloat)addInlineHtmlToSubviews:(NSMutableArray*)subviews
                          atOffset:(CGFloat)offset {
  if (!contents_)
    return 0;

  // Determine a good size for the inline disposition window.
  gfx::Size size = WebIntentPicker::GetMinInlineDispositionSize();
  NSRect frame = NSMakeRect(kFramePadding, offset, size.width(), size.height());

  [contents_->web_contents()->GetNativeView() setFrame:frame];
  [subviews addObject:contents_->web_contents()->GetNativeView()];

  return NSHeight(frame);
}

- (CGFloat)addAnotherServiceLinkToSubviews:(NSMutableArray*)subviews
                                  atOffset:(CGFloat)offset {

  NSRect textFrame = NSMakeRect(kFramePadding, offset, kTextWidth, 1);
  [inlineDispositionTitleField_ setFrame:textFrame];
  [subviews addObject:inlineDispositionTitleField_];
  [GTMUILocalizerAndLayoutTweaker sizeToFitView:inlineDispositionTitleField_];
  textFrame = [inlineDispositionTitleField_ frame];

  // Add link for "choose another service" if other suggestions are available
  // or if more than one (the current) service is installed.
  if (model_->GetInstalledServiceCount() > 1 ||
    model_->GetSuggestedExtensionCount()) {
    NSRect frame = NSMakeRect(
        NSMaxX(textFrame) + kFramePadding, offset,
        kTitleLinkMaxWidth, 1);
    NSString* string = l10n_util::GetNSStringWithFixup(
        IDS_INTENT_PICKER_USE_ALTERNATE_SERVICE);
    scoped_nsobject<NSButton> button(CreateHyperlinkButton(string, frame));
    [[button cell] setControlSize:NSRegularControlSize];
    [button setTarget:self];
    [button setAction:@selector(chooseAnotherService:)];
    [subviews addObject:button];

    // Call size-to-fit to fixup for the localized string.
    [GTMUILocalizerAndLayoutTweaker sizeToFitView:button];

    // And finally, make sure the link and the title are horizontally centered.
    frame = [button frame];
    CGFloat height = std::max(NSHeight(textFrame), NSHeight(frame));
    frame.origin.y += (height - NSHeight(frame)) / 2.0;
    frame.size.height = height;
    textFrame.origin.y += (height - NSHeight(textFrame)) / 2.0;
    textFrame.size.height = height;
    [button setFrame:frame];
    [inlineDispositionTitleField_ setFrame:textFrame];
  }

  return NSHeight(textFrame);
}

// Add a single button for a specific service
- (CGFloat)addServiceButton:(NSString*)title
                 withImage:(NSImage*)image
                     index:(NSUInteger)index
                toSubviews:(NSMutableArray*)subviews
                  atOffset:(CGFloat)offset {
  // Buttons are displayed centered.
  CGFloat offsetX = (kWindowWidth - kServiceButtonWidth) / 2.0;
  NSRect frame = NSMakeRect(offsetX, offset, kServiceButtonWidth, 45);
  scoped_nsobject<NSButton> button([[NSButton alloc] initWithFrame:frame]);

  if (image) {
    [button setImage:image];
    [button setImagePosition:NSImageLeft];
  }
  [button setAlignment:NSLeftTextAlignment];
  [button setButtonType:NSMomentaryPushInButton];
  [button setBezelStyle:NSRegularSquareBezelStyle];
  [button setTarget:self];
  [button setTitle:title];
  [button setTag:index];
  [button setAction:@selector(invokeService:)];
  [subviews addObject:button];
  [intentButtons_ addObject:button];

  // Call size-to-fit to fixup size.
  [GTMUILocalizerAndLayoutTweaker sizeToFitView:button.get()];

  // But make sure we're limited to a fixed size.
  frame = [button frame];
  frame.size.width = kServiceButtonWidth;
  [button setFrame:frame];

  return NSHeight([button frame]);
}

- (NSView*)createEmptyView {
  NSMutableArray* subviews = [NSMutableArray array];

  NSRect titleFrame = NSMakeRect(kFramePadding, kFramePadding,
                                 kTextWidth, 1);
  scoped_nsobject<NSTextField> title(
      [[NSTextField alloc] initWithFrame:titleFrame]);
  ConfigureTextFieldAsLabel(title);
  [title setFont:[NSFont systemFontOfSize:kHeaderFontSize]];
  [title setStringValue:
      l10n_util::GetNSStringWithFixup(IDS_INTENT_PICKER_NO_SERVICES_TITLE)];
  titleFrame.size.height +=
      [GTMUILocalizerAndLayoutTweaker sizeToFitFixedWidthTextField:title];

  NSRect bodyFrame = titleFrame;
  bodyFrame.origin.y += NSHeight(titleFrame) + kFramePadding;

  scoped_nsobject<NSTextField> body(
      [[NSTextField alloc] initWithFrame:bodyFrame]);
  ConfigureTextFieldAsLabel(body);
  [body setStringValue:
      l10n_util::GetNSStringWithFixup(IDS_INTENT_PICKER_NO_SERVICES)];
  bodyFrame.size.height +=
      [GTMUILocalizerAndLayoutTweaker sizeToFitFixedWidthTextField:body];
  NSRect viewFrame = NSMakeRect(
      0,
      kFramePadding,
      std::max(NSWidth(bodyFrame), NSWidth(titleFrame)) + 2 * kFramePadding,
      NSHeight(titleFrame) + NSHeight(bodyFrame) + kVerticalSpacing);

  titleFrame.origin.y = NSHeight(viewFrame) - NSHeight(titleFrame);
  bodyFrame.origin.y = 0;

  [title setFrame:titleFrame];
  [body setFrame: bodyFrame];

  [subviews addObject:title];
  [subviews addObject:body];

  NSView* view = [[NSView alloc] initWithFrame:viewFrame];
  [view setAutoresizingMask:NSViewMinYMargin ];
  [view setSubviews:subviews];

  return view;
}

- (void)performLayoutWithModel:(WebIntentPickerModel*)model {
  model_ = model;

  // |offset| is the Y position that should be drawn at next.
  CGFloat offset = kFramePadding;

  // Keep the new subviews in an array that gets replaced at the end.
  NSMutableArray* subviews = [NSMutableArray array];

  if (isEmpty_) {
    scoped_nsobject<NSView> emptyView([self createEmptyView]);
    [subviews addObject:emptyView];
    offset += NSHeight([emptyView frame]);
  } else if (contents_) {
    offset += [self addAnotherServiceLinkToSubviews:subviews
                                           atOffset:offset];
    offset += kFramePadding;
    offset += [self addInlineHtmlToSubviews:subviews atOffset:offset];
  } else {
    offset += [self addHeaderToSubviews:subviews atOffset:offset];

    offset += kVerticalSpacing;

    if (model) {
      [intentButtons_ removeAllObjects];

      for (NSUInteger i = 0; i < model->GetInstalledServiceCount(); ++i) {
        const WebIntentPickerModel::InstalledService& service =
            model->GetInstalledServiceAt(i);
        offset += [self addServiceButton:base::SysUTF16ToNSString(service.title)
                               withImage:service.favicon.ToNSImage()
                                   index:i
                              toSubviews:subviews
                                atOffset:offset];
      }

      // Leave room for defaults section. TODO(groby): Add defaults.
      offset += kVerticalSpacing * 3;

      suggestionView_.reset(
          [[SuggestionView alloc] initWithModel:model forController:self]);
      offset += [self addStackedView:suggestionView_
                          toSubviews:subviews
                            atOffset:offset];
    }
    offset += [self addCwsButtonToSubviews:subviews atOffset:offset];
  }
  [self addCloseButtonToSubviews:subviews];

  // Add the bottom padding.
  offset += kVerticalSpacing;

  // Replace the window's content.
  [flipView_ setSubviews:subviews];

  // And resize to fit.
  [self setContainerSize:NSMakeSize(kWindowWidth, offset)];
}

- (void)setActionString:(NSString*)actionString {
  NSRect textFrame;
  if (!actionTextField_.get()) {
    textFrame = NSMakeRect(kFramePadding, 0,
                           kTextWidth, 1);

    actionTextField_.reset([[NSTextField alloc] initWithFrame:textFrame]);
    ConfigureTextFieldAsLabel(actionTextField_);
    [actionTextField_ setFont:[NSFont systemFontOfSize:kHeaderFontSize]];
  } else {
    textFrame = [actionTextField_ frame];
  }

  [actionTextField_ setStringValue:actionString];
  textFrame.size.height +=
      [GTMUILocalizerAndLayoutTweaker sizeToFitFixedWidthTextField:
            actionTextField_];
  [actionTextField_ setFrame: textFrame];
}

- (void)setInlineDispositionTitle:(NSString*)title {
  NSFont* nsfont = [inlineDispositionTitleField_ font];
  gfx::Font font(
      base::SysNSStringToUTF8([nsfont fontName]), [nsfont pointSize]);
  NSString* elidedTitle = base::SysUTF16ToNSString(ui::ElideText(
        base::SysNSStringToUTF16(title),
        font, kTitleLinkMaxWidth, ui::ELIDE_AT_END));
  [inlineDispositionTitleField_ setStringValue:elidedTitle];
}

- (void)stopThrobber {
  [closeButton_ setEnabled:YES];
  [self setIntentButtonsEnabled:YES];
  [suggestionView_ stopThrobber];
}

- (void)closeSheet {
  [NSApp endSheet:[self window]];
}

- (void)pendingAsyncCompleted {
  // Requests to both the WebIntentService and the Chrome Web Store have
  // completed. If there are any services, installed or suggested, there's
  // nothing to do.
  DCHECK(model_);
  isEmpty_ = !model_->GetInstalledServiceCount() &&
      !model_->GetSuggestedExtensionCount();
  [self performLayoutWithModel:model_];
}

@end  // WebIntentPickerSheetController
