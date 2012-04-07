// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <set>
#include <string>

#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/platform_file.h"
#include "base/scoped_temp_dir.h"
#include "base/sys_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_operation_context.h"
#include "webkit/fileapi/file_system_test_helper.h"
#include "webkit/fileapi/file_system_usage_cache.h"
#include "webkit/fileapi/mock_file_system_options.h"
#include "webkit/fileapi/obfuscated_file_util.h"
#include "webkit/fileapi/test_file_set.h"
#include "webkit/quota/mock_special_storage_policy.h"
#include "webkit/quota/quota_manager.h"
#include "webkit/quota/quota_types.h"

using namespace fileapi;

namespace {

FilePath UTF8ToFilePath(const std::string& str) {
  FilePath::StringType result;
#if defined(OS_POSIX)
  result = str;
#elif defined(OS_WIN)
  result = base::SysUTF8ToWide(str);
#endif
  return FilePath(result);
}

bool FileExists(const FilePath& path) {
  return file_util::PathExists(path) && !file_util::DirectoryExists(path);
}

int64 GetSize(const FilePath& path) {
  int64 size;
  EXPECT_TRUE(file_util::GetFileSize(path, &size));
  return size;
}

// After a move, the dest exists and the source doesn't.
// After a copy, both source and dest exist.
struct CopyMoveTestCaseRecord {
  bool is_copy_not_move;
  const char source_path[64];
  const char dest_path[64];
  bool cause_overwrite;
};

const CopyMoveTestCaseRecord kCopyMoveTestCases[] = {
  // This is the combinatoric set of:
  //  rename vs. same-name
  //  different directory vs. same directory
  //  overwrite vs. no-overwrite
  //  copy vs. move
  //  We can never be called with source and destination paths identical, so
  //  those cases are omitted.
  {true, "dir0/file0", "dir0/file1", false},
  {false, "dir0/file0", "dir0/file1", false},
  {true, "dir0/file0", "dir0/file1", true},
  {false, "dir0/file0", "dir0/file1", true},

  {true, "dir0/file0", "dir1/file0", false},
  {false, "dir0/file0", "dir1/file0", false},
  {true, "dir0/file0", "dir1/file0", true},
  {false, "dir0/file0", "dir1/file0", true},
  {true, "dir0/file0", "dir1/file1", false},
  {false, "dir0/file0", "dir1/file1", false},
  {true, "dir0/file0", "dir1/file1", true},
  {false, "dir0/file0", "dir1/file1", true},
};

struct OriginEnumerationTestRecord {
  std::string origin_url;
  bool has_temporary;
  bool has_persistent;
};

const OriginEnumerationTestRecord kOriginEnumerationTestRecords[] = {
  {"http://example.com", false, true},
  {"http://example1.com", true, false},
  {"https://example1.com", true, true},
  {"file://", false, true},
  {"http://example.com:8000", false, true},
};

}  // namespace (anonymous)

// TODO(ericu): The vast majority of this and the other FSFU subclass tests
// could theoretically be shared.  It would basically be a FSFU interface
// compliance test, and only the subclass-specific bits that look into the
// implementation would need to be written per-subclass.
class ObfuscatedFileUtilTest : public testing::Test {
 public:
  ObfuscatedFileUtilTest()
      : origin_(GURL("http://www.example.com")),
        type_(kFileSystemTypeTemporary),
        weak_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
        test_helper_(origin_, type_),
        quota_status_(quota::kQuotaStatusUnknown),
        usage_(-1) {
  }

  void SetUp() {
    ASSERT_TRUE(data_dir_.CreateUniqueTempDir());

    scoped_refptr<quota::SpecialStoragePolicy> storage_policy =
        new quota::MockSpecialStoragePolicy();

    quota_manager_ = new quota::QuotaManager(
        false /* is_incognito */,
        data_dir_.path(),
        base::MessageLoopProxy::current(),
        base::MessageLoopProxy::current(),
        storage_policy);

    // Every time we create a new helper, it creates another context, which
    // creates another path manager, another sandbox_mount_point_provider, and
    // another OFU.  We need to pass in the context to skip all that.
    file_system_context_ = new FileSystemContext(
        base::MessageLoopProxy::current(),
        base::MessageLoopProxy::current(),
        storage_policy,
        quota_manager_->proxy(),
        data_dir_.path(),
        CreateAllowFileAccessOptions());

    obfuscated_file_util_ = static_cast<ObfuscatedFileUtil*>(
        file_system_context_->GetFileUtil(type_));

    test_helper_.SetUp(file_system_context_.get(),
                       obfuscated_file_util_.get());
  }

  void TearDown() {
    quota_manager_ = NULL;
    test_helper_.TearDown();
  }

  FileSystemOperationContext* NewContext(FileSystemTestOriginHelper* helper) {
    FileSystemOperationContext* context;
    if (helper)
      context = helper->NewOperationContext();
    else
      context = test_helper_.NewOperationContext();
    context->set_allowed_bytes_growth(1024 * 1024); // Big enough for all tests.
    return context;
  }

  // This can only be used after SetUp has run and created file_system_context_
  // and obfuscated_file_util_.
  // Use this for tests which need to run in multiple origins; we need a test
  // helper per origin.
  FileSystemTestOriginHelper* NewHelper(
      const GURL& origin, fileapi::FileSystemType type) {
    FileSystemTestOriginHelper* helper =
        new FileSystemTestOriginHelper(origin, type);

    helper->SetUp(file_system_context_.get(),
                  obfuscated_file_util_.get());
    return helper;
  }

  ObfuscatedFileUtil* ofu() {
    return obfuscated_file_util_.get();
  }

  const FilePath& test_directory() const {
    return data_dir_.path();
  }

  const GURL& origin() const {
    return origin_;
  }

  fileapi::FileSystemType type() const {
    return type_;
  }

  void GetUsageFromQuotaManager() {
    quota_manager_->GetUsageAndQuota(
      origin(), test_helper_.storage_type(),
      base::Bind(&ObfuscatedFileUtilTest::OnGetUsage,
                 weak_factory_.GetWeakPtr()));
    MessageLoop::current()->RunAllPending();
    EXPECT_EQ(quota::kQuotaStatusOk, quota_status_);
  }

  void RevokeUsageCache() {
    quota_manager_->ResetUsageTracker(test_helper_.storage_type());
    ASSERT_TRUE(test_helper_.RevokeUsageCache());
  }

  int64 SizeInUsageFile() {
    return test_helper_.GetCachedOriginUsage();
  }

  int64 usage() const { return usage_; }

  void OnGetUsage(quota::QuotaStatusCode status, int64 usage, int64 unused) {
    EXPECT_EQ(quota::kQuotaStatusOk, status);
    quota_status_ = status;
    usage_ = usage;
  }

  void CheckFileAndCloseHandle(
      const FilePath& virtual_path, PlatformFile file_handle) {
    scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
    FilePath local_path;
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetLocalFilePath(
        context.get(), virtual_path, &local_path));

