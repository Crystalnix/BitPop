// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "base/command_line.h"
#include "base/eintr_wrapper.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/time.h"
#include "content/common/sandbox_linux.h"
#include "content/common/seccomp_sandbox.h"
#include "content/common/sandbox_seccomp_bpf_linux.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/sandbox_linux.h"
#include "sandbox/linux/suid/client/setuid_sandbox_client.h"

namespace {

void LogSandboxStarted(const std::string& sandbox_name) {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  const std::string process_type =
      command_line.GetSwitchValueASCII(switches::kProcessType);
  const std::string activated_sandbox =
      "Activated " + sandbox_name + " sandbox for process type: " +
      process_type + ".";
#if defined(OS_CHROMEOS)
  LOG(WARNING) << activated_sandbox;
#else
  VLOG(1) << activated_sandbox;
#endif
}

// Implement the command line enabling logic for seccomp-legacy.
bool IsSeccompLegacyDesired() {
#if defined(SECCOMP_SANDBOX)
#if defined(NDEBUG)
  // Off by default; allow turning on with a switch.
  return CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableSeccompSandbox);
#else
  // On by default; allow turning off with a switch.
  return !CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableSeccompSandbox);
#endif  // NDEBUG
#endif  // SECCOMP_SANDBOX
  return false;
}

// Our "policy" on whether or not to enable seccomp-legacy. Only renderers are
// supported.
bool ShouldEnableSeccompLegacy(const std::string& process_type) {
  if (IsSeccompLegacyDesired() &&
      process_type == switches::kRendererProcess) {
    return true;
  } else {
    return false;
  }
}

}  // namespace

