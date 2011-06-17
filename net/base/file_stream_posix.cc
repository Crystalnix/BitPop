// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For 64-bit file access (off_t = off64_t, lseek64, etc).
#define _FILE_OFFSET_BITS 64

#include "net/base/file_stream.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/eintr_wrapper.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "base/task.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/worker_pool.h"
#include "base/synchronization/waitable_event.h"
#include "net/base/net_errors.h"

namespace net {

// We cast back and forth, so make sure it's the size we're expecting.
COMPILE_ASSERT(sizeof(int64) == sizeof(off_t), off_t_64_bit);

// Make sure our Whence mappings match the system headers.
COMPILE_ASSERT(FROM_BEGIN   == SEEK_SET &&
               FROM_CURRENT == SEEK_CUR &&
               FROM_END     == SEEK_END, whence_matches_system);

namespace {

// Map from errno to net error codes.
int64 MapErrorCode(int err) {
  switch (err) {
    case ENOENT:
      return ERR_FILE_NOT_FOUND;
    case EACCES:
      return ERR_ACCESS_DENIED;
    default:
      LOG(WARNING) << "Unknown error " << err << " mapped to net::ERR_FAILED";
      return ERR_FAILED;
  }
}

// ReadFile() is a simple wrapper around read() that handles EINTR signals and
// calls MapErrorCode() to map errno to net error codes.
int ReadFile(base::PlatformFile file, char* buf, int buf_len) {
  base::ThreadRestrictions::AssertIOAllowed();
  // read(..., 0) returns 0 to indicate end-of-file.

  // Loop in the case of getting interrupted by a signal.
  ssize_t res = HANDLE_EINTR(read(file, buf, static_cast<size_t>(buf_len)));
  if (res == static_cast<ssize_t>(-1))
    return MapErrorCode(errno);
  return static_cast<int>(res);
}

void ReadFileTask(base::PlatformFile file,
                  char* buf,
                  int buf_len,
                  CompletionCallback* callback) {
  callback->Run(ReadFile(file, buf, buf_len));
}

// WriteFile() is a simple wrapper around write() that handles EINTR signals and
// calls MapErrorCode() to map errno to net error codes.  It tries to write to
// completion.
int WriteFile(base::PlatformFile file, const char* buf, int buf_len) {
  base::ThreadRestrictions::AssertIOAllowed();
  ssize_t res = HANDLE_EINTR(write(file, buf, buf_len));
  if (res == -1)
    return MapErrorCode(errno);
  return res;
}

void WriteFileTask(base::PlatformFile file,
                   const char* buf,
                   int buf_len,
                   CompletionCallback* callback) {
  callback->Run(WriteFile(file, buf, buf_len));
}

// FlushFile() is a simple wrapper around fsync() that handles EINTR signals and
// calls MapErrorCode() to map errno to net error codes.  It tries to flush to
// completion.
int FlushFile(base::PlatformFile file) {
  base::ThreadRestrictions::AssertIOAllowed();
  ssize_t res = HANDLE_EINTR(fsync(file));
  if (res == -1)
    return MapErrorCode(errno);
  return res;
}

}  // namespace

// CancelableCallbackTask takes ownership of the Callback.  This task gets
// posted to the MessageLoopForIO instance.
class CancelableCallbackTask : public CancelableTask {
 public:
  explicit CancelableCallbackTask(Callback0::Type* callback)
      : canceled_(false), callback_(callback) {}

  virtual void Run() {
    if (!canceled_)
      callback_->Run();
  }

  virtual void Cancel() {
    canceled_ = true;
  }

 private:
  bool canceled_;
  scoped_ptr<Callback0::Type> callback_;
};

// FileStream::AsyncContext ----------------------------------------------

class FileStream::AsyncContext {
 public:
  AsyncContext();
  ~AsyncContext();

  // These methods post synchronous read() and write() calls to a WorkerThread.
  void InitiateAsyncRead(
      base::PlatformFile file, char* buf, int buf_len,
      CompletionCallback* callback);
  void InitiateAsyncWrite(
      base::PlatformFile file, const char* buf, int buf_len,
      CompletionCallback* callback);

  CompletionCallback* callback() const { return callback_; }

  // Called by the WorkerPool thread executing the IO after the IO completes.
  // This method queues RunAsynchronousCallback() on the MessageLoop and signals
  // |background_io_completed_callback_|, in case the destructor is waiting.  In
  // that case, the destructor will call RunAsynchronousCallback() instead, and
  // cancel |message_loop_task_|.
  // |result| is the result of the Read/Write task.
  void OnBackgroundIOCompleted(int result);

