// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SANDBOX_BPF_H__
#define SANDBOX_BPF_H__

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
// #include <linux/seccomp.h>
#include <linux/unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#ifndef SECCOMP_BPF_STANDALONE
#include "base/basictypes.h"
#include "base/eintr_wrapper.h"
#include "base/logging.h"
#endif

// The Seccomp2 kernel ABI is not part of older versions of glibc.
// As we can't break compilation with these versions of the library,
// we explicitly define all missing symbols.

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS          38
#define PR_GET_NO_NEW_PRIVS          39
#endif
#ifndef IPC_64
#define IPC_64                   0x0100
#endif
#ifndef SECCOMP_MODE_FILTER
#define SECCOMP_MODE_DISABLED         0
#define SECCOMP_MODE_STRICT           1
#define SECCOMP_MODE_FILTER           2  // User user-supplied filter
#define SECCOMP_RET_KILL    0x00000000U  // Kill the task immediately
#define SECCOMP_RET_TRAP    0x00030000U  // Disallow and force a SIGSYS
#define SECCOMP_RET_ERRNO   0x00050000U  // Returns an errno
#define SECCOMP_RET_TRACE   0x7ff00000U  // Pass to a tracer or disallow
#define SECCOMP_RET_ALLOW   0x7fff0000U  // Allow
#define SECCOMP_RET_INVALID 0x8f8f8f8fU  // Illegal return value
#define SECCOMP_RET_ACTION  0xffff0000U  // Masks for the return value
#define SECCOMP_RET_DATA    0x0000ffffU  //   sections
#endif
#define SECCOMP_DENY_ERRNO  EPERM
#ifndef SYS_SECCOMP
#define SYS_SECCOMP                   1
#endif

// Impose some reasonable maximum BPF program size. Realistically, the
// kernel probably has much lower limits. But by limiting to less than
// 30 bits, we can ease requirements on some of our data types.
#define SECCOMP_MAX_PROGRAM_SIZE (1<<30)

#if defined(__i386__)
#define MIN_SYSCALL  0u
#define MAX_SYSCALL  1024u
#define SECCOMP_ARCH AUDIT_ARCH_I386
#define REG_RESULT   REG_EAX
#define REG_SYSCALL  REG_EAX
#define REG_IP       REG_EIP
#define REG_PARM1    REG_EBX
#define REG_PARM2    REG_ECX
#define REG_PARM3    REG_EDX
#define REG_PARM4    REG_ESI
#define REG_PARM5    REG_EDI
#define REG_PARM6    REG_EBP
#elif defined(__x86_64__)
#define MIN_SYSCALL  0u
#define MAX_SYSCALL  1024u
#define SECCOMP_ARCH AUDIT_ARCH_X86_64
#define REG_RESULT   REG_RAX
#define REG_SYSCALL  REG_RAX
#define REG_IP       REG_RIP
#define REG_PARM1    REG_RDI
#define REG_PARM2    REG_RSI
#define REG_PARM3    REG_RDX
#define REG_PARM4    REG_R10
#define REG_PARM5    REG_R8
#define REG_PARM6    REG_R9
#else
#error Unsupported target platform
#endif

struct arch_seccomp_data {
  int      nr;
  uint32_t arch;
  uint64_t instruction_pointer;
  uint64_t args[6];
};

struct arch_sigsys {
  void         *ip;
  int          nr;
  unsigned int arch;
};

#ifdef SECCOMP_BPF_STANDALONE
#define arraysize(x) sizeof(x)/sizeof(*(x)))
#define HANDLE_EINTR TEMP_FAILURE_RETRY
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName();                                    \
  TypeName(const TypeName&);                     \
  void operator=(const TypeName&)
#endif


namespace playground2 {

class Sandbox {
 public:
  enum SandboxStatus {
    STATUS_UNKNOWN,      // Status prior to calling supportsSeccompSandbox()
    STATUS_UNSUPPORTED,  // The kernel does not appear to support sandboxing
    STATUS_UNAVAILABLE,  // Currently unavailable but might work again later
    STATUS_AVAILABLE,    // Sandboxing is available but not currently active
    STATUS_ENABLED       // The sandbox is now active
  };

  enum {
    SB_INVALID       = -1,
    SB_ALLOWED       = 0x0000,
    SB_INSPECT_ARG_1 = 0x8001,
    SB_INSPECT_ARG_2 = 0x8002,
    SB_INSPECT_ARG_3 = 0x8004,
    SB_INSPECT_ARG_4 = 0x8008,
    SB_INSPECT_ARG_5 = 0x8010,
    SB_INSPECT_ARG_6 = 0x8020
  };

