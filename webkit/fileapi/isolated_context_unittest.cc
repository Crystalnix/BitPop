// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/isolated_context.h"

#include <string>

#include "base/basictypes.h"
#include "base/logging.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/fileapi/isolated_context.h"

#define FPL(x) FILE_PATH_LITERAL(x)

#if defined(FILE_PATH_USES_DRIVE_LETTERS)
#define DRIVE FPL("C:")
#else
#define DRIVE
#endif

namespace fileapi {

typedef IsolatedContext::FileInfo FileInfo;

namespace {

const FilePath kTestPaths[] = {
  FilePath(DRIVE FPL("/a/b.txt")),
  FilePath(DRIVE FPL("/c/d/e")),
  FilePath(DRIVE FPL("/h/")),
  FilePath(DRIVE FPL("/")),
#if defined(FILE_PATH_USES_WIN_SEPARATORS)
  FilePath(DRIVE FPL("\\foo\\bar")),
  FilePath(DRIVE FPL("\\")),
#endif
  // For duplicated base name test.
  FilePath(DRIVE FPL("/")),
  FilePath(DRIVE FPL("/f/e")),
  FilePath(DRIVE FPL("/f/b.txt")),
};

}  // namespace

class IsolatedContextTest : public testing::Test {
 public:
  IsolatedContextTest() {
    for (size_t i = 0; i < arraysize(kTestPaths); ++i)
      fileset_.insert(kTestPaths[i].NormalizePathSeparators());
  }

  void SetUp() {
    IsolatedContext::FileInfoSet files;
    for (size_t i = 0; i < arraysize(kTestPaths); ++i) {
      std::string name;
      ASSERT_TRUE(
          files.AddPath(kTestPaths[i].NormalizePathSeparators(), &name));
      names_.push_back(name);
    }
    id_ = IsolatedContext::GetInstance()->RegisterDraggedFileSystem(files);
    IsolatedContext::GetInstance()->AddReference(id_);
    ASSERT_FALSE(id_.empty());
  }

  void TearDown() {
    IsolatedContext::GetInstance()->RemoveReference(id_);
  }

  IsolatedContext* isolated_context() const {
    return IsolatedContext::GetInstance();
  }

 protected:
  std::string id_;
  std::multiset<FilePath> fileset_;
  std::vector<std::string> names_;

