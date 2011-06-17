// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/file_system_operation.h"

#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_temp_dir.h"
#include "base/message_loop.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/fileapi/file_system_callback_dispatcher.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_file_util.h"
#include "webkit/fileapi/file_system_mount_point_provider.h"
#include "webkit/fileapi/file_system_operation.h"
#include "webkit/fileapi/file_system_path_manager.h"
#include "webkit/fileapi/file_system_util.h"

namespace fileapi {

const int kFileOperationStatusNotSet = 1;
const int kFileOperationSucceeded = 0;

static bool FileExists(FilePath path) {
  return file_util::PathExists(path) && !file_util::DirectoryExists(path);
}

class MockDispatcher;

// Test class for FileSystemOperation.  Note that this just tests low-level
// operations but doesn't test OpenFileSystem or any additional checks
// that require FileSystemContext (e.g. sandboxed paths, unlimited_storage
// quota handling etc).
// See SimpleFileSystem for more complete test environment for sandboxed
// FileSystem.
class FileSystemOperationTest : public testing::Test {
 public:
  FileSystemOperationTest()
      : status_(kFileOperationStatusNotSet) {
    EXPECT_TRUE(base_.CreateUniqueTempDir());
  }

  FileSystemOperation* operation();

  void set_local_path(const FilePath& path) { local_path_ = path; }
  const FilePath& local_path() const { return local_path_; }
  void set_status(int status) { status_ = status; }
  int status() const { return status_; }
  void set_info(const base::PlatformFileInfo& info) { info_ = info; }
  const base::PlatformFileInfo& info() const { return info_; }
  void set_path(const FilePath& path) { path_ = path; }
  const FilePath& path() const { return path_; }
  void set_entries(const std::vector<base::FileUtilProxy::Entry>& entries) {
    entries_ = entries;
  }
  const std::vector<base::FileUtilProxy::Entry>& entries() const {
    return entries_;
  }

 protected:
  // Common temp base for nondestructive uses.
  ScopedTempDir base_;

  GURL URLForRelativePath(const std::string& path) const {
    // Only the path will actually get used.
    return GURL("file://").Resolve(base_.path().value()).Resolve(path);
  }

  GURL URLForPath(const FilePath& path) const {
    // Only the path will actually get used.
    return GURL("file://").Resolve(path.value());
  }

  // For post-operation status.
  int status_;
  base::PlatformFileInfo info_;
  FilePath path_;
  FilePath local_path_;
  std::vector<base::FileUtilProxy::Entry> entries_;

  DISALLOW_COPY_AND_ASSIGN(FileSystemOperationTest);
};

class MockDispatcher : public FileSystemCallbackDispatcher {
 public:
  MockDispatcher(FileSystemOperationTest* test) : test_(test) { }

  virtual void DidFail(base::PlatformFileError status) {
    test_->set_status(status);
  }

  virtual void DidSucceed() {
    test_->set_status(kFileOperationSucceeded);
  }

  virtual void DidGetLocalPath(const FilePath& local_path) {
    test_->set_local_path(local_path);
    test_->set_status(kFileOperationSucceeded);
  }

  virtual void DidReadMetadata(
      const base::PlatformFileInfo& info,
      const FilePath& platform_path) {
    test_->set_info(info);
    test_->set_path(platform_path);
    test_->set_status(kFileOperationSucceeded);
  }

  virtual void DidReadDirectory(
      const std::vector<base::FileUtilProxy::Entry>& entries,
      bool /* has_more */) {
    test_->set_entries(entries);
  }

  virtual void DidOpenFileSystem(const std::string&, const GURL&) {
    NOTREACHED();
  }

  virtual void DidWrite(int64 bytes, bool complete) {
    NOTREACHED();
  }

