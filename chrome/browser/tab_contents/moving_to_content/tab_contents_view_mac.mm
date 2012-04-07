// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Carbon/Carbon.h>

#include "chrome/browser/tab_contents/moving_to_content/tab_contents_view_mac.h"

#include <string>

#import "base/mac/scoped_sending_event.h"
#import "base/message_pump_mac.h"
#import "chrome/browser/renderer_host/chrome_render_widget_host_view_mac_delegate.h"
#include "chrome/browser/tab_contents/render_view_context_menu_mac.h"
#include "chrome/browser/tab_contents/web_drag_bookmark_handler_mac.h"
#import "chrome/browser/ui/cocoa/focus_tracker.h"
#import "chrome/browser/ui/cocoa/view_id_util.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/render_view_host_factory.h"
#include "content/browser/renderer_host/render_widget_host.h"
#include "content/browser/renderer_host/render_widget_host_view_mac.h"
#include "content/browser/tab_contents/popup_menu_helper_mac.h"
#include "content/browser/tab_contents/tab_contents.h"
#import "content/browser/tab_contents/web_drag_dest_mac.h"
#import "content/browser/tab_contents/web_drag_source_mac.h"
#include "content/common/view_messages.h"
#include "content/public/browser/web_contents_delegate.h"
#include "skia/ext/skia_utils_mac.h"
#import "third_party/mozilla/NSPasteboard+Utils.h"

using WebKit::WebDragOperation;
using WebKit::WebDragOperationsMask;
using content::WebContents;

