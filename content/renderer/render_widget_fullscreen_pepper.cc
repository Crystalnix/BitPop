// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/render_widget_fullscreen_pepper.h"

#include "base/message_loop.h"
#include "content/common/view_messages.h"
#include "content/renderer/renderer_gl_context.h"
#include "content/renderer/gpu_channel_host.h"
#include "content/renderer/pepper_platform_context_3d_impl.h"
#include "content/renderer/render_thread.h"
#include "gpu/command_buffer/client/gles2_implementation.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebCursorInfo.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSize.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebWidget.h"
#include "webkit/plugins/ppapi/plugin_delegate.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"

using WebKit::WebCanvas;
using WebKit::WebCompositionUnderline;
using WebKit::WebCursorInfo;
using WebKit::WebInputEvent;
using WebKit::WebMouseEvent;
using WebKit::WebPoint;
using WebKit::WebRect;
using WebKit::WebSize;
using WebKit::WebString;
using WebKit::WebTextDirection;
using WebKit::WebTextInputType;
using WebKit::WebVector;
using WebKit::WebWidget;

namespace {

// WebWidget that simply wraps the pepper plugin.
class PepperWidget : public WebWidget {
 public:
  PepperWidget(webkit::ppapi::PluginInstance* plugin,
               RenderWidgetFullscreenPepper* widget)
      : plugin_(plugin),
        widget_(widget),
        cursor_(WebCursorInfo::TypePointer) {
  }

  // WebWidget API
  virtual void close() {
    delete this;
  }

  virtual WebSize size() {
    return size_;
  }

  virtual void resize(const WebSize& size) {
    size_ = size;
    WebRect plugin_rect(0, 0, size_.width, size_.height);
    plugin_->ViewChanged(plugin_rect, plugin_rect);
    widget_->Invalidate();
  }

  virtual void animate() {
  }

  virtual void layout() {
  }

  virtual void paint(WebCanvas* canvas, const WebRect& rect) {
    WebRect plugin_rect(0, 0, size_.width, size_.height);
    plugin_->Paint(canvas, plugin_rect, rect);
  }

  virtual void composite(bool finish) {
    RendererGLContext* context = widget_->context();
    DCHECK(context);
    gpu::gles2::GLES2Implementation* gl = context->GetImplementation();
    unsigned int texture = plugin_->GetBackingTextureId();
    gl->BindTexture(GL_TEXTURE_2D, texture);
    gl->DrawArrays(GL_TRIANGLES, 0, 3);
    context->SwapBuffers();
  }

  virtual void themeChanged() {
    NOTIMPLEMENTED();
  }

  virtual bool handleInputEvent(const WebInputEvent& event) {
    bool result = plugin_->HandleInputEvent(event, &cursor_);

    // For normal web pages, WebViewImpl does input event translations and
    // generates context menu events. Since we don't have a WebView, we need to
    // do the necessary translation ourselves.
    if (WebInputEvent::isMouseEventType(event.type)) {
      const WebMouseEvent& mouse_event =
          reinterpret_cast<const WebMouseEvent&>(event);
      bool send_context_menu_event = false;
      // On Mac/Linux, we handle it on mouse down.
      // On Windows, we handle it on mouse up.
#if defined(OS_WIN)
      send_context_menu_event =
          mouse_event.type == WebInputEvent::MouseUp &&
          mouse_event.button == WebMouseEvent::ButtonRight;
#elif defined(OS_MACOSX)
      send_context_menu_event =
          mouse_event.type == WebInputEvent::MouseDown &&
          (mouse_event.button == WebMouseEvent::ButtonRight ||
           (mouse_event.button == WebMouseEvent::ButtonLeft &&
            mouse_event.modifiers & WebMouseEvent::ControlKey));
#else
      send_context_menu_event =
          mouse_event.type == WebInputEvent::MouseDown &&
          mouse_event.button == WebMouseEvent::ButtonRight;
#endif
      if (send_context_menu_event) {
        WebMouseEvent context_menu_event(mouse_event);
        context_menu_event.type = WebInputEvent::ContextMenu;
        plugin_->HandleInputEvent(context_menu_event, &cursor_);
      }
    }
    return result;
  }

  virtual void mouseCaptureLost() {
    NOTIMPLEMENTED();
  }

  virtual void setFocus(bool focus) {
    NOTIMPLEMENTED();
  }

  // TODO(piman): figure out IME and implement these if necessary.
  virtual bool setComposition(
      const WebString& text,
      const WebVector<WebCompositionUnderline>& underlines,
      int selectionStart,
      int selectionEnd) {
    return false;
  }

  virtual bool confirmComposition() {
    return false;
  }

  virtual bool confirmComposition(const WebString& text) {
    return false;
  }

  virtual WebTextInputType textInputType() {
    return WebKit::WebTextInputTypeNone;
  }

  virtual WebRect caretOrSelectionBounds() {
    return WebRect();
  }

  virtual bool selectionRange(WebPoint& start, WebPoint& end) const {
    return false;
  }

  virtual void setTextDirection(WebTextDirection) {
  }