 private:
  FileSystemOperationTest* test_;
};

FileSystemOperation* FileSystemOperationTest::operation() {
  FileSystemOperation* operation = new FileSystemOperation(
      new MockDispatcher(this),
      base::MessageLoopProxy::CreateForCurrentThread(),
      NULL,
      FileSystemFileUtil::GetInstance());
  operation->file_system_operation_context()->set_src_type(
      kFileSystemTypeTemporary);
  operation->file_system_operation_context()->set_dest_type(
      kFileSystemTypeTemporary);
  GURL origin_url("fake://fake.foo/");
  operation->file_system_operation_context()->set_src_origin_url(origin_url);
  operation->file_system_operation_context()->set_dest_origin_url(origin_url);

  return operation;
}

TEST_F(FileSystemOperationTest, TestMoveFailureSrcDoesntExist) {
  GURL src(URLForRelativePath("a"));
  GURL dest(URLForRelativePath("b"));
  operation()->Move(src, dest);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());
}

TEST_F(FileSystemOperationTest, TestMoveFailureContainsPath) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath dest_dir_path;
  ASSERT_TRUE(file_util::CreateTemporaryDirInDir(src_dir.path(),
                                                 FILE_PATH_LITERAL("child_dir"),
                                                 &dest_dir_path));
  operation()->Move(URLForPath(src_dir.path()), URLForPath(dest_dir_path));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_INVALID_OPERATION, status());
}

TEST_F(FileSystemOperationTest, TestMoveFailureSrcDirExistsDestFile) {
  // Src exists and is dir. Dest is a file.
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());
  FilePath dest_file;
  file_util::CreateTemporaryFileInDir(dest_dir.path(), &dest_file);

  operation()->Move(URLForPath(src_dir.path()), URLForPath(dest_file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_A_DIRECTORY, status());
}

TEST_F(FileSystemOperationTest, TestMoveFailureSrcFileExistsDestNonEmptyDir) {
  // Src exists and is a directory. Dest is a non-empty directory.
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());
  FilePath child_file;
  file_util::CreateTemporaryFileInDir(dest_dir.path(), &child_file);

  operation()->Move(URLForPath(src_dir.path()), URLForPath(dest_dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_EMPTY, status());
}

TEST_F(FileSystemOperationTest, TestMoveFailureSrcFileExistsDestDir) {
  // Src exists and is a file. Dest is a directory.
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath src_file;
  file_util::CreateTemporaryFileInDir(src_dir.path(), &src_file);

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());

  operation()->Move(URLForPath(src_file), URLForPath(dest_dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_A_FILE, status());
}

TEST_F(FileSystemOperationTest, TestMoveFailureDestParentDoesntExist) {
  // Dest. parent path does not exist.
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath nonexisting_file = base_.path().Append(
      FILE_PATH_LITERAL("NonexistingDir")).Append(
          FILE_PATH_LITERAL("NonexistingFile"));

  operation()->Move(URLForPath(src_dir.path()), URLForPath(nonexisting_file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());
}

TEST_F(FileSystemOperationTest, TestMoveSuccessSrcFileAndOverwrite) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath src_file;
  file_util::CreateTemporaryFileInDir(src_dir.path(), &src_file);

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());
  FilePath dest_file;
  file_util::CreateTemporaryFileInDir(dest_dir.path(), &dest_file);

  operation()->Move(URLForPath(src_file), URLForPath(dest_file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(FileExists(dest_file));
}

TEST_F(FileSystemOperationTest, TestMoveSuccessSrcFileAndNew) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath src_file;
  file_util::CreateTemporaryFileInDir(src_dir.path(), &src_file);

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());
  FilePath dest_file(dest_dir.path().Append(FILE_PATH_LITERAL("NewFile")));

  operation()->Move(URLForPath(src_file), URLForPath(dest_file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(FileExists(dest_file));
}

TEST_F(FileSystemOperationTest, TestMoveSuccessSrcDirAndOverwrite) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());

  operation()->Move(URLForPath(src_dir.path()), URLForPath(dest_dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_FALSE(file_util::DirectoryExists(src_dir.path()));

  // Make sure we've overwritten but not moved the source under the |dest_dir|.
  EXPECT_TRUE(file_util::DirectoryExists(dest_dir.path()));
  EXPECT_FALSE(file_util::DirectoryExists(
      dest_dir.path().Append(src_dir.path().BaseName())));
}

TEST_F(FileSystemOperationTest, TestMoveSuccessSrcDirAndNew) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());

  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath dest_dir_path(dir.path().Append(FILE_PATH_LITERAL("NewDirectory")));

  operation()->Move(URLForPath(src_dir.path()), URLForPath(dest_dir_path));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_FALSE(file_util::DirectoryExists(src_dir.path()));
  EXPECT_TRUE(file_util::DirectoryExists(dest_dir_path));
}