    base::PlatformFileInfo file_info0;
    FilePath data_path;
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetFileInfo(
        context.get(), virtual_path, &file_info0, &data_path));
    EXPECT_EQ(data_path, local_path);
    EXPECT_TRUE(FileExists(data_path));
    EXPECT_EQ(0, GetSize(data_path));

    const char data[] = "test data";
    const int length = arraysize(data) - 1;

    if (base::kInvalidPlatformFileValue == file_handle) {
      bool created = true;
      PlatformFileError error;
      file_handle = base::CreatePlatformFile(
          data_path,
          base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_WRITE,
          &created,
          &error);
      ASSERT_NE(base::kInvalidPlatformFileValue, file_handle);
      ASSERT_EQ(base::PLATFORM_FILE_OK, error);
      EXPECT_FALSE(created);
    }
    ASSERT_EQ(length, base::WritePlatformFile(file_handle, 0, data, length));
    EXPECT_TRUE(base::ClosePlatformFile(file_handle));

    base::PlatformFileInfo file_info1;
    EXPECT_EQ(length, GetSize(data_path));
    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetFileInfo(
        context.get(), virtual_path, &file_info1, &data_path));
    EXPECT_EQ(data_path, local_path);

    EXPECT_FALSE(file_info0.is_directory);
    EXPECT_FALSE(file_info1.is_directory);
    EXPECT_FALSE(file_info0.is_symbolic_link);
    EXPECT_FALSE(file_info1.is_symbolic_link);
    EXPECT_EQ(0, file_info0.size);
    EXPECT_EQ(length, file_info1.size);
    EXPECT_LE(file_info0.last_modified, file_info1.last_modified);

    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->Truncate(
        context.get(), virtual_path, length * 2));
    EXPECT_EQ(length * 2, GetSize(data_path));

    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->Truncate(
        context.get(), virtual_path, 0));
    EXPECT_EQ(0, GetSize(data_path));
  }

  void ValidateTestDirectory(
      const FilePath& root_path,
      const std::set<FilePath::StringType>& files,
      const std::set<FilePath::StringType>& directories) {
    scoped_ptr<FileSystemOperationContext> context;
    std::set<FilePath::StringType>::const_iterator iter;
    for (iter = files.begin(); iter != files.end(); ++iter) {
      bool created = true;
      context.reset(NewContext(NULL));
      ASSERT_EQ(base::PLATFORM_FILE_OK,
          ofu()->EnsureFileExists(
              context.get(), root_path.Append(*iter),
              &created));
      ASSERT_FALSE(created);
    }
    for (iter = directories.begin(); iter != directories.end(); ++iter) {
      context.reset(NewContext(NULL));
      EXPECT_TRUE(ofu()->DirectoryExists(context.get(),
          root_path.Append(*iter)));
    }
  }

  void FillTestDirectory(
      const FilePath& root_path,
      std::set<FilePath::StringType>* files,
      std::set<FilePath::StringType>* directories) {
    scoped_ptr<FileSystemOperationContext> context;
    context.reset(NewContext(NULL));
    std::vector<base::FileUtilProxy::Entry> entries;
    EXPECT_EQ(base::PLATFORM_FILE_OK,
        ofu()->ReadDirectory(context.get(), root_path, &entries));
    EXPECT_EQ(0UL, entries.size());

    files->clear();
    files->insert(FILE_PATH_LITERAL("first"));
    files->insert(FILE_PATH_LITERAL("second"));
    files->insert(FILE_PATH_LITERAL("third"));
    directories->clear();
    directories->insert(FILE_PATH_LITERAL("fourth"));
    directories->insert(FILE_PATH_LITERAL("fifth"));
    directories->insert(FILE_PATH_LITERAL("sixth"));
    std::set<FilePath::StringType>::iterator iter;
    for (iter = files->begin(); iter != files->end(); ++iter) {
      bool created = false;
      context.reset(NewContext(NULL));
      ASSERT_EQ(base::PLATFORM_FILE_OK,
          ofu()->EnsureFileExists(
              context.get(), root_path.Append(*iter), &created));
      ASSERT_TRUE(created);
    }
    for (iter = directories->begin(); iter != directories->end(); ++iter) {
      bool exclusive = true;
      bool recursive = false;
      context.reset(NewContext(NULL));
      EXPECT_EQ(base::PLATFORM_FILE_OK,
          ofu()->CreateDirectory(
              context.get(), root_path.Append(*iter), exclusive, recursive));
    }
    ValidateTestDirectory(root_path, *files, *directories);
  }

  void TestReadDirectoryHelper(const FilePath& root_path) {
    std::set<FilePath::StringType> files;
    std::set<FilePath::StringType> directories;
    FillTestDirectory(root_path, &files, &directories);

    scoped_ptr<FileSystemOperationContext> context;
    std::vector<base::FileUtilProxy::Entry> entries;
    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK,
        ofu()->ReadDirectory(context.get(), root_path, &entries));
    std::vector<base::FileUtilProxy::Entry>::iterator entry_iter;
    EXPECT_EQ(files.size() + directories.size(), entries.size());
    for (entry_iter = entries.begin(); entry_iter != entries.end();
        ++entry_iter) {
      const base::FileUtilProxy::Entry& entry = *entry_iter;
      std::set<FilePath::StringType>::iterator iter = files.find(entry.name);
      if (iter != files.end()) {
        EXPECT_FALSE(entry.is_directory);
        files.erase(iter);
        continue;
      }
      iter = directories.find(entry.name);
      EXPECT_FALSE(directories.end() == iter);
      EXPECT_TRUE(entry.is_directory);
      directories.erase(iter);
    }
  }

  void TestTouchHelper(const FilePath& path, bool is_file) {
    base::Time last_access_time = base::Time::Now();
    base::Time last_modified_time = base::Time::Now();

    scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->Touch(
                  context.get(), path, last_access_time, last_modified_time));
    FilePath local_path;
    base::PlatformFileInfo file_info;
    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetFileInfo(
        context.get(), path, &file_info, &local_path));
    // We compare as time_t here to lower our resolution, to avoid false
    // negatives caused by conversion to the local filesystem's native
    // representation and back.
    EXPECT_EQ(file_info.last_modified.ToTimeT(), last_modified_time.ToTimeT());

    context.reset(NewContext(NULL));
    last_modified_time += base::TimeDelta::FromHours(1);
    last_access_time += base::TimeDelta::FromHours(14);
    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->Touch(
                  context.get(), path, last_access_time, last_modified_time));
    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetFileInfo(
        context.get(), path, &file_info, &local_path));
    EXPECT_EQ(file_info.last_modified.ToTimeT(), last_modified_time.ToTimeT());
    if (is_file)  // Directories in OFU don't support atime.
      EXPECT_EQ(file_info.last_accessed.ToTimeT(), last_access_time.ToTimeT());
  }

  void TestCopyInForeignFileHelper(bool overwrite) {
    ScopedTempDir source_dir;
    ASSERT_TRUE(source_dir.CreateUniqueTempDir());
    FilePath root_path = source_dir.path();
    FilePath src_path = root_path.AppendASCII("file_name");
    FilePath dest_path(FILE_PATH_LITERAL("new file"));
    int64 src_file_length = 87;

    base::PlatformFileError error_code;
    bool created = false;
    int file_flags = base::PLATFORM_FILE_CREATE | base::PLATFORM_FILE_WRITE;
    base::PlatformFile file_handle =
        base::CreatePlatformFile(
            src_path, file_flags, &created, &error_code);
    EXPECT_TRUE(created);
    ASSERT_EQ(base::PLATFORM_FILE_OK, error_code);
    ASSERT_NE(base::kInvalidPlatformFileValue, file_handle);
    ASSERT_TRUE(base::TruncatePlatformFile(file_handle, src_file_length));
    EXPECT_TRUE(base::ClosePlatformFile(file_handle));

    scoped_ptr<FileSystemOperationContext> context;

    if (overwrite) {
      context.reset(NewContext(NULL));
      EXPECT_EQ(base::PLATFORM_FILE_OK,
          ofu()->EnsureFileExists(context.get(), dest_path, &created));
      EXPECT_TRUE(created);
    }

    const int64 path_cost =
        ObfuscatedFileUtil::ComputeFilePathCost(dest_path);
    if (!overwrite) {
      // Verify that file creation requires sufficient quota for the path.
      context.reset(NewContext(NULL));
      context->set_allowed_bytes_growth(path_cost + src_file_length - 1);
      EXPECT_EQ(base::PLATFORM_FILE_ERROR_NO_SPACE,
          ofu()->CopyInForeignFile(context.get(), src_path, dest_path));
    }

    context.reset(NewContext(NULL));
    context->set_allowed_bytes_growth(path_cost + src_file_length);
    EXPECT_EQ(base::PLATFORM_FILE_OK,
        ofu()->CopyInForeignFile(context.get(), src_path, dest_path));

    context.reset(NewContext(NULL));
    EXPECT_TRUE(ofu()->PathExists(context.get(), dest_path));
    context.reset(NewContext(NULL));
    EXPECT_FALSE(ofu()->DirectoryExists(context.get(), dest_path));
    context.reset(NewContext(NULL));
    base::PlatformFileInfo file_info;
    FilePath data_path;
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetFileInfo(
        context.get(), dest_path, &file_info, &data_path));
    EXPECT_NE(data_path, src_path);
    EXPECT_TRUE(FileExists(data_path));
    EXPECT_EQ(src_file_length, GetSize(data_path));

    EXPECT_EQ(base::PLATFORM_FILE_OK,
        ofu()->DeleteFile(context.get(), dest_path));
  }

  void ClearTimestamp(const FilePath& path) {
    scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->Touch(context.get(), path, base::Time(), base::Time()));
    EXPECT_EQ(base::Time(), GetModifiedTime(path));
  }

  base::Time GetModifiedTime(const FilePath& path) {
    scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
    FilePath data_path;
    base::PlatformFileInfo file_info;
    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->GetFileInfo(context.get(), path, &file_info, &data_path));
    return file_info.last_modified;
  }

  void TestDirectoryTimestampHelper(const FilePath& base_dir,
                                    bool copy,
                                    bool overwrite) {
    scoped_ptr<FileSystemOperationContext> context;
    const FilePath src_dir_path(base_dir.AppendASCII("foo_dir"));
    const FilePath dest_dir_path(base_dir.AppendASCII("bar_dir"));

    const FilePath src_file_path(src_dir_path.AppendASCII("hoge"));
    const FilePath dest_file_path(dest_dir_path.AppendASCII("fuga"));

    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->CreateDirectory(context.get(), src_dir_path, true, true));
    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->CreateDirectory(context.get(), dest_dir_path, true, true));

    bool created = false;
    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->EnsureFileExists(context.get(), src_file_path, &created));
    if (overwrite) {
      context.reset(NewContext(NULL));
      EXPECT_EQ(base::PLATFORM_FILE_OK,
                ofu()->EnsureFileExists(context.get(),
                                        dest_file_path, &created));
    }

    ClearTimestamp(src_dir_path);
    ClearTimestamp(dest_dir_path);
    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->CopyOrMoveFile(context.get(),
                                    src_file_path, dest_file_path,
                                    copy));

    if (copy)
      EXPECT_EQ(base::Time(), GetModifiedTime(src_dir_path));
    else
      EXPECT_NE(base::Time(), GetModifiedTime(src_dir_path));
    EXPECT_NE(base::Time(), GetModifiedTime(dest_dir_path));
  }

 private:
  ScopedTempDir data_dir_;
  scoped_refptr<ObfuscatedFileUtil> obfuscated_file_util_;
  scoped_refptr<quota::QuotaManager> quota_manager_;
  scoped_refptr<FileSystemContext> file_system_context_;
  GURL origin_;
  fileapi::FileSystemType type_;
  base::WeakPtrFactory<ObfuscatedFileUtilTest> weak_factory_;
  FileSystemTestOriginHelper test_helper_;
  quota::QuotaStatusCode quota_status_;
  int64 usage_;

  DISALLOW_COPY_AND_ASSIGN(ObfuscatedFileUtilTest);
};

