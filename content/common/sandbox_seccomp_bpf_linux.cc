// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <asm/unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#include <vector>

#include "base/command_line.h"
#include "base/logging.h"
#include "content/common/sandbox_linux.h"
#include "content/common/sandbox_seccomp_bpf_linux.h"
#include "content/public/common/content_switches.h"

// These are the only architectures supported for now.
#if defined(__i386__) || defined(__x86_64__)
#define SECCOMP_BPF_SANDBOX
#endif

#if defined(SECCOMP_BPF_SANDBOX)
#include "sandbox/linux/seccomp-bpf/sandbox_bpf.h"

// These are fairly new and not defined in all headers yet.
#if defined(__x86_64__)

#ifndef __NR_process_vm_readv
  #define __NR_process_vm_readv 310
#endif

#ifndef __NR_process_vm_writev
  #define __NR_process_vm_writev 311
#endif

#elif defined(__i386__)

#ifndef __NR_process_vm_readv
  #define __NR_process_vm_readv 347
#endif

#ifndef __NR_process_vm_writev
  #define __NR_process_vm_writev 348
#endif

#endif

namespace {

inline bool IsChromeOS() {
#if defined(OS_CHROMEOS)
  return true;
#else
  return false;
#endif
}

void LogSandboxStarted(const std::string& sandbox_name,
                       const std::string& process_type) {
  const std::string activated_sandbox =
      "Activated " + sandbox_name + " sandbox for process type: " +
      process_type + ".";
  if (IsChromeOS()) {
    LOG(WARNING) << activated_sandbox;
  } else {
    VLOG(1) << activated_sandbox;
  }
}

intptr_t CrashSIGSYS_Handler(const struct arch_seccomp_data& args, void* aux) {
  int syscall = args.nr;
  if (syscall >= 1024)
    syscall = 0;
  // Encode 8-bits of the 1st two arguments too, so we can discern which socket
  // type, which fcntl, ... etc., without being likely to hit a mapped
  // address.
  // Do not encode more bits here without thinking about increasing the
  // likelihood of collision with mapped pages.
  syscall |= ((args.args[0] & 0xffUL) << 12);
  syscall |= ((args.args[1] & 0xffUL) << 20);
  // Purposefully dereference the syscall as an address so it'll show up very
  // clearly and easily in crash dumps.
  volatile char* addr = reinterpret_cast<volatile char*>(syscall);
  *addr = '\0';
  // In case we hit a mapped address, hit the null page with just the syscall,
  // for paranoia.
  syscall &= 0xfffUL;
  addr = reinterpret_cast<volatile char*>(syscall);
  *addr = '\0';
  for (;;)
    _exit(1);
}

// TODO(jln) we need to restrict the first parameter!
bool IsKillSyscall(int sysno) {
  switch (sysno) {
    case __NR_kill:
    case __NR_tkill:
    case __NR_tgkill:
      return true;
    default:
      return false;
  }
}

bool IsGettimeSyscall(int sysno) {
  switch (sysno) {
    case __NR_clock_gettime:
    case __NR_gettimeofday:
    case __NR_time:
      return true;
    default:
      return false;
  }
}

bool IsFileSystemSyscall(int sysno) {
  switch (sysno) {
    case __NR_open:
    case __NR_openat:
    case __NR_execve:
    case __NR_access:
    case __NR_mkdir:
    case __NR_mkdirat:
    case __NR_readlink:
    case __NR_readlinkat:
    case __NR_stat:
    case __NR_lstat:
    case __NR_chdir:
    case __NR_mknod:
    case __NR_mknodat:
      return true;
    default:
      return false;
  }
}

bool IsAcceleratedVideoDecodeEnabled() {
  // Accelerated video decode is currently enabled on Chrome OS,
  // but not on Linux: crbug.com/137247.
  bool is_enabled = IsChromeOS();

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  is_enabled = is_enabled &&
      !command_line.HasSwitch(switches::kDisableAcceleratedVideoDecode);

  return is_enabled;
}

static const char kDriRcPath[] = "/etc/drirc";

// TODO(jorgelo): limited to /etc/drirc for now, extend this to cover
// other sandboxed file access cases.
int OpenWithCache(const char* pathname, int flags) {
  static int drircfd = -1;
  static bool do_open = true;
  int res = -1;

  if (strcmp(pathname, kDriRcPath) == 0 && flags == O_RDONLY) {
    if (do_open) {
      drircfd = open(pathname, flags);
      do_open = false;
      res = drircfd;
    } else {
      // dup() man page:
      // "After a successful return from one of these system calls,
      // the old and new file descriptors may be used interchangeably.
      // They refer to the same open file description and thus share
      // file offset and file status flags; for example, if the file offset
      // is modified by using lseek(2) on one of the descriptors,
      // the offset is also changed for the other."
      // Since |drircfd| can be dup()'ed and read many times, we need to
      // lseek() it to the beginning of the file before returning.
      // We assume the caller will not keep more than one fd open at any
      // one time. Intel driver code in Mesa that parses /etc/drirc does
      // open()/read()/close() in the same function.
      if (drircfd < 0) {
        errno = ENOENT;
        return -1;
      }
      int newfd = dup(drircfd);
      if (newfd < 0) {
        errno = ENOMEM;
        return -1;
      }
      if (lseek(newfd, 0, SEEK_SET) == static_cast<off_t>(-1)) {
        (void) HANDLE_EINTR(close(newfd));
        errno = ENOMEM;
        return -1;
      }
      res = newfd;
    }
  } else {
    res = open(pathname, flags);
  }

  return res;
}

// We allow the GPU process to open /etc/drirc because it's needed by Mesa.
// OpenWithCache() has been called before enabling the sandbox, and has cached
// a file descriptor for /etc/drirc.
intptr_t GpuOpenSIGSYS_Handler(const struct arch_seccomp_data& args,
                               void* aux) {
  uint64_t arg0 = args.args[0];
  uint64_t arg1 = args.args[1];
  const char* pathname = reinterpret_cast<const char*>(arg0);
  int flags = static_cast<int>(arg1);

  if (strcmp(pathname, kDriRcPath) == 0) {
    int ret = OpenWithCache(pathname, flags);
    return (ret == -1) ? -errno : ret;
  } else {
    return -ENOENT;
  }
}

#if defined(__x86_64__)
// x86_64 only because it references system calls that are multiplexed on IA32.
playground2::Sandbox::ErrorCode GpuProcessPolicy_x86_64(int sysno) {
  switch(sysno) {
    case __NR_read:
    case __NR_ioctl:
    case __NR_poll:
    case __NR_epoll_wait:
    case __NR_recvfrom:
    case __NR_write:
    case __NR_writev:
    case __NR_gettid:
    case __NR_sched_yield:  // Nvidia binary driver.

    case __NR_futex:
    case __NR_madvise:
    case __NR_sendmsg:
    case __NR_recvmsg:
    case __NR_eventfd2:
    case __NR_pipe:
    case __NR_mmap:
    case __NR_mprotect:
    case __NR_clone:  // TODO(jln) restrict flags.
    case __NR_set_robust_list:
    case __NR_getuid:
    case __NR_geteuid:
    case __NR_getgid:
    case __NR_getegid:
    case __NR_epoll_create:
    case __NR_fcntl:
    case __NR_socketpair:
    case __NR_epoll_ctl:
    case __NR_prctl:
    case __NR_fstat:
    case __NR_close:
    case __NR_restart_syscall:
    case __NR_rt_sigreturn:
    case __NR_brk:
    case __NR_rt_sigprocmask:
    case __NR_munmap:
    case __NR_dup:
    case __NR_mlock:
    case __NR_munlock:
    case __NR_exit:
    case __NR_exit_group:
    case __NR_lseek:
    case __NR_getpid:  // Nvidia binary driver.
    case __NR_getppid:  // ATI binary driver.
    case __NR_shutdown:  // Virtual driver.
    case __NR_rt_sigaction:  // Breakpad signal handler.
      return playground2::Sandbox::SB_ALLOWED;
    case __NR_socket:
      return EACCES;  // Nvidia binary driver.
    case __NR_fchmod:
      return EPERM;  // ATI binary driver.
    case __NR_open:
      // Accelerated video decode is enabled by default only on Chrome OS.
      if (IsAcceleratedVideoDecodeEnabled()) {
        // Accelerated video decode needs to open /dev/dri/card0, and
        // dup()'ing an already open file descriptor does not work.
        // Allow open() even though it severely weakens the sandbox,
        // to test the sandboxing mechanism in general.
        // TODO(jorgelo): remove this once we solve the libva issue.
        return playground2::Sandbox::SB_ALLOWED;
      } else {
        // Hook open() in the GPU process to allow opening /etc/drirc,
        // needed by Mesa.
        // The hook needs dup(), lseek(), and close() to be allowed.
        return playground2::Sandbox::ErrorCode(GpuOpenSIGSYS_Handler, NULL);
      }
    default:
      if (IsGettimeSyscall(sysno) ||
          IsKillSyscall(sysno)) { // GPU watchdog.
        return playground2::Sandbox::SB_ALLOWED;
      }
      // Generally, filename-based syscalls will fail with ENOENT to behave
      // similarly to a possible future setuid sandbox.
      if (IsFileSystemSyscall(sysno)) {
        return ENOENT;
      }
      // In any other case crash the program with our SIGSYS handler
      return playground2::Sandbox::ErrorCode(CrashSIGSYS_Handler, NULL);
  }
}

// x86_64 only because it references system calls that are multiplexed on IA32.
playground2::Sandbox::ErrorCode FlashProcessPolicy_x86_64(int sysno) {
  switch (sysno) {
    case __NR_futex:
    case __NR_write:
    case __NR_epoll_wait:
    case __NR_read:
    case __NR_times:
    case __NR_clone:  // TODO(jln): restrict flags.
    case __NR_set_robust_list:
    case __NR_getuid:
    case __NR_geteuid:
    case __NR_getgid:
    case __NR_getegid:
    case __NR_epoll_create:
    case __NR_fcntl:
    case __NR_socketpair:
    case __NR_pipe:
    case __NR_epoll_ctl:
    case __NR_gettid:
    case __NR_prctl:
    case __NR_fstat:
    case __NR_sendmsg:
    case __NR_mmap:
    case __NR_munmap:
    case __NR_mprotect:
    case __NR_madvise:
    case __NR_rt_sigaction:
    case __NR_rt_sigprocmask:
    case __NR_wait4:
    case __NR_exit_group:
    case __NR_exit:
    case __NR_rt_sigreturn:
    case __NR_restart_syscall:
    case __NR_close:
    case __NR_recvmsg:
    case __NR_lseek:
    case __NR_brk:
    case __NR_sched_yield:
    case __NR_shutdown:
    case __NR_sched_getaffinity:
    case __NR_sched_setscheduler:
    case __NR_dup:  // Flash Access.
    // These are under investigation, and hopefully not here for the long term.
    case __NR_shmctl:
    case __NR_shmat:
    case __NR_shmdt:
      return playground2::Sandbox::SB_ALLOWED;
    case __NR_ioctl:
      return ENOTTY;  // Flash Access.
    case __NR_socket:
      return EACCES;
    default:
      if (IsGettimeSyscall(sysno) ||
          IsKillSyscall(sysno)) {
        return playground2::Sandbox::SB_ALLOWED;
      }
      if (IsFileSystemSyscall(sysno)) {
        return ENOENT;
      }
      // In any other case crash the program with our SIGSYS handler.
      return playground2::Sandbox::ErrorCode(CrashSIGSYS_Handler, NULL);
  }
}
#endif

playground2::Sandbox::ErrorCode BlacklistPtracePolicy(int sysno) {
  if (sysno < static_cast<int>(MIN_SYSCALL) ||
      sysno > static_cast<int>(MAX_SYSCALL)) {
    // TODO(jln) we should not have to do that in a trivial policy.
    return ENOSYS;
  }
  switch (sysno) {
    case __NR_ptrace:
    case __NR_process_vm_readv:
    case __NR_process_vm_writev:
    case __NR_migrate_pages:
    case __NR_move_pages:
      return playground2::Sandbox::ErrorCode(CrashSIGSYS_Handler, NULL);
    default:
      return playground2::Sandbox::SB_ALLOWED;
  }
}

// Allow all syscalls.
// This will still deny x32 or IA32 calls in 64 bits mode or
// 64 bits system calls in compatibility mode.
playground2::Sandbox::ErrorCode AllowAllPolicy(int sysno) {
  if (sysno < static_cast<int>(MIN_SYSCALL) ||
      sysno > static_cast<int>(MAX_SYSCALL)) {
    // TODO(jln) we should not have to do that in a trivial policy.
    return ENOSYS;
  } else {
    return playground2::Sandbox::SB_ALLOWED;
  }
}

// Warms up/preloads resources needed by the policies.
void WarmupPolicy(playground2::Sandbox::EvaluateSyscall policy) {
#if defined(__x86_64__)
  if (policy == GpuProcessPolicy_x86_64) {
    OpenWithCache(kDriRcPath, O_RDONLY);
    // Accelerated video decode dlopen()'s this shared object
    // inside the sandbox, so preload it now.
    // TODO(jorgelo): generalize this to other platforms.
    if (IsAcceleratedVideoDecodeEnabled()) {
      const char kI965DrvVideoPath_64[] =
          "/usr/lib64/va/drivers/i965_drv_video.so";
      dlopen(kI965DrvVideoPath_64, RTLD_NOW|RTLD_GLOBAL|RTLD_NODELETE);
    }
  }
#endif
}

// Is the sandbox fully disabled for this process?
bool ShouldDisableBpfSandbox(const CommandLine& command_line,
                             const std::string& process_type) {
  if (process_type == switches::kGpuProcess) {
    // The GPU sandbox is disabled by default in ChromeOS, enabled by default on
    // generic Linux.
    // TODO(jorgelo): when we feel comfortable, make this a policy decision
    // instead. (i.e. move this to GetProcessSyscallPolicy) and return an
    // AllowAllPolicy for lack of "--enable-gpu-sandbox".
    bool should_disable;
    if (IsChromeOS()) {
      should_disable = true;
    } else {
      should_disable = false;
    }

    if (command_line.HasSwitch(switches::kEnableGpuSandbox))
      should_disable = false;
    if (command_line.HasSwitch(switches::kDisableGpuSandbox))
      should_disable = true;
    return should_disable;
  }

  return false;
}

playground2::Sandbox::EvaluateSyscall GetProcessSyscallPolicy(
    const CommandLine& command_line,
    const std::string& process_type) {
#if defined(__x86_64__)
  if (process_type == switches::kGpuProcess) {
    return GpuProcessPolicy_x86_64;
  }

  if (process_type == switches::kPpapiPluginProcess) {
    // TODO(jln): figure out what to do with non-Flash PPAPI
    // out-of-process plug-ins.
    return FlashProcessPolicy_x86_64;
  }

  if (process_type == switches::kRendererProcess ||
      process_type == switches::kWorkerProcess) {
    return BlacklistPtracePolicy;
  }
  NOTREACHED();
  // This will be our default if we need one.
  return AllowAllPolicy;
#else
  // On IA32, we only have a small blacklist at the moment.
  (void) process_type;
  return BlacklistPtracePolicy;
#endif  // __x86_64__
}

// Initialize the seccomp-bpf sandbox.
bool StartBpfSandbox_x86(const CommandLine& command_line,
                         const std::string& process_type) {
  playground2::Sandbox::EvaluateSyscall SyscallPolicy =
      GetProcessSyscallPolicy(command_line, process_type);

  // Warms up resources needed by the policy we're about to enable.
  WarmupPolicy(SyscallPolicy);

  playground2::Sandbox::setSandboxPolicy(SyscallPolicy, NULL);
  playground2::Sandbox::startSandbox();

  return true;
}

}  // namespace

