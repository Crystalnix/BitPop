// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/gpu/gpu_data_manager_impl.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/metrics/field_trial.h"
#include "base/stringprintf.h"
#include "base/sys_info.h"
#include "base/values.h"
#include "base/version.h"
#include "content/browser/gpu/gpu_process_host.h"
#include "content/common/gpu/gpu_messages.h"
#include "content/gpu/gpu_info_collector.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/gpu_data_manager_observer.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "ui/base/ui_base_switches.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_switches.h"
#include "webkit/plugins/plugin_switches.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

using content::BrowserThread;
using content::GpuDataManagerObserver;
using content::GpuFeatureType;

// static
content::GpuDataManager* content::GpuDataManager::GetInstance() {
  return GpuDataManagerImpl::GetInstance();
}

// static
GpuDataManagerImpl* GpuDataManagerImpl::GetInstance() {
  return Singleton<GpuDataManagerImpl>::get();
}

GpuDataManagerImpl::GpuDataManagerImpl()
    : complete_gpu_info_already_requested_(false),
      complete_gpu_info_available_(false),
      gpu_feature_type_(content::GPU_FEATURE_TYPE_UNKNOWN),
      preliminary_gpu_feature_type_(content::GPU_FEATURE_TYPE_UNKNOWN),
      observer_list_(new GpuDataManagerObserverList),
      software_rendering_(false),
      card_blacklisted_(false) {
  Initialize();
}

void GpuDataManagerImpl::Initialize() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kDisableAcceleratedCompositing)) {
    command_line->AppendSwitch(switches::kDisableAccelerated2dCanvas);
    command_line->AppendSwitch(switches::kDisableAcceleratedLayers);
  }

  if (!command_line->HasSwitch(switches::kSkipGpuDataLoading)) {
    content::GPUInfo gpu_info;
    gpu_info_collector::CollectPreliminaryGraphicsInfo(&gpu_info);
    {
      base::AutoLock auto_lock(gpu_info_lock_);
      gpu_info_ = gpu_info;
    }
  }
  if (command_line->HasSwitch(switches::kDisableGpu))
    BlacklistCard();
}

GpuDataManagerImpl::~GpuDataManagerImpl() {
}

void GpuDataManagerImpl::RequestCompleteGpuInfoIfNeeded() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (complete_gpu_info_already_requested_ || complete_gpu_info_available_)
    return;
  complete_gpu_info_already_requested_ = true;

  GpuProcessHost::SendOnIO(
      GpuProcessHost::GPU_PROCESS_KIND_UNSANDBOXED,
      content::CAUSE_FOR_GPU_LAUNCH_GPUDATAMANAGER_REQUESTCOMPLETEGPUINFOIFNEEDED,
      new GpuMsg_CollectGraphicsInfo());
}

bool GpuDataManagerImpl::IsCompleteGPUInfoAvailable() const {
  return complete_gpu_info_available_;
}

void GpuDataManagerImpl::UpdateGpuInfo(const content::GPUInfo& gpu_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  {
    base::AutoLock auto_lock(gpu_info_lock_);
#if defined(ARCH_CPU_X86_FAMILY)
    if (!gpu_info.gpu.vendor_id || !gpu_info.gpu.device_id)
      gpu_info_.finalized = true;
    else
#endif
      gpu_info_ = gpu_info;
    complete_gpu_info_available_ =
        complete_gpu_info_available_ || gpu_info_.finalized;
    complete_gpu_info_already_requested_ =
        complete_gpu_info_already_requested_ || gpu_info_.finalized;
    content::GetContentClient()->SetGpuInfo(gpu_info_);
  }

  // We have to update GpuFeatureType before notify all the observers.
  NotifyGpuInfoUpdate();
}

content::GPUInfo GpuDataManagerImpl::GetGPUInfo() const {
  return gpu_info_;
}

void GpuDataManagerImpl::AddLogMessage(Value* msg) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  log_messages_.Append(msg);
}

GpuFeatureType GpuDataManagerImpl::GetGpuFeatureType() {
  if (software_rendering_) {
    GpuFeatureType flags;

    // Skia's software rendering is probably more efficient than going through
    // software emulation of the GPU, so use that.
    flags = content::GPU_FEATURE_TYPE_ACCELERATED_2D_CANVAS;
    return flags;
  }

  return gpu_feature_type_;
}