TEST_F(ObfuscatedFileUtilTest, TestCreateAndDeleteFile) {
  base::PlatformFile file_handle = base::kInvalidPlatformFileValue;
  bool created;
  FilePath path = UTF8ToFilePath("fake/file");
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
  int file_flags = base::PLATFORM_FILE_CREATE | base::PLATFORM_FILE_WRITE;

  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
            ofu()->CreateOrOpen(
                context.get(), path, file_flags, &file_handle,
                &created));

  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
            ofu()->DeleteFile(context.get(), path));

  path = UTF8ToFilePath("test file");

  // Verify that file creation requires sufficient quota for the path.
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(path) - 1);
  ASSERT_EQ(base::PLATFORM_FILE_ERROR_NO_SPACE,
            ofu()->CreateOrOpen(
                context.get(), path, file_flags, &file_handle, &created));

  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(path));
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateOrOpen(
                context.get(), path, file_flags, &file_handle, &created));
  ASSERT_TRUE(created);
  EXPECT_NE(base::kInvalidPlatformFileValue, file_handle);

  CheckFileAndCloseHandle(path, file_handle);

  context.reset(NewContext(NULL));
  FilePath local_path;
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetLocalFilePath(
      context.get(), path, &local_path));
  EXPECT_TRUE(file_util::PathExists(local_path));

  // Verify that deleting a file isn't stopped by zero quota, and that it frees
  // up quote from its path.
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(0);
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->DeleteFile(context.get(), path));
  EXPECT_FALSE(file_util::PathExists(local_path));
  EXPECT_EQ(ObfuscatedFileUtil::ComputeFilePathCost(path),
      context->allowed_bytes_growth());

  context.reset(NewContext(NULL));
  bool exclusive = true;
  bool recursive = true;
  FilePath directory_path = UTF8ToFilePath("series/of/directories");
  path = directory_path.AppendASCII("file name");
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), directory_path, exclusive, recursive));

  context.reset(NewContext(NULL));
  file_handle = base::kInvalidPlatformFileValue;
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateOrOpen(
                context.get(), path, file_flags, &file_handle, &created));
  ASSERT_TRUE(created);
  EXPECT_NE(base::kInvalidPlatformFileValue, file_handle);

  CheckFileAndCloseHandle(path, file_handle);

  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetLocalFilePath(
      context.get(), path, &local_path));
  EXPECT_TRUE(file_util::PathExists(local_path));

  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->DeleteFile(context.get(), path));
  EXPECT_FALSE(file_util::PathExists(local_path));
}

