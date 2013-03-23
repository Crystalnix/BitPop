// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_widget_host_view_android.h"

#include <android/bitmap.h>

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/utf_string_conversions.h"
#include "cc/layer.h"
#include "cc/texture_layer.h"
#include "content/browser/android/content_view_core_impl.h"
#include "content/browser/gpu/gpu_surface_tracker.h"
#include "content/browser/renderer_host/compositor_impl_android.h"
#include "content/browser/renderer_host/image_transport_factory_android.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/smooth_scroll_gesture_android.h"
#include "content/browser/renderer_host/surface_texture_transport_client_android.h"
#include "content/common/android/device_info.h"
#include "content/common/gpu/client/gl_helper.h"
#include "content/common/gpu/gpu_messages.h"
#include "content/common/view_messages.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#include "third_party/WebKit/Source/Platform/chromium/public/Platform.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebExternalTextureLayer.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebSize.h"
#include "ui/gfx/android/java_bitmap.h"
#include "ui/gfx/size_conversions.h"
#include "webkit/compositor_bindings/web_compositor_support_impl.h"

namespace content {

namespace {

// TODO(pliard): http://crbug.com/142585. Remove this helper function and update
// the clients to deal directly with WebKit::WebTextDirection.
base::i18n::TextDirection ConvertTextDirection(WebKit::WebTextDirection dir) {
  switch (dir) {
    case WebKit::WebTextDirectionDefault: return base::i18n::UNKNOWN_DIRECTION;
    case WebKit::WebTextDirectionLeftToRight: return base::i18n::LEFT_TO_RIGHT;
    case WebKit::WebTextDirectionRightToLeft: return base::i18n::RIGHT_TO_LEFT;
  }
  NOTREACHED() << "Unsupported text direction " << dir;
  return base::i18n::UNKNOWN_DIRECTION;
}

}  // namespace

RenderWidgetHostViewAndroid::RenderWidgetHostViewAndroid(
    RenderWidgetHostImpl* widget_host,
    ContentViewCoreImpl* content_view_core)
    : host_(widget_host),
      is_layer_attached_(true),
      content_view_core_(NULL),
      ime_adapter_android_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
      cached_background_color_(SK_ColorWHITE),
      texture_id_in_layer_(0),
      current_buffer_id_(0) {
  if (CompositorImpl::UsesDirectGL()) {
    surface_texture_transport_.reset(new SurfaceTextureTransportClient());
    layer_ = surface_texture_transport_->Initialize();
  } else {
    texture_layer_ = cc::TextureLayer::create(0);
    layer_ = texture_layer_;
  }

  layer_->setContentsOpaque(true);
  layer_->setIsDrawable(true);

  host_->SetView(this);
  SetContentViewCore(content_view_core);
}

RenderWidgetHostViewAndroid::~RenderWidgetHostViewAndroid() {
  SetContentViewCore(NULL);
  if (texture_id_in_layer_) {
    ImageTransportFactoryAndroid::GetInstance()->DeleteTexture(
        texture_id_in_layer_);
  }
}

void RenderWidgetHostViewAndroid::InitAsChild(gfx::NativeView parent_view) {
  NOTIMPLEMENTED();
}

void RenderWidgetHostViewAndroid::InitAsPopup(
    RenderWidgetHostView* parent_host_view, const gfx::Rect& pos) {
  NOTIMPLEMENTED();
}

void RenderWidgetHostViewAndroid::InitAsFullscreen(
    RenderWidgetHostView* reference_host_view) {
  NOTIMPLEMENTED();
}

RenderWidgetHost*
RenderWidgetHostViewAndroid::GetRenderWidgetHost() const {
  return host_;
}

void RenderWidgetHostViewAndroid::WasShown() {
  if (!host_->is_hidden())
    return;

  host_->WasShown();
}

void RenderWidgetHostViewAndroid::WasHidden() {
  if (host_->is_hidden())
    return;

  // Inform the renderer that we are being hidden so it can reduce its resource
  // utilization.
  host_->WasHidden();
}

void RenderWidgetHostViewAndroid::SetSize(const gfx::Size& size) {
  if (surface_texture_transport_.get())
    surface_texture_transport_->SetSize(size);

  host_->WasResized();
}

void RenderWidgetHostViewAndroid::SetBounds(const gfx::Rect& rect) {
  if (rect.origin().x() || rect.origin().y()) {
    VLOG(0) << "SetBounds not implemented for (x,y)!=(0,0)";
  }
  SetSize(rect.size());
}

WebKit::WebGLId RenderWidgetHostViewAndroid::GetScaledContentTexture(
    float scale,
    gfx::Size* out_size) {
  gfx::Size size(gfx::ToCeiledSize(
      gfx::ScaleSize(texture_size_in_layer_, scale)));

  if (!CompositorImpl::IsInitialized() ||
      texture_id_in_layer_ == 0 ||
      texture_size_in_layer_.IsEmpty() ||
      size.IsEmpty()) {
    if (out_size)
        out_size->SetSize(0, 0);

    return 0;
  }

  if (out_size)
    *out_size = size;

  GLHelper* helper = ImageTransportFactoryAndroid::GetInstance()->GetGLHelper();
  return helper->CopyAndScaleTexture(texture_id_in_layer_,
                                     texture_size_in_layer_,
                                     size,
                                     true);
}

bool RenderWidgetHostViewAndroid::PopulateBitmapWithContents(jobject jbitmap) {
  if (!CompositorImpl::IsInitialized() ||
      texture_id_in_layer_ == 0 ||
      texture_size_in_layer_.IsEmpty())
    return false;

  gfx::JavaBitmap bitmap(jbitmap);

  // TODO(dtrainor): Eventually add support for multiple formats here.
  DCHECK(bitmap.format() == ANDROID_BITMAP_FORMAT_RGBA_8888);

  GLHelper* helper = ImageTransportFactoryAndroid::GetInstance()->GetGLHelper();

  WebKit::WebGLId texture = helper->CopyAndScaleTexture(texture_id_in_layer_,
                                                        texture_size_in_layer_,
                                                        bitmap.size(),
                                                        true);
  if (texture == 0)
    return false;

  helper->ReadbackTextureSync(texture,
                              gfx::Rect(bitmap.size()),
                              static_cast<unsigned char*> (bitmap.pixels()));

  WebKit::WebGraphicsContext3D* context =
      ImageTransportFactoryAndroid::GetInstance()->GetContext3D();
  context->deleteTexture(texture);

  return true;
}

bool RenderWidgetHostViewAndroid::HasValidFrame() const {
  return texture_id_in_layer_ != 0 &&
      content_view_core_ &&
      !texture_size_in_layer_.IsEmpty() &&
      texture_size_in_layer_ == content_view_core_->GetBounds().size();
}

gfx::NativeView RenderWidgetHostViewAndroid::GetNativeView() const {
  return content_view_core_;
}

gfx::NativeViewId RenderWidgetHostViewAndroid::GetNativeViewId() const {
  return reinterpret_cast<gfx::NativeViewId>(
      const_cast<RenderWidgetHostViewAndroid*>(this));
}

gfx::NativeViewAccessible
RenderWidgetHostViewAndroid::GetNativeViewAccessible() {
  NOTIMPLEMENTED();
  return NULL;
}

void RenderWidgetHostViewAndroid::MovePluginWindows(
    const gfx::Vector2d& scroll_offset,
    const std::vector<webkit::npapi::WebPluginGeometry>& moves) {
  // We don't have plugin windows on Android. Do nothing. Note: this is called
  // from RenderWidgetHost::OnMsgUpdateRect which is itself invoked while
  // processing the corresponding message from Renderer.
}

void RenderWidgetHostViewAndroid::Focus() {
  host_->Focus();
  host_->SetInputMethodActive(true);
}

void RenderWidgetHostViewAndroid::Blur() {
  host_->Send(new ViewMsg_ExecuteEditCommand(
      host_->GetRoutingID(), "Unselect", ""));
  host_->SetInputMethodActive(false);
  host_->Blur();
}

bool RenderWidgetHostViewAndroid::HasFocus() const {
  if (!content_view_core_)
    return false;  // ContentViewCore not created yet.

  return content_view_core_->HasFocus();
}

bool RenderWidgetHostViewAndroid::IsSurfaceAvailableForCopy() const {
  NOTIMPLEMENTED();
  return false;
}

void RenderWidgetHostViewAndroid::Show() {
  if (is_layer_attached_)
    return;

  is_layer_attached_ = true;
  if (content_view_core_)
    content_view_core_->AttachLayer(layer_);
}

void RenderWidgetHostViewAndroid::Hide() {
  if (!is_layer_attached_)
    return;

  is_layer_attached_ = false;
  if (content_view_core_)
    content_view_core_->RemoveLayer(layer_);
}

bool RenderWidgetHostViewAndroid::IsShowing() {
  // ContentViewCoreImpl represents the native side of the Java
  // ContentViewCore.  It being NULL means that it is not attached
  // to the View system yet, so we treat this RWHVA as hidden.
  return is_layer_attached_ && content_view_core_;
}

gfx::Rect RenderWidgetHostViewAndroid::GetViewBounds() const {
  if (!content_view_core_)
    return gfx::Rect();

  return content_view_core_->GetBounds();
}

void RenderWidgetHostViewAndroid::UpdateCursor(const WebCursor& cursor) {
  // There are no cursors on Android.
}

void RenderWidgetHostViewAndroid::SetIsLoading(bool is_loading) {
  // Do nothing. The UI notification is handled through ContentViewClient which
  // is TabContentsDelegate.
}

void RenderWidgetHostViewAndroid::TextInputStateChanged(
    const ViewHostMsg_TextInputState_Params& params) {
  if (!IsShowing())
    return;

  // TODO(miguelg): this currently dispatches messages for text inputs
  // and date/time value inputs. Split it into two adapters.
  content_view_core_->ImeUpdateAdapter(
      GetNativeImeAdapter(),
      static_cast<int>(params.type),
      params.value, params.selection_start, params.selection_end,
      params.composition_start, params.composition_end,
      params.show_ime_if_needed);
}

int RenderWidgetHostViewAndroid::GetNativeImeAdapter() {
  return reinterpret_cast<int>(&ime_adapter_android_);
}

void RenderWidgetHostViewAndroid::ImeCancelComposition() {
  ime_adapter_android_.CancelComposition();
}

void RenderWidgetHostViewAndroid::DidUpdateBackingStore(
    const gfx::Rect& scroll_rect,
    const gfx::Vector2d& scroll_delta,
    const std::vector<gfx::Rect>& copy_rects) {
  NOTIMPLEMENTED();
}

void RenderWidgetHostViewAndroid::RenderViewGone(
    base::TerminationStatus status, int error_code) {
  Destroy();
}

void RenderWidgetHostViewAndroid::Destroy() {
  if (content_view_core_) {
    content_view_core_->RemoveLayer(layer_);
    content_view_core_ = NULL;
  }

  // The RenderWidgetHost's destruction led here, so don't call it.
  host_ = NULL;

  delete this;
}

void RenderWidgetHostViewAndroid::SetTooltipText(
    const string16& tooltip_text) {
  // Tooltips don't makes sense on Android.
}

void RenderWidgetHostViewAndroid::SelectionChanged(const string16& text,
                                                   size_t offset,
                                                   const ui::Range& range) {
  RenderWidgetHostViewBase::SelectionChanged(text, offset, range);

  if (text.empty() || range.is_empty() || !content_view_core_)
    return;
  size_t pos = range.GetMin() - offset;
  size_t n = range.length();

  DCHECK(pos + n <= text.length()) << "The text can not fully cover range.";
  if (pos >= text.length()) {
    NOTREACHED() << "The text can not cover range.";
    return;
  }

  std::string utf8_selection = UTF16ToUTF8(text.substr(pos, n));

  content_view_core_->OnSelectionChanged(utf8_selection);
}

void RenderWidgetHostViewAndroid::SelectionBoundsChanged(
    const gfx::Rect& start_rect,
    WebKit::WebTextDirection start_direction,
    const gfx::Rect& end_rect,
    WebKit::WebTextDirection end_direction) {
  if (content_view_core_) {
    content_view_core_->OnSelectionBoundsChanged(
        start_rect,
        ConvertTextDirection(start_direction),
        end_rect,
        ConvertTextDirection(end_direction));
  }
}

BackingStore* RenderWidgetHostViewAndroid::AllocBackingStore(
    const gfx::Size& size) {
  NOTIMPLEMENTED();
  return NULL;
}

void RenderWidgetHostViewAndroid::SetBackground(const SkBitmap& background) {
  RenderWidgetHostViewBase::SetBackground(background);
  host_->Send(new ViewMsg_SetBackground(host_->GetRoutingID(), background));
}

void RenderWidgetHostViewAndroid::CopyFromCompositingSurface(
    const gfx::Rect& src_subrect,
    const gfx::Size& dst_size,
    const base::Callback<void(bool)>& callback,
    skia::PlatformBitmap* output) {
  NOTIMPLEMENTED();
  callback.Run(false);
}

void RenderWidgetHostViewAndroid::ShowDisambiguationPopup(
    const gfx::Rect& target_rect, const SkBitmap& zoomed_bitmap) {
  if (!content_view_core_)
    return;

  content_view_core_->ShowDisambiguationPopup(target_rect, zoomed_bitmap);
}

SmoothScrollGesture* RenderWidgetHostViewAndroid::CreateSmoothScrollGesture(
    bool scroll_down, int pixels_to_scroll, int mouse_event_x,
    int mouse_event_y) {
  return new SmoothScrollGestureAndroid(
      pixels_to_scroll,
      GetRenderWidgetHost(),
      content_view_core_->CreateSmoothScroller(
          scroll_down, mouse_event_x, mouse_event_y));
}

void RenderWidgetHostViewAndroid::OnAcceleratedCompositingStateChange() {
}

void RenderWidgetHostViewAndroid::AcceleratedSurfaceBuffersSwapped(
    const GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params& params,
    int gpu_host_id) {
  ImageTransportFactoryAndroid* factory =
      ImageTransportFactoryAndroid::GetInstance();

  // TODO(sievers): When running the impl thread in the browser we
  // need to delay the ACK until after commit and use more than a single
  // texture.
  DCHECK(!CompositorImpl::IsThreadingEnabled());

  uint64 previous_buffer = current_buffer_id_;
  if (previous_buffer && texture_id_in_layer_) {
    DCHECK(id_to_mailbox_.find(previous_buffer) != id_to_mailbox_.end());
    ImageTransportFactoryAndroid::GetInstance()->ReleaseTexture(
        texture_id_in_layer_,
        reinterpret_cast<const signed char*>(
            id_to_mailbox_[previous_buffer].c_str()));
  }

  current_buffer_id_ = params.surface_handle;
  if (!texture_id_in_layer_) {
    texture_id_in_layer_ = factory->CreateTexture();
    texture_layer_->setTextureId(texture_id_in_layer_);
  }

  DCHECK(id_to_mailbox_.find(current_buffer_id_) != id_to_mailbox_.end());
  ImageTransportFactoryAndroid::GetInstance()->AcquireTexture(
      texture_id_in_layer_,
      reinterpret_cast<const signed char*>(
          id_to_mailbox_[current_buffer_id_].c_str()));

  // We need to tell ContentViewCore about the new frame before calling
  // setNeedsDisplay() below so that it has the needed information schedule the
  // next compositor frame.
  if (content_view_core_)
    content_view_core_->DidProduceRendererFrame();

  texture_layer_->setNeedsDisplay();
  texture_layer_->setBounds(params.size);
  texture_size_in_layer_ = params.size;

  uint32 sync_point =
      ImageTransportFactoryAndroid::GetInstance()->InsertSyncPoint();

  AcceleratedSurfaceMsg_BufferPresented_Params ack_params;
  ack_params.surface_handle = previous_buffer;
  ack_params.sync_point = sync_point;
   RenderWidgetHostImpl::AcknowledgeBufferPresent(
      params.route_id, gpu_host_id, ack_params);
}

void RenderWidgetHostViewAndroid::AcceleratedSurfacePostSubBuffer(
    const GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params& params,
    int gpu_host_id) {
  NOTREACHED();
}

void RenderWidgetHostViewAndroid::AcceleratedSurfaceSuspend() {
  NOTREACHED();
}

void RenderWidgetHostViewAndroid::AcceleratedSurfaceNew(
    uint64 surface_id,
    const std::string& mailbox_name) {
  DCHECK(surface_id == 1 || surface_id == 2);
  id_to_mailbox_[surface_id] = mailbox_name;
}

void RenderWidgetHostViewAndroid::AcceleratedSurfaceRelease() {
  // This tells us we should free the frontbuffer.
  if (texture_id_in_layer_) {
    texture_layer_->setTextureId(0);
    ImageTransportFactoryAndroid::GetInstance()->DeleteTexture(
        texture_id_in_layer_);
    texture_id_in_layer_ = 0;
  }
}

bool RenderWidgetHostViewAndroid::HasAcceleratedSurface(
    const gfx::Size& desired_size) {
  NOTREACHED();
  return false;
}

void RenderWidgetHostViewAndroid::StartContentIntent(
    const GURL& content_url) {
  if (content_view_core_)
    content_view_core_->StartContentIntent(content_url);
}

gfx::GLSurfaceHandle RenderWidgetHostViewAndroid::GetCompositingSurface() {
  if (surface_texture_transport_.get()) {
    return surface_texture_transport_->GetCompositingSurface(
        host_->surface_id());
  } else {
    return gfx::GLSurfaceHandle(gfx::kNullPluginWindow, true);
  }
}

void RenderWidgetHostViewAndroid::GetScreenInfo(WebKit::WebScreenInfo* result) {
  // ScreenInfo isn't tied to the widget on Android. Always return the default.
  RenderWidgetHostViewBase::GetDefaultScreenInfo(result);
}

// TODO(jrg): Find out the implications and answer correctly here,
// as we are returning the WebView and not root window bounds.
gfx::Rect RenderWidgetHostViewAndroid::GetBoundsInRootWindow() {
  return GetViewBounds();
}

void RenderWidgetHostViewAndroid::UnhandledWheelEvent(
    const WebKit::WebMouseWheelEvent& event) {
  // intentionally empty, like RenderWidgetHostViewViews
}

void RenderWidgetHostViewAndroid::ProcessAckedTouchEvent(
    const WebKit::WebTouchEvent& touch_event, InputEventAckState ack_result) {
  if (content_view_core_)
    content_view_core_->ConfirmTouchEvent(ack_result);
}

void RenderWidgetHostViewAndroid::SetHasHorizontalScrollbar(
    bool has_horizontal_scrollbar) {
  // intentionally empty, like RenderWidgetHostViewViews
}

void RenderWidgetHostViewAndroid::SetScrollOffsetPinning(
    bool is_pinned_to_left, bool is_pinned_to_right) {
  // intentionally empty, like RenderWidgetHostViewViews
}

bool RenderWidgetHostViewAndroid::LockMouse() {
  NOTIMPLEMENTED();
  return false;
}

void RenderWidgetHostViewAndroid::UnlockMouse() {
  NOTIMPLEMENTED();
}

// Methods called from the host to the render

void RenderWidgetHostViewAndroid::SendKeyEvent(
    const NativeWebKeyboardEvent& event) {
  if (host_)
    host_->ForwardKeyboardEvent(event);
}

void RenderWidgetHostViewAndroid::SendTouchEvent(
    const WebKit::WebTouchEvent& event) {
  if (host_)
    host_->ForwardTouchEvent(event);
}


void RenderWidgetHostViewAndroid::SendMouseEvent(
    const WebKit::WebMouseEvent& event) {
  if (host_)
    host_->ForwardMouseEvent(event);
}

void RenderWidgetHostViewAndroid::SendMouseWheelEvent(
    const WebKit::WebMouseWheelEvent& event) {
  if (host_)
    host_->ForwardWheelEvent(event);
}

void RenderWidgetHostViewAndroid::SendGestureEvent(
    const WebKit::WebGestureEvent& event) {
  if (host_)
    host_->ForwardGestureEvent(event);
}

void RenderWidgetHostViewAndroid::SelectRange(const gfx::Point& start,
                                              const gfx::Point& end) {
  if (host_)
    host_->SelectRange(start, end);
}

void RenderWidgetHostViewAndroid::MoveCaret(const gfx::Point& point) {
  if (host_)
    host_->MoveCaret(point);
}


void RenderWidgetHostViewAndroid::SetCachedBackgroundColor(SkColor color) {
  cached_background_color_ = color;
}

SkColor RenderWidgetHostViewAndroid::GetCachedBackgroundColor() const {
  return cached_background_color_;
}

void RenderWidgetHostViewAndroid::SetCachedPageScaleFactorLimits(
    float minimum_scale,
    float maximum_scale) {
  if (content_view_core_)
    content_view_core_->UpdatePageScaleLimits(minimum_scale, maximum_scale);
}

void RenderWidgetHostViewAndroid::UpdateFrameInfo(
    const gfx::Vector2d& scroll_offset,
    float page_scale_factor,
    float min_page_scale_factor,
    float max_page_scale_factor,
    const gfx::Size& content_size) {
  if (content_view_core_) {
    content_view_core_->UpdateContentSize(content_size.width(),
                                          content_size.height());
    content_view_core_->UpdatePageScaleLimits(min_page_scale_factor,
                                              max_page_scale_factor);
    content_view_core_->UpdateScrollOffsetAndPageScaleFactor(scroll_offset.x(),
                                                             scroll_offset.y(),
                                                             page_scale_factor);
  }
}

void RenderWidgetHostViewAndroid::SetContentViewCore(
    ContentViewCoreImpl* content_view_core) {
  if (content_view_core_ && is_layer_attached_)
    content_view_core_->RemoveLayer(layer_);

  content_view_core_ = content_view_core;
  if (content_view_core_ && is_layer_attached_)
    content_view_core_->AttachLayer(layer_);
}

void RenderWidgetHostViewAndroid::HasTouchEventHandlers(
    bool need_touch_events) {
  if (content_view_core_)
    content_view_core_->HasTouchEventHandlers(need_touch_events);
}

// static
void RenderWidgetHostViewPort::GetDefaultScreenInfo(
    WebKit::WebScreenInfo* results) {
  DeviceInfo info;
  const int width = info.GetWidth();
  const int height = info.GetHeight();
  results->deviceScaleFactor = info.GetDPIScale();
  results->depth = info.GetBitsPerPixel();
  results->depthPerComponent = info.GetBitsPerComponent();
  results->isMonochrome = (results->depthPerComponent == 0);
  results->rect = WebKit::WebRect(0, 0, width, height);
  // TODO(husky): Remove any system controls from availableRect.
  results->availableRect = WebKit::WebRect(0, 0, width, height);
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostView, public:

// static
RenderWidgetHostView*
RenderWidgetHostView::CreateViewForWidget(RenderWidgetHost* widget) {
  RenderWidgetHostImpl* rwhi = RenderWidgetHostImpl::From(widget);
  return new RenderWidgetHostViewAndroid(rwhi, NULL);
}

} // namespace content