TEST_F(FileSystemOperationTest, TestMoveSuccessSrcDirRecursive) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath child_dir;
  file_util::CreateTemporaryDirInDir(src_dir.path(),
      FILE_PATH_LITERAL("prefix"), &child_dir);
  FilePath grandchild_file;
  file_util::CreateTemporaryFileInDir(child_dir, &grandchild_file);

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());

  operation()->Move(URLForPath(src_dir.path()), URLForPath(dest_dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(file_util::DirectoryExists(dest_dir.path().Append(
      child_dir.BaseName())));
  EXPECT_TRUE(FileExists(dest_dir.path().Append(
      child_dir.BaseName()).Append(
      grandchild_file.BaseName())));
}

TEST_F(FileSystemOperationTest, TestCopyFailureSrcDoesntExist) {
  operation()->Copy(URLForRelativePath("a"), URLForRelativePath("b"));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());
}

TEST_F(FileSystemOperationTest, TestCopyFailureContainsPath) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath dest_dir_path;
  ASSERT_TRUE(file_util::CreateTemporaryDirInDir(src_dir.path(),
                                                 FILE_PATH_LITERAL("child_dir"),
                                                 &dest_dir_path));
  operation()->Copy(URLForPath(src_dir.path()), URLForPath(dest_dir_path));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_INVALID_OPERATION, status());
}

TEST_F(FileSystemOperationTest, TestCopyFailureSrcDirExistsDestFile) {
  // Src exists and is dir. Dest is a file.
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());
  FilePath dest_file;
  file_util::CreateTemporaryFileInDir(dest_dir.path(), &dest_file);

  operation()->Copy(URLForPath(src_dir.path()), URLForPath(dest_file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_A_DIRECTORY, status());
}

TEST_F(FileSystemOperationTest, TestCopyFailureSrcFileExistsDestNonEmptyDir) {
  // Src exists and is a directory. Dest is a non-empty directory.
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());
  FilePath child_file;
  file_util::CreateTemporaryFileInDir(dest_dir.path(), &child_file);

  operation()->Copy(URLForPath(src_dir.path()), URLForPath(dest_dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_EMPTY, status());
}

TEST_F(FileSystemOperationTest, TestCopyFailureSrcFileExistsDestDir) {
  // Src exists and is a file. Dest is a directory.
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath src_file;
  file_util::CreateTemporaryFileInDir(src_dir.path(), &src_file);

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());

  operation()->Copy(URLForPath(src_file), URLForPath(dest_dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_A_FILE, status());
}

TEST_F(FileSystemOperationTest, TestCopyFailureDestParentDoesntExist) {
  // Dest. parent path does not exist.
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath src_dir = dir.path();

  FilePath nonexisting(base_.path().Append(FILE_PATH_LITERAL("DontExistDir")));
  file_util::EnsureEndsWithSeparator(&nonexisting);
  FilePath nonexisting_file = nonexisting.Append(
      FILE_PATH_LITERAL("DontExistFile"));

  operation()->Copy(URLForPath(src_dir), URLForPath(nonexisting_file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());
}

TEST_F(FileSystemOperationTest, TestCopySuccessSrcFileAndOverwrite) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath src_file;
  file_util::CreateTemporaryFileInDir(src_dir.path(), &src_file);

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());
  FilePath dest_file;
  file_util::CreateTemporaryFileInDir(dest_dir.path(), &dest_file);

  operation()->Copy(URLForPath(src_file), URLForPath(dest_file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(FileExists(dest_file));
}

TEST_F(FileSystemOperationTest, TestCopySuccessSrcFileAndNew) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath src_file;
  file_util::CreateTemporaryFileInDir(src_dir.path(), &src_file);

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());
  FilePath dest_file(dest_dir.path().Append(FILE_PATH_LITERAL("NewFile")));

  operation()->Copy(URLForPath(src_file), URLForPath(dest_file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(FileExists(dest_file));
}

TEST_F(FileSystemOperationTest, TestCopySuccessSrcDirAndOverwrite) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());

  operation()->Copy(URLForPath(src_dir.path()), URLForPath(dest_dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());

  // Make sure we've overwritten but not copied the source under the |dest_dir|.
  EXPECT_TRUE(file_util::DirectoryExists(dest_dir.path()));
  EXPECT_FALSE(file_util::DirectoryExists(
      dest_dir.path().Append(src_dir.path().BaseName())));
}