TEST_F(ObfuscatedFileUtilTest, TestTruncate) {
  bool created = false;
  FilePath path = UTF8ToFilePath("file");
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));

  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
            ofu()->Truncate(context.get(), path, 4));

  context.reset(NewContext(NULL));
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      ofu()->EnsureFileExists(context.get(), path, &created));
  ASSERT_TRUE(created);

  context.reset(NewContext(NULL));
  FilePath local_path;
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetLocalFilePath(
      context.get(), path, &local_path));
  EXPECT_EQ(0, GetSize(local_path));

  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->Truncate(
      context.get(), path, 10));
  EXPECT_EQ(10, GetSize(local_path));

  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->Truncate(
      context.get(), path, 1));
  EXPECT_EQ(1, GetSize(local_path));

  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->DirectoryExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->PathExists(context.get(), path));
}

TEST_F(ObfuscatedFileUtilTest, TestEnsureFileExists) {
  FilePath path = UTF8ToFilePath("fake/file");
  bool created = false;
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
            ofu()->EnsureFileExists(
                context.get(), path, &created));

  // Verify that file creation requires sufficient quota for the path.
  context.reset(NewContext(NULL));
  path = UTF8ToFilePath("test file");
  created = false;
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(path) - 1);
  ASSERT_EQ(base::PLATFORM_FILE_ERROR_NO_SPACE,
            ofu()->EnsureFileExists(context.get(), path, &created));
  ASSERT_FALSE(created);

  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(path));
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), path, &created));
  ASSERT_TRUE(created);

  CheckFileAndCloseHandle(path, base::kInvalidPlatformFileValue);

  context.reset(NewContext(NULL));
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), path, &created));
  ASSERT_FALSE(created);

  // Also test in a subdirectory.
  path = UTF8ToFilePath("path/to/file.txt");
  context.reset(NewContext(NULL));
  bool exclusive = true;
  bool recursive = true;
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), path.DirName(), exclusive, recursive));

  context.reset(NewContext(NULL));
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), path, &created));
  ASSERT_TRUE(created);
  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->DirectoryExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->PathExists(context.get(), path));
}

TEST_F(ObfuscatedFileUtilTest, TestDirectoryOps) {
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));

  bool exclusive = false;
  bool recursive = false;
  FilePath path = UTF8ToFilePath("foo/bar");
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));

  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
      ofu()->DeleteSingleDirectory(context.get(), path));

  FilePath root = UTF8ToFilePath("");
  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->DirectoryExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->PathExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->IsDirectoryEmpty(context.get(), root));

  context.reset(NewContext(NULL));
  exclusive = false;
  recursive = true;
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));

  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->DirectoryExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->PathExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->IsDirectoryEmpty(context.get(), root));
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->DirectoryExists(context.get(), path.DirName()));
  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->IsDirectoryEmpty(context.get(), path.DirName()));

  // Can't remove a non-empty directory.
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_EMPTY,
      ofu()->DeleteSingleDirectory(context.get(), path.DirName()));

  base::PlatformFileInfo file_info;
  FilePath local_path;
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetFileInfo(
      context.get(), path, &file_info, &local_path));
  EXPECT_TRUE(local_path.empty());
  EXPECT_TRUE(file_info.is_directory);
  EXPECT_FALSE(file_info.is_symbolic_link);

  // Same create again should succeed, since exclusive is false.
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));

  exclusive = true;
  recursive = true;
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));

  // Verify that deleting a directory isn't stopped by zero quota, and that it
  // frees up quota from its path.
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(0);
  EXPECT_EQ(base::PLATFORM_FILE_OK,
      ofu()->DeleteSingleDirectory(context.get(), path));
  EXPECT_EQ(ObfuscatedFileUtil::ComputeFilePathCost(path),
      context->allowed_bytes_growth());

  path = UTF8ToFilePath("foo/bop");

  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->DirectoryExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->PathExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->IsDirectoryEmpty(context.get(), path));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, ofu()->GetFileInfo(
      context.get(), path, &file_info, &local_path));

  // Verify that file creation requires sufficient quota for the path.
  exclusive = true;
  recursive = false;
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(path) - 1);
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NO_SPACE, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));

  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(path));
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));

  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->DirectoryExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->PathExists(context.get(), path));

  exclusive = true;
  recursive = false;
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));

  exclusive = true;
  recursive = false;
  path = UTF8ToFilePath("foo");
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));

  path = UTF8ToFilePath("blah");

  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->DirectoryExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->PathExists(context.get(), path));

  exclusive = true;
  recursive = false;
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));

  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->DirectoryExists(context.get(), path));
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->PathExists(context.get(), path));

  exclusive = true;
  recursive = false;
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));
}

TEST_F(ObfuscatedFileUtilTest, TestReadDirectory) {
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
  bool exclusive = true;
  bool recursive = true;
  FilePath path = UTF8ToFilePath("directory/to/use");
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));
  TestReadDirectoryHelper(path);
}

TEST_F(ObfuscatedFileUtilTest, TestReadRootWithSlash) {
  TestReadDirectoryHelper(UTF8ToFilePath(""));
}

TEST_F(ObfuscatedFileUtilTest, TestReadRootWithEmptyString) {
  TestReadDirectoryHelper(UTF8ToFilePath("/"));
}

TEST_F(ObfuscatedFileUtilTest, TestReadDirectoryOnFile) {
  FilePath path = UTF8ToFilePath("file");
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));

  bool created = false;
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      ofu()->EnsureFileExists(context.get(), path, &created));
  ASSERT_TRUE(created);

  context.reset(NewContext(NULL));
  std::vector<base::FileUtilProxy::Entry> entries;
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
      ofu()->ReadDirectory(context.get(), path, &entries));

  EXPECT_TRUE(ofu()->IsDirectoryEmpty(context.get(), path));
}

TEST_F(ObfuscatedFileUtilTest, TestTouch) {
  FilePath path = UTF8ToFilePath("file");
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));

  base::Time last_access_time = base::Time::Now();
  base::Time last_modified_time = base::Time::Now();

  // It's not there yet.
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
            ofu()->Touch(
                context.get(), path, last_access_time, last_modified_time));

  // OK, now create it.
  context.reset(NewContext(NULL));
  bool created = false;
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), path, &created));
  ASSERT_TRUE(created);
  TestTouchHelper(path, true);

  // Now test a directory:
  context.reset(NewContext(NULL));
  bool exclusive = true;
  bool recursive = false;
  path = UTF8ToFilePath("dir");
  ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(context.get(),
      path, exclusive, recursive));
  TestTouchHelper(path, false);
}

