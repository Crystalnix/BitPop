// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/tools/player_x11/gl_video_renderer.h"

#include <X11/Xutil.h>

#include "media/base/buffers.h"
#include "media/base/video_frame.h"
#include "media/base/yuv_convert.h"
#include "ui/gfx/gl/gl_implementation.h"

GlVideoRenderer* GlVideoRenderer::instance_ = NULL;

GlVideoRenderer::GlVideoRenderer(Display* display, Window window,
                                 MessageLoop* message_loop)
    : display_(display),
      window_(window),
      gl_context_(NULL),
      glx_thread_message_loop_(message_loop) {
}

GlVideoRenderer::~GlVideoRenderer() {
}

void GlVideoRenderer::OnStop(media::FilterCallback* callback) {
  glXMakeCurrent(display_, 0, NULL);
  glXDestroyContext(display_, gl_context_);
  if (callback) {
    callback->Run();
    delete callback;
  }
}

static GLXContext InitGLContext(Display* display, Window window) {
  // Some versions of NVIDIA's GL libGL.so include a broken version of
  // dlopen/dlsym, and so linking it into chrome breaks it. So we dynamically
  // load it, and use glew to dynamically resolve symbols.
  // See http://code.google.com/p/chromium/issues/detail?id=16800
  if (!InitializeGLBindings(gfx::kGLImplementationDesktopGL)) {
    LOG(ERROR) << "InitializeGLBindings failed";
    return NULL;
  }

  XWindowAttributes attributes;
  XGetWindowAttributes(display, window, &attributes);
  XVisualInfo visual_info_template;
  visual_info_template.visualid = XVisualIDFromVisual(attributes.visual);
  int visual_info_count = 0;
  XVisualInfo* visual_info_list = XGetVisualInfo(display, VisualIDMask,
                                                 &visual_info_template,
                                                 &visual_info_count);
  GLXContext context = NULL;
  for (int i = 0; i < visual_info_count && !context; ++i) {
    context = glXCreateContext(display, visual_info_list + i, 0,
                               True /* Direct rendering */);
  }

  XFree(visual_info_list);
  if (!context) {
    return NULL;
  }

  if (!glXMakeCurrent(display, window, context)) {
    glXDestroyContext(display, context);
    return NULL;
  }

  return context;
}

// Matrix used for the YUV to RGB conversion.
static const float kYUV2RGB[9] = {
  1.f, 0.f, 1.403f,
  1.f, -.344f, -.714f,
  1.f, 1.772f, 0.f,
};

// Vertices for a full screen quad.
static const float kVertices[8] = {
  -1.f, 1.f,
  -1.f, -1.f,
  1.f, 1.f,
  1.f, -1.f,
};

// Texture Coordinates mapping the entire texture.
static const float kTextureCoords[8] = {
  0, 0,
  0, 1,
  1, 0,
  1, 1,
};

// Pass-through vertex shader.
static const char kVertexShader[] =
    "varying vec2 interp_tc;\n"
    "\n"
    "attribute vec4 in_pos;\n"
    "attribute vec2 in_tc;\n"
    "\n"
    "void main() {\n"
    "  interp_tc = in_tc;\n"
    "  gl_Position = in_pos;\n"
    "}\n";

// YUV to RGB pixel shader. Loads a pixel from each plane and pass through the
// matrix.
static const char kFragmentShader[] =
    "varying vec2 interp_tc;\n"
    "\n"
    "uniform sampler2D y_tex;\n"
    "uniform sampler2D u_tex;\n"
    "uniform sampler2D v_tex;\n"
    "uniform mat3 yuv2rgb;\n"
    "\n"
    "void main() {\n"
    "  float y = texture2D(y_tex, interp_tc).x;\n"
    "  float u = texture2D(u_tex, interp_tc).r - .5;\n"
    "  float v = texture2D(v_tex, interp_tc).r - .5;\n"
    "  vec3 rgb = yuv2rgb * vec3(y, u, v);\n"
    "  gl_FragColor = vec4(rgb, 1);\n"
    "}\n";

// Buffer size for compile errors.
static const unsigned int kErrorSize = 4096;