TEST_F(FileSystemOperationTest, TestCopySuccessSrcDirAndNew) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());

  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath dest_dir(dir.path().Append(FILE_PATH_LITERAL("NewDirectory")));

  operation()->Copy(URLForPath(src_dir.path()), URLForPath(dest_dir));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(file_util::DirectoryExists(dest_dir));
}

TEST_F(FileSystemOperationTest, TestCopySuccessSrcDirRecursive) {
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  FilePath child_dir;
  file_util::CreateTemporaryDirInDir(src_dir.path(),
      FILE_PATH_LITERAL("prefix"), &child_dir);
  FilePath grandchild_file;
  file_util::CreateTemporaryFileInDir(child_dir, &grandchild_file);

  ScopedTempDir dest_dir;
  ASSERT_TRUE(dest_dir.CreateUniqueTempDir());

  operation()->Copy(URLForPath(src_dir.path()), URLForPath(dest_dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(file_util::DirectoryExists(dest_dir.path().Append(
      child_dir.BaseName())));
  EXPECT_TRUE(FileExists(dest_dir.path().Append(
      child_dir.BaseName()).Append(
      grandchild_file.BaseName())));
}

TEST_F(FileSystemOperationTest, TestCreateFileFailure) {
  // Already existing file and exclusive true.
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath file;

  file_util::CreateTemporaryFileInDir(dir.path(), &file);
  operation()->CreateFile(URLForPath(file), true);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS, status());
}

TEST_F(FileSystemOperationTest, TestCreateFileSuccessFileExists) {
  // Already existing file and exclusive false.
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath file;
  file_util::CreateTemporaryFileInDir(dir.path(), &file);

  operation()->CreateFile(URLForPath(file), false);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(FileExists(file));
}

TEST_F(FileSystemOperationTest, TestCreateFileSuccessExclusive) {
  // File doesn't exist but exclusive is true.
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath file = dir.path().Append(FILE_PATH_LITERAL("FileDoesntExist"));
  operation()->CreateFile(URLForPath(file), true);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(FileExists(file));
}

TEST_F(FileSystemOperationTest, TestCreateFileSuccessFileDoesntExist) {
  // Non existing file.
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath file = dir.path().Append(FILE_PATH_LITERAL("FileDoesntExist"));
  operation()->CreateFile(URLForPath(file), false);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
}

TEST_F(FileSystemOperationTest,
       TestCreateDirFailureDestParentDoesntExist) {
  // Dest. parent path does not exist.
  FilePath nonexisting(base_.path().Append(
      FILE_PATH_LITERAL("DirDoesntExist")));
  FilePath nonexisting_file = nonexisting.Append(
      FILE_PATH_LITERAL("FileDoesntExist"));
  operation()->CreateDirectory(URLForPath(nonexisting_file), false, false);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());
}

TEST_F(FileSystemOperationTest, TestCreateDirFailureDirExists) {
  // Exclusive and dir existing at path.
  ScopedTempDir src_dir;
  ASSERT_TRUE(src_dir.CreateUniqueTempDir());
  operation()->CreateDirectory(URLForPath(src_dir.path()), true, false);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS, status());
}

TEST_F(FileSystemOperationTest, TestCreateDirFailureFileExists) {
  // Exclusive true and file existing at path.
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath file;
  file_util::CreateTemporaryFileInDir(dir.path(), &file);
  operation()->CreateDirectory(URLForPath(file), true, false);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_EXISTS, status());
}