namespace content {

LinuxSandbox::LinuxSandbox()
    : proc_fd_(-1),
      pre_initialized_(false),
      seccomp_legacy_supported_(false),
      seccomp_bpf_supported_(false),
      setuid_sandbox_client_(sandbox::SetuidSandboxClient::Create()) {
  if (setuid_sandbox_client_ == NULL) {
    LOG(FATAL) << "Failed to instantiate the setuid sandbox client.";
  }
}

LinuxSandbox::~LinuxSandbox() {
}

LinuxSandbox* LinuxSandbox::GetInstance() {
  LinuxSandbox* instance = Singleton<LinuxSandbox>::get();
  CHECK(instance);
  return instance;
}

void LinuxSandbox::PreinitializeSandboxBegin() {
  CHECK(!pre_initialized_);
  seccomp_legacy_supported_ = false;
  seccomp_bpf_supported_ = false;
#if defined(SECCOMP_SANDBOX)
  if (IsSeccompLegacyDesired()) {
    proc_fd_ = open("/proc", O_DIRECTORY | O_RDONLY);
    if (proc_fd_ < 0) {
      LOG(ERROR) << "Cannot access \"/proc\". Disabling seccomp-legacy "
                    "sandboxing.";
      // Now is a good time to figure out if we can support seccomp sandboxing
      // at all. We will call SupportsSeccompSandbox again later, when actually
      // enabling it, but we allow the implementation to cache some information.
      // This is the only place where we will log full lack of seccomp-legacy
      // support.
    } else if (!SupportsSeccompSandbox(proc_fd_)) {
      VLOG(1) << "Lacking support for seccomp-legacy sandbox.";
      CHECK_EQ(HANDLE_EINTR(close(proc_fd_)), 0);
      proc_fd_ = -1;
    } else {
      seccomp_legacy_supported_ = true;
    }
  }
#endif  // SECCOMP_SANDBOX
  // Similarly, we "pre-warm" the code that detects supports for seccomp BPF.
  // TODO(jln): Use proc_fd_ here too once we're comfortable it does not create
  // an additional security risk.
  if (SandboxSeccompBpf::IsSeccompBpfDesired()) {
    if (!SandboxSeccompBpf::SupportsSandbox()) {
      VLOG(1) << "Lacking support for seccomp-bpf sandbox.";
    } else {
      seccomp_bpf_supported_ = true;
    }
  }
  pre_initialized_ = true;
}

// Once we finally know our process type, we can cleanup proc_fd_
// or pass it to seccomp-legacy.
void LinuxSandbox::PreinitializeSandboxFinish(
    const std::string& process_type) {
  CHECK(pre_initialized_);
  if (proc_fd_ >= 0) {
    if (ShouldEnableSeccompLegacy(process_type)) {
#if defined(SECCOMP_SANDBOX)
      SeccompSandboxSetProcFd(proc_fd_);
#endif
    } else {
      DCHECK_GE(proc_fd_, 0);
      CHECK_EQ(HANDLE_EINTR(close(proc_fd_)), 0);
    }
    proc_fd_ = -1;
  }
}

void LinuxSandbox::PreinitializeSandbox(const std::string& process_type) {
  PreinitializeSandboxBegin();
  PreinitializeSandboxFinish(process_type);
}

int LinuxSandbox::GetStatus() const {
  CHECK(pre_initialized_);
  int sandbox_flags = 0;
  if (setuid_sandbox_client_->IsSandboxed()) {
    sandbox_flags |= kSandboxLinuxSUID;
    if (setuid_sandbox_client_->IsInNewPIDNamespace())
      sandbox_flags |= kSandboxLinuxPIDNS;
    if (setuid_sandbox_client_->IsInNewNETNamespace())
      sandbox_flags |= kSandboxLinuxNetNS;
  }
  if (seccomp_legacy_supported() &&
      ShouldEnableSeccompLegacy(switches::kRendererProcess)) {
    // We report whether the sandbox will be activated when renderers go
    // through sandbox initialization.
    sandbox_flags |= kSandboxLinuxSeccompLegacy;
  }
  if (seccomp_bpf_supported() &&
      SandboxSeccompBpf::ShouldEnableSeccompBpf(switches::kRendererProcess)) {
    // Same here, what we report is what we will do for the renderer.
    sandbox_flags |= kSandboxLinuxSeccompBpf;
  }
  return sandbox_flags;
}

bool LinuxSandbox::IsSingleThreaded() const {
  // TODO(jln): re-implement this properly and use our proc_fd_ if available.
  // Possibly racy, but it's ok because this is more of a debug check to catch
  // new threaded situations arising during development.
  int num_threads = file_util::CountFilesCreatedAfter(
      FilePath("/proc/self/task"),
      base::Time::UnixEpoch());

  // We pass the test if we don't know ( == 0), because the setuid sandbox
  // will prevent /proc access in some contexts.
  return num_threads == 1 || num_threads == 0;
}

sandbox::SetuidSandboxClient*
    LinuxSandbox::setuid_sandbox_client() const {
  return setuid_sandbox_client_.get();
}

// For seccomp-legacy, we implement the policy inline, here.
bool LinuxSandbox::StartSeccompLegacy(const std::string& process_type) {
  if (!pre_initialized_)
    PreinitializeSandbox(process_type);
  if (seccomp_legacy_supported() && ShouldEnableSeccompLegacy(process_type)) {
    // SupportsSeccompSandbox() returns a cached result, as we already
    // called it earlier in the PreinitializeSandbox(). Thus, it is OK for us
    // to not pass in a file descriptor for "/proc".
#if defined(SECCOMP_SANDBOX)
    if (SupportsSeccompSandbox(-1)) {
      StartSeccompSandbox();
      LogSandboxStarted("seccomp-legacy");
      return true;
    }
#endif
  }
  return false;
}

// For seccomp-bpf, we use the SandboxSeccompBpf class.
bool LinuxSandbox::StartSeccompBpf(const std::string& process_type) {
  if (!pre_initialized_)
    PreinitializeSandbox(process_type);
  bool started_bpf_sandbox = false;
  if (seccomp_bpf_supported())
    started_bpf_sandbox = SandboxSeccompBpf::StartSandbox(process_type);

  if (started_bpf_sandbox)
    LogSandboxStarted("seccomp-bpf");

  return started_bpf_sandbox;
}

bool LinuxSandbox::seccomp_legacy_supported() const {
  CHECK(pre_initialized_);
  return seccomp_legacy_supported_;
}

bool LinuxSandbox::seccomp_bpf_supported() const {
  CHECK(pre_initialized_);
  return seccomp_bpf_supported_;
}

}  // namespace content

