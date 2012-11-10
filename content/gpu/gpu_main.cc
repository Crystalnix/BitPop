// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#if defined(OS_WIN)
#include <windows.h>
#endif

#include "base/debug/trace_event.h"
#include "base/message_loop.h"
#include "base/rand_util.h"
#include "base/string_number_conversions.h"
#include "base/stringprintf.h"
#include "base/threading/platform_thread.h"
#include "base/win/scoped_com_initializer.h"
#include "build/build_config.h"
#include "content/common/gpu/gpu_config.h"
#include "content/gpu/gpu_child_thread.h"
#include "content/gpu/gpu_info_collector.h"
#include "content/gpu/gpu_process.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/main_function_params.h"
#include "crypto/hmac.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_switches.h"

#if defined(OS_WIN)
#include "content/common/gpu/media/dxva_video_decode_accelerator.h"
#include "sandbox/win/src/sandbox.h"
#elif defined(OS_CHROMEOS) && defined(ARCH_CPU_ARMEL)
#include "content/common/gpu/media/omx_video_decode_accelerator.h"
#elif defined(OS_CHROMEOS) && defined(ARCH_CPU_X86_FAMILY)
#include "content/common/gpu/media/vaapi_video_decode_accelerator.h"
#endif

#if defined(USE_X11)
#include "ui/base/x/x11_util.h"
#endif

#if defined(OS_LINUX)
#include "content/public/common/sandbox_init.h"
#endif

namespace {
void WarmUpSandbox(const content::GPUInfo&, bool);
void CollectGraphicsInfo(content::GPUInfo*);
}

// Main function for starting the Gpu process.
int GpuMain(const content::MainFunctionParams& parameters) {
  TRACE_EVENT0("gpu", "GpuMain");

  base::Time start_time = base::Time::Now();

  const CommandLine& command_line = parameters.command_line;
  if (command_line.HasSwitch(switches::kGpuStartupDialog)) {
    ChildProcess::WaitForDebugger("Gpu");
  }

  if (!command_line.HasSwitch(switches::kSingleProcess)) {
#if defined(OS_WIN)
    // Prevent Windows from displaying a modal dialog on failures like not being
    // able to load a DLL.
    SetErrorMode(
        SEM_FAILCRITICALERRORS |
        SEM_NOGPFAULTERRORBOX |
        SEM_NOOPENFILEERRORBOX);
#elif defined(USE_X11)
    ui::SetDefaultX11ErrorHandlers();
#endif
  }

  // Initialization of the OpenGL bindings may fail, in which case we
  // will need to tear down this process. However, we can not do so
  // safely until the IPC channel is set up, because the detection of
  // early return of a child process is implemented using an IPC
  // channel error. If the IPC channel is not fully set up between the
  // browser and GPU process, and the GPU process crashes or exits
  // early, the browser process will never detect it.  For this reason
  // we defer tearing down the GPU process until receiving the
  // GpuMsg_Initialize message from the browser.
  bool dead_on_arrival = false;

  content::GPUInfo gpu_info;
  // Get vendor_id, device_id, driver_version from browser process through
  // commandline switches.
  DCHECK(command_line.HasSwitch(switches::kGpuVendorID) &&
         command_line.HasSwitch(switches::kGpuDeviceID) &&
         command_line.HasSwitch(switches::kGpuDriverVersion));
  bool success = base::HexStringToInt(
      command_line.GetSwitchValueASCII(switches::kGpuVendorID),
      reinterpret_cast<int*>(&(gpu_info.gpu.vendor_id)));
  DCHECK(success);
  success = base::HexStringToInt(
      command_line.GetSwitchValueASCII(switches::kGpuDeviceID),
      reinterpret_cast<int*>(&(gpu_info.gpu.device_id)));
  DCHECK(success);
  gpu_info.driver_vendor =
      command_line.GetSwitchValueASCII(switches::kGpuDriverVendor);
  gpu_info.driver_version =
      command_line.GetSwitchValueASCII(switches::kGpuDriverVersion);
  content::GetContentClient()->SetGpuInfo(gpu_info);

  // We need to track that information for the WarmUpSandbox function.
  bool initialized_gl_context = false;
  // Load and initialize the GL implementation and locate the GL entry points.
  if (gfx::GLSurface::InitializeOneOff()) {
#if defined(OS_LINUX)
    // We collect full GPU info on demand in Win/Mac, i.e., when about:gpu
    // page opens.  This is because we can make blacklist decisions based on
    // preliminary GPU info.
    // However, on Linux, we may not have enough info for blacklisting.
    if (!gpu_info.gpu.vendor_id || !gpu_info.gpu.device_id ||
        gpu_info.driver_vendor.empty() || gpu_info.driver_version.empty()) {
      CollectGraphicsInfo(&gpu_info);
      // We know that CollectGraphicsInfo will initialize a GLContext.
      initialized_gl_context = true;
    }

#if !defined(OS_CHROMEOS)
    if (gpu_info.gpu.vendor_id == 0x10de &&  // NVIDIA
        gpu_info.driver_vendor == "NVIDIA") {
      base::ThreadRestrictions::AssertIOAllowed();
      if (access("/dev/nvidiactl", R_OK) != 0) {
        VLOG(1) << "NVIDIA device file /dev/nvidiactl access denied";
        gpu_info.gpu_accessible = false;
        dead_on_arrival = true;
      }
    }
#endif  // OS_CHROMEOS
#endif  // OS_LINUX
  } else {
    VLOG(1) << "gfx::GLSurface::InitializeOneOff failed";
    gpu_info.gpu_accessible = false;
    gpu_info.finalized = true;
    dead_on_arrival = true;
  }

  {
    const bool should_initialize_gl_context = !initialized_gl_context &&
                                              !dead_on_arrival;
    // Warm up the current process before enabling the sandbox.
    WarmUpSandbox(gpu_info, should_initialize_gl_context);
  }

#if defined(OS_LINUX)
  {
    TRACE_EVENT0("gpu", "Initialize sandbox");
    bool do_init_sandbox = true;

#if defined(OS_CHROMEOS) && defined(NDEBUG)
    // On Chrome OS and when not on a debug build, initialize
    // the GPU process' sandbox only for Intel GPUs.
    do_init_sandbox = gpu_info.gpu.vendor_id == 0x8086;   // Intel GPU.
#endif

    if (do_init_sandbox) {
      content::InitializeSandbox();
    }
  }
#endif

#if defined(OS_WIN)
  {
    TRACE_EVENT0("gpu", "Lower token");
    // For windows, if the target_services interface is not zero, the process
    // is sandboxed and we must call LowerToken() before rendering untrusted
    // content.
    sandbox::TargetServices* target_services =
        parameters.sandbox_info->target_services;
    if (target_services)
      target_services->LowerToken();
  }
#endif

  MessageLoop::Type message_loop_type = MessageLoop::TYPE_IO;
#if defined(OS_WIN)
  // Unless we're running on desktop GL, we don't need a UI message
  // loop, so avoid its use to work around apparent problems with some
  // third-party software.
  if (command_line.HasSwitch(switches::kUseGL) &&
      command_line.GetSwitchValueASCII(switches::kUseGL) ==
          gfx::kGLImplementationDesktopName) {
      message_loop_type = MessageLoop::TYPE_UI;
  }
#elif defined(OS_LINUX)
  message_loop_type = MessageLoop::TYPE_DEFAULT;
#endif

  MessageLoop main_message_loop(message_loop_type);
  base::PlatformThread::SetName("CrGpuMain");

  GpuProcess gpu_process;

  GpuChildThread* child_thread = new GpuChildThread(dead_on_arrival, gpu_info);

  child_thread->Init(start_time);

  gpu_process.set_main_thread(child_thread);

  {
    TRACE_EVENT0("gpu", "Run Message Loop");
    main_message_loop.Run();
  }

  child_thread->StopWatchdog();

  return 0;
}