  virtual bool isAcceleratedCompositingActive() const {
    return widget_->context() && (plugin_->GetBackingTextureId() != 0);
  }

 private:
  scoped_refptr<webkit::ppapi::PluginInstance> plugin_;
  RenderWidgetFullscreenPepper* widget_;
  WebSize size_;
  WebCursorInfo cursor_;

  DISALLOW_COPY_AND_ASSIGN(PepperWidget);
};

void DestroyContext(RendererGLContext* context, GLuint program, GLuint buffer) {
  DCHECK(context);
  gpu::gles2::GLES2Implementation* gl = context->GetImplementation();
  if (program)
    gl->DeleteProgram(program);
  if (buffer)
    gl->DeleteBuffers(1, &buffer);
  delete context;
}

}  // anonymous namespace

// static
RenderWidgetFullscreenPepper* RenderWidgetFullscreenPepper::Create(
    int32 opener_id, RenderThreadBase* render_thread,
    webkit::ppapi::PluginInstance* plugin,
    const GURL& active_url) {
  DCHECK_NE(MSG_ROUTING_NONE, opener_id);
  scoped_refptr<RenderWidgetFullscreenPepper> widget(
      new RenderWidgetFullscreenPepper(render_thread, plugin, active_url));
  widget->Init(opener_id);
  return widget.release();
}

RenderWidgetFullscreenPepper::RenderWidgetFullscreenPepper(
    RenderThreadBase* render_thread,
    webkit::ppapi::PluginInstance* plugin,
    const GURL& active_url)
    : RenderWidgetFullscreen(render_thread),
      active_url_(active_url),
      plugin_(plugin),
      context_(NULL),
      buffer_(0),
      program_(0) {
}

RenderWidgetFullscreenPepper::~RenderWidgetFullscreenPepper() {
  if (context_)
    DestroyContext(context_, program_, buffer_);
}

void RenderWidgetFullscreenPepper::Invalidate() {
  InvalidateRect(gfx::Rect(size_.width(), size_.height()));
}

void RenderWidgetFullscreenPepper::InvalidateRect(const WebKit::WebRect& rect) {
  if (CheckCompositing()) {
    scheduleComposite();
  } else {
    didInvalidateRect(rect);
  }
}

void RenderWidgetFullscreenPepper::ScrollRect(
    int dx, int dy, const WebKit::WebRect& rect) {
  if (CheckCompositing()) {
    scheduleComposite();
  } else {
    didScrollRect(dx, dy, rect);
  }
}

void RenderWidgetFullscreenPepper::Destroy() {
  // This function is called by the plugin instance as it's going away, so reset
  // plugin_ to NULL to avoid calling into a dangling pointer e.g. on Close().
  plugin_ = NULL;
  Send(new ViewHostMsg_Close(routing_id_));
}

webkit::ppapi::PluginDelegate::PlatformContext3D*
RenderWidgetFullscreenPepper::CreateContext3D() {
  if (!context_) {
    CreateContext();
  }
  if (!context_)
    return NULL;
  return new PlatformContext3DImpl(context_);
}

void RenderWidgetFullscreenPepper::DidInitiatePaint() {
  if (plugin_)
    plugin_->ViewInitiatedPaint();
}

void RenderWidgetFullscreenPepper::DidFlushPaint() {
  if (plugin_)
    plugin_->ViewFlushedPaint();
}

void RenderWidgetFullscreenPepper::Close() {
  // If the fullscreen window is closed (e.g. user pressed escape), reset to
  // normal mode.
  if (plugin_)
    plugin_->SetFullscreen(false, false);
}

webkit::ppapi::PluginInstance*
RenderWidgetFullscreenPepper::GetBitmapForOptimizedPluginPaint(
    const gfx::Rect& paint_bounds,
    TransportDIB** dib,
    gfx::Rect* location,
    gfx::Rect* clip) {
  if (plugin_ &&
      plugin_->GetBitmapForOptimizedPluginPaint(paint_bounds, dib,
                                                location, clip))
    return plugin_;
  return NULL;
}

void RenderWidgetFullscreenPepper::OnResize(const gfx::Size& size,
                                            const gfx::Rect& resizer_rect) {
  if (context_) {
    gpu::gles2::GLES2Implementation* gl = context_->GetImplementation();
#if defined(OS_MACOSX)
    context_->ResizeOnscreen(size);
#else
    gl->ResizeCHROMIUM(size.width(), size.height());
#endif
    gl->Viewport(0, 0, size.width(), size.height());
  }
  RenderWidget::OnResize(size, resizer_rect);
}

WebWidget* RenderWidgetFullscreenPepper::CreateWebWidget() {
  return new PepperWidget(plugin_, this);
}