TEST_F(ObfuscatedFileUtilTest, TestPathQuotas) {
  FilePath path = UTF8ToFilePath("fake/file");
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));

  path = UTF8ToFilePath("file name");
  context->set_allowed_bytes_growth(5);
  bool created = false;
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NO_SPACE,
      ofu()->EnsureFileExists(context.get(), path, &created));
  EXPECT_FALSE(created);
  context->set_allowed_bytes_growth(1024);
  EXPECT_EQ(base::PLATFORM_FILE_OK,
      ofu()->EnsureFileExists(context.get(), path, &created));
  EXPECT_TRUE(created);
  int64 path_cost = ObfuscatedFileUtil::ComputeFilePathCost(path);
  EXPECT_EQ(1024 - path_cost, context->allowed_bytes_growth());

  context->set_allowed_bytes_growth(1024);
  bool exclusive = true;
  bool recursive = true;
  path = UTF8ToFilePath("directory/to/use");
  std::vector<FilePath::StringType> components;
  path.GetComponents(&components);
  path_cost = 0;
  for (std::vector<FilePath::StringType>::iterator iter = components.begin();
      iter != components.end(); ++iter) {
    path_cost += ObfuscatedFileUtil::ComputeFilePathCost(
        FilePath(*iter));
  }
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(1024);
  EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), path, exclusive, recursive));
  EXPECT_EQ(1024 - path_cost, context->allowed_bytes_growth());
}

TEST_F(ObfuscatedFileUtilTest, TestCopyOrMoveFileNotFound) {
  FilePath source_path = UTF8ToFilePath("path0.txt");
  FilePath dest_path = UTF8ToFilePath("path1.txt");
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));

  bool is_copy_not_move = false;
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
      ofu()->CopyOrMoveFile(context.get(), source_path, dest_path,
          is_copy_not_move));
  context.reset(NewContext(NULL));
  is_copy_not_move = true;
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
      ofu()->CopyOrMoveFile(context.get(), source_path, dest_path,
          is_copy_not_move));
  source_path = UTF8ToFilePath("dir/dir/file");
  bool exclusive = true;
  bool recursive = true;
  context.reset(NewContext(NULL));
  ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), source_path.DirName(), exclusive, recursive));
  is_copy_not_move = false;
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
      ofu()->CopyOrMoveFile(context.get(), source_path, dest_path,
          is_copy_not_move));
  context.reset(NewContext(NULL));
  is_copy_not_move = true;
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
      ofu()->CopyOrMoveFile(context.get(), source_path, dest_path,
          is_copy_not_move));
}

TEST_F(ObfuscatedFileUtilTest, TestCopyOrMoveFileSuccess) {
  const int64 kSourceLength = 5;
  const int64 kDestLength = 50;

  for (size_t i = 0; i < arraysize(kCopyMoveTestCases); ++i) {
    SCOPED_TRACE(testing::Message() << "kCopyMoveTestCase " << i);
    const CopyMoveTestCaseRecord& test_case = kCopyMoveTestCases[i];
    SCOPED_TRACE(testing::Message() << "\t is_copy_not_move " <<
      test_case.is_copy_not_move);
    SCOPED_TRACE(testing::Message() << "\t source_path " <<
      test_case.source_path);
    SCOPED_TRACE(testing::Message() << "\t dest_path " <<
      test_case.dest_path);
    SCOPED_TRACE(testing::Message() << "\t cause_overwrite " <<
      test_case.cause_overwrite);
    scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));

    bool exclusive = false;
    bool recursive = true;
    FilePath source_path = UTF8ToFilePath(test_case.source_path);
    FilePath dest_path = UTF8ToFilePath(test_case.dest_path);

    context.reset(NewContext(NULL));
    ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
        context.get(), source_path.DirName(), exclusive, recursive));
    context.reset(NewContext(NULL));
    ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
        context.get(), dest_path.DirName(), exclusive, recursive));

    bool created = false;
    context.reset(NewContext(NULL));
    ASSERT_EQ(base::PLATFORM_FILE_OK,
              ofu()->EnsureFileExists(context.get(), source_path, &created));
    ASSERT_TRUE(created);
    context.reset(NewContext(NULL));
    ASSERT_EQ(base::PLATFORM_FILE_OK,
              ofu()->Truncate(context.get(), source_path, kSourceLength));

    if (test_case.cause_overwrite) {
      context.reset(NewContext(NULL));
      created = false;
      ASSERT_EQ(base::PLATFORM_FILE_OK,
                ofu()->EnsureFileExists(context.get(), dest_path, &created));
      ASSERT_TRUE(created);
      context.reset(NewContext(NULL));
      ASSERT_EQ(base::PLATFORM_FILE_OK,
                ofu()->Truncate(context.get(), dest_path, kDestLength));
    }

    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->CopyOrMoveFile(context.get(),
        source_path, dest_path, test_case.is_copy_not_move));
    if (test_case.is_copy_not_move) {
      base::PlatformFileInfo file_info;
      FilePath local_path;
      context.reset(NewContext(NULL));
      EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetFileInfo(
          context.get(), source_path, &file_info, &local_path));
      EXPECT_EQ(kSourceLength, file_info.size);
      EXPECT_EQ(base::PLATFORM_FILE_OK,
                ofu()->DeleteFile(context.get(), source_path));
    } else {
      base::PlatformFileInfo file_info;
      FilePath local_path;
      context.reset(NewContext(NULL));
      EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, ofu()->GetFileInfo(
          context.get(), source_path, &file_info, &local_path));
    }
    base::PlatformFileInfo file_info;
    FilePath local_path;
    EXPECT_EQ(base::PLATFORM_FILE_OK, ofu()->GetFileInfo(
        context.get(), dest_path, &file_info, &local_path));
    EXPECT_EQ(kSourceLength, file_info.size);

    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->DeleteFile(context.get(), dest_path));
  }
}

TEST_F(ObfuscatedFileUtilTest, TestCopyPathQuotas) {
  FilePath src_path = UTF8ToFilePath("src path");
  FilePath dest_path = UTF8ToFilePath("destination path");
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
  bool created = false;
  ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->EnsureFileExists(
      context.get(), src_path, &created));

  bool is_copy = true;
  // Copy, no overwrite.
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(dest_path) - 1);
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NO_SPACE,
      ofu()->CopyOrMoveFile(context.get(), src_path, dest_path, is_copy));
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(dest_path));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
      ofu()->CopyOrMoveFile(context.get(), src_path, dest_path, is_copy));

  // Copy, with overwrite.
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(0);
  EXPECT_EQ(base::PLATFORM_FILE_OK,
      ofu()->CopyOrMoveFile(context.get(), src_path, dest_path, is_copy));
}

TEST_F(ObfuscatedFileUtilTest, TestMovePathQuotasWithRename) {
  FilePath src_path = UTF8ToFilePath("src path");
  FilePath dest_path = UTF8ToFilePath("destination path");
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
  bool created = false;
  ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->EnsureFileExists(
      context.get(), src_path, &created));

  bool is_copy = false;
  // Move, rename, no overwrite.
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(dest_path) -
      ObfuscatedFileUtil::ComputeFilePathCost(src_path) - 1);
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NO_SPACE,
      ofu()->CopyOrMoveFile(context.get(), src_path, dest_path, is_copy));
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(
      ObfuscatedFileUtil::ComputeFilePathCost(dest_path) -
      ObfuscatedFileUtil::ComputeFilePathCost(src_path));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
      ofu()->CopyOrMoveFile(context.get(), src_path, dest_path, is_copy));

  context.reset(NewContext(NULL));
  ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->EnsureFileExists(
      context.get(), src_path, &created));

  // Move, rename, with overwrite.
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(0);
  EXPECT_EQ(base::PLATFORM_FILE_OK,
      ofu()->CopyOrMoveFile(context.get(), src_path, dest_path, is_copy));
}