 private:
  DISALLOW_COPY_AND_ASSIGN(IsolatedContextTest);
};

TEST_F(IsolatedContextTest, RegisterAndRevokeTest) {
  // See if the returned top-level entries match with what we registered.
  std::vector<FileInfo> toplevels;
  ASSERT_TRUE(isolated_context()->GetDraggedFileInfo(id_, &toplevels));
  ASSERT_EQ(fileset_.size(), toplevels.size());
  for (size_t i = 0; i < toplevels.size(); ++i) {
    ASSERT_TRUE(fileset_.find(toplevels[i].path) != fileset_.end());
  }

  // See if the name of each registered kTestPaths (that is what we
  // register in SetUp() by RegisterDraggedFileSystem) is properly cracked as
  // a valid virtual path in the isolated filesystem.
  for (size_t i = 0; i < arraysize(kTestPaths); ++i) {
    FilePath virtual_path = isolated_context()->CreateVirtualRootPath(id_)
        .AppendASCII(names_[i]);
    std::string cracked_id;
    FilePath cracked_path;
    FileSystemType cracked_type;
    ASSERT_TRUE(isolated_context()->CrackIsolatedPath(
        virtual_path, &cracked_id, &cracked_type, &cracked_path));
    ASSERT_EQ(kTestPaths[i].NormalizePathSeparators().value(),
              cracked_path.value());
    ASSERT_EQ(id_, cracked_id);
    ASSERT_EQ(kFileSystemTypeDragged, cracked_type);
  }

  // Make sure GetRegisteredPath returns false for id_ since it is
  // registered for dragged files.
  FilePath path;
  ASSERT_FALSE(isolated_context()->GetRegisteredPath(id_, &path));

  // Deref the current one and registering a new one.
  isolated_context()->RemoveReference(id_);

  std::string id2 = isolated_context()->RegisterFileSystemForPath(
      kFileSystemTypeIsolated, FilePath(DRIVE FPL("/foo")), NULL);

  // Make sure the GetDraggedFileInfo returns false for both ones.
  ASSERT_FALSE(isolated_context()->GetDraggedFileInfo(id2, &toplevels));
  ASSERT_FALSE(isolated_context()->GetDraggedFileInfo(id_, &toplevels));

  // Make sure the GetRegisteredPath returns true only for the new one.
  ASSERT_FALSE(isolated_context()->GetRegisteredPath(id_, &path));
  ASSERT_TRUE(isolated_context()->GetRegisteredPath(id2, &path));

  // Try registering two more file systems for the same path as id2.
  std::string id3 = isolated_context()->RegisterFileSystemForPath(
      kFileSystemTypeIsolated, path, NULL);
  std::string id4 = isolated_context()->RegisterFileSystemForPath(
      kFileSystemTypeIsolated, path, NULL);

  // Remove file system for id4.
  isolated_context()->AddReference(id4);
  isolated_context()->RemoveReference(id4);

  // Only id4 should become invalid now.
  ASSERT_TRUE(isolated_context()->GetRegisteredPath(id2, &path));
  ASSERT_TRUE(isolated_context()->GetRegisteredPath(id3, &path));
  ASSERT_FALSE(isolated_context()->GetRegisteredPath(id4, &path));

  // Revoke the file systems by path.
  isolated_context()->RevokeFileSystemByPath(path);

  // Now all the file systems associated to the path must be invalid.
  ASSERT_FALSE(isolated_context()->GetRegisteredPath(id2, &path));
  ASSERT_FALSE(isolated_context()->GetRegisteredPath(id3, &path));
  ASSERT_FALSE(isolated_context()->GetRegisteredPath(id4, &path));
}

TEST_F(IsolatedContextTest, CrackWithRelativePaths) {
  const struct {
    FilePath::StringType path;
    bool valid;
  } relatives[] = {
    { FPL("foo"), true },
    { FPL("foo/bar"), true },
    { FPL(".."), false },
    { FPL("foo/.."), false },
    { FPL("foo/../bar"), false },
#if defined(FILE_PATH_USES_WIN_SEPARATORS)
# define SHOULD_FAIL_WITH_WIN_SEPARATORS false
#else
# define SHOULD_FAIL_WITH_WIN_SEPARATORS true
#endif
    { FPL("foo\\..\\baz"), SHOULD_FAIL_WITH_WIN_SEPARATORS },
    { FPL("foo/..\\baz"), SHOULD_FAIL_WITH_WIN_SEPARATORS },
  };

  for (size_t i = 0; i < arraysize(kTestPaths); ++i) {
    for (size_t j = 0; j < ARRAYSIZE_UNSAFE(relatives); ++j) {
      SCOPED_TRACE(testing::Message() << "Testing "
                   << kTestPaths[i].value() << " " << relatives[j].path);
      FilePath virtual_path = isolated_context()->CreateVirtualRootPath(id_)
          .AppendASCII(names_[i]).Append(relatives[j].path);
      std::string cracked_id;
      FilePath cracked_path;
    FileSystemType cracked_type;
      if (!relatives[j].valid) {
        ASSERT_FALSE(isolated_context()->CrackIsolatedPath(
            virtual_path, &cracked_id, &cracked_type, &cracked_path));
        continue;
      }
      ASSERT_TRUE(isolated_context()->CrackIsolatedPath(
          virtual_path, &cracked_id, &cracked_type, &cracked_path));
      ASSERT_EQ(kTestPaths[i].Append(relatives[j].path)
                    .NormalizePathSeparators().value(),
                cracked_path.value());
      ASSERT_EQ(id_, cracked_id);
      ASSERT_EQ(kFileSystemTypeDragged, cracked_type);
    }
  }
}

TEST_F(IsolatedContextTest, TestWithVirtualRoot) {
  std::string cracked_id;
  FilePath cracked_path;

  // Trying to crack virtual root "/" returns true but with empty cracked path
  // as "/" of the isolated filesystem is a pure virtual directory
  // that has no corresponding platform directory.
  FilePath virtual_path = isolated_context()->CreateVirtualRootPath(id_);
  ASSERT_TRUE(isolated_context()->CrackIsolatedPath(
      virtual_path, &cracked_id, NULL, &cracked_path));
  ASSERT_EQ(FPL(""), cracked_path.value());
  ASSERT_EQ(id_, cracked_id);

  // Trying to crack "/foo" should fail (because "foo" is not the one
  // included in the kTestPaths).
  virtual_path = isolated_context()->CreateVirtualRootPath(
      id_).AppendASCII("foo");
  ASSERT_FALSE(isolated_context()->CrackIsolatedPath(
      virtual_path, &cracked_id, NULL, &cracked_path));
}

}  // namespace fileapi
