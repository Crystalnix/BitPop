// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#include "base/scoped_temp_dir.h"
#include "googleurl/src/gurl.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_job.h"
#include "net/url_request/url_request_job_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/blob/blob_data.h"
#include "webkit/blob/blob_storage_controller.h"
#include "webkit/blob/blob_url_request_job.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_file_util.h"
#include "webkit/fileapi/file_system_util.h"
#include "webkit/fileapi/local_file_system_operation.h"
#include "webkit/fileapi/local_file_system_test_helper.h"
#include "webkit/fileapi/local_file_util.h"
#include "webkit/quota/quota_manager.h"

using quota::QuotaManager;

namespace fileapi {

namespace {

void AssertStatusEq(base::PlatformFileError expected,
                    base::PlatformFileError actual) {
  ASSERT_EQ(expected, actual);
}

class MockQuotaManager : public QuotaManager {
 public:
  MockQuotaManager(const FilePath& base_dir, int64 quota)
      : QuotaManager(false /* is_incognito */, base_dir,
                     base::MessageLoopProxy::current(),
                     base::MessageLoopProxy::current(),
                     NULL /* special_storage_policy */),
        usage_(0),
        quota_(quota) {}

  virtual void GetUsageAndQuota(
      const GURL& origin, quota::StorageType type,
      const GetUsageAndQuotaCallback& callback) OVERRIDE {
    callback.Run(quota::kQuotaStatusOk, usage_, quota_);
  }

  void set_usage(int64 usage) { usage_ = usage; }
  void set_quota(int64 quota) { quota_ = quota; }

 protected:
  virtual ~MockQuotaManager() {}

 private:
  int64 usage_;
  int64 quota_;
};

}  // namespace

class LocalFileSystemOperationWriteTest
    : public testing::Test,
      public base::SupportsWeakPtr<LocalFileSystemOperationWriteTest> {
 public:
  LocalFileSystemOperationWriteTest()
      : test_helper_(GURL("http://example.com"), kFileSystemTypeTest),
        loop_(MessageLoop::TYPE_IO),
        status_(base::PLATFORM_FILE_OK),
        cancel_status_(base::PLATFORM_FILE_ERROR_FAILED),
        bytes_written_(0),
        complete_(false) {}

  LocalFileSystemOperation* operation();

  base::PlatformFileError status() const { return status_; }
  base::PlatformFileError cancel_status() const { return cancel_status_; }
  void add_bytes_written(int64 bytes, bool complete) {
    bytes_written_ += bytes;
    EXPECT_FALSE(complete_);
    complete_ = complete;
  }
  int64 bytes_written() const { return bytes_written_; }
  bool complete() const { return complete_; }

  virtual void SetUp();
  virtual void TearDown();

 protected:
  FileSystemURL URLForPath(const FilePath& path) const {
    return test_helper_.CreateURL(path);
  }

  // Callback function for recording test results.
  FileSystemOperationInterface::WriteCallback RecordWriteCallback() {
    return base::Bind(&LocalFileSystemOperationWriteTest::DidWrite,
                      AsWeakPtr());
  }

  FileSystemOperationInterface::StatusCallback RecordCancelCallback() {
    return base::Bind(&LocalFileSystemOperationWriteTest::DidCancel,
                      AsWeakPtr());
  }

  void DidWrite(base::PlatformFileError status, int64 bytes, bool complete) {
    if (status == base::PLATFORM_FILE_OK) {
      add_bytes_written(bytes, complete);
      if (complete)
        MessageLoop::current()->Quit();
    } else {
      EXPECT_FALSE(complete_);
      EXPECT_EQ(status_, base::PLATFORM_FILE_OK);
      complete_ = true;
      status_ = status;
      if (MessageLoop::current()->is_running())
        MessageLoop::current()->Quit();
    }
  }

  void DidCancel(base::PlatformFileError status) {
    cancel_status_ = status;
  }

  FileSystemFileUtil* file_util() {
    return test_helper_.file_util();
  }

  scoped_refptr<MockQuotaManager> quota_manager_;
  LocalFileSystemTestOriginHelper test_helper_;

  MessageLoop loop_;

  ScopedTempDir dir_;
  FilePath virtual_path_;

  // For post-operation status.
  base::PlatformFileError status_;
  base::PlatformFileError cancel_status_;
  int64 bytes_written_;
  bool complete_;

  DISALLOW_COPY_AND_ASSIGN(LocalFileSystemOperationWriteTest);
};

namespace {

class TestProtocolHandler : public net::URLRequestJobFactory::ProtocolHandler {
 public:
  explicit TestProtocolHandler(
      webkit_blob::BlobStorageController* blob_storage_controller)
      : blob_storage_controller_(blob_storage_controller) {}

  virtual ~TestProtocolHandler() {}

  virtual net::URLRequestJob* MaybeCreateJob(
      net::URLRequest* request) const OVERRIDE {
    return new webkit_blob::BlobURLRequestJob(
        request,
        blob_storage_controller_->GetBlobDataFromUrl(request->url()),
        base::MessageLoopProxy::current());
  }