TEST_F(ObfuscatedFileUtilTest, TestMovePathQuotasWithoutRename) {
  FilePath src_path = UTF8ToFilePath("src path");
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
  bool created = false;
  ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->EnsureFileExists(
      context.get(), src_path, &created));

  bool exclusive = true;
  bool recursive = false;
  FilePath dir_path = UTF8ToFilePath("directory path");
  context.reset(NewContext(NULL));
  ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), dir_path, exclusive, recursive));

  FilePath dest_path = dir_path.Append(src_path);

  bool is_copy = false;
  int64 allowed_bytes_growth = -1000;  // Over quota, this should still work.
  // Move, no rename, no overwrite.
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(allowed_bytes_growth);
  EXPECT_EQ(base::PLATFORM_FILE_OK,
      ofu()->CopyOrMoveFile(context.get(), src_path, dest_path, is_copy));
  EXPECT_EQ(allowed_bytes_growth, context->allowed_bytes_growth());

  // Move, no rename, with overwrite.
  context.reset(NewContext(NULL));
  ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->EnsureFileExists(
      context.get(), src_path, &created));
  context.reset(NewContext(NULL));
  context->set_allowed_bytes_growth(allowed_bytes_growth);
  EXPECT_EQ(base::PLATFORM_FILE_OK,
      ofu()->CopyOrMoveFile(context.get(), src_path, dest_path, is_copy));
  EXPECT_EQ(
      allowed_bytes_growth +
          ObfuscatedFileUtil::ComputeFilePathCost(src_path),
      context->allowed_bytes_growth());
}

TEST_F(ObfuscatedFileUtilTest, TestCopyInForeignFile) {
  TestCopyInForeignFileHelper(false /* overwrite */);
  TestCopyInForeignFileHelper(true /* overwrite */);
}

TEST_F(ObfuscatedFileUtilTest, TestEnumerator) {
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
  FilePath src_path = UTF8ToFilePath("source dir");
  bool exclusive = true;
  bool recursive = false;
  ASSERT_EQ(base::PLATFORM_FILE_OK, ofu()->CreateDirectory(
      context.get(), src_path, exclusive, recursive));

  std::set<FilePath::StringType> files;
  std::set<FilePath::StringType> directories;
  FillTestDirectory(src_path, &files, &directories);

  FilePath dest_path = UTF8ToFilePath("destination dir");

  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->DirectoryExists(context.get(), dest_path));
  context.reset(NewContext(NULL));
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      ofu()->Copy(context.get(), src_path, dest_path));

  ValidateTestDirectory(dest_path, files, directories);
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->DirectoryExists(context.get(), src_path));
  context.reset(NewContext(NULL));
  EXPECT_TRUE(ofu()->DirectoryExists(context.get(), dest_path));
  context.reset(NewContext(NULL));
  recursive = true;
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      ofu()->Delete(context.get(), dest_path, recursive));
  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->DirectoryExists(context.get(), dest_path));
}

TEST_F(ObfuscatedFileUtilTest, TestMigration) {
  ScopedTempDir source_dir;
  ASSERT_TRUE(source_dir.CreateUniqueTempDir());
  FilePath root_path = source_dir.path().AppendASCII("chrome-pLmnMWXE7NzTFRsn");
  ASSERT_TRUE(file_util::CreateDirectory(root_path));

  test::SetUpRegularTestCases(root_path);

  EXPECT_TRUE(ofu()->MigrateFromOldSandbox(origin(), type(), root_path));

  FilePath new_root =
    test_directory().AppendASCII("File System").AppendASCII("000").Append(
        ofu()->GetDirectoryNameForType(type())).AppendASCII("Legacy");
  for (size_t i = 0; i < test::kRegularTestCaseSize; ++i) {
    SCOPED_TRACE(testing::Message() << "Validating kMigrationTestPath " << i);
    const test::TestCaseRecord& test_case = test::kRegularTestCases[i];
    FilePath local_data_path = new_root.Append(test_case.path);
#if defined(OS_WIN)
    local_data_path = local_data_path.NormalizeWindowsPathSeparators();
#endif
    scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
    base::PlatformFileInfo ofu_file_info;
    FilePath data_path;
    SCOPED_TRACE(testing::Message() << "Path is " << test_case.path);
    EXPECT_EQ(base::PLATFORM_FILE_OK,
        ofu()->GetFileInfo(context.get(), FilePath(test_case.path),
            &ofu_file_info, &data_path));
    if (test_case.is_directory) {
      EXPECT_TRUE(ofu_file_info.is_directory);
    } else {
      base::PlatformFileInfo platform_file_info;
      SCOPED_TRACE(testing::Message() << "local_data_path is " <<
          local_data_path.value());
      SCOPED_TRACE(testing::Message() << "data_path is " << data_path.value());
      ASSERT_TRUE(file_util::GetFileInfo(local_data_path, &platform_file_info));
      EXPECT_EQ(test_case.data_file_size, platform_file_info.size);
      EXPECT_FALSE(platform_file_info.is_directory);
      scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
      EXPECT_EQ(local_data_path, data_path);
      EXPECT_EQ(platform_file_info.size, ofu_file_info.size);
      EXPECT_FALSE(ofu_file_info.is_directory);
    }
  }
}

