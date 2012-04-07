// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_FILEAPI_FILE_WRITER_DELEGATE_H_
#define WEBKIT_FILEAPI_FILE_WRITER_DELEGATE_H_

#include "base/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop_proxy.h"
#include "base/platform_file.h"
#include "base/time.h"
#include "net/base/file_stream.h"
#include "net/base/io_buffer.h"
#include "net/url_request/url_request.h"

namespace fileapi {

class FileSystemOperation;
class FileSystemOperationContext;
class FileSystemQuotaUtil;

class FileWriterDelegate : public net::URLRequest::Delegate {
 public:
  FileWriterDelegate(
      FileSystemOperation* write_operation,
      int64 offset,
      scoped_refptr<base::MessageLoopProxy> proxy);
  virtual ~FileWriterDelegate();

  void Start(base::PlatformFile file,
             net::URLRequest* request);
  base::PlatformFile file() {
    return file_;
  }

  virtual void OnReceivedRedirect(net::URLRequest* request,
                                  const GURL& new_url,
                                  bool* defer_redirect) OVERRIDE;
  virtual void OnAuthRequired(net::URLRequest* request,
                              net::AuthChallengeInfo* auth_info) OVERRIDE;
  virtual void OnCertificateRequested(
      net::URLRequest* request,
      net::SSLCertRequestInfo* cert_request_info) OVERRIDE;
  virtual void OnSSLCertificateError(net::URLRequest* request,
                                     const net::SSLInfo& ssl_info,
                                     bool fatal) OVERRIDE;
  virtual void OnResponseStarted(net::URLRequest* request) OVERRIDE;
  virtual void OnReadCompleted(net::URLRequest* request,
                               int bytes_read) OVERRIDE;

 private:
  void OnGetFileInfoAndCallStartUpdate(
      base::PlatformFileError error,
      const base::PlatformFileInfo& file_info);
  void Read();
  void OnDataReceived(int bytes_read);
  void Write();
  void OnDataWritten(int write_response);
  void OnError(base::PlatformFileError error);
  void OnProgress(int bytes_read, bool done);

  FileSystemOperationContext* file_system_operation_context() const;
  FileSystemQuotaUtil* quota_util() const;

  FileSystemOperation* file_system_operation_;
  base::PlatformFile file_;
  int64 size_;
  int64 offset_;
  scoped_refptr<base::MessageLoopProxy> proxy_;
  base::Time last_progress_event_time_;
  int bytes_written_backlog_;
  int bytes_written_;
  int bytes_read_;
  int64 total_bytes_written_;
  int64 allowed_bytes_to_write_;
  scoped_refptr<net::IOBufferWithSize> io_buffer_;
  scoped_ptr<net::FileStream> file_stream_;
  net::URLRequest* request_;
  base::WeakPtrFactory<FileWriterDelegate> weak_factory_;
};

}  // namespace fileapi

#endif  // WEBKIT_FILEAPI_FILE_WRITER_DELEGATE_H_
