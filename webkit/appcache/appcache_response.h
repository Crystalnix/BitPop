// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_APPCACHE_APPCACHE_RESPONSE_H_
#define WEBKIT_APPCACHE_APPCACHE_RESPONSE_H_

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/task.h"
#include "googleurl/src/gurl.h"
#include "net/base/completion_callback.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_response_info.h"
#include "webkit/appcache/appcache_interfaces.h"

namespace net {
class IOBuffer;
}
namespace disk_cache {
class Entry;
};

namespace appcache {

class AppCacheDiskCache;
class AppCacheService;

static const int kUnkownResponseDataSize = -1;

// Response info for a particular response id. Instances are tracked in
// the working set.
class AppCacheResponseInfo
    : public base::RefCounted<AppCacheResponseInfo> {
 public:
  // AppCacheResponseInfo takes ownership of the http_info.
  AppCacheResponseInfo(AppCacheService* service, const GURL& manifest_url,
                       int64 response_id, net::HttpResponseInfo* http_info,
                       int64 response_data_size);

  const GURL& manifest_url() const { return manifest_url_; }
  int64 response_id() const { return response_id_; }
  const net::HttpResponseInfo* http_response_info() const {
    return http_response_info_.get();
  }
  int64 response_data_size() const { return response_data_size_; }

 private:
  friend class base::RefCounted<AppCacheResponseInfo>;
  virtual ~AppCacheResponseInfo();

  const GURL manifest_url_;
  const int64 response_id_;
  const scoped_ptr<net::HttpResponseInfo> http_response_info_;
  const int64 response_data_size_;
  const AppCacheService* service_;
};

// A refcounted wrapper for HttpResponseInfo so we can apply the
// refcounting semantics used with IOBuffer with these structures too.
struct HttpResponseInfoIOBuffer
    : public base::RefCountedThreadSafe<HttpResponseInfoIOBuffer> {
  scoped_ptr<net::HttpResponseInfo> http_info;
  int response_data_size;

  HttpResponseInfoIOBuffer();
  explicit HttpResponseInfoIOBuffer(net::HttpResponseInfo* info);

 private:
  friend class base::RefCountedThreadSafe<HttpResponseInfoIOBuffer>;
  virtual ~HttpResponseInfoIOBuffer();
};

// Common base class for response reader and writer.
class AppCacheResponseIO {
 public:
  virtual ~AppCacheResponseIO();
  int64 response_id() const { return response_id_; }

 protected:
  friend class ScopedRunnableMethodFactory<AppCacheResponseIO>;

  template <class T>
  class EntryCallback : public net::CancelableCompletionCallback<T> {
   public:
    typedef net::CancelableCompletionCallback<T> BaseClass;
    EntryCallback(T* object,  void (T::* method)(int))
        : BaseClass(object, method), entry_ptr_(NULL) {}

    disk_cache::Entry* entry_ptr_;  // Accessed directly.
   private:
    ~EntryCallback() {
      if (entry_ptr_)
        entry_ptr_->Close();
    }
  };

  AppCacheResponseIO(int64 response_id, AppCacheDiskCache* disk_cache);

  virtual void OnIOComplete(int result) = 0;

  bool IsIOPending() { return user_callback_ ? true : false; }
  void ScheduleIOCompletionCallback(int result);
  void InvokeUserCompletionCallback(int result);
  void ReadRaw(int index, int offset, net::IOBuffer* buf, int buf_len);
  void WriteRaw(int index, int offset, net::IOBuffer* buf, int buf_len);

  const int64 response_id_;
  AppCacheDiskCache* disk_cache_;
  disk_cache::Entry* entry_;
  scoped_refptr<HttpResponseInfoIOBuffer> info_buffer_;
  scoped_refptr<net::IOBuffer> buffer_;
  int buffer_len_;
  net::CompletionCallback* user_callback_;

 private:
  void OnRawIOComplete(int result);

  ScopedRunnableMethodFactory<AppCacheResponseIO> method_factory_;
  scoped_refptr<net::CancelableCompletionCallback<AppCacheResponseIO> >
      raw_callback_;
};

// Reads existing response data from storage. If the object is deleted
// and there is a read in progress, the implementation will return
// immediately but will take care of any side effect of cancelling the
// operation.  In other words, instances are safe to delete at will.
class AppCacheResponseReader : public AppCacheResponseIO {
 public:
  virtual ~AppCacheResponseReader();