TEST_F(ObfuscatedFileUtilTest, TestOriginEnumerator) {
  scoped_ptr<ObfuscatedFileUtil::AbstractOriginEnumerator>
      enumerator(ofu()->CreateOriginEnumerator());
  // The test helper starts out with a single filesystem.
  EXPECT_TRUE(enumerator.get());
  EXPECT_EQ(origin(), enumerator->Next());
  ASSERT_TRUE(type() == kFileSystemTypeTemporary);
  EXPECT_TRUE(enumerator->HasFileSystemType(kFileSystemTypeTemporary));
  EXPECT_FALSE(enumerator->HasFileSystemType(kFileSystemTypePersistent));
  EXPECT_EQ(GURL(), enumerator->Next());
  EXPECT_FALSE(enumerator->HasFileSystemType(kFileSystemTypeTemporary));
  EXPECT_FALSE(enumerator->HasFileSystemType(kFileSystemTypePersistent));

  std::set<GURL> origins_expected;
  origins_expected.insert(origin());

  for (size_t i = 0; i < arraysize(kOriginEnumerationTestRecords); ++i) {
    SCOPED_TRACE(testing::Message() <<
        "Validating kOriginEnumerationTestRecords " << i);
    const OriginEnumerationTestRecord& record =
        kOriginEnumerationTestRecords[i];
    GURL origin_url(record.origin_url);
    origins_expected.insert(origin_url);
    if (record.has_temporary) {
      scoped_ptr<FileSystemTestOriginHelper> helper(
          NewHelper(origin_url, kFileSystemTypeTemporary));
      scoped_ptr<FileSystemOperationContext> context(NewContext(helper.get()));
      context->set_src_origin_url(origin_url);
      context->set_src_type(kFileSystemTypeTemporary);
      bool created = false;
      ASSERT_EQ(base::PLATFORM_FILE_OK,
                ofu()->EnsureFileExists(context.get(),
                    FilePath().AppendASCII("file"), &created));
      EXPECT_TRUE(created);
    }
    if (record.has_persistent) {
      scoped_ptr<FileSystemTestOriginHelper> helper(
          NewHelper(origin_url, kFileSystemTypePersistent));
      scoped_ptr<FileSystemOperationContext> context(NewContext(helper.get()));
      context->set_src_origin_url(origin_url);
      context->set_src_type(kFileSystemTypePersistent);
      bool created = false;
      ASSERT_EQ(base::PLATFORM_FILE_OK,
                ofu()->EnsureFileExists(context.get(),
                    FilePath().AppendASCII("file"), &created));
      EXPECT_TRUE(created);
    }
  }
  enumerator.reset(ofu()->CreateOriginEnumerator());
  EXPECT_TRUE(enumerator.get());
  std::set<GURL> origins_found;
  GURL origin_url;
  while (!(origin_url = enumerator->Next()).is_empty()) {
    origins_found.insert(origin_url);
    SCOPED_TRACE(testing::Message() << "Handling " << origin_url.spec());
    bool found = false;
    for (size_t i = 0; !found && i < arraysize(kOriginEnumerationTestRecords);
        ++i) {
      const OriginEnumerationTestRecord& record =
          kOriginEnumerationTestRecords[i];
      if (GURL(record.origin_url) != origin_url)
        continue;
      found = true;
      EXPECT_EQ(record.has_temporary,
          enumerator->HasFileSystemType(kFileSystemTypeTemporary));
      EXPECT_EQ(record.has_persistent,
          enumerator->HasFileSystemType(kFileSystemTypePersistent));
    }
    // Deal with the default filesystem created by the test helper.
    if (!found && origin_url == origin()) {
      ASSERT_TRUE(type() == kFileSystemTypeTemporary);
      EXPECT_EQ(true,
          enumerator->HasFileSystemType(kFileSystemTypeTemporary));
      EXPECT_FALSE(enumerator->HasFileSystemType(kFileSystemTypePersistent));
      found = true;
    }
    EXPECT_TRUE(found);
  }

  std::set<GURL> diff;
  std::set_symmetric_difference(origins_expected.begin(),
      origins_expected.end(), origins_found.begin(), origins_found.end(),
      inserter(diff, diff.begin()));
  EXPECT_TRUE(diff.empty());
}

TEST_F(ObfuscatedFileUtilTest, TestRevokeUsageCache) {
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));

  int64 expected_quota = 0;

  for (size_t i = 0; i < test::kRegularTestCaseSize; ++i) {
    SCOPED_TRACE(testing::Message() << "Creating kMigrationTestPath " << i);
    const test::TestCaseRecord& test_case = test::kRegularTestCases[i];
    FilePath path(test_case.path);
    expected_quota += ObfuscatedFileUtil::ComputeFilePathCost(path);
    if (test_case.is_directory) {
      bool exclusive = true;
      bool recursive = false;
      ASSERT_EQ(base::PLATFORM_FILE_OK,
          ofu()->CreateDirectory(context.get(), path, exclusive, recursive));
    } else {
      bool created = false;
      ASSERT_EQ(base::PLATFORM_FILE_OK,
          ofu()->EnsureFileExists(context.get(), path, &created));
      ASSERT_TRUE(created);
      ASSERT_EQ(base::PLATFORM_FILE_OK,
                ofu()->Truncate(context.get(), path,
                    test_case.data_file_size));
      expected_quota += test_case.data_file_size;
    }
  }
  EXPECT_EQ(expected_quota, SizeInUsageFile());
  RevokeUsageCache();
  EXPECT_EQ(-1, SizeInUsageFile());
  GetUsageFromQuotaManager();
  EXPECT_EQ(expected_quota, SizeInUsageFile());
  EXPECT_EQ(expected_quota, usage());
}

TEST_F(ObfuscatedFileUtilTest, TestInconsistency) {
  const FilePath kPath1 = FilePath().AppendASCII("hoge");
  const FilePath kPath2 = FilePath().AppendASCII("fuga");

  scoped_ptr<FileSystemOperationContext> context;
  base::PlatformFile file;
  base::PlatformFileInfo file_info;
  FilePath data_path;
  bool created = false;

  // Create a non-empty file.
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), kPath1, &created));
  EXPECT_TRUE(created);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->Truncate(context.get(), kPath1, 10));
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->GetFileInfo(
                context.get(), kPath1, &file_info, &data_path));
  EXPECT_EQ(10, file_info.size);

  // Destroy database to make inconsistency between database and filesystem.
  ofu()->DestroyDirectoryDatabase(origin(), type());

  // Try to get file info of broken file.
  context.reset(NewContext(NULL));
  EXPECT_FALSE(ofu()->PathExists(context.get(), kPath1));
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), kPath1, &created));
  EXPECT_TRUE(created);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->GetFileInfo(
                context.get(), kPath1, &file_info, &data_path));
  EXPECT_EQ(0, file_info.size);

  // Make another broken file to |kPath2|.
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), kPath2, &created));
  EXPECT_TRUE(created);

  // Destroy again.
  ofu()->DestroyDirectoryDatabase(origin(), type());

  // Repair broken |kPath1|.
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
            ofu()->Touch(context.get(), kPath1, base::Time::Now(),
                           base::Time::Now()));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), kPath1, &created));
  EXPECT_TRUE(created);

  // Copy from sound |kPath1| to broken |kPath2|.
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CopyOrMoveFile(context.get(), kPath1, kPath2,
                                    true /* copy */));

  ofu()->DestroyDirectoryDatabase(origin(), type());
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateOrOpen(
                context.get(), kPath1,
                base::PLATFORM_FILE_READ | base::PLATFORM_FILE_CREATE,
                &file, &created));
  EXPECT_TRUE(created);
  EXPECT_TRUE(base::GetPlatformFileInfo(file, &file_info));
  EXPECT_EQ(0, file_info.size);
  EXPECT_TRUE(base::ClosePlatformFile(file));
}