bool GpuDataManagerImpl::GpuAccessAllowed() {
  if (software_rendering_)
    return true;

  if (!gpu_info_.gpu_accessible)
    return false;

  if (card_blacklisted_)
    return false;

  // We only need to block GPU process if more features are disallowed other
  // than those in the preliminary gpu feature flags because the latter work
  // through renderer commandline switches.
  uint32 mask = ~(preliminary_gpu_feature_type_);
  return (gpu_feature_type_ & mask) == 0;
}

void GpuDataManagerImpl::AddObserver(GpuDataManagerObserver* observer) {
  observer_list_->AddObserver(observer);
}

void GpuDataManagerImpl::RemoveObserver(GpuDataManagerObserver* observer) {
  observer_list_->RemoveObserver(observer);
}

void GpuDataManagerImpl::AppendRendererCommandLine(
    CommandLine* command_line) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(command_line);

  uint32 flags = GetGpuFeatureType();
  if ((flags & content::GPU_FEATURE_TYPE_WEBGL)) {
#if !defined(OS_ANDROID)
    if (!command_line->HasSwitch(switches::kDisableExperimentalWebGL))
      command_line->AppendSwitch(switches::kDisableExperimentalWebGL);
#endif
    if (!command_line->HasSwitch(switches::kDisablePepper3dForUntrustedUse))
      command_line->AppendSwitch(switches::kDisablePepper3dForUntrustedUse);
  }
  if ((flags & content::GPU_FEATURE_TYPE_MULTISAMPLING) &&
      !command_line->HasSwitch(switches::kDisableGLMultisampling))
    command_line->AppendSwitch(switches::kDisableGLMultisampling);
  if ((flags & content::GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING) &&
      !command_line->HasSwitch(switches::kDisableAcceleratedCompositing))
    command_line->AppendSwitch(switches::kDisableAcceleratedCompositing);
  if ((flags & content::GPU_FEATURE_TYPE_ACCELERATED_2D_CANVAS) &&
      !command_line->HasSwitch(switches::kDisableAccelerated2dCanvas))
    command_line->AppendSwitch(switches::kDisableAccelerated2dCanvas);
  if (ShouldUseSoftwareRendering())
    command_line->AppendSwitch(switches::kDisableFlashFullscreen3d);
}

void GpuDataManagerImpl::AppendGpuCommandLine(
    CommandLine* command_line) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(command_line);

  std::string use_gl =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(switches::kUseGL);
  FilePath swiftshader_path =
      CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          switches::kSwiftShaderPath);
  uint32 flags = GetGpuFeatureType();
  if ((flags & content::GPU_FEATURE_TYPE_MULTISAMPLING) &&
      !command_line->HasSwitch(switches::kDisableGLMultisampling))
    command_line->AppendSwitch(switches::kDisableGLMultisampling);
  if (flags & content::GPU_FEATURE_TYPE_TEXTURE_SHARING)
    command_line->AppendSwitch(switches::kDisableImageTransportSurface);

  if (software_rendering_) {
    command_line->AppendSwitchASCII(switches::kUseGL, "swiftshader");
    if (swiftshader_path.empty())
      swiftshader_path = swiftshader_path_;
  } else if ((flags & (content::GPU_FEATURE_TYPE_WEBGL |
                content::GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING |
                content::GPU_FEATURE_TYPE_ACCELERATED_2D_CANVAS)) &&
      (use_gl == "any")) {
    command_line->AppendSwitchASCII(
        switches::kUseGL, gfx::kGLImplementationOSMesaName);
  } else if (!use_gl.empty()) {
    command_line->AppendSwitchASCII(switches::kUseGL, use_gl);
  }

  if (!swiftshader_path.empty())
    command_line->AppendSwitchPath(switches::kSwiftShaderPath,
                                   swiftshader_path);

  {
    base::AutoLock auto_lock(gpu_info_lock_);
    if (gpu_info_.optimus)
      command_line->AppendSwitch(switches::kReduceGpuSandbox);
    if (gpu_info_.amd_switchable) {
      // The image transport surface currently doesn't work with AMD Dynamic
      // Switchable graphics.
      command_line->AppendSwitch(switches::kReduceGpuSandbox);
      command_line->AppendSwitch(switches::kDisableImageTransportSurface);
    }
    // Pass GPU and driver information to GPU process. We try to avoid full GPU
    // info collection at GPU process startup, but we need gpu vendor_id,
    // device_id, driver_vendor, driver_version for deciding whether we need to
    // collect full info (on Linux) and for crash reporting purpose.
    command_line->AppendSwitchASCII(switches::kGpuVendorID,
        base::StringPrintf("0x%04x", gpu_info_.gpu.vendor_id));
    command_line->AppendSwitchASCII(switches::kGpuDeviceID,
        base::StringPrintf("0x%04x", gpu_info_.gpu.device_id));
    command_line->AppendSwitchASCII(switches::kGpuDriverVendor,
        gpu_info_.driver_vendor);
    command_line->AppendSwitchASCII(switches::kGpuDriverVersion,
        gpu_info_.driver_version);
  }
}