 private:
  webkit_blob::BlobStorageController* const blob_storage_controller_;

  DISALLOW_COPY_AND_ASSIGN(TestProtocolHandler);
};

class TestURLRequestContext : public net::URLRequestContext {
 public:
  TestURLRequestContext()
      : blob_storage_controller_(new webkit_blob::BlobStorageController) {
    // Job factory owns the protocol handler.
    job_factory_.SetProtocolHandler(
        "blob", new TestProtocolHandler(blob_storage_controller_.get()));
    set_job_factory(&job_factory_);
  }

  virtual ~TestURLRequestContext() {}

  webkit_blob::BlobStorageController* blob_storage_controller() const {
    return blob_storage_controller_.get();
  }

 private:
  net::URLRequestJobFactory job_factory_;
  scoped_ptr<webkit_blob::BlobStorageController> blob_storage_controller_;

  DISALLOW_COPY_AND_ASSIGN(TestURLRequestContext);
};

}  // namespace (anonymous)

void LocalFileSystemOperationWriteTest::SetUp() {
  ASSERT_TRUE(dir_.CreateUniqueTempDir());
  FilePath base_dir = dir_.path().AppendASCII("filesystem");

  quota_manager_ = new MockQuotaManager(base_dir, 1024);
  test_helper_.SetUp(base_dir,
                     false /* unlimited quota */,
                     quota_manager_->proxy(),
                     NULL);
  virtual_path_ = FilePath(FILE_PATH_LITERAL("temporary file"));

  operation()->CreateFile(
      URLForPath(virtual_path_), true /* exclusive */,
      base::Bind(&AssertStatusEq, base::PLATFORM_FILE_OK));
}

void LocalFileSystemOperationWriteTest::TearDown() {
  quota_manager_ = NULL;
  test_helper_.TearDown();
}

LocalFileSystemOperation* LocalFileSystemOperationWriteTest::operation() {
  return test_helper_.NewOperation();
}

TEST_F(LocalFileSystemOperationWriteTest, TestWriteSuccess) {
  GURL blob_url("blob:success");
  scoped_refptr<webkit_blob::BlobData> blob_data(new webkit_blob::BlobData());
  blob_data->AppendData("Hello, world!\n");

  TestURLRequestContext url_request_context;
  url_request_context.blob_storage_controller()->AddFinishedBlob(
      blob_url, blob_data);

  operation()->Write(&url_request_context, URLForPath(virtual_path_), blob_url,
                     0, RecordWriteCallback());
  MessageLoop::current()->Run();

  url_request_context.blob_storage_controller()->RemoveBlob(blob_url);

  EXPECT_EQ(14, bytes_written());
  EXPECT_EQ(base::PLATFORM_FILE_OK, status());
  EXPECT_TRUE(complete());
}

TEST_F(LocalFileSystemOperationWriteTest, TestWriteZero) {
  GURL blob_url("blob:zero");
  scoped_refptr<webkit_blob::BlobData> blob_data(new webkit_blob::BlobData());

  TestURLRequestContext url_request_context;
  url_request_context.blob_storage_controller()->AddFinishedBlob(
      blob_url, blob_data);

  operation()->Write(&url_request_context, URLForPath(virtual_path_),
                     blob_url, 0, RecordWriteCallback());
  MessageLoop::current()->Run();

  url_request_context.blob_storage_controller()->RemoveBlob(blob_url);

  EXPECT_EQ(0, bytes_written());
  EXPECT_EQ(base::PLATFORM_FILE_OK, status());
  EXPECT_TRUE(complete());
}

TEST_F(LocalFileSystemOperationWriteTest, TestWriteInvalidBlobUrl) {
  TestURLRequestContext url_request_context;

  operation()->Write(&url_request_context, URLForPath(virtual_path_),
      GURL("blob:invalid"), 0, RecordWriteCallback());
  MessageLoop::current()->Run();

  EXPECT_EQ(0, bytes_written());
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_FAILED, status());
  EXPECT_TRUE(complete());
}

TEST_F(LocalFileSystemOperationWriteTest, TestWriteInvalidFile) {
  GURL blob_url("blob:writeinvalidfile");
  scoped_refptr<webkit_blob::BlobData> blob_data(new webkit_blob::BlobData());
  blob_data->AppendData("It\'ll not be written.");

  TestURLRequestContext url_request_context;
  url_request_context.blob_storage_controller()->AddFinishedBlob(
      blob_url, blob_data);

  operation()->Write(&url_request_context,
                     URLForPath(FilePath(FILE_PATH_LITERAL("nonexist"))),
                     blob_url, 0, RecordWriteCallback());
  MessageLoop::current()->Run();

  url_request_context.blob_storage_controller()->RemoveBlob(blob_url);

  EXPECT_EQ(0, bytes_written());
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());
  EXPECT_TRUE(complete());
}