TEST_F(ObfuscatedFileUtilTest, TestIncompleteDirectoryReading) {
  const FilePath kPath[] = {
    FilePath().AppendASCII("foo"),
    FilePath().AppendASCII("bar"),
    FilePath().AppendASCII("baz")
  };
  scoped_ptr<FileSystemOperationContext> context;

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kPath); ++i) {
    bool created = false;
    context.reset(NewContext(NULL));
    EXPECT_EQ(base::PLATFORM_FILE_OK,
              ofu()->EnsureFileExists(context.get(), kPath[i], &created));
    EXPECT_TRUE(created);
  }

  context.reset(NewContext(NULL));
  std::vector<base::FileUtilProxy::Entry> entries;
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->ReadDirectory(context.get(), FilePath(), &entries));
  EXPECT_EQ(3u, entries.size());

  context.reset(NewContext(NULL));
  FilePath local_path;
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->GetLocalFilePath(context.get(), kPath[0], &local_path));
  EXPECT_TRUE(file_util::Delete(local_path, false));

  context.reset(NewContext(NULL));
  entries.clear();
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->ReadDirectory(context.get(), FilePath(), &entries));
  EXPECT_EQ(ARRAYSIZE_UNSAFE(kPath) - 1, entries.size());
}

TEST_F(ObfuscatedFileUtilTest, TestDirectoryTimestampForCreation) {
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
  const FilePath dir_path(FILE_PATH_LITERAL("foo_dir"));

  // Create working directory.
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateDirectory(context.get(), dir_path, false, false));

  // EnsureFileExists, create case.
  FilePath path(dir_path.AppendASCII("EnsureFileExists_file"));
  bool created = false;
  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), path, &created));
  EXPECT_TRUE(created);
  EXPECT_NE(base::Time(), GetModifiedTime(dir_path));

  // non create case.
  created = true;
  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), path, &created));
  EXPECT_FALSE(created);
  EXPECT_EQ(base::Time(), GetModifiedTime(dir_path));

  // fail case.
  path = dir_path.AppendASCII("EnsureFileExists_dir");
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateDirectory(context.get(), path, false, false));

  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_A_FILE,
            ofu()->EnsureFileExists(context.get(), path, &created));
  EXPECT_EQ(base::Time(), GetModifiedTime(dir_path));

  // CreateOrOpen, create case.
  path = dir_path.AppendASCII("CreateOrOpen_file");
  PlatformFile file_handle = base::kInvalidPlatformFileValue;
  created = false;
  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateOrOpen(
                context.get(), path,
                base::PLATFORM_FILE_CREATE | base::PLATFORM_FILE_WRITE,
                &file_handle, &created));
  EXPECT_NE(base::kInvalidPlatformFileValue, file_handle);
  EXPECT_TRUE(created);
  EXPECT_TRUE(base::ClosePlatformFile(file_handle));
  EXPECT_NE(base::Time(), GetModifiedTime(dir_path));

  // open case.
  file_handle = base::kInvalidPlatformFileValue;
  created = true;
  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateOrOpen(
                context.get(), path,
                base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_WRITE,
                &file_handle, &created));
  EXPECT_NE(base::kInvalidPlatformFileValue, file_handle);
  EXPECT_FALSE(created);
  EXPECT_TRUE(base::ClosePlatformFile(file_handle));
  EXPECT_EQ(base::Time(), GetModifiedTime(dir_path));

  // fail case
  file_handle = base::kInvalidPlatformFileValue;
  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS,
            ofu()->CreateOrOpen(
                context.get(), path,
                base::PLATFORM_FILE_CREATE | base::PLATFORM_FILE_WRITE,
                &file_handle, &created));
  EXPECT_EQ(base::kInvalidPlatformFileValue, file_handle);
  EXPECT_EQ(base::Time(), GetModifiedTime(dir_path));

  // CreateDirectory, create case.
  // Creating CreateDirectory_dir and CreateDirectory_dir/subdir.
  path = dir_path.AppendASCII("CreateDirectory_dir");
  FilePath subdir_path(path.AppendASCII("subdir"));
  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateDirectory(context.get(), subdir_path,
                                   true /* exclusive */, true /* recursive */));
  EXPECT_NE(base::Time(), GetModifiedTime(dir_path));

  // create subdir case.
  // Creating CreateDirectory_dir/subdir2.
  subdir_path = path.AppendASCII("subdir2");
  ClearTimestamp(dir_path);
  ClearTimestamp(path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateDirectory(context.get(), subdir_path,
                                   true /* exclusive */, true /* recursive */));
  EXPECT_EQ(base::Time(), GetModifiedTime(dir_path));
  EXPECT_NE(base::Time(), GetModifiedTime(path));

  // fail case.
  path = dir_path.AppendASCII("CreateDirectory_dir");
  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS,
            ofu()->CreateDirectory(context.get(), path,
                                   true /* exclusive */, true /* recursive */));
  EXPECT_EQ(base::Time(), GetModifiedTime(dir_path));

  // CopyInForeignFile, create case.
  path = dir_path.AppendASCII("CopyInForeignFile_file");
  FilePath src_path = dir_path.AppendASCII("CopyInForeignFile_src_file");
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), src_path, &created));
  EXPECT_TRUE(created);
  FilePath src_local_path;
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->GetLocalFilePath(context.get(), src_path, &src_local_path));

  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CopyInForeignFile(context.get(), src_local_path, path));
  EXPECT_NE(base::Time(), GetModifiedTime(dir_path));
}

TEST_F(ObfuscatedFileUtilTest, TestDirectoryTimestampForDeletion) {
  scoped_ptr<FileSystemOperationContext> context(NewContext(NULL));
  const FilePath dir_path(FILE_PATH_LITERAL("foo_dir"));

  // Create working directory.
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateDirectory(context.get(), dir_path, false, false));

  // DeleteFile, delete case.
  FilePath path = dir_path.AppendASCII("DeleteFile_file");
  bool created = false;
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), path, &created));
  EXPECT_TRUE(created);

  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->DeleteFile(context.get(), path));
  EXPECT_NE(base::Time(), GetModifiedTime(dir_path));

  // fail case.
  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
            ofu()->DeleteFile(context.get(), path));
  EXPECT_EQ(base::Time(), GetModifiedTime(dir_path));

  // DeleteSingleDirectory, fail case.
  path = dir_path.AppendASCII("DeleteSingleDirectory_dir");
  FilePath file_path(path.AppendASCII("pakeratta"));
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->CreateDirectory(context.get(), path, true, true));
  created = false;
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->EnsureFileExists(context.get(), file_path, &created));
  EXPECT_TRUE(created);

  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_EMPTY,
            ofu()->DeleteSingleDirectory(context.get(), path));
  EXPECT_EQ(base::Time(), GetModifiedTime(dir_path));

  // delete case.
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->DeleteFile(context.get(), file_path));

  ClearTimestamp(dir_path);
  context.reset(NewContext(NULL));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            ofu()->DeleteSingleDirectory(context.get(), path));
  EXPECT_NE(base::Time(), GetModifiedTime(dir_path));
}

TEST_F(ObfuscatedFileUtilTest, TestDirectoryTimestampForCopyAndMove) {
  TestDirectoryTimestampHelper(
      FilePath(FILE_PATH_LITERAL("copy overwrite")), true, true);
  TestDirectoryTimestampHelper(
      FilePath(FILE_PATH_LITERAL("copy non-overwrite")), true, false);
  TestDirectoryTimestampHelper(
      FilePath(FILE_PATH_LITERAL("move overwrite")), false, true);
  TestDirectoryTimestampHelper(
      FilePath(FILE_PATH_LITERAL("move non-overwrite")), false, false);
}
