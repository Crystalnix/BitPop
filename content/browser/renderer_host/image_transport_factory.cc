// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/image_transport_factory.h"

#include <algorithm>
#include <map>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "content/browser/gpu/browser_gpu_channel_host_factory.h"
#include "content/browser/gpu/gpu_surface_tracker.h"
#include "content/common/gpu/client/gl_helper.h"
#include "content/common/gpu/client/gpu_channel_host.h"
#include "content/common/gpu/client/webgraphicscontext3d_command_buffer_impl.h"
#include "content/common/gpu/gpu_process_launch_causes.h"
#include "content/common/webkitplatformsupport_impl.h"
#include "content/public/common/content_switches.h"
#include "gpu/ipc/command_buffer_proxy.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebGraphicsContext3D.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_setup.h"
#include "ui/compositor/test_web_graphics_context_3d.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/size.h"

using content::BrowserGpuChannelHostFactory;
using content::GLHelper;

namespace {

ImageTransportFactory* g_factory;

class DefaultTransportFactory
    : public ui::DefaultContextFactory,
      public ImageTransportFactory {
 public:
  DefaultTransportFactory() {
    ui::DefaultContextFactory::Initialize();
  }

  virtual ui::ContextFactory* AsContextFactory() OVERRIDE {
    return this;
  }

  virtual gfx::GLSurfaceHandle CreateSharedSurfaceHandle(
      ui::Compositor* compositor) OVERRIDE {
    return gfx::GLSurfaceHandle();
  }

  virtual void DestroySharedSurfaceHandle(
      gfx::GLSurfaceHandle surface) OVERRIDE {
  }

  virtual scoped_refptr<ui::Texture> CreateTransportClient(
      const gfx::Size& size,
      uint64 transport_handle) OVERRIDE {
    return NULL;
  }

  virtual GLHelper* GetGLHelper(ui::Compositor* compositor) OVERRIDE {
    return NULL;
  }

  virtual uint32 InsertSyncPoint(ui::Compositor* compositor) OVERRIDE {
    return 0;
  }

  // We don't generate lost context events, so we don't need to keep track of
  // observers
  virtual void AddObserver(ImageTransportFactoryObserver* observer) OVERRIDE {
  }

  virtual void RemoveObserver(
      ImageTransportFactoryObserver* observer) OVERRIDE {
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DefaultTransportFactory);
};

class ImageTransportClientTexture : public ui::Texture {
 public:
  ImageTransportClientTexture(
      const gfx::Size& size,
      uint64 surface_id)
          : ui::Texture(true, size) {
    set_texture_id(surface_id);
  }

 protected:
  virtual ~ImageTransportClientTexture() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ImageTransportClientTexture);
};

class GpuProcessTransportFactory;

class CompositorSwapClient
    : public base::SupportsWeakPtr<CompositorSwapClient>,
      public WebGraphicsContext3DSwapBuffersClient {
 public:
  CompositorSwapClient(ui::Compositor* compositor,
                       GpuProcessTransportFactory* factory)
      : compositor_(compositor),
        factory_(factory) {
  }

  ~CompositorSwapClient() {
  }

  virtual void OnViewContextSwapBuffersPosted() OVERRIDE {
    compositor_->OnSwapBuffersPosted();
  }

  virtual void OnViewContextSwapBuffersComplete() OVERRIDE {
    compositor_->OnSwapBuffersComplete();
  }

  virtual void OnViewContextSwapBuffersAborted() OVERRIDE {
    // Recreating contexts directly from here causes issues, so post a task
    // instead.
    // TODO(piman): Fix the underlying issues.
    MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(&CompositorSwapClient::OnLostContext, this->AsWeakPtr()));
  }

 private:
  void OnLostContext();
  ui::Compositor* compositor_;
  GpuProcessTransportFactory* factory_;

  DISALLOW_COPY_AND_ASSIGN(CompositorSwapClient);
};