TEST_F(LocalFileSystemOperationWriteTest, TestWriteDir) {
  FilePath virtual_dir_path(FILE_PATH_LITERAL("d"));
  operation()->CreateDirectory(
      URLForPath(virtual_dir_path),
      true /* exclusive */, false /* recursive */,
      base::Bind(&AssertStatusEq, base::PLATFORM_FILE_OK));

  GURL blob_url("blob:writedir");
  scoped_refptr<webkit_blob::BlobData> blob_data(new webkit_blob::BlobData());
  blob_data->AppendData("It\'ll not be written, too.");

  TestURLRequestContext url_request_context;
  url_request_context.blob_storage_controller()->AddFinishedBlob(
      blob_url, blob_data);

  operation()->Write(&url_request_context, URLForPath(virtual_dir_path),
                     blob_url, 0, RecordWriteCallback());
  MessageLoop::current()->Run();

  url_request_context.blob_storage_controller()->RemoveBlob(blob_url);

  EXPECT_EQ(0, bytes_written());
  // TODO(kinuko): This error code is platform- or fileutil- dependent
  // right now.  Make it return PLATFORM_FILE_ERROR_NOT_A_FILE in every case.
  EXPECT_TRUE(status() == base::PLATFORM_FILE_ERROR_NOT_A_FILE ||
              status() == base::PLATFORM_FILE_ERROR_ACCESS_DENIED ||
              status() == base::PLATFORM_FILE_ERROR_FAILED);
  EXPECT_TRUE(complete());
}

TEST_F(LocalFileSystemOperationWriteTest, TestWriteFailureByQuota) {
  GURL blob_url("blob:success");
  scoped_refptr<webkit_blob::BlobData> blob_data(new webkit_blob::BlobData());
  blob_data->AppendData("Hello, world!\n");

  TestURLRequestContext url_request_context;
  url_request_context.blob_storage_controller()->AddFinishedBlob(
      blob_url, blob_data);

  quota_manager_->set_quota(10);
  operation()->Write(&url_request_context, URLForPath(virtual_path_), blob_url,
                     0, RecordWriteCallback());
  MessageLoop::current()->Run();

  url_request_context.blob_storage_controller()->RemoveBlob(blob_url);

  EXPECT_EQ(10, bytes_written());
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NO_SPACE, status());
  EXPECT_TRUE(complete());
}

TEST_F(LocalFileSystemOperationWriteTest, TestImmediateCancelSuccessfulWrite) {
  GURL blob_url("blob:success");
  scoped_refptr<webkit_blob::BlobData> blob_data(new webkit_blob::BlobData());
  blob_data->AppendData("Hello, world!\n");

  TestURLRequestContext url_request_context;
  url_request_context.blob_storage_controller()->AddFinishedBlob(
      blob_url, blob_data);

  FileSystemOperationInterface* write_operation = operation();
  write_operation->Write(&url_request_context, URLForPath(virtual_path_),
                         blob_url, 0, RecordWriteCallback());
  write_operation->Cancel(RecordCancelCallback());
  // We use RunAllPendings() instead of Run() here, because we won't dispatch
  // callbacks after Cancel() is issued (so no chance to Quit) nor do we need
  // to run another write cycle.
  MessageLoop::current()->RunAllPending();

  url_request_context.blob_storage_controller()->RemoveBlob(blob_url);

  // Issued Cancel() before receiving any response from Write(),
  // so nothing should have happen.
  EXPECT_EQ(0, bytes_written());
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_ABORT, status());
  EXPECT_EQ(base::PLATFORM_FILE_OK, cancel_status());
  EXPECT_TRUE(complete());
}

TEST_F(LocalFileSystemOperationWriteTest, TestImmediateCancelFailingWrite) {
  GURL blob_url("blob:writeinvalidfile");
  scoped_refptr<webkit_blob::BlobData> blob_data(new webkit_blob::BlobData());
  blob_data->AppendData("It\'ll not be written.");

  TestURLRequestContext url_request_context;
  url_request_context.blob_storage_controller()->AddFinishedBlob(
      blob_url, blob_data);

  FileSystemOperationInterface* write_operation = operation();
  write_operation->Write(&url_request_context,
                         URLForPath(FilePath(FILE_PATH_LITERAL("nonexist"))),
                         blob_url, 0, RecordWriteCallback());
  write_operation->Cancel(RecordCancelCallback());
  // We use RunAllPendings() instead of Run() here, because we won't dispatch
  // callbacks after Cancel() is issued (so no chance to Quit) nor do we need
  // to run another write cycle.
  MessageLoop::current()->RunAllPending();

  url_request_context.blob_storage_controller()->RemoveBlob(blob_url);

  // Issued Cancel() before receiving any response from Write(),
  // so nothing should have happen.
  EXPECT_EQ(0, bytes_written());
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_ABORT, status());
  EXPECT_EQ(base::PLATFORM_FILE_OK, cancel_status());
  EXPECT_TRUE(complete());
}

// TODO(ericu,dmikurube,kinuko): Add more tests for cancel cases.

}  // namespace fileapi
