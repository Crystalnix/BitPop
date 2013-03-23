// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/drive_file_system_util.h"

#include "base/file_path.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace util {

TEST(DriveFileSystemUtilTest, IsUnderDriveMountPoint) {
  EXPECT_FALSE(IsUnderDriveMountPoint(
      FilePath::FromUTF8Unsafe("/wherever/foo.txt")));
  EXPECT_FALSE(IsUnderDriveMountPoint(
      FilePath::FromUTF8Unsafe("/special/foo.txt")));
  EXPECT_FALSE(IsUnderDriveMountPoint(
      FilePath::FromUTF8Unsafe("/special/drivex/foo.txt")));
  EXPECT_FALSE(IsUnderDriveMountPoint(
      FilePath::FromUTF8Unsafe("special/drivex/foo.txt")));

  EXPECT_TRUE(IsUnderDriveMountPoint(
      FilePath::FromUTF8Unsafe("/special/drive")));
  EXPECT_TRUE(IsUnderDriveMountPoint(
      FilePath::FromUTF8Unsafe("/special/drive/foo.txt")));
  EXPECT_TRUE(IsUnderDriveMountPoint(
      FilePath::FromUTF8Unsafe("/special/drive/subdir/foo.txt")));
}

TEST(DriveFileSystemUtilTest, ExtractDrivePath) {
  EXPECT_EQ(FilePath(),
            ExtractDrivePath(
                FilePath::FromUTF8Unsafe("/wherever/foo.txt")));
  EXPECT_EQ(FilePath(),
            ExtractDrivePath(
                FilePath::FromUTF8Unsafe("/special/foo.txt")));
  EXPECT_EQ(FilePath(),
            ExtractDrivePath(
                FilePath::FromUTF8Unsafe("/special/drivex/foo.txt")));

  EXPECT_EQ(FilePath::FromUTF8Unsafe("drive"),
            ExtractDrivePath(
                FilePath::FromUTF8Unsafe("/special/drive")));
  EXPECT_EQ(FilePath::FromUTF8Unsafe("drive/foo.txt"),
            ExtractDrivePath(
                FilePath::FromUTF8Unsafe("/special/drive/foo.txt")));
  EXPECT_EQ(FilePath::FromUTF8Unsafe("drive/subdir/foo.txt"),
            ExtractDrivePath(
                FilePath::FromUTF8Unsafe("/special/drive/subdir/foo.txt")));
}

TEST(DriveFileSystemUtilTest, EscapeUnescapeCacheFileName) {
  const std::string kUnescapedFileName(
      "tmp:`~!@#$%^&*()-_=+[{|]}\\\\;\',<.>/?");
  const std::string kEscapedFileName(
      "tmp:`~!@#$%25^&*()-_=+[{|]}\\\\;\',<%2E>%2F?");
  EXPECT_EQ(kEscapedFileName, EscapeCacheFileName(kUnescapedFileName));
  EXPECT_EQ(kUnescapedFileName, UnescapeCacheFileName(kEscapedFileName));
}

TEST(DriveFileSystemUtilTest, EscapeUtf8FileName) {
  EXPECT_EQ("", EscapeUtf8FileName(""));
  EXPECT_EQ("foo", EscapeUtf8FileName("foo"));
  EXPECT_EQ("foo\xE2\x88\x95zzz", EscapeUtf8FileName("foo/zzz"));
  EXPECT_EQ("\xE2\x88\x95\xE2\x88\x95\xE2\x88\x95", EscapeUtf8FileName("///"));
}

TEST(DriveFileSystemUtilTest, ExtractResourceIdFromUrl) {
  EXPECT_EQ("file:2_file_resource_id", ExtractResourceIdFromUrl(
      GURL("https://file1_link_self/file:2_file_resource_id")));
  // %3A should be unescaped.
  EXPECT_EQ("file:2_file_resource_id", ExtractResourceIdFromUrl(
      GURL("https://file1_link_self/file%3A2_file_resource_id")));

  // The resource ID cannot be extracted, hence empty.
  EXPECT_EQ("", ExtractResourceIdFromUrl(GURL("https://www.example.com/")));
}

TEST(DriveFileSystemUtilTest, ParseCacheFilePath) {
  std::string resource_id, md5, extra_extension;
  ParseCacheFilePath(
      FilePath::FromUTF8Unsafe(
          "/home/user/GCache/v1/persistent/pdf:a1b2.0123456789abcdef.mounted"),
      &resource_id,
      &md5,
      &extra_extension);
  EXPECT_EQ(resource_id, "pdf:a1b2");
  EXPECT_EQ(md5, "0123456789abcdef");
  EXPECT_EQ(extra_extension, "mounted");

  ParseCacheFilePath(
      FilePath::FromUTF8Unsafe(
          "/home/user/GCache/v1/tmp/pdf:a1b2.0123456789abcdef"),
      &resource_id,
      &md5,
      &extra_extension);
  EXPECT_EQ(resource_id, "pdf:a1b2");
  EXPECT_EQ(md5, "0123456789abcdef");
  EXPECT_EQ(extra_extension, "");

  ParseCacheFilePath(
      FilePath::FromUTF8Unsafe(
          "/home/user/GCache/v1/pinned/pdf:a1b2"),
      &resource_id,
      &md5,
      &extra_extension);
  EXPECT_EQ(resource_id, "pdf:a1b2");
  EXPECT_EQ(md5, "");
  EXPECT_EQ(extra_extension, "");
}

}  // namespace util
}  // namespace drive