#endif  // SECCOMP_BPF_SANDBOX

namespace content {

// Is seccomp BPF globally enabled?
bool SandboxSeccompBpf::IsSeccompBpfDesired() {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(switches::kNoSandbox) &&
      !command_line.HasSwitch(switches::kDisableSeccompFilterSandbox)) {
    return true;
  } else {
    return false;
  }
}

bool SandboxSeccompBpf::ShouldEnableSeccompBpf(
    const std::string& process_type) {
#if defined(SECCOMP_BPF_SANDBOX)
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  return !ShouldDisableBpfSandbox(command_line, process_type);
#endif
  return false;
}

bool SandboxSeccompBpf::SupportsSandbox() {
#if defined(SECCOMP_BPF_SANDBOX)
  // TODO(jln): pass the saved proc_fd_ from the LinuxSandbox singleton
  // here.
  if (playground2::Sandbox::supportsSeccompSandbox(-1) ==
      playground2::Sandbox::STATUS_AVAILABLE) {
    return true;
  }
#endif
  return false;
}

bool SandboxSeccompBpf::StartSandbox(const std::string& process_type) {
#if defined(SECCOMP_BPF_SANDBOX)
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  if (IsSeccompBpfDesired() &&  // Global switches policy.
      // Process-specific policy.
      ShouldEnableSeccompBpf(process_type) &&
      SupportsSandbox()) {
    return StartBpfSandbox_x86(command_line, process_type);
  }
#endif
  return false;
}

}  // namespace content