// Ensure that the WebKit::WebDragOperation enum values stay in sync with
// NSDragOperation constants, since the code below static_casts between 'em.
#define COMPILE_ASSERT_MATCHING_ENUM(name) \
  COMPILE_ASSERT(int(NS##name) == int(WebKit::Web##name), enum_mismatch_##name)
COMPILE_ASSERT_MATCHING_ENUM(DragOperationNone);
COMPILE_ASSERT_MATCHING_ENUM(DragOperationCopy);
COMPILE_ASSERT_MATCHING_ENUM(DragOperationLink);
COMPILE_ASSERT_MATCHING_ENUM(DragOperationGeneric);
COMPILE_ASSERT_MATCHING_ENUM(DragOperationPrivate);
COMPILE_ASSERT_MATCHING_ENUM(DragOperationMove);
COMPILE_ASSERT_MATCHING_ENUM(DragOperationDelete);
COMPILE_ASSERT_MATCHING_ENUM(DragOperationEvery);

@interface TabContentsViewCocoa (Private)
- (id)initWithTabContentsViewMac:(TabContentsViewMac*)w;
- (void)registerDragTypes;
- (void)setCurrentDragOperation:(NSDragOperation)operation;
- (void)startDragWithDropData:(const WebDropData&)dropData
            dragOperationMask:(NSDragOperation)operationMask
                        image:(NSImage*)image
                       offset:(NSPoint)offset;
- (void)cancelDeferredClose;
- (void)clearTabContentsView;
- (void)closeTabAfterEvent;
- (void)viewDidBecomeFirstResponder:(NSNotification*)notification;
// Notify the RenderWidgetHost that the frame was updated so it can resize
// its contents.
- (void)renderWidgetHostWasResized;
@end

namespace tab_contents_view_mac {
content::WebContentsView* CreateWebContentsView(WebContents* web_contents) {
  return new TabContentsViewMac(web_contents);
}
}

TabContentsViewMac::TabContentsViewMac(WebContents* web_contents)
    : tab_contents_(static_cast<TabContents*>(web_contents)),
      overlaid_view_(nil) {
}

TabContentsViewMac::~TabContentsViewMac() {
  // This handles the case where a renderer close call was deferred
  // while the user was operating a UI control which resulted in a
  // close.  In that case, the Cocoa view outlives the
  // TabContentsViewMac instance due to Cocoa retain count.
  [cocoa_view_ cancelDeferredClose];
  [cocoa_view_ clearTabContentsView];
}

void TabContentsViewMac::CreateView(const gfx::Size& initial_size) {
  TabContentsViewCocoa* view =
      [[TabContentsViewCocoa alloc] initWithTabContentsViewMac:this];
  cocoa_view_.reset(view);
}

RenderWidgetHostView* TabContentsViewMac::CreateViewForWidget(
    RenderWidgetHost* render_widget_host) {
  if (render_widget_host->view()) {
    // During testing, the view will already be set up in most cases to the
    // test view, so we don't want to clobber it with a real one. To verify that
    // this actually is happening (and somebody isn't accidentally creating the
    // view twice), we check for the RVH Factory, which will be set when we're
    // making special ones (which go along with the special views).
    DCHECK(RenderViewHostFactory::has_factory());
    return render_widget_host->view();
  }

  RenderWidgetHostViewMac* view =
      new RenderWidgetHostViewMac(render_widget_host);

  // TODO(avi): Find a better place to do this.
  ChromeRenderWidgetHostViewMacDelegate* rw_delegate =
      [[ChromeRenderWidgetHostViewMacDelegate alloc]
          initWithRenderWidgetHost:render_widget_host];
  view->SetDelegate((RenderWidgetHostViewMacDelegate*)rw_delegate);

  // Fancy layout comes later; for now just make it our size and resize it
  // with us. In case there are other siblings of the content area, we want
  // to make sure the content area is on the bottom so other things draw over
  // it.
  NSView* view_view = view->GetNativeView();
  [view_view setFrame:[cocoa_view_.get() bounds]];
  [view_view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  // Add the new view below all other views; this also keeps it below any
  // overlay view installed.
  [cocoa_view_.get() addSubview:view_view
                     positioned:NSWindowBelow
                     relativeTo:nil];
  // For some reason known only to Cocoa, the autorecalculation of the key view
  // loop set on the window doesn't set the next key view when the subview is
  // added. On 10.6 things magically work fine; on 10.5 they fail
  // <http://crbug.com/61493>. Digging into Cocoa key view loop code yielded
  // madness; TODO(avi,rohit): look at this again and figure out what's really
  // going on.
  [cocoa_view_.get() setNextKeyView:view_view];
  return view;
}

gfx::NativeView TabContentsViewMac::GetNativeView() const {
  return cocoa_view_.get();
}

gfx::NativeView TabContentsViewMac::GetContentNativeView() const {
  RenderWidgetHostView* rwhv = tab_contents_->GetRenderWidgetHostView();
  if (!rwhv)
    return NULL;
  return rwhv->GetNativeView();
}

gfx::NativeWindow TabContentsViewMac::GetTopLevelNativeWindow() const {
  return [cocoa_view_.get() window];
}

void TabContentsViewMac::GetContainerBounds(gfx::Rect* out) const {
  // Convert bounds to window coordinate space.
  NSRect bounds =
      [cocoa_view_.get() convertRect:[cocoa_view_.get() bounds] toView:nil];

  // Convert bounds to screen coordinate space.
  NSWindow* window = [cocoa_view_.get() window];
  bounds.origin = [window convertBaseToScreen:bounds.origin];

  // Flip y to account for screen flip.
  NSScreen* screen = [[NSScreen screens] objectAtIndex:0];
  bounds.origin.y = [screen frame].size.height - bounds.origin.y
      - bounds.size.height;
  *out = gfx::Rect(NSRectToCGRect(bounds));
}

void TabContentsViewMac::StartDragging(
    const WebDropData& drop_data,
    WebDragOperationsMask allowed_operations,
    const SkBitmap& image,
    const gfx::Point& image_offset) {
  // By allowing nested tasks, the code below also allows Close(),
  // which would deallocate |this|.  The same problem can occur while
  // processing -sendEvent:, so Close() is deferred in that case.
  // Drags from web content do not come via -sendEvent:, this sets the
  // same flag -sendEvent: would.
  base::mac::ScopedSendingEvent sending_event_scoper;

  // The drag invokes a nested event loop, arrange to continue
  // processing events.
  MessageLoop::ScopedNestableTaskAllower allow(MessageLoop::current());
  NSDragOperation mask = static_cast<NSDragOperation>(allowed_operations);
  NSPoint offset = NSPointFromCGPoint(image_offset.ToCGPoint());
  [cocoa_view_ startDragWithDropData:drop_data
                   dragOperationMask:mask
                               image:gfx::SkBitmapToNSImage(image)
                              offset:offset];
}

void TabContentsViewMac::RenderViewCreated(RenderViewHost* host) {
  // We want updates whenever the intrinsic width of the webpage changes.
  // Put the RenderView into that mode. The preferred width is used for example
  // when the "zoom" button in the browser window is clicked.
  host->EnablePreferredSizeMode();
}

void TabContentsViewMac::SetPageTitle(const string16& title) {
  // Meaningless on the Mac; widgets don't have a "title" attribute
}

void TabContentsViewMac::OnTabCrashed(base::TerminationStatus /* status */,
                                      int /* error_code */) {
}

void TabContentsViewMac::SizeContents(const gfx::Size& size) {
  // TODO(brettw | japhet) This is a hack and should be removed.
  // See web_contents_view.h.
  gfx::Rect rect(gfx::Point(), size);
  TabContentsViewCocoa* view = cocoa_view_.get();
  [view setFrame:[view flipRectToNSRect:rect]];
}

void TabContentsViewMac::Focus() {
  [[cocoa_view_.get() window] makeFirstResponder:GetContentNativeView()];
  [[cocoa_view_.get() window] makeKeyAndOrderFront:GetContentNativeView()];
}

void TabContentsViewMac::SetInitialFocus() {
  if (tab_contents_->FocusLocationBarByDefault())
    tab_contents_->SetFocusToLocationBar(false);
  else
    [[cocoa_view_.get() window] makeFirstResponder:GetContentNativeView()];
}

void TabContentsViewMac::StoreFocus() {
  // We're explicitly being asked to store focus, so don't worry if there's
  // already a view saved.
  focus_tracker_.reset(
      [[FocusTracker alloc] initWithWindow:[cocoa_view_ window]]);
}

void TabContentsViewMac::RestoreFocus() {
  // TODO(avi): Could we be restoring a view that's no longer in the key view
  // chain?
  if (!(focus_tracker_.get() &&
        [focus_tracker_ restoreFocusInWindow:[cocoa_view_ window]])) {
    // Fall back to the default focus behavior if we could not restore focus.
    // TODO(shess): If location-bar gets focus by default, this will
    // select-all in the field.  If there was a specific selection in
    // the field when we navigated away from it, we should restore
    // that selection.
    SetInitialFocus();
  }

  focus_tracker_.reset(nil);
}

bool TabContentsViewMac::IsDoingDrag() const {
  return false;
}

void TabContentsViewMac::CancelDragAndCloseTab() {
}

void TabContentsViewMac::UpdateDragCursor(WebDragOperation operation) {
  [cocoa_view_ setCurrentDragOperation: operation];
}

void TabContentsViewMac::GotFocus() {
  // This is only used in the views FocusManager stuff but it bleeds through
  // all subclasses. http://crbug.com/21875
}

// This is called when the renderer asks us to take focus back (i.e., it has
// iterated past the last focusable element on the page).
void TabContentsViewMac::TakeFocus(bool reverse) {
  if (reverse) {
    [[cocoa_view_ window] selectPreviousKeyView:cocoa_view_.get()];
  } else {
    [[cocoa_view_ window] selectNextKeyView:cocoa_view_.get()];
  }
}

void TabContentsViewMac::CreateNewWindow(
    int route_id,
    const ViewHostMsg_CreateWindow_Params& params) {
  tab_contents_view_helper_.CreateNewWindow(tab_contents_, route_id, params);
}

void TabContentsViewMac::CreateNewWidget(
    int route_id, WebKit::WebPopupType popup_type) {
  RenderWidgetHostView* widget_view =
      tab_contents_view_helper_.CreateNewWidget(tab_contents_,
                                                route_id,
                                                false,
                                                popup_type);

  // A RenderWidgetHostViewMac has lifetime scoped to the view. We'll retain it
  // to allow it to survive the trip without being hosted.
  RenderWidgetHostViewMac* widget_view_mac =
      static_cast<RenderWidgetHostViewMac*>(widget_view);
  [widget_view_mac->GetNativeView() retain];
}

void TabContentsViewMac::CreateNewFullscreenWidget(int route_id) {
  RenderWidgetHostView* widget_view =
      tab_contents_view_helper_.CreateNewWidget(tab_contents_,
                                                route_id,
                                                true,
                                                WebKit::WebPopupTypeNone);

  // A RenderWidgetHostViewMac has lifetime scoped to the view. We'll retain it
  // to allow it to survive the trip without being hosted.
  RenderWidgetHostViewMac* widget_view_mac =
      static_cast<RenderWidgetHostViewMac*>(widget_view);
  [widget_view_mac->GetNativeView() retain];
}

void TabContentsViewMac::ShowCreatedWindow(int route_id,
                                           WindowOpenDisposition disposition,
                                           const gfx::Rect& initial_pos,
                                           bool user_gesture) {
  tab_contents_view_helper_.ShowCreatedWindow(
      tab_contents_, route_id, disposition, initial_pos, user_gesture);
}

void TabContentsViewMac::ShowCreatedWidget(
    int route_id, const gfx::Rect& initial_pos) {
  RenderWidgetHostView* widget_host_view =
      tab_contents_view_helper_.ShowCreatedWidget(tab_contents_,
                                                  route_id,
                                                  false,
                                                  initial_pos);

  // A RenderWidgetHostViewMac has lifetime scoped to the view. Now that it's
  // properly embedded (or purposefully ignored) we can release the retain we
  // took in CreateNewWidgetInternal().
  RenderWidgetHostViewMac* widget_view_mac =
      static_cast<RenderWidgetHostViewMac*>(widget_host_view);
  [widget_view_mac->GetNativeView() release];
}

void TabContentsViewMac::ShowCreatedFullscreenWidget(int route_id) {
  RenderWidgetHostView* widget_host_view =
      tab_contents_view_helper_.ShowCreatedWidget(tab_contents_,
                                                  route_id,
                                                  true,
                                                  gfx::Rect());

  // A RenderWidgetHostViewMac has lifetime scoped to the view. Now that it's
  // properly embedded (or purposely ignored) we can release the retain we took
  // in CreateNewFullscreenWidgetInternal().
  RenderWidgetHostViewMac* widget_view_mac =
      static_cast<RenderWidgetHostViewMac*>(widget_host_view);
  [widget_view_mac->GetNativeView() release];
}

void TabContentsViewMac::ShowContextMenu(const ContextMenuParams& params) {
  // The renderer may send the "show context menu" message multiple times, one
  // for each right click mouse event it receives. Normally, this doesn't happen
  // because mouse events are not forwarded once the context menu is showing.
  // However, there's a race - the context menu may not yet be showing when
  // the second mouse event arrives. In this case, |ShowContextMenu()| will
  // get called multiple times - if so, don't create another context menu.
  // TODO(asvitkine): Fix the renderer so that it doesn't do this.
  RenderWidgetHostView* widget_view = tab_contents_->GetRenderWidgetHostView();
  if (widget_view) {
    RenderWidgetHostViewMac* widget_view_mac =
        static_cast<RenderWidgetHostViewMac*>(widget_view);
    if (widget_view_mac->is_showing_context_menu())
      return;
  }

  // Allow delegates to handle the context menu operation first.
  if (tab_contents_->GetDelegate() &&
      tab_contents_->GetDelegate()->HandleContextMenu(params)) {
    return;
  }

  context_menu_.reset(new RenderViewContextMenuMac(tab_contents(),
                                                   params,
                                                   GetContentNativeView()));
  context_menu_->Init();
}

// Display a popup menu for WebKit using Cocoa widgets.
void TabContentsViewMac::ShowPopupMenu(
    const gfx::Rect& bounds,
    int item_height,
    double item_font_size,
    int selected_item,
    const std::vector<WebMenuItem>& items,
    bool right_aligned) {
  PopupMenuHelper popup_menu_helper(tab_contents_->GetRenderViewHost());
  popup_menu_helper.ShowPopupMenu(bounds, item_height, item_font_size,
                                  selected_item, items, right_aligned);
}

bool TabContentsViewMac::IsEventTracking() const {
  return base::MessagePumpMac::IsHandlingSendEvent();
}

// Arrange to call CloseTab() after we're back to the main event loop.
// The obvious way to do this would be PostNonNestableTask(), but that
// will fire when the event-tracking loop polls for events.  So we
// need to bounce the message via Cocoa, instead.
void TabContentsViewMac::CloseTabAfterEventTracking() {
  [cocoa_view_ cancelDeferredClose];
  [cocoa_view_ performSelector:@selector(closeTabAfterEvent)
                    withObject:nil
                    afterDelay:0.0];
}

void TabContentsViewMac::GetViewBounds(gfx::Rect* out) const {
  // This method is not currently used on mac.
  NOTIMPLEMENTED();
}

void TabContentsViewMac::InstallOverlayView(gfx::NativeView view) {
  DCHECK(!overlaid_view_);
  overlaid_view_ = view;
  [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
  [cocoa_view_.get() addSubview:view];
  [view setFrame:[cocoa_view_.get() bounds]];
}

void TabContentsViewMac::RemoveOverlayView() {
  DCHECK(overlaid_view_);
  [overlaid_view_ removeFromSuperview];
  overlaid_view_ = nil;
}

void TabContentsViewMac::CloseTab() {
  tab_contents_->Close(tab_contents_->GetRenderViewHost());
}

@implementation TabContentsViewCocoa

- (id)initWithTabContentsViewMac:(TabContentsViewMac*)w {
  self = [super initWithFrame:NSZeroRect];
  if (self != nil) {
    tabContentsView_ = w;
    dragDest_.reset(
        [[WebDragDest alloc] initWithTabContents:[self tabContents]]);
    bookmarkHandler_.reset(new WebDragBookmarkHandlerMac);
    [dragDest_ setDragDelegate:
        static_cast<content::WebDragDestDelegate*>(bookmarkHandler_.get())];
    [self registerDragTypes];
    // TabContentsViewCocoa's ViewID may be changed to VIEW_ID_DEV_TOOLS_DOCKED
    // by TabContentsController, so we can't just override -viewID method to
    // return it.
    view_id_util::SetID(self, VIEW_ID_TAB_CONTAINER);

    [[NSNotificationCenter defaultCenter]
         addObserver:self
            selector:@selector(viewDidBecomeFirstResponder:)
                name:kViewDidBecomeFirstResponder
              object:nil];
  }
  return self;
}

- (void)dealloc {
  view_id_util::UnsetID(self);

  // Cancel any deferred tab closes, just in case.
  [self cancelDeferredClose];

  // This probably isn't strictly necessary, but can't hurt.
  [self unregisterDraggedTypes];

  [[NSNotificationCenter defaultCenter] removeObserver:self];

  [super dealloc];
}

// Registers for the view for the appropriate drag types.
- (void)registerDragTypes {
  NSArray* types = [NSArray arrayWithObjects:NSStringPboardType,
      NSHTMLPboardType, NSURLPboardType, nil];
  [self registerForDraggedTypes:types];
}

- (void)setCurrentDragOperation:(NSDragOperation)operation {
  [dragDest_ setCurrentOperation:operation];
}

- (TabContents*)tabContents {
  if (tabContentsView_ == NULL)
    return NULL;
  return tabContentsView_->tab_contents();
}

- (void)mouseEvent:(NSEvent *)theEvent {
  TabContents* tabContents = [self tabContents];
  if (tabContents && tabContents->GetDelegate()) {
    NSPoint location = [NSEvent mouseLocation];
    if ([theEvent type] == NSMouseMoved)
      tabContents->GetDelegate()->ContentsMouseEvent(
          tabContents, gfx::Point(location.x, location.y), true);
    if ([theEvent type] == NSMouseExited)
      tabContents->GetDelegate()->ContentsMouseEvent(
          tabContents, gfx::Point(location.x, location.y), false);
  }
}

- (BOOL)mouseDownCanMoveWindow {
  // This is needed to prevent mouseDowns from moving the window
  // around.  The default implementation returns YES only for opaque
  // views.  TabContentsViewCocoa does not draw itself in any way, but
  // its subviews do paint their entire frames.  Returning NO here
  // saves us the effort of overriding this method in every possible
  // subview.
  return NO;
}

- (void)pasteboard:(NSPasteboard*)sender provideDataForType:(NSString*)type {
  [dragSource_ lazyWriteToPasteboard:sender
                             forType:type];
}

- (void)startDragWithDropData:(const WebDropData&)dropData
            dragOperationMask:(NSDragOperation)operationMask
                        image:(NSImage*)image
                       offset:(NSPoint)offset {
  dragSource_.reset([[WebDragSource alloc]
      initWithContents:[self tabContents]
                  view:self
              dropData:&dropData
                 image:image
                offset:offset
            pasteboard:[NSPasteboard pasteboardWithName:NSDragPboard]
     dragOperationMask:operationMask]);
  [dragSource_ startDrag];
}

- (void)setFrameWithDeferredUpdate:(NSRect)frameRect {
  [super setFrame:frameRect];
  [self performSelector:@selector(renderWidgetHostWasResized)
             withObject:nil
             afterDelay:0];
}

- (void)renderWidgetHostWasResized {
  TabContents* tabContents = [self tabContents];
  if (tabContents && tabContents->GetRenderViewHost())
    tabContents->GetRenderViewHost()->WasResized();
}

// NSDraggingSource methods

// Returns what kind of drag operations are available. This is a required
// method for NSDraggingSource.
- (NSDragOperation)draggingSourceOperationMaskForLocal:(BOOL)isLocal {
  if (dragSource_.get())
    return [dragSource_ draggingSourceOperationMaskForLocal:isLocal];
  // No web drag source - this is the case for dragging a file from the
  // downloads manager. Default to copy operation. Note: It is desirable to
  // allow the user to either move or copy, but this requires additional
  // plumbing to update the download item's path once its moved.
  return NSDragOperationCopy;
}

// Called when a drag initiated in our view ends.
- (void)draggedImage:(NSImage*)anImage
             endedAt:(NSPoint)screenPoint
           operation:(NSDragOperation)operation {
  [dragSource_ endDragAt:screenPoint operation:operation];

  // Might as well throw out this object now.
  dragSource_.reset();
}

// Called when a drag initiated in our view moves.
- (void)draggedImage:(NSImage*)draggedImage movedTo:(NSPoint)screenPoint {
  [dragSource_ moveDragTo:screenPoint];
}

// Called when we're informed where a file should be dropped.
- (NSArray*)namesOfPromisedFilesDroppedAtDestination:(NSURL*)dropDest {
  if (![dropDest isFileURL])
    return nil;

  NSString* file_name = [dragSource_ dragPromisedFileTo:[dropDest path]];
  if (!file_name)
    return nil;

  return [NSArray arrayWithObject:file_name];
}

// NSDraggingDestination methods

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
  return [dragDest_ draggingEntered:sender view:self];
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
  [dragDest_ draggingExited:sender];
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
  return [dragDest_ draggingUpdated:sender view:self];
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
  return [dragDest_ performDragOperation:sender view:self];
}

- (void)cancelDeferredClose {
  SEL aSel = @selector(closeTabAfterEvent);
  [NSObject cancelPreviousPerformRequestsWithTarget:self
                                           selector:aSel
                                             object:nil];
}

- (void)clearTabContentsView {
  tabContentsView_ = NULL;
}

- (void)closeTabAfterEvent {
  tabContentsView_->CloseTab();
}

- (void)viewDidBecomeFirstResponder:(NSNotification*)notification {
  NSView* view = [notification object];
  if (![[self subviews] containsObject:view])
    return;

  NSSelectionDirection direction =
      [[[notification userInfo] objectForKey:kSelectionDirection]
        unsignedIntegerValue];
  if (direction == NSDirectSelection)
    return;

  [self tabContents]->
      FocusThroughTabTraversal(direction == NSSelectingPrevious);
}

@end