bool GlVideoRenderer::OnInitialize(media::VideoDecoder* decoder) {
  LOG(INFO) << "Initializing GL Renderer...";

  // Resize the window to fit that of the video.
  XResizeWindow(display_, window_, width(), height());

  gl_context_ = InitGLContext(display_, window_);
  if (!gl_context_)
    return false;

  // Create 3 textures, one for each plane, and bind them to different
  // texture units.
  glGenTextures(media::VideoFrame::kNumYUVPlanes, textures_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textures_[0]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glEnable(GL_TEXTURE_2D);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, textures_[1]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glEnable(GL_TEXTURE_2D);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, textures_[2]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glEnable(GL_TEXTURE_2D);

  GLuint program = glCreateProgram();

  // Create our YUV->RGB shader.
  GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  const char* vs_source = kVertexShader;
  int vs_size = sizeof(kVertexShader);
  glShaderSource(vertex_shader, 1, &vs_source, &vs_size);
  glCompileShader(vertex_shader);
  int result = GL_FALSE;
  glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &result);
  if (!result) {
    char log[kErrorSize];
    int len = 0;
    glGetShaderInfoLog(vertex_shader, kErrorSize - 1, &len, log);
    log[kErrorSize - 1] = 0;
    LOG(FATAL) << log;
  }
  glAttachShader(program, vertex_shader);
  glDeleteShader(vertex_shader);

  GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  const char* ps_source = kFragmentShader;
  int ps_size = sizeof(kFragmentShader);
  glShaderSource(fragment_shader, 1, &ps_source, &ps_size);
  glCompileShader(fragment_shader);
  result = GL_FALSE;
  glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &result);
  if (!result) {
    char log[kErrorSize];
    int len = 0;
    glGetShaderInfoLog(fragment_shader, kErrorSize - 1, &len, log);
    log[kErrorSize - 1] = 0;
    LOG(FATAL) << log;
  }
  glAttachShader(program, fragment_shader);
  glDeleteShader(fragment_shader);

  glLinkProgram(program);
  result = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &result);
  if (!result) {
    char log[kErrorSize];
    int len = 0;
    glGetProgramInfoLog(program, kErrorSize - 1, &len, log);
    log[kErrorSize - 1] = 0;
    LOG(FATAL) << log;
  }
  glUseProgram(program);
  glDeleteProgram(program);

  // Bind parameters.
  glUniform1i(glGetUniformLocation(program, "y_tex"), 0);
  glUniform1i(glGetUniformLocation(program, "u_tex"), 1);
  glUniform1i(glGetUniformLocation(program, "v_tex"), 2);
  int yuv2rgb_location = glGetUniformLocation(program, "yuv2rgb");
  glUniformMatrix3fv(yuv2rgb_location, 1, GL_TRUE, kYUV2RGB);

  int pos_location = glGetAttribLocation(program, "in_pos");
  glEnableVertexAttribArray(pos_location);
  glVertexAttribPointer(pos_location, 2, GL_FLOAT, GL_FALSE, 0, kVertices);

  int tc_location = glGetAttribLocation(program, "in_tc");
  glEnableVertexAttribArray(tc_location);
  glVertexAttribPointer(tc_location, 2, GL_FLOAT, GL_FALSE, 0,
                        kTextureCoords);

  // We are getting called on a thread. Release the context so that it can be
  // made current on the main thread.
  glXMakeCurrent(display_, 0, NULL);

  // Save this instance.
  DCHECK(!instance_);
  instance_ = this;
  return true;
}

void GlVideoRenderer::OnFrameAvailable() {
  if (glx_thread_message_loop()) {
    glx_thread_message_loop()->PostTask(FROM_HERE,
        NewRunnableMethod(this, &GlVideoRenderer::Paint));
  }
}

void GlVideoRenderer::Paint() {
  scoped_refptr<media::VideoFrame> video_frame;
  GetCurrentFrame(&video_frame);

  if (!video_frame) {
    // TODO(jiesun): Use color fill rather than create black frame then scale.
    PutCurrentFrame(video_frame);
    return;
  }

  // Convert YUV frame to RGB.
  DCHECK(video_frame->format() == media::VideoFrame::YV12 ||
         video_frame->format() == media::VideoFrame::YV16);
  DCHECK(video_frame->stride(media::VideoFrame::kUPlane) ==
         video_frame->stride(media::VideoFrame::kVPlane));
  DCHECK(video_frame->planes() == media::VideoFrame::kNumYUVPlanes);

  if (glXGetCurrentContext() != gl_context_ ||
      glXGetCurrentDrawable() != window_) {
    glXMakeCurrent(display_, window_, gl_context_);
  }
  for (unsigned int i = 0; i < media::VideoFrame::kNumYUVPlanes; ++i) {
    unsigned int width = (i == media::VideoFrame::kYPlane) ?
        video_frame->width() : video_frame->width() / 2;
    unsigned int height = (i == media::VideoFrame::kYPlane ||
                           video_frame->format() == media::VideoFrame::YV16) ?
        video_frame->height() : video_frame->height() / 2;
    glActiveTexture(GL_TEXTURE0 + i);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, video_frame->stride(i));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, video_frame->data(i));
  }
  PutCurrentFrame(video_frame);

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glXSwapBuffers(display_, window_);
}