  // TrapFnc is a pointer to a function that handles Seccomp traps in
  // user-space. The seccomp policy can request that a trap handler gets
  // installed; it does so by returning a suitable ErrorCode() from the
  // syscallEvaluator. See the ErrorCode() constructor for how to pass in
  // the function pointer.
  // Please note that TrapFnc is executed from signal context and must be
  // async-signal safe:
  // http://pubs.opengroup.org/onlinepubs/009695399/functions/xsh_chap02_04.html
  typedef intptr_t (*TrapFnc)(const struct arch_seccomp_data& args, void *aux);

  class ErrorCode {
    friend class Sandbox;
  public:
    // We can either wrap a symbolic ErrorCode (i.e. enum values), an errno
    // value (in the range 1..4095), or a pointer to a TrapFnc callback
    // handling a SECCOMP_RET_TRAP trap.
    // All of these different values are stored in the "err_" field. So, code
    // that is using the ErrorCode class typically operates on a single 32bit
    // field.
    // This is not only quiet efficient, it also makes the API really easy to
    // use.
    ErrorCode(int err = SB_INVALID)
        : id_(0),
          fnc_(NULL),
          aux_(NULL) {
      switch (err) {
      case SB_INVALID:
        err_ = SECCOMP_RET_INVALID;
        break;
      case SB_ALLOWED:
        err_ = SECCOMP_RET_ALLOW;
        break;
      case SB_INSPECT_ARG_1...SB_INSPECT_ARG_6:
        die("Not implemented");
        break;
      case 1 ... 4095:
        err_ = SECCOMP_RET_ERRNO + err;
        break;
      default:
        die("Invalid use of ErrorCode object");
      }
    }

    // If we are wrapping a callback, we must assign a unique id. This id is
    // how the kernel tells us which one of our different SECCOMP_RET_TRAP
    // cases has been triggered.
    // The getTrapId() function assigns one unique id (starting at 1) for
    // each distinct pair of TrapFnc and auxiliary data.
    ErrorCode(TrapFnc fnc, const void *aux, int id = 0) :
      id_(id ? id : getTrapId(fnc, aux)),
      fnc_(fnc),
      aux_(const_cast<void *>(aux)),
      err_(SECCOMP_RET_TRAP + id_) {
    }

    // Destructor doesn't need to do anything.
    ~ErrorCode() { }

    // Always return the value that goes into the BPF filter program.
    operator uint32_t() const { return err_; }

  protected:
    // Fields needed for SECCOMP_RET_TRAP callbacks
    int      id_;
    TrapFnc  fnc_;
    void     *aux_;

    // 32bit field used for all possible types of ErrorCode values
    uint32_t err_;
  };

  enum Operation {
    OP_NOP, OP_EQUAL, OP_NOTEQUAL, OP_LESS,
    OP_LESS_EQUAL, OP_GREATER, OP_GREATER_EQUAL,
    OP_HAS_BITS, OP_DOES_NOT_HAVE_BITS
  };

  struct Constraint {
    bool      is32bit;
    Operation op;
    uint32_t  value;
    ErrorCode passed;
    ErrorCode failed;
  };

  typedef ErrorCode (*EvaluateSyscall)(int sysno);
  typedef int       (*EvaluateArguments)(int sysno, int arg,
                                         Constraint *constraint);
  typedef std::vector<std::pair<EvaluateSyscall,EvaluateArguments> >Evaluators;

  // There are a lot of reasons why the Seccomp sandbox might not be available.
  // This could be because the kernel does not support Seccomp mode, or it
  // could be because another sandbox is already active.
  // "proc_fd" should be a file descriptor for "/proc", or -1 if not
  // provided by the caller.
  static SandboxStatus supportsSeccompSandbox(int proc_fd);

  // The sandbox needs to be able to access files in "/proc/self". If this
  // directory is not accessible when "startSandbox()" gets called, the caller
  // can provide an already opened file descriptor by calling "setProcFd()".
  // The sandbox becomes the new owner of this file descriptor and will
  // eventually close it when "startSandbox()" executes.
  static void setProcFd(int proc_fd);

  // The system call evaluator function is called with the system
  // call number. It can decide to allow the system call unconditionally
  // by returning "0"; it can deny the system call unconditionally by
  // returning an appropriate "errno" value; or it can request inspection
  // of system call argument(s) by returning a suitable combination of
  // SB_INSPECT_ARG_x bits.
  // The system argument evaluator is called (if needed) to query additional
  // constraints for the system call arguments. In the vast majority of
  // cases, it will set a "Constraint" that forces a new "errno" value.
  // But for more complex filters, it is possible to return another mask
  // of SB_INSPECT_ARG_x bits.
  static void setSandboxPolicy(EvaluateSyscall syscallEvaluator,
                               EvaluateArguments argumentEvaluator);

