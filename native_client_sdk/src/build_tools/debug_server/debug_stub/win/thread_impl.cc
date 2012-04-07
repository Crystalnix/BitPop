/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <windows.h>
#include <exception>
#include <stdexcept>

#include "native_client/src/debug_server/port/mutex.h"
#include "native_client/src/debug_server/port/thread.h"

/*
 * Define the OS specific portions of gdb_utils IThread interface.
 */

#define AAA(x) case x: return #x

//0x40010006

  const char* ExceptionCodeStr(int code) {
  switch(code) {
    AAA(EXCEPTION_ACCESS_VIOLATION);
    AAA(EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
    AAA(EXCEPTION_BREAKPOINT);
    AAA(EXCEPTION_DATATYPE_MISALIGNMENT);
    AAA(EXCEPTION_FLT_DENORMAL_OPERAND);
    AAA(EXCEPTION_FLT_DIVIDE_BY_ZERO);
    AAA(EXCEPTION_FLT_INEXACT_RESULT);
    AAA(EXCEPTION_FLT_INVALID_OPERATION);
    AAA(EXCEPTION_FLT_OVERFLOW);
    AAA(EXCEPTION_FLT_STACK_CHECK);
    AAA(EXCEPTION_FLT_UNDERFLOW);
    AAA(EXCEPTION_ILLEGAL_INSTRUCTION);
    AAA(EXCEPTION_IN_PAGE_ERROR);
    AAA(EXCEPTION_INT_DIVIDE_BY_ZERO);
    AAA(EXCEPTION_INT_OVERFLOW);
    AAA(EXCEPTION_INVALID_DISPOSITION);
    AAA(EXCEPTION_NONCONTINUABLE_EXCEPTION);
    AAA(EXCEPTION_PRIV_INSTRUCTION);
    AAA(EXCEPTION_SINGLE_STEP);
    AAA(EXCEPTION_STACK_OVERFLOW);
  }
  return "N/A";
}

namespace port {

static IThread::CatchFunc_t s_CatchFunc = NULL;
static void* s_CatchCookie = NULL;
static PVOID s_OldCatch = NULL;

enum PosixSignals {
  SIGINT  = 2,
  SIGQUIT = 3,
  SIGILL  = 4,
  SIGTRACE= 5,
  SIGBUS  = 7,
  SIGFPE  = 8,
  SIGKILL = 9,
  SIGSEGV = 11,
  SIGSTKFLT = 16,
};


static IMutex* ThreadGetLock() {
  static IMutex* mutex_ = IMutex::Allocate();
  return mutex_;
}

static IThread::ThreadMap_t *ThreadGetMap() {
  static IThread::ThreadMap_t* map_ = new IThread::ThreadMap_t;
  return map_;
}

static int8_t ExceptionToSignal(int ex) {
  switch (ex) {
    case EXCEPTION_GUARD_PAGE:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
      return SIGSEGV;

    case EXCEPTION_BREAKPOINT:
    case EXCEPTION_SINGLE_STEP:
      return SIGTRACE;

    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
      return SIGFPE;

    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
      return SIGILL;

    case EXCEPTION_STACK_OVERFLOW:
      return SIGSTKFLT;

    case CONTROL_C_EXIT:
      return SIGQUIT;

    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
    case EXCEPTION_INVALID_DISPOSITION:
    case EXCEPTION_INVALID_HANDLE:
      return SIGILL;
  }
  return SIGILL;
}


#ifdef _WIN64
static void *GetPointerToRegInCtx(CONTEXT *ctx, int32_t num) {
  switch (num) {
    case 0: return &ctx->Rax;
    case 1: return &ctx->Rbx;
    case 2: return &ctx->Rcx;
    case 3: return &ctx->Rdx;
    case 4: return &ctx->Rsi;
    case 5: return &ctx->Rdi;
    case 6: return &ctx->Rbp;
    case 7: return &ctx->Rsp;
    case 8: return &ctx->R8;
    case 9: return &ctx->R9;
    case 10:return &ctx->R10;
    case 11:return &ctx->R11;
    case 12:return &ctx->R12;
    case 13:return &ctx->R13;
    case 14:return &ctx->R14;
    case 15:return &ctx->R15;
    case 16:return &ctx->Rip;
    case 17:return &ctx->EFlags;
    case 18:return &ctx->SegCs;
    case 19:return &ctx->SegSs;
    case 20:return &ctx->SegDs;
    case 21:return &ctx->SegEs;
    case 22:return &ctx->SegFs;
    case 23:return &ctx->SegGs;
  }

  throw std::out_of_range("Register index out of range.");
}

static int GetSizeofRegInCtx(int32_t num) {
  CONTEXT *ctx = NULL;
  switch (num) {
    case 0: return sizeof ctx->Rax;
    case 1: return sizeof ctx->Rbx;
    case 2: return sizeof ctx->Rcx;
    case 3: return sizeof ctx->Rdx;
    case 4: return sizeof ctx->Rsi;
    case 5: return sizeof ctx->Rdi;
    case 6: return sizeof ctx->Rbp;
    case 7: return sizeof ctx->Rsp;
    case 8: return sizeof ctx->R8;
    case 9: return sizeof ctx->R9;
    case 10:return sizeof ctx->R10;
    case 11:return sizeof ctx->R11;
    case 12:return sizeof ctx->R12;
    case 13:return sizeof ctx->R13;
    case 14:return sizeof ctx->R14;
    case 15:return sizeof ctx->R15;
    case 16:return sizeof ctx->Rip;
    case 17:return sizeof ctx->EFlags;
    case 18:return sizeof ctx->SegCs;
    case 19:return sizeof ctx->SegSs;
    case 20:return sizeof ctx->SegDs;
    case 21:return sizeof ctx->SegEs;
    case 22:return sizeof ctx->SegFs;
    case 23:return sizeof ctx->SegGs;
  }

  throw std::out_of_range("Register index out of range.");
}

#else

static void *GetPointerToRegInCtx(CONTEXT *ctx, int32_t num) {
  switch (num) {
    case 0: return &ctx->Eax;
    case 1: return &ctx->Ecx;
    case 2: return &ctx->Edx;
    case 3: return &ctx->Ebx;
    case 4: return &ctx->Ebp;
    case 5: return &ctx->Esp;
    case 6: return &ctx->Esi;
    case 7: return &ctx->Edi;
    case 8: return &ctx->Eip;
    case 9: return &ctx->EFlags;
    case 10:return &ctx->SegCs;
    case 11:return &ctx->SegSs;
    case 12:return &ctx->SegDs;
    case 13:return &ctx->SegEs;
    case 14:return &ctx->SegFs;
    case 15:return &ctx->SegGs;
  }

  throw std::out_of_range("Register index out of range.");
}

static int GetSizeofRegInCtx(int32_t num) {
  CONTEXT *ctx = NULL;
  switch (num) {
    case 0: return sizeof ctx->Eax;
    case 1: return sizeof ctx->Ecx;
    case 2: return sizeof ctx->Edx;
    case 3: return sizeof ctx->Ebx;
    case 4: return sizeof ctx->Ebp;
    case 5: return sizeof ctx->Esp;
    case 6: return sizeof ctx->Esi;
    case 7: return sizeof ctx->Edi;
    case 8: return sizeof ctx->Eip;
    case 9: return sizeof ctx->EFlags;
    case 10:return sizeof ctx->SegCs;
    case 11:return sizeof ctx->SegSs;
    case 12:return sizeof ctx->SegDs;
    case 13:return sizeof ctx->SegEs;
    case 14:return sizeof ctx->SegFs;
    case 15:return sizeof ctx->SegGs;
  }

  throw std::out_of_range("Register index out of range.");
}
#endif


class Thread : public IThread {
 public:
  explicit Thread(uint32_t id) : ref_(1), id_(id),
                                 handle_(NULL), state_(RUNNING) {
    handle_ = OpenThread(THREAD_ALL_ACCESS, false, id);
    memset(&context_, 0, sizeof(context_));
    context_.ContextFlags = CONTEXT_ALL;
    if (NULL == handle_) state_ = DEAD;
  }

  ~Thread() {
    if (NULL == handle_) return;

    // This should always succeed, so ignore the return.
    (void) CloseHandle(handle_);
  }

  uint32_t GetId() {
    return id_;
  }

  State GetState() {
    return state_;
  }

  virtual bool Suspend() {
    MutexLock lock(ThreadGetLock());
    if (state_ != RUNNING) return false;

    // Attempt to suspend the thread
    DWORD count = SuspendThread(handle_);

    // Ignore result, since there is nothing we can do about
    // it at this point.
    (void) GetThreadContext(handle_, &context_);

    if (count != -1) {
      state_ = SUSPENDED;
      return true;
    }

    return false;
  }

  virtual bool Resume() {
    MutexLock lock(ThreadGetLock());
    if (state_ != SUSPENDED) return false;

    // Ignore result, since there is nothing we can do about
    // it at this point if the set fails.
    (void) SetThreadContext(handle_, &context_);

    // Attempt to resume the thread
    if (ResumeThread(handle_) != -1) {
      state_ = RUNNING;
      return true;
    }

    return false;
  }

  #define TRAP_FLAG (1 << 8)
  virtual bool SetStep(bool on) {
    if ((state_ == RUNNING) || (state_ == DEAD)) return false;

    if (on) {
      context_.EFlags |= TRAP_FLAG;
    } else {
      context_.EFlags &= ~TRAP_FLAG;
    }
    return true;
  }

  virtual bool GetRegister(uint32_t index, void *dst, uint32_t len) {
    uint32_t clen = GetSizeofRegInCtx(index);
    void* src = GetPointerToRegInCtx(&context_, index);

    if ((state_ == RUNNING) || (state_ == DEAD)) return false;

    // TODO(noelallen) we assume big endian
    if (clen < len) len = clen;
    memcpy(dst, src, len);

    return true;
  }

  virtual bool SetRegister(uint32_t index, void* src, uint32_t len) {
    uint32_t clen = GetSizeofRegInCtx(index);
    void* dst = GetPointerToRegInCtx(&context_, index);

    if ((state_ == RUNNING) || (state_ == DEAD) ||
        (state_ != SYSCALL)) return false;

    // TODO(noelallen) we assume big endian
    if (clen < len) len = clen;
    memcpy(dst, src, len);

    return true;
  }

  virtual void* GetContext() { return &context_; }

  //swi_lsp: non-browser app; disable

  static LONG NTAPI ExceptionCatch(PEXCEPTION_POINTERS ep) {
/*
    printf("---> ExceptionCatch(PEXCEPTION_POINTERS: addr=%p code=%d=0x%X=%s [%d-%s]\n",
      ep->ExceptionRecord->ExceptionAddress,
      ep->ExceptionRecord->ExceptionCode,
      ep->ExceptionRecord->ExceptionCode,
      ExceptionCodeStr(ep->ExceptionRecord->ExceptionCode), ep->ExceptionRecord->NumberParameters,
      ep->ExceptionRecord->ExceptionInformation[1]);
*/
    if(DBG_PRINTEXCEPTION_C == ep->ExceptionRecord->ExceptionCode)
      return EXCEPTION_CONTINUE_EXECUTION;

    uint32_t id = static_cast<uint32_t>(GetCurrentThreadId());
    Thread* thread = static_cast<Thread*>(Acquire(id));

    // If we are not tracking this thread, then ignore it
    if (NULL == thread) return EXCEPTION_CONTINUE_SEARCH;

    State old_state = thread->state_;
    thread->state_ = SIGNALED;
    int8_t sig = ExceptionToSignal(ep->ExceptionRecord->ExceptionCode);

    void *ctx = thread->GetContext();

    memcpy(ctx, ep->ContextRecord, sizeof(CONTEXT));
    if (NULL != s_CatchFunc) s_CatchFunc(id, sig, s_CatchCookie);
    memcpy(ep->ContextRecord, ctx, sizeof(CONTEXT));

    thread->state_ = old_state;
    Release(thread);
    return EXCEPTION_CONTINUE_EXECUTION;
  }



 private:
  uint32_t ref_;
  uint32_t id_;
  State  state_;
  HANDLE handle_;
  CONTEXT context_;

  friend class IThread;
};

IThread* IThread::Acquire(uint32_t id, bool create) {
  MutexLock lock(ThreadGetLock());
  Thread* thread;
  ThreadMap_t &map = *ThreadGetMap();

  // Check if we have that thread
  if (map.count(id)) {
    thread = static_cast<Thread*>(map[id]);
    thread->ref_++;
    return thread;
  }

  // If not, can we create it?
  if (create) {
    // If not add it to the map
    thread = new Thread(id);
    if (NULL == thread->handle_) {
      delete thread;
      return NULL;
    }

    map[id] = thread;
    return thread;
  }

  return NULL;
}

void IThread::Release(IThread *ithread) {
  MutexLock lock(ThreadGetLock());
  Thread* thread = static_cast<Thread*>(ithread);
  thread->ref_--;

  if (thread->ref_ == 0) {
    ThreadGetMap()->erase(thread->id_);
    delete static_cast<IThread*>(thread);
  }
}

void IThread::SetExceptionCatch(IThread::CatchFunc_t func, void *cookie) {
  MutexLock lock(ThreadGetLock());

  // Remove our old catch if there is one, this allows us to add again
  if (NULL != s_OldCatch) RemoveVectoredExceptionHandler(s_OldCatch);

  // Add the new one, at the front of the list
  s_OldCatch = AddVectoredExceptionHandler(1, Thread::ExceptionCatch);
  s_CatchFunc = func;
  s_CatchCookie = cookie;
}


}  // End of port namespace

