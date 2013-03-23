// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_GPU_DATA_MANAGER_H_
#define CONTENT_PUBLIC_BROWSER_GPU_DATA_MANAGER_H_

#include <list>
#include <string>

#include "base/process.h"
#include "content/common/content_export.h"
#include "content/public/common/gpu_feature_type.h"
#include "content/public/common/gpu_switching_option.h"

class FilePath;
class GURL;

namespace base {
class ListValue;
}

namespace content {

class GpuDataManagerObserver;
struct GPUInfo;

// This class is fully thread-safe.
class GpuDataManager {
 public:
  typedef base::Callback<void(const std::list<base::ProcessHandle>&)>
      GetGpuProcessHandlesCallback;

  // Getter for the singleton.
  CONTENT_EXPORT static GpuDataManager* GetInstance();

  virtual void InitializeForTesting(const std::string& gpu_blacklist_json,
                                    const content::GPUInfo& gpu_info) = 0;

  virtual std::string GetBlacklistVersion() const = 0;

  virtual GpuFeatureType GetBlacklistedFeatures() const = 0;

  virtual GpuSwitchingOption GetGpuSwitchingOption() const = 0;

  // Returns the reasons for the latest run of blacklisting decisions.
  // For the structure of returned value, see documentation for
  // GpuBlacklist::GetBlacklistedReasons().
  // Caller is responsible to release the returned value.
  virtual base::ListValue* GetBlacklistReasons() const = 0;

  virtual GPUInfo GetGPUInfo() const = 0;

  // Retrieves a list of process handles for all gpu processes.
  virtual void GetGpuProcessHandles(
      const GetGpuProcessHandlesCallback& callback) const = 0;

  // This indicator might change because we could collect more GPU info or
  // because the GPU blacklist could be updated.
  // If this returns false, any further GPU access, including launching GPU
  // process, establish GPU channel, and GPU info collection, should be
  // blocked.
  // Can be called on any thread.
  virtual bool GpuAccessAllowed() const = 0;

  // Requests complete GPUinfo if it has not already been requested
  virtual void RequestCompleteGpuInfoIfNeeded() = 0;

  virtual bool IsCompleteGpuInfoAvailable() const = 0;

  // Requests that the GPU process report its current video memory usage stats,
  // which can be retrieved via the GPU data manager's on-update function.
  virtual void RequestVideoMemoryUsageStatsUpdate() const = 0;

  // Returns true if the software rendering should currently be used.
  virtual bool ShouldUseSoftwareRendering() const = 0;

  // Register a path to the SwiftShader software renderer.
  virtual void RegisterSwiftShaderPath(const FilePath& path) = 0;

  virtual void AddLogMessage(
      int level, const std::string& header, const std::string& message) = 0;

  // Returns a new copy of the ListValue.  Caller is responsible to release
  // the returned value.
  virtual base::ListValue* GetLogMessages() const = 0;

  // Registers/unregister |observer|.
  virtual void AddObserver(GpuDataManagerObserver* observer) = 0;
  virtual void RemoveObserver(GpuDataManagerObserver* observer) = 0;

  // Notifies the gpu process about the number of browser windows, so
  // they can be used to determine managed memory allocation.
  virtual void SetWindowCount(uint32 count) = 0;
  virtual uint32 GetWindowCount() const = 0;

  // Allows a given domain previously blocked from accessing 3D APIs
  // to access them again.
  virtual void UnblockDomainFrom3DAPIs(const GURL& url) = 0;
  // Disables domain blocking for 3D APIs. For use only in tests.
  virtual void DisableDomainBlockingFor3DAPIsForTesting() = 0;

  // Disable the gpu process watchdog thread.
  virtual void DisableGpuWatchdog() = 0;

  // Set GL strings. This triggers a re-calculation of GPU blacklist
  // decision.
  virtual void SetGLStrings(const std::string& gl_vendor,
                            const std::string& gl_renderer,
                            const std::string& gl_version) = 0;

  // Obtain collected GL strings.
  virtual void GetGLStrings(std::string* gl_vendor,
                            std::string* gl_renderer,
                            std::string* gl_version) = 0;

 protected:
  virtual ~GpuDataManager() {}
};

};  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_GPU_DATA_MANAGER_H_