namespace {

void CreateDummyGlContext() {
  scoped_refptr<gfx::GLSurface> surface(
      gfx::GLSurface::CreateOffscreenGLSurface(false, gfx::Size(1, 1)));
  if (!surface.get()) {
    VLOG(1) << "gfx::GLSurface::CreateOffscreenGLSurface failed";
    return;
  }

  // On Linux, this is needed to make sure /dev/nvidiactl has
  // been opened and its descriptor cached.
  scoped_refptr<gfx::GLContext> context(
      gfx::GLContext::CreateGLContext(NULL,
                                      surface,
                                      gfx::PreferDiscreteGpu));
  if (!context.get()) {
    VLOG(1) << "gfx::GLContext::CreateGLContext failed";
    return;
  }

  // Similarly, this is needed for /dev/nvidia0.
  if (context->MakeCurrent(surface)) {
    context->ReleaseCurrent(surface.get());
  } else {
    VLOG(1)  << "gfx::GLContext::MakeCurrent failed";
  }
}

void WarmUpSandbox(const content::GPUInfo& gpu_info,
                   bool should_initialize_gl_context) {
  {
    TRACE_EVENT0("gpu", "Warm up rand");
    // Warm up the random subsystem, which needs to be done pre-sandbox on all
    // platforms.
    (void) base::RandUint64();
  }
  {
    TRACE_EVENT0("gpu", "Warm up HMAC");
    // Warm up the crypto subsystem, which needs to done pre-sandbox on all
    // platforms.
    crypto::HMAC hmac(crypto::HMAC::SHA256);
    unsigned char key = '\0';
    bool ret = hmac.Init(&key, sizeof(key));
    (void) ret;
  }

#if defined(OS_CHROMEOS) && defined(ARCH_CPU_ARMEL)
  OmxVideoDecodeAccelerator::PreSandboxInitialization();
#elif defined(OS_CHROMEOS) && defined(ARCH_CPU_X86_FAMILY)
  VaapiVideoDecodeAccelerator::PreSandboxInitialization();
#endif

#if defined(OS_LINUX)
  if (gpu_info.gpu.vendor_id == 0x10de &&  // NVIDIA
      gpu_info.driver_vendor == "NVIDIA" &&
      should_initialize_gl_context) {
    // We need this on Nvidia to pre-open /dev/nvidiactl and /dev/nvidia0.
    CreateDummyGlContext();
  }
#endif

  {
    TRACE_EVENT0("gpu", "Initialize COM");
    base::win::ScopedCOMInitializer com_initializer;
  }

#if defined(OS_WIN)
  {
    TRACE_EVENT0("gpu", "Preload setupapi.dll");
    // Preload this DLL because the sandbox prevents it from loading.
    LoadLibrary(L"setupapi.dll");
  }

  {
    TRACE_EVENT0("gpu", "Initialize DXVA");
    // Initialize H/W video decoding stuff which fails in the sandbox.
    DXVAVideoDecodeAccelerator::PreSandboxInitialization();
  }
#endif
}

void CollectGraphicsInfo(content::GPUInfo* gpu_info) {
  if (!gpu_info_collector::CollectGraphicsInfo(gpu_info))
    VLOG(1) << "gpu_info_collector::CollectGraphicsInfo failed";
  content::GetContentClient()->SetGpuInfo(*gpu_info);
}

}  // namespace.