TEST_F(FileSystemOperationTest, TestCreateDirSuccess) {
  // Dir exists and exclusive is false.
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  operation()->CreateDirectory(URLForPath(dir.path()), false, false);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());

  // Dir doesn't exist.
  FilePath nonexisting_dir_path(base_.path().Append(
      FILE_PATH_LITERAL("nonexistingdir")));
  operation()->CreateDirectory(URLForPath(nonexisting_dir_path), false, false);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(file_util::DirectoryExists(nonexisting_dir_path));
}

TEST_F(FileSystemOperationTest, TestCreateDirSuccessExclusive) {
  // Dir doesn't exist.
  FilePath nonexisting_dir_path(base_.path().Append(
      FILE_PATH_LITERAL("nonexistingdir")));

  operation()->CreateDirectory(URLForPath(nonexisting_dir_path), true, false);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(file_util::DirectoryExists(nonexisting_dir_path));
}

TEST_F(FileSystemOperationTest, TestExistsAndMetadataFailure) {
  FilePath nonexisting_dir_path(base_.path().Append(
      FILE_PATH_LITERAL("nonexistingdir")));
  operation()->GetMetadata(URLForPath(nonexisting_dir_path));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());

  operation()->FileExists(URLForPath(nonexisting_dir_path));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());

  file_util::EnsureEndsWithSeparator(&nonexisting_dir_path);
  operation()->DirectoryExists(URLForPath(nonexisting_dir_path));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());
}