  // Reads http info from storage. Always returns the result of the read
  // asynchronously through the 'callback'. Returns the number of bytes read
  // or a net:: error code. Guaranteed to not perform partial reads of
  // the info data. The reader acquires a reference to the 'info_buf' until
  // completion at which time the callback is invoked with either a negative
  // error code or the number of bytes read. The 'info_buf' argument should
  // contain a NULL http_info when ReadInfo is called. The 'callback' is a
  // required parameter.
  // Should only be called where there is no Read operation in progress.
  void ReadInfo(HttpResponseInfoIOBuffer* info_buf,
                net::CompletionCallback* callback);

  // Reads data from storage. Always returns the result of the read
  // asynchronously through the 'callback'. Returns the number of bytes read
  // or a net:: error code. EOF is indicated with a return value of zero.
  // The reader acquires a reference to the provided 'buf' until completion
  // at which time the callback is invoked with either a negative error code
  // or the number of bytes read. The 'callback' is a required parameter.
  // Should only be called where there is no Read operation in progress.
  void ReadData(net::IOBuffer* buf, int buf_len,
                net::CompletionCallback* callback);

  // Returns true if there is a read operation, for data or info, pending.
  bool IsReadPending() { return IsIOPending(); }

  // Used to support range requests. If not called, the reader will
  // read the entire response body. If called, this must be called prior
  // to the first call to the ReadData method.
  void SetReadRange(int offset, int length);

 private:
  friend class AppCacheStorageImpl;
  friend class MockAppCacheStorage;

  // Should only be constructed by the storage class.
  AppCacheResponseReader(int64 response_id, AppCacheDiskCache* disk_cache);

  virtual void OnIOComplete(int result);
  void ContinueReadInfo();
  void ContinueReadData();
  void OpenEntryIfNeededAndContinue();
  void OnOpenEntryComplete(int rv);

  int range_offset_;
  int range_length_;
  int read_position_;
  scoped_refptr<EntryCallback<AppCacheResponseReader> > open_callback_;
};

// Writes new response data to storage. If the object is deleted
// and there is a write in progress, the implementation will return
// immediately but will take care of any side effect of cancelling the
// operation. In other words, instances are safe to delete at will.
class AppCacheResponseWriter : public AppCacheResponseIO {
 public:
  virtual ~AppCacheResponseWriter();

  // Writes the http info to storage. Always returns the result of the write
  // asynchronously through the 'callback'. Returns the number of bytes written
  // or a net:: error code. The writer acquires a reference to the 'info_buf'
  // until completion at which time the callback is invoked with either a
  // negative error code or the number of bytes written. The 'callback' is a
  // required parameter. The contents of 'info_buf' are not modified.
  // Should only be called where there is no Write operation in progress.
  void WriteInfo(HttpResponseInfoIOBuffer* info_buf,
                 net::CompletionCallback* callback);

  // Writes data to storage. Always returns the result of the write
  // asynchronously through the 'callback'. Returns the number of bytes written
  // or a net:: error code. Guaranteed to not perform partial writes.
  // The writer acquires a reference to the provided 'buf' until completion at
  // which time the callback is invoked with either a negative error code or
  // the number of bytes written. The 'callback' is a required parameter.
  // The contents of 'buf' are not modified.
  // Should only be called where there is no Write operation in progress.
  void WriteData(net::IOBuffer* buf, int buf_len,
                 net::CompletionCallback* callback);

  // Returns true if there is a write pending.
  bool IsWritePending() { return IsIOPending(); }

  // Returns the amount written, info and data.
  int64 amount_written() { return info_size_ + write_position_; }

 private:
  friend class AppCacheStorageImpl;
  friend class MockAppCacheStorage;

  enum CreationPhase {
    NO_ATTEMPT,
    INITIAL_ATTEMPT,
    DOOM_EXISTING,
    SECOND_ATTEMPT
  };

  // Should only be constructed by the storage class.
  AppCacheResponseWriter(int64 response_id, AppCacheDiskCache* disk_cache);

  virtual void OnIOComplete(int result);
  void ContinueWriteInfo();
  void ContinueWriteData();
  void CreateEntryIfNeededAndContinue();
  void OnCreateEntryComplete(int rv);

  int info_size_;
  int write_position_;
  int write_amount_;
  CreationPhase creation_phase_;
  scoped_refptr<EntryCallback<AppCacheResponseWriter> > create_callback_;
};

}  // namespace appcache

#endif  // WEBKIT_APPCACHE_APPCACHE_RESPONSE_H_