 private:
  // Always called on the IO thread, either directly by a task on the
  // MessageLoop or by ~AsyncContext().
  void RunAsynchronousCallback();

  // The MessageLoopForIO that this AsyncContext is running on.
  MessageLoopForIO* const message_loop_;
  CompletionCallback* callback_;  // The user provided callback.

  // A callback wrapper around OnBackgroundIOCompleted().  Run by the WorkerPool
  // thread doing the background IO on our behalf.
  CompletionCallbackImpl<AsyncContext> background_io_completed_callback_;

  // This is used to synchronize between the AsyncContext destructor (which runs
  // on the IO thread and OnBackgroundIOCompleted() which runs on the WorkerPool
  // thread.
  base::WaitableEvent background_io_completed_;

  // These variables are only valid when background_io_completed is signaled.
  int result_;
  CancelableCallbackTask* message_loop_task_;

  bool is_closing_;

  DISALLOW_COPY_AND_ASSIGN(AsyncContext);
};

FileStream::AsyncContext::AsyncContext()
    : message_loop_(MessageLoopForIO::current()),
      callback_(NULL),
      background_io_completed_callback_(
          this, &AsyncContext::OnBackgroundIOCompleted),
      background_io_completed_(true, false),
      message_loop_task_(NULL),
      is_closing_(false) {}

FileStream::AsyncContext::~AsyncContext() {
  is_closing_ = true;
  if (callback_) {
    // If |callback_| is non-NULL, that implies either the worker thread is
    // still running the IO task, or the completion callback is queued up on the
    // MessageLoopForIO, but AsyncContext() got deleted before then.
    const bool need_to_wait = !background_io_completed_.IsSignaled();
    base::TimeTicks start = base::TimeTicks::Now();
    RunAsynchronousCallback();
    if (need_to_wait) {
      // We want to see if we block the message loop for too long.
      UMA_HISTOGRAM_TIMES("AsyncIO.FileStreamClose",
                          base::TimeTicks::Now() - start);
    }
  }
}

void FileStream::AsyncContext::InitiateAsyncRead(
    base::PlatformFile file, char* buf, int buf_len,
    CompletionCallback* callback) {
  DCHECK(!callback_);
  callback_ = callback;

  base::WorkerPool::PostTask(FROM_HERE,
                             NewRunnableFunction(
                                 &ReadFileTask,
                                 file, buf, buf_len,
                                 &background_io_completed_callback_),
                             true /* task_is_slow */);
}

void FileStream::AsyncContext::InitiateAsyncWrite(
    base::PlatformFile file, const char* buf, int buf_len,
    CompletionCallback* callback) {
  DCHECK(!callback_);
  callback_ = callback;

  base::WorkerPool::PostTask(FROM_HERE,
                             NewRunnableFunction(
                                 &WriteFileTask,
                                 file, buf, buf_len,
                                 &background_io_completed_callback_),
                             true /* task_is_slow */);
}

void FileStream::AsyncContext::OnBackgroundIOCompleted(int result) {
  result_ = result;
  message_loop_task_ = new CancelableCallbackTask(
      NewCallback(this, &AsyncContext::RunAsynchronousCallback));
  message_loop_->PostTask(FROM_HERE, message_loop_task_);
  background_io_completed_.Signal();
}

void FileStream::AsyncContext::RunAsynchronousCallback() {
  // Wait() here ensures that all modifications from the WorkerPool thread are
  // now visible.
  background_io_completed_.Wait();

  // Either we're in the MessageLoop's task, in which case Cancel() doesn't do
  // anything, or we're in ~AsyncContext(), in which case this prevents the call
  // from happening again.  Must do it here after calling Wait().
  message_loop_task_->Cancel();
  message_loop_task_ = NULL;

  if (is_closing_) {
    callback_ = NULL;
    return;
  }

  DCHECK(callback_);
  CompletionCallback* temp = NULL;
  std::swap(temp, callback_);
  background_io_completed_.Reset();
  temp->Run(result_);
}

// FileStream ------------------------------------------------------------

FileStream::FileStream()
    : file_(base::kInvalidPlatformFileValue),
      open_flags_(0),
      auto_closed_(true) {
  DCHECK(!IsOpen());
}

FileStream::FileStream(base::PlatformFile file, int flags)
    : file_(file),
      open_flags_(flags),
      auto_closed_(false) {
  // If the file handle is opened with base::PLATFORM_FILE_ASYNC, we need to
  // make sure we will perform asynchronous File IO to it.
  if (flags & base::PLATFORM_FILE_ASYNC) {
    async_context_.reset(new AsyncContext());
  }
}

FileStream::~FileStream() {
  if (auto_closed_)
    Close();
}

void FileStream::Close() {
  // Abort any existing asynchronous operations.
  async_context_.reset();

  if (file_ != base::kInvalidPlatformFileValue) {
    if (close(file_) != 0) {
      NOTREACHED();
    }
    file_ = base::kInvalidPlatformFileValue;
  }
}

int FileStream::Open(const FilePath& path, int open_flags) {
  if (IsOpen()) {
    DLOG(FATAL) << "File is already open!";
    return ERR_UNEXPECTED;
  }

  open_flags_ = open_flags;
  file_ = base::CreatePlatformFile(path, open_flags_, NULL, NULL);
  if (file_ == base::kInvalidPlatformFileValue) {
    return MapErrorCode(errno);
  }

  if (open_flags_ & base::PLATFORM_FILE_ASYNC) {
    async_context_.reset(new AsyncContext());
  }

  return OK;
}

bool FileStream::IsOpen() const {
  return file_ != base::kInvalidPlatformFileValue;
}

int64 FileStream::Seek(Whence whence, int64 offset) {
  base::ThreadRestrictions::AssertIOAllowed();

  if (!IsOpen())
    return ERR_UNEXPECTED;

  // If we're in async, make sure we don't have a request in flight.
  DCHECK(!async_context_.get() || !async_context_->callback());

  off_t res = lseek(file_, static_cast<off_t>(offset),
                    static_cast<int>(whence));
  if (res == static_cast<off_t>(-1))
    return MapErrorCode(errno);

  return res;
}

int64 FileStream::Available() {
  base::ThreadRestrictions::AssertIOAllowed();

  if (!IsOpen())
    return ERR_UNEXPECTED;

  int64 cur_pos = Seek(FROM_CURRENT, 0);
  if (cur_pos < 0)
    return cur_pos;

  struct stat info;
  if (fstat(file_, &info) != 0)
    return MapErrorCode(errno);

  int64 size = static_cast<int64>(info.st_size);
  DCHECK_GT(size, cur_pos);

  return size - cur_pos;
}

int FileStream::Read(
    char* buf, int buf_len, CompletionCallback* callback) {
  if (!IsOpen())
    return ERR_UNEXPECTED;

  // read(..., 0) will return 0, which indicates end-of-file.
  DCHECK(buf_len > 0);
  DCHECK(open_flags_ & base::PLATFORM_FILE_READ);

  if (async_context_.get()) {
    DCHECK(open_flags_ & base::PLATFORM_FILE_ASYNC);
    // If we're in async, make sure we don't have a request in flight.
    DCHECK(!async_context_->callback());
    async_context_->InitiateAsyncRead(file_, buf, buf_len, callback);
    return ERR_IO_PENDING;
  } else {
    return ReadFile(file_, buf, buf_len);
  }
}

int FileStream::ReadUntilComplete(char *buf, int buf_len) {
  int to_read = buf_len;
  int bytes_total = 0;

  do {
    int bytes_read = Read(buf, to_read, NULL);
    if (bytes_read <= 0) {
      if (bytes_total == 0)
        return bytes_read;

      return bytes_total;
    }

    bytes_total += bytes_read;
    buf += bytes_read;
    to_read -= bytes_read;
  } while (bytes_total < buf_len);

  return bytes_total;
}

int FileStream::Write(
    const char* buf, int buf_len, CompletionCallback* callback) {
  // write(..., 0) will return 0, which indicates end-of-file.
  DCHECK_GT(buf_len, 0);

  if (!IsOpen())
    return ERR_UNEXPECTED;

  if (async_context_.get()) {
    DCHECK(open_flags_ & base::PLATFORM_FILE_ASYNC);
    // If we're in async, make sure we don't have a request in flight.
    DCHECK(!async_context_->callback());
    async_context_->InitiateAsyncWrite(file_, buf, buf_len, callback);
    return ERR_IO_PENDING;
  } else {
    return WriteFile(file_, buf, buf_len);
  }
}

int64 FileStream::Truncate(int64 bytes) {
  base::ThreadRestrictions::AssertIOAllowed();

  if (!IsOpen())
    return ERR_UNEXPECTED;

  // We better be open for reading.
  DCHECK(open_flags_ & base::PLATFORM_FILE_WRITE);

  // Seek to the position to truncate from.
  int64 seek_position = Seek(FROM_BEGIN, bytes);
  if (seek_position != bytes)
    return ERR_UNEXPECTED;

  // And truncate the file.
  int result = ftruncate(file_, bytes);
  return result == 0 ? seek_position : MapErrorCode(errno);
}

int FileStream::Flush() {
  if (!IsOpen())
    return ERR_UNEXPECTED;

  return FlushFile(file_);
}

}  // namespace net