#if defined(OS_WIN)
bool GpuDataManagerImpl::IsUsingAcceleratedSurface() {
  if (base::win::GetVersion() < base::win::VERSION_VISTA)
    return false;

  base::AutoLock auto_lock(gpu_info_lock_);
  if (gpu_info_.amd_switchable)
    return false;
  if (software_rendering_)
    return false;
  uint32 flags = GetGpuFeatureType();
  if (flags & content::GPU_FEATURE_TYPE_TEXTURE_SHARING)
    return false;

  return true;
}
#endif

void GpuDataManagerImpl::AppendPluginCommandLine(
    CommandLine* command_line) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(command_line);

#if defined(OS_MACOSX)
  uint32 flags = GetGpuFeatureType();
  // TODO(jbauman): Add proper blacklist support for core animation plugins so
  // special-casing this video card won't be necessary. See
  // http://crbug.com/134015
  if ((flags & content::GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING) ||
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableAcceleratedCompositing)) {
    if (!command_line->HasSwitch(
           switches::kDisableCoreAnimationPlugins))
      command_line->AppendSwitch(
          switches::kDisableCoreAnimationPlugins);
  }
#endif
}

void GpuDataManagerImpl::SetGpuFeatureType(GpuFeatureType feature_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  UpdateGpuFeatureType(feature_type);
  preliminary_gpu_feature_type_ = gpu_feature_type_;
}

void GpuDataManagerImpl::NotifyGpuInfoUpdate() {
  observer_list_->Notify(&GpuDataManagerObserver::OnGpuInfoUpdate);
}

// Experiment to determine whether Stage3D should be blacklisted on XP.
bool Stage3DBlacklisted() {
  return base::FieldTrialList::FindFullName(content::kStage3DFieldTrialName) ==
      content::kStage3DFieldTrialBlacklistedName;
}

void GpuDataManagerImpl::UpdateGpuFeatureType(
    GpuFeatureType embedder_feature_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  int flags = embedder_feature_type;

  // Force disable using the GPU for these features, even if they would
  // otherwise be allowed.
  if (card_blacklisted_ ||
      command_line->HasSwitch(switches::kBlacklistAcceleratedCompositing)) {
    flags |= content::GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING;
  }
  if (card_blacklisted_ ||
      command_line->HasSwitch(switches::kBlacklistWebGL)) {
    flags |= content::GPU_FEATURE_TYPE_WEBGL;
  }
  if (Stage3DBlacklisted()) {
    flags |= content::GPU_FEATURE_TYPE_FLASH_STAGE3D;
  }
  gpu_feature_type_ = static_cast<GpuFeatureType>(flags);

  EnableSoftwareRenderingIfNecessary();
}

void GpuDataManagerImpl::RegisterSwiftShaderPath(const FilePath& path) {
  swiftshader_path_ = path;
  EnableSoftwareRenderingIfNecessary();
}

const base::ListValue& GpuDataManagerImpl::GetLogMessages() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return log_messages_;
}

void GpuDataManagerImpl::EnableSoftwareRenderingIfNecessary() {
  if (!GpuAccessAllowed() ||
      (gpu_feature_type_ & content::GPU_FEATURE_TYPE_WEBGL)) {
#if defined(ENABLE_SWIFTSHADER)
    if (!swiftshader_path_.empty() &&
        !CommandLine::ForCurrentProcess()->HasSwitch(
             switches::kDisableSoftwareRasterizer))
      software_rendering_ = true;
#endif
  }
}

bool GpuDataManagerImpl::ShouldUseSoftwareRendering() {
  return software_rendering_;
}

void GpuDataManagerImpl::BlacklistCard() {
  card_blacklisted_ = true;

  gpu_feature_type_ = content::GPU_FEATURE_TYPE_ALL;

  EnableSoftwareRenderingIfNecessary();
  NotifyGpuInfoUpdate();
}