class GpuProcessTransportFactory : public ui::ContextFactory,
                                   public ImageTransportFactory {
 public:
  GpuProcessTransportFactory() {}
  virtual ~GpuProcessTransportFactory() {
    DCHECK(per_compositor_data_.empty());
  }

  virtual WebKit::WebGraphicsContext3D* CreateContext(
      ui::Compositor* compositor) OVERRIDE {
    return CreateContextCommon(compositor, false);
  }

  virtual WebKit::WebGraphicsContext3D* CreateOffscreenContext(
      ui::Compositor* compositor) OVERRIDE {
    return CreateContextCommon(compositor, true);
  }

  virtual void RemoveCompositor(ui::Compositor* compositor) OVERRIDE {
    PerCompositorDataMap::iterator it = per_compositor_data_.find(compositor);
    if (it == per_compositor_data_.end())
      return;
    PerCompositorData* data = it->second;
    DCHECK(data);
    GpuSurfaceTracker::Get()->RemoveSurface(data->surface_id);
    delete data;
    per_compositor_data_.erase(it);
  }

  virtual ui::ContextFactory* AsContextFactory() OVERRIDE {
    return this;
  }

  virtual gfx::GLSurfaceHandle CreateSharedSurfaceHandle(
      ui::Compositor* compositor) OVERRIDE {
    PerCompositorData* data = per_compositor_data_[compositor];
    if (!data)
      data = CreatePerCompositorData(compositor);
    gfx::GLSurfaceHandle handle = gfx::GLSurfaceHandle(
        gfx::kNullPluginWindow, true);
    handle.parent_gpu_process_id = data->shared_context->GetGPUProcessID();
    handle.parent_client_id = data->shared_context->GetChannelID();
    handle.parent_context_id = data->shared_context->GetContextID();
    handle.parent_texture_id[0] = data->shared_context->createTexture();
    handle.parent_texture_id[1] = data->shared_context->createTexture();
    handle.sync_point = data->shared_context->insertSyncPoint();

    return handle;
  }

  virtual void DestroySharedSurfaceHandle(
      gfx::GLSurfaceHandle surface) OVERRIDE {
    for (PerCompositorDataMap::iterator it = per_compositor_data_.begin();
         it != per_compositor_data_.end(); ++it) {
      PerCompositorData* data = it->second;
      DCHECK(data);
      int gpu_process_id = data->shared_context->GetGPUProcessID();
      uint32 client_id = data->shared_context->GetChannelID();
      uint32 context_id = data->shared_context->GetContextID();
      if (surface.parent_gpu_process_id == gpu_process_id &&
          surface.parent_client_id == client_id &&
          surface.parent_context_id == context_id) {
        data->shared_context->deleteTexture(surface.parent_texture_id[0]);
        data->shared_context->deleteTexture(surface.parent_texture_id[1]);
        data->shared_context->flush();
        break;
      }
    }
  }

  virtual scoped_refptr<ui::Texture> CreateTransportClient(
      const gfx::Size& size,
      uint64 transport_handle) {
    scoped_refptr<ImageTransportClientTexture> image(
        new ImageTransportClientTexture(size, transport_handle));
    return image;
  }

  virtual GLHelper* GetGLHelper(ui::Compositor* compositor) {
    PerCompositorData* data = per_compositor_data_[compositor];
    if (!data)
      data = CreatePerCompositorData(compositor);
    if (!data->gl_helper.get()) {
      WebKit::WebGraphicsContext3D* context_for_thread =
          CreateContextCommon(compositor, true);
      if (!context_for_thread)
        return NULL;
      data->gl_helper.reset(new GLHelper(data->shared_context.get(),
                                         context_for_thread));
    }
    return data->gl_helper.get();
  }

  virtual uint32 InsertSyncPoint(ui::Compositor* compositor) OVERRIDE {
    PerCompositorData* data = per_compositor_data_[compositor];
    if (!data)
      data = CreatePerCompositorData(compositor);
    return data->shared_context->insertSyncPoint();
  }

  virtual void AddObserver(ImageTransportFactoryObserver* observer) {
    observer_list_.AddObserver(observer);
  }

  virtual void RemoveObserver(ImageTransportFactoryObserver* observer) {
    observer_list_.RemoveObserver(observer);
  }

  void OnLostContext(ui::Compositor* compositor) {
    LOG(ERROR) << "Lost UI compositor context.";
    PerCompositorData* data = per_compositor_data_[compositor];
    DCHECK(data);

    // Keep old resources around while we call the observers, but ensure that
    // new resources are created if needed.
    scoped_ptr<WebGraphicsContext3DCommandBufferImpl> old_shared_context(
        data->shared_context.release());
    scoped_ptr<GLHelper> old_helper(data->gl_helper.release());

    // Note: this has the effect of recreating the swap_client, which means we
    // won't get more reports of lost context from the same gpu process. It's a
    // good thing.
    CreateSharedContext(compositor);

    FOR_EACH_OBSERVER(ImageTransportFactoryObserver,
        observer_list_,
        OnLostResources(compositor));
    compositor->OnSwapBuffersAborted();
  }

 private:
  struct PerCompositorData {
    int surface_id;
    scoped_ptr<CompositorSwapClient> swap_client;
    scoped_ptr<WebGraphicsContext3DCommandBufferImpl> shared_context;
    scoped_ptr<GLHelper> gl_helper;
  };

  PerCompositorData* CreatePerCompositorData(ui::Compositor* compositor) {
    DCHECK(!per_compositor_data_[compositor]);

    gfx::AcceleratedWidget widget = compositor->widget();
    GpuSurfaceTracker* tracker = GpuSurfaceTracker::Get();

    PerCompositorData* data = new PerCompositorData;
    data->surface_id = tracker->AddSurfaceForNativeWidget(widget);
    tracker->SetSurfaceHandle(
        data->surface_id,
        gfx::GLSurfaceHandle(widget, false));
    per_compositor_data_[compositor] = data;

    CreateSharedContext(compositor);

    return data;
  }

  WebKit::WebGraphicsContext3D* CreateContextCommon(
      ui::Compositor* compositor,
      bool offscreen) {
    PerCompositorData* data = per_compositor_data_[compositor];
    if (!data)
      data = CreatePerCompositorData(compositor);

    WebKit::WebGraphicsContext3D::Attributes attrs;
    attrs.shareResources = true;
    GpuChannelHostFactory* factory = BrowserGpuChannelHostFactory::instance();
    GURL url("chrome://gpu/GpuProcessTransportFactory::CreateContextCommon");
    scoped_ptr<WebGraphicsContext3DCommandBufferImpl> context(
        new WebGraphicsContext3DCommandBufferImpl(
            offscreen ? 0 : data->surface_id,
            url,
            factory,
            data->swap_client->AsWeakPtr()));
    if (!context->Initialize(
        attrs,
        false,
        content::CAUSE_FOR_GPU_LAUNCH_WEBGRAPHICSCONTEXT3DCOMMANDBUFFERIMPL_INITIALIZE))
      return NULL;
    return context.release();
  }

  void CreateSharedContext(ui::Compositor* compositor) {
    PerCompositorData* data = per_compositor_data_[compositor];
    DCHECK(data);

    data->swap_client.reset(new CompositorSwapClient(compositor, this));

    GpuChannelHostFactory* factory = BrowserGpuChannelHostFactory::instance();
    WebKit::WebGraphicsContext3D::Attributes attrs;
    attrs.shareResources = true;
    GURL url("chrome://gpu/GpuProcessTransportFactory::CreateSharedContext");
    data->shared_context.reset(new WebGraphicsContext3DCommandBufferImpl(
          0,
          url,
          factory,
          data->swap_client->AsWeakPtr()));
    if (!data->shared_context->Initialize(
        attrs,
        false,
        content::CAUSE_FOR_GPU_LAUNCH_WEBGRAPHICSCONTEXT3DCOMMANDBUFFERIMPL_INITIALIZE)) {
      // If we can't recreate contexts, we won't be able to show the UI. Better
      // crash at this point.
      LOG(FATAL) << "Failed to initialize compositor shared context.";
    }
    if (!data->shared_context->makeContextCurrent()) {
      // If we can't recreate contexts, we won't be able to show the UI. Better
      // crash at this point.
      LOG(FATAL) << "Failed to make compositor shared context current.";
    }
  }

  typedef std::map<ui::Compositor*, PerCompositorData*> PerCompositorDataMap;
  PerCompositorDataMap per_compositor_data_;
  ObserverList<ImageTransportFactoryObserver> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(GpuProcessTransportFactory);
};

void CompositorSwapClient::OnLostContext() {
  factory_->OnLostContext(compositor_);
  // Note: previous line destroyed this. Don't access members from now on.
}

WebKit::WebGraphicsContext3D* CreateTestContext() {
  ui::TestWebGraphicsContext3D* test_context =
      new ui::TestWebGraphicsContext3D();
  test_context->Initialize();
  return test_context;
}

}  // anonymous namespace

// static
void ImageTransportFactory::Initialize() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kTestCompositor)) {
    ui::SetupTestCompositor();
  }
  if (ui::IsTestCompositorEnabled()) {
    g_factory = new DefaultTransportFactory();
    content::WebKitPlatformSupportImpl::SetOffscreenContextFactoryForTest(
        CreateTestContext);
  } else {
#if defined(OS_WIN)
    g_factory = new DefaultTransportFactory();
#else
    g_factory = new GpuProcessTransportFactory();
#endif
  }
  ui::ContextFactory::SetInstance(g_factory->AsContextFactory());
}

// static
void ImageTransportFactory::Terminate() {
  ui::ContextFactory::SetInstance(NULL);
  delete g_factory;
  g_factory = NULL;
}

// static
ImageTransportFactory* ImageTransportFactory::GetInstance() {
  return g_factory;
}