  // This is the main public entry point. It finds all system calls that
  // need rewriting, sets up the resources needed by the sandbox, and
  // enters Seccomp mode.
  static void startSandbox();

 protected:
  // Print an error message and terminate the program. Used for fatal errors.
  static void die(const char *msg) __attribute__((noreturn)) {
    if (msg) {
#ifndef SECCOMP_BPF_STANDALONE
      if (!dryRun_) {
        // LOG(FATAL) is not neccessarily async-signal safe. It would be
        // better to always use the code for the SECCOMP_BPF_STANDALONE case.
        // But that prevents the logging and reporting infrastructure from
        // picking up sandbox related crashes.
        // For now, in picking between two evils, we decided in favor of
        // LOG(FATAL). In the long run, we probably want to rewrite this code
        // to be async-signal safe.
        LOG(FATAL) << msg;
      } else
#endif
      {
        // If there is no logging infrastructure in place, we just write error
        // messages to stderr.
        // We also write to stderr, if we are called in a child process from
        // supportsSeccompSandbox(). This makes sure we can actually do the
        // correct logging from the parent process, which is more likely to
        // have access to logging infrastructure.
        if (HANDLE_EINTR(write(2, msg, strlen(msg)))) { }
        if (HANDLE_EINTR(write(2, "\n", 1))) { }
      }
    }
    for (;;) {
      // exit_group() should exit our program. After all, it is defined as a
      // function that doesn't return. But things can theoretically go wrong.
      // Especially, since we are dealing with system call filters. Continuing
      // execution would be very bad in most cases where die() gets called.
      // So, if there is no way for us to ask for the program to exit, the next
      // best thing we can do is to loop indefinitely. Maybe, somebody will
      // notice and file a bug...
      syscall(__NR_exit_group, 1);
      _exit(1);
    }
  }

  // Get a file descriptor pointing to "/proc", if currently available.
  static int getProcFd() { return proc_fd_; }

 private:
  friend class Util;
  friend class Verifier;
  struct Range {
    Range(uint32_t f, uint32_t t, const ErrorCode& e) :
      from(f),
      to(t),
      err(e) {
    }
    uint32_t  from, to;
    ErrorCode err;
  };
  struct FixUp {
    FixUp(unsigned int a, bool j) :
      jt(j), addr(a) { }
    bool     jt:1;
    unsigned addr:31;
  };
  typedef std::vector<Range> Ranges;
  typedef std::map<uint32_t, std::vector<FixUp> > RetInsns;
  typedef std::vector<struct sock_filter> Program;
  typedef std::vector<ErrorCode> Traps;
  typedef std::map<std::pair<TrapFnc, const void *>, int> TrapIds;

  static ErrorCode probeEvaluator(int signo) __attribute__((const));
  static void      probeProcess(void);
  static ErrorCode allowAllEvaluator(int signo);
  static void      tryVsyscallProcess(void);
  static bool      kernelSupportSeccompBPF(int proc_fd);
  static bool      RunFunctionInPolicy(void (*function)(),
                                       EvaluateSyscall syscallEvaluator,
                                       int proc_fd);
  static bool      isSingleThreaded(int proc_fd);
  static bool      disableFilesystem();
  static void      policySanityChecks(EvaluateSyscall syscallEvaluator,
                                      EvaluateArguments argumentEvaluator);
  static void      installFilter();
  static void      findRanges(Ranges *ranges);
  static void      emitJumpStatements(Program *program, RetInsns *rets,
                                      Ranges::const_iterator start,
                                      Ranges::const_iterator stop);
  static void      emitReturnStatements(Program *prog, const RetInsns& rets);
  static void      sigSys(int nr, siginfo_t *info, void *void_context);
  static intptr_t  bpfFailure(const struct arch_seccomp_data& data, void *aux);
  static int       getTrapId(TrapFnc fnc, const void *aux);

  static bool          dryRun_;
  static SandboxStatus status_;
  static int           proc_fd_;
  static Evaluators    evaluators_;
  static Traps         *traps_;
  static TrapIds       trapIds_;
  static ErrorCode     *trapArray_;
  static size_t        trapArraySize_;
  DISALLOW_IMPLICIT_CONSTRUCTORS(Sandbox);
};

}  // namespace

#endif  // SANDBOX_BPF_H__