void RenderWidgetFullscreenPepper::CreateContext() {
  DCHECK(!context_);
  RenderThread* render_thread = RenderThread::current();
  DCHECK(render_thread);
  GpuChannelHost* host = render_thread->EstablishGpuChannelSync(
    content::CAUSE_FOR_GPU_LAUNCH_RENDERWIDGETFULLSCREENPEPPER_CREATECONTEXT);
  if (!host)
    return;
  const int32 attribs[] = {
    RendererGLContext::ALPHA_SIZE, 8,
    RendererGLContext::DEPTH_SIZE, 0,
    RendererGLContext::STENCIL_SIZE, 0,
    RendererGLContext::SAMPLES, 0,
    RendererGLContext::SAMPLE_BUFFERS, 0,
    RendererGLContext::NONE,
  };
  context_ = RendererGLContext::CreateViewContext(
      host,
      compositing_surface(),
      routing_id(),
      "GL_OES_packed_depth_stencil GL_OES_depth24",
      attribs,
      active_url_);
  if (!context_)
    return;

  if (!InitContext()) {
    DestroyContext(context_, program_, buffer_);
    context_ = NULL;
    return;
  }
  context_->SetSwapBuffersCallback(
      NewCallback(this, &RenderWidgetFullscreenPepper::DidFlushPaint));
  context_->SetContextLostCallback(
      NewCallback(this, &RenderWidgetFullscreenPepper::OnLostContext));
}

namespace {

const char kVertexShader[] =
    "attribute vec2 in_tex_coord;\n"
    "varying vec2 tex_coord;\n"
    "void main() {\n"
    "  gl_Position = vec4(in_tex_coord.x * 2. - 1.,\n"
    "                     in_tex_coord.y * 2. - 1.,\n"
    "                     0.,\n"
    "                     1.);\n"
    "  tex_coord = vec2(in_tex_coord.x, in_tex_coord.y);\n"
    "}\n";

const char kFragmentShader[] =
    "precision mediump float;\n"
    "varying vec2 tex_coord;\n"
    "uniform sampler2D in_texture;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(in_texture, tex_coord);\n"
    "}\n";

GLuint CreateShaderFromSource(gpu::gles2::GLES2Implementation* gl,
                              GLenum type,
                              const char* source) {
    GLuint shader = gl->CreateShader(type);
    gl->ShaderSource(shader, 1, &source, NULL);
    gl->CompileShader(shader);
    int status;
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        int size = 0;
        gl->GetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        scoped_array<char> log(new char[size]);
        gl->GetShaderInfoLog(shader, size, NULL, log.get());
        DLOG(ERROR) << "Compilation failed: " << log.get();
        gl->DeleteShader(shader);
        shader = 0;
    }
    return shader;
}

const float kTexCoords[] = {
    0.f, 0.f,
    0.f, 2.f,
    2.f, 0.f,
};

}  // anonymous namespace

bool RenderWidgetFullscreenPepper::InitContext() {
  gpu::gles2::GLES2Implementation* gl = context_->GetImplementation();
  program_ = gl->CreateProgram();

  GLuint vertex_shader =
      CreateShaderFromSource(gl, GL_VERTEX_SHADER, kVertexShader);
  if (!vertex_shader)
    return false;
  gl->AttachShader(program_, vertex_shader);
  gl->DeleteShader(vertex_shader);

  GLuint fragment_shader =
      CreateShaderFromSource(gl, GL_FRAGMENT_SHADER, kFragmentShader);
  if (!fragment_shader)
    return false;
  gl->AttachShader(program_, fragment_shader);
  gl->DeleteShader(fragment_shader);

  gl->BindAttribLocation(program_, 0, "in_tex_coord");
  gl->LinkProgram(program_);
  int status;
  gl->GetProgramiv(program_, GL_LINK_STATUS, &status);
  if (!status) {
    int size = 0;
    gl->GetProgramiv(program_, GL_INFO_LOG_LENGTH, &size);
    scoped_array<char> log(new char[size]);
    gl->GetProgramInfoLog(program_, size, NULL, log.get());
    DLOG(ERROR) << "Link failed: " << log.get();
    return false;
  }
  gl->UseProgram(program_);
  int texture_location = gl->GetUniformLocation(program_, "in_texture");
  gl->Uniform1i(texture_location, 0);

  gl->GenBuffers(1, &buffer_);
  gl->BindBuffer(GL_ARRAY_BUFFER, buffer_);
  gl->BufferData(GL_ARRAY_BUFFER,
                 sizeof(kTexCoords),
                 kTexCoords,
                 GL_STATIC_DRAW);
  gl->VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  gl->EnableVertexAttribArray(0);
  return true;
}

bool RenderWidgetFullscreenPepper::CheckCompositing() {
  bool compositing = webwidget_->isAcceleratedCompositingActive();
  if (compositing != is_accelerated_compositing_active_) {
    didActivateAcceleratedCompositing(compositing);
  }
  return compositing;
}

void RenderWidgetFullscreenPepper::OnLostContext() {
  if (!context_)
    return;
  // Destroy the context later, in case we got called from InitContext for
  // example. We still need to reset context_ now so that a new context gets
  // created when the plugin recreates its own.
  MessageLoop::current()->PostTask(
      FROM_HERE,
      NewRunnableFunction(DestroyContext, context_, program_, buffer_));
  context_ = NULL;
  program_ = 0;
  buffer_ = 0;
}