TEST_F(FileSystemOperationTest, TestExistsAndMetadataSuccess) {
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());

  operation()->DirectoryExists(URLForPath(dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());

  operation()->GetMetadata(URLForPath(dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_TRUE(info().is_directory);
  EXPECT_EQ(dir.path(), path());

  FilePath file;
  file_util::CreateTemporaryFileInDir(dir.path(), &file);
  operation()->FileExists(URLForPath(file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());

  operation()->GetMetadata(URLForPath(file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_FALSE(info().is_directory);
  EXPECT_EQ(file, path());
}

TEST_F(FileSystemOperationTest, TestGetLocalFilePathSuccess) {
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  operation()->GetLocalPath(URLForPath(dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_EQ(local_path().value(), dir.path().value());

  FilePath file;
  file_util::CreateTemporaryFileInDir(dir.path(), &file);
  operation()->GetLocalPath(URLForPath(file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_EQ(local_path().value(), file.value());
}

TEST_F(FileSystemOperationTest, TestTypeMismatchErrors) {
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  operation()->FileExists(URLForPath(dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_A_FILE, status());

  FilePath file;
  ASSERT_TRUE(file_util::CreateTemporaryFileInDir(dir.path(), &file));
  operation()->DirectoryExists(URLForPath(file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_A_DIRECTORY, status());
}

TEST_F(FileSystemOperationTest, TestReadDirFailure) {
  // Path doesn't exist
  FilePath nonexisting_dir_path(base_.path().Append(
      FILE_PATH_LITERAL("NonExistingDir")));
  file_util::EnsureEndsWithSeparator(&nonexisting_dir_path);
  operation()->ReadDirectory(URLForPath(nonexisting_dir_path));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());

  // File exists.
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath file;
  file_util::CreateTemporaryFileInDir(dir.path(), &file);
  operation()->ReadDirectory(URLForPath(file));
  MessageLoop::current()->RunAllPending();
  // TODO(kkanetkar) crbug.com/54309 to change the error code.
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());
}

TEST_F(FileSystemOperationTest, TestReadDirSuccess) {
  //      parent_dir
  //       |       |
  //  child_dir  child_file
  // Verify reading parent_dir.
  ScopedTempDir parent_dir;
  ASSERT_TRUE(parent_dir.CreateUniqueTempDir());
  FilePath child_file;
  file_util::CreateTemporaryFileInDir(parent_dir.path(), &child_file);
  FilePath child_dir;
  ASSERT_TRUE(file_util::CreateTemporaryDirInDir(
      parent_dir.path(), FILE_PATH_LITERAL("child_dir"), &child_dir));

  operation()->ReadDirectory(URLForPath(parent_dir.path()));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationStatusNotSet, status());
  EXPECT_EQ(2u, entries().size());

  for (size_t i = 0; i < entries().size(); ++i) {
    if (entries()[i].is_directory) {
      EXPECT_EQ(child_dir.BaseName().value(),
                entries()[i].name);
    } else {
      EXPECT_EQ(child_file.BaseName().value(),
                entries()[i].name);
    }
  }
}

TEST_F(FileSystemOperationTest, TestRemoveFailure) {
  // Path doesn't exist.
  FilePath nonexisting(base_.path().Append(
      FILE_PATH_LITERAL("NonExistingDir")));
  file_util::EnsureEndsWithSeparator(&nonexisting);

  operation()->Remove(URLForPath(nonexisting), false /* recursive */);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, status());

  // It's an error to try to remove a non-empty directory if recursive flag
  // is false.
  //      parent_dir
  //       |       |
  //  child_dir  child_file
  // Verify deleting parent_dir.
  ScopedTempDir parent_dir;
  ASSERT_TRUE(parent_dir.CreateUniqueTempDir());
  FilePath child_file;
  file_util::CreateTemporaryFileInDir(parent_dir.path(), &child_file);
  FilePath child_dir;
  ASSERT_TRUE(file_util::CreateTemporaryDirInDir(
      parent_dir.path(), FILE_PATH_LITERAL("child_dir"), &child_dir));

  operation()->Remove(URLForPath(parent_dir.path()), false /* recursive */);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_EMPTY,
            status());
}

TEST_F(FileSystemOperationTest, TestRemoveSuccess) {
  ScopedTempDir empty_dir;
  ASSERT_TRUE(empty_dir.CreateUniqueTempDir());
  EXPECT_TRUE(file_util::DirectoryExists(empty_dir.path()));

  operation()->Remove(URLForPath(empty_dir.path()), false /* recursive */);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_FALSE(file_util::DirectoryExists(empty_dir.path()));

  // Removing a non-empty directory with recursive flag == true should be ok.
  //      parent_dir
  //       |       |
  //  child_dir  child_file
  // Verify deleting parent_dir.
  ScopedTempDir parent_dir;
  ASSERT_TRUE(parent_dir.CreateUniqueTempDir());
  FilePath child_file;
  file_util::CreateTemporaryFileInDir(parent_dir.path(), &child_file);
  FilePath child_dir;
  ASSERT_TRUE(file_util::CreateTemporaryDirInDir(
      parent_dir.path(), FILE_PATH_LITERAL("child_dir"), &child_dir));

  operation()->Remove(URLForPath(parent_dir.path()), true /* recursive */);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_FALSE(file_util::DirectoryExists(parent_dir.path()));
}

TEST_F(FileSystemOperationTest, TestTruncate) {
  ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath file;
  file_util::CreateTemporaryFileInDir(dir.path(), &file);

  char test_data[] = "test data";
  int data_size = static_cast<int>(sizeof(test_data));
  EXPECT_EQ(data_size,
            file_util::WriteFile(file, test_data, data_size));

  // Check that its length is the size of the data written.
  operation()->GetMetadata(URLForPath(file));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());
  EXPECT_FALSE(info().is_directory);
  EXPECT_EQ(data_size, info().size);

  // Extend the file by truncating it.
  int length = 17;
  operation()->Truncate(URLForPath(file), length);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());

  // Check that its length is now 17 and that it's all zeroes after the test
  // data.
  base::PlatformFileInfo info;

  EXPECT_TRUE(file_util::GetFileInfo(file, &info));
  EXPECT_EQ(length, info.size);
  char data[100];
  EXPECT_EQ(length, file_util::ReadFile(file, data, length));
  for (int i = 0; i < length; ++i) {
    if (i < static_cast<int>(sizeof(test_data)))
      EXPECT_EQ(test_data[i], data[i]);
    else
      EXPECT_EQ(0, data[i]);
  }

  // Shorten the file by truncating it.
  length = 3;
  operation()->Truncate(URLForPath(file), length);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kFileOperationSucceeded, status());

  // Check that its length is now 3 and that it contains only bits of test data.
  EXPECT_TRUE(file_util::GetFileInfo(file, &info));
  EXPECT_EQ(length, info.size);
  EXPECT_EQ(length, file_util::ReadFile(file, data, length));
  for (int i = 0; i < length; ++i)
    EXPECT_EQ(test_data[i], data[i]);
}

}  // namespace fileapi
