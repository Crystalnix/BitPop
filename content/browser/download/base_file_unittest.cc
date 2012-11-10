// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/download/base_file.h"

#include "base/file_util.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/scoped_temp_dir.h"
#include "base/string_number_conversions.h"
#include "base/test/test_file_util.h"
#include "content/browser/browser_thread_impl.h"
#include "crypto/secure_hash.h"
#include "net/base/file_stream.h"
#include "net/base/mock_file_stream.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;
using content::BrowserThreadImpl;

namespace {

const char kTestData1[] = "Let's write some data to the file!\n";
const char kTestData2[] = "Writing more data.\n";
const char kTestData3[] = "Final line.";
const char kTestData4[] = "supercalifragilisticexpialidocious";
const int kTestDataLength1 = arraysize(kTestData1) - 1;
const int kTestDataLength2 = arraysize(kTestData2) - 1;
const int kTestDataLength3 = arraysize(kTestData3) - 1;
const int kTestDataLength4 = arraysize(kTestData4) - 1;
const int kElapsedTimeSeconds = 5;
const base::TimeDelta kElapsedTimeDelta = base::TimeDelta::FromSeconds(
    kElapsedTimeSeconds);

}  // namespace

class BaseFileTest : public testing::Test {
 public:
  static const size_t kSha256HashLen = 32;
  static const unsigned char kEmptySha256Hash[kSha256HashLen];

  BaseFileTest()
      : expect_file_survives_(false),
        expect_in_progress_(true),
        expected_error_(false),
        file_thread_(BrowserThread::FILE, &message_loop_) {
  }

  virtual void SetUp() {
    ResetHash();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_file_.reset(new BaseFile(FilePath(),
                                  GURL(),
                                  GURL(),
                                  0,
                                  false,
                                  "",
                                  file_stream_,
                                  net::BoundNetLog()));
  }

  virtual void TearDown() {
    EXPECT_FALSE(base_file_->in_progress());
    if (!expected_error_) {
      EXPECT_EQ(static_cast<int64>(expected_data_.size()),
                base_file_->bytes_so_far());
    }

    FilePath full_path = base_file_->full_path();

    if (!expected_data_.empty() && !expected_error_) {
      // Make sure the data has been properly written to disk.
      std::string disk_data;
      EXPECT_TRUE(file_util::ReadFileToString(full_path, &disk_data));
      EXPECT_EQ(expected_data_, disk_data);
    }

    // Make sure the mock BrowserThread outlives the BaseFile to satisfy
    // thread checks inside it.
    base_file_.reset();

    EXPECT_EQ(expect_file_survives_, file_util::PathExists(full_path));
  }

  void ResetHash() {
    secure_hash_.reset(crypto::SecureHash::Create(crypto::SecureHash::SHA256));
    memcpy(sha256_hash_, kEmptySha256Hash, kSha256HashLen);
  }

  void UpdateHash(const char* data, size_t length) {
    secure_hash_->Update(data, length);
  }

  std::string GetFinalHash() {
    std::string hash;
    secure_hash_->Finish(sha256_hash_, kSha256HashLen);
    hash.assign(reinterpret_cast<const char*>(sha256_hash_),
                sizeof(sha256_hash_));
    return hash;
  }

  void MakeFileWithHash() {
    base_file_.reset(new BaseFile(FilePath(),
                                  GURL(),
                                  GURL(),
                                  0,
                                  true,
                                  "",
                                  file_stream_,
                                  net::BoundNetLog()));
  }

  bool OpenMockFileStream() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

    FilePath path;
    if (!file_util::CreateTemporaryFile(&path))
      return false;

    // Create a new file stream.
    mock_file_stream_.reset(new net::testing::MockFileStream(NULL));
    if (mock_file_stream_->OpenSync(
        path,
        base::PLATFORM_FILE_OPEN_ALWAYS | base::PLATFORM_FILE_WRITE) != 0) {
      mock_file_stream_.reset();
      return false;
    }

    return true;
  }

  void ForceError(net::Error error) {
    mock_file_stream_->set_forced_error(error);
  }

  int AppendDataToFile(const std::string& data) {
    EXPECT_EQ(expect_in_progress_, base_file_->in_progress());
    expected_error_ = mock_file_stream_.get() &&
                      (mock_file_stream_->forced_error() != net::OK);
    int appended = base_file_->AppendDataToFile(data.data(), data.size());
    if (appended == net::OK)
      EXPECT_TRUE(expect_in_progress_)
          << " appended = " << appended;
    if (base_file_->in_progress()) {
      expected_data_ += data;
      if (!expected_error_) {
        EXPECT_EQ(static_cast<int64>(expected_data_.size()),
                  base_file_->bytes_so_far());
      }
    }
    return appended;
  }

  void set_expected_data(const std::string& data) { expected_data_ = data; }

  // Helper functions.
  // Create a file.  Returns the complete file path.
  static FilePath CreateTestFile() {
    FilePath file_name;
    linked_ptr<net::FileStream> dummy_file_stream;
    BaseFile file(FilePath(),
                  GURL(),
                  GURL(),
                  0,
                  false,
                  "",
                  dummy_file_stream,
                  net::BoundNetLog());

    EXPECT_EQ(net::OK, file.Initialize());
    file_name = file.full_path();
    EXPECT_NE(FilePath::StringType(), file_name.value());

    EXPECT_EQ(net::OK, file.AppendDataToFile(kTestData4, kTestDataLength4));

    // Keep the file from getting deleted when existing_file_name is deleted.
    file.Detach();

    return file_name;
  }

  // Create a file with the specified file name.
  static void CreateFileWithName(const FilePath& file_name) {
    EXPECT_NE(FilePath::StringType(), file_name.value());
    linked_ptr<net::FileStream> dummy_file_stream;
    BaseFile duplicate_file(file_name,
                            GURL(),
                            GURL(),
                            0,
                            false,
                            "",
                            dummy_file_stream,
                            net::BoundNetLog());
    EXPECT_EQ(net::OK, duplicate_file.Initialize());
    // Write something into it.
    duplicate_file.AppendDataToFile(kTestData4, kTestDataLength4);
    // Detach the file so it isn't deleted on destruction of |duplicate_file|.
    duplicate_file.Detach();
  }

  int64 CurrentSpeedAtTime(base::TimeTicks current_time) {
    EXPECT_TRUE(base_file_.get());
    return base_file_->CurrentSpeedAtTime(current_time);
  }

  base::TimeTicks StartTick() {
    EXPECT_TRUE(base_file_.get());
    return base_file_->start_tick_;
  }

 protected:
  linked_ptr<net::FileStream> file_stream_;
  linked_ptr<net::testing::MockFileStream> mock_file_stream_;

  // BaseClass instance we are testing.
  scoped_ptr<BaseFile> base_file_;

  // Temporary directory for renamed downloads.
  ScopedTempDir temp_dir_;

  // Expect the file to survive deletion of the BaseFile instance.
  bool expect_file_survives_;

  // Expect the file to be in progress.
  bool expect_in_progress_;

  // Hash calculator.
  scoped_ptr<crypto::SecureHash> secure_hash_;

  unsigned char sha256_hash_[kSha256HashLen];

 private:
  // Keep track of what data should be saved to the disk file.
  std::string expected_data_;
  bool expected_error_;

  // Mock file thread to satisfy debug checks in BaseFile.
  MessageLoop message_loop_;
  BrowserThreadImpl file_thread_;
};

// This will initialize the entire array to zero.
const unsigned char BaseFileTest::kEmptySha256Hash[] = { 0 };

// Test the most basic scenario: just create the object and do a sanity check
// on all its accessors. This is actually a case that rarely happens
// in production, where we would at least Initialize it.
TEST_F(BaseFileTest, CreateDestroy) {
  EXPECT_EQ(FilePath().value(), base_file_->full_path().value());
}

// Cancel the download explicitly.
TEST_F(BaseFileTest, Cancel) {
  ASSERT_EQ(net::OK, base_file_->Initialize());
  EXPECT_TRUE(file_util::PathExists(base_file_->full_path()));
  base_file_->Cancel();
  EXPECT_FALSE(file_util::PathExists(base_file_->full_path()));
  EXPECT_NE(FilePath().value(), base_file_->full_path().value());
}

// Write data to the file and detach it, so it doesn't get deleted
// automatically when base_file_ is destructed.
TEST_F(BaseFileTest, WriteAndDetach) {
  ASSERT_EQ(net::OK, base_file_->Initialize());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  base_file_->Finish();
  base_file_->Detach();
  expect_file_survives_ = true;
}

// Write data to the file and detach it, and calculate its sha256 hash.
TEST_F(BaseFileTest, WriteWithHashAndDetach) {
  // Calculate the final hash.
  ResetHash();
  UpdateHash(kTestData1, kTestDataLength1);
  std::string expected_hash = GetFinalHash();
  std::string expected_hash_hex =
      base::HexEncode(expected_hash.data(), expected_hash.size());

  MakeFileWithHash();
  ASSERT_EQ(net::OK, base_file_->Initialize());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  base_file_->Finish();

  std::string hash;
  base_file_->GetHash(&hash);
  EXPECT_EQ("0B2D3F3F7943AD64B860DF94D05CB56A8A97C6EC5768B5B70B930C5AA7FA9ADE",
            expected_hash_hex);
  EXPECT_EQ(expected_hash_hex, base::HexEncode(hash.data(), hash.size()));

  base_file_->Detach();
  expect_file_survives_ = true;
}

// Rename the file after writing to it, then detach.
TEST_F(BaseFileTest, WriteThenRenameAndDetach) {
  ASSERT_EQ(net::OK, base_file_->Initialize());

  FilePath initial_path(base_file_->full_path());
  EXPECT_TRUE(file_util::PathExists(initial_path));
  FilePath new_path(temp_dir_.path().AppendASCII("NewFile"));
  EXPECT_FALSE(file_util::PathExists(new_path));

  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));

  EXPECT_EQ(net::OK, base_file_->Rename(new_path));
  EXPECT_FALSE(file_util::PathExists(initial_path));
  EXPECT_TRUE(file_util::PathExists(new_path));

  base_file_->Finish();
  base_file_->Detach();
  expect_file_survives_ = true;
}

// Write data to the file once.
TEST_F(BaseFileTest, SingleWrite) {
  ASSERT_EQ(net::OK, base_file_->Initialize());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  base_file_->Finish();
}

// Write data to the file multiple times.
TEST_F(BaseFileTest, MultipleWrites) {
  ASSERT_EQ(net::OK, base_file_->Initialize());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData2));
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData3));
  std::string hash;
  EXPECT_FALSE(base_file_->GetHash(&hash));
  base_file_->Finish();
}

// Write data to the file once and calculate its sha256 hash.
TEST_F(BaseFileTest, SingleWriteWithHash) {
  // Calculate the final hash.
  ResetHash();
  UpdateHash(kTestData1, kTestDataLength1);
  std::string expected_hash = GetFinalHash();
  std::string expected_hash_hex =
      base::HexEncode(expected_hash.data(), expected_hash.size());

  MakeFileWithHash();
  ASSERT_EQ(net::OK, base_file_->Initialize());
  // Can get partial hash states before Finish() is called.
  EXPECT_STRNE(std::string().c_str(), base_file_->GetHashState().c_str());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  EXPECT_STRNE(std::string().c_str(), base_file_->GetHashState().c_str());
  base_file_->Finish();

  std::string hash;
  base_file_->GetHash(&hash);
  EXPECT_EQ(expected_hash_hex, base::HexEncode(hash.data(), hash.size()));
}

// Write data to the file multiple times and calculate its sha256 hash.
TEST_F(BaseFileTest, MultipleWritesWithHash) {
  // Calculate the final hash.
  ResetHash();
  UpdateHash(kTestData1, kTestDataLength1);
  UpdateHash(kTestData2, kTestDataLength2);
  UpdateHash(kTestData3, kTestDataLength3);
  std::string expected_hash = GetFinalHash();
  std::string expected_hash_hex =
      base::HexEncode(expected_hash.data(), expected_hash.size());

  std::string hash;
  MakeFileWithHash();
  ASSERT_EQ(net::OK, base_file_->Initialize());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData2));
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData3));
  // No hash before Finish() is called.
  EXPECT_FALSE(base_file_->GetHash(&hash));
  base_file_->Finish();

  EXPECT_TRUE(base_file_->GetHash(&hash));
  EXPECT_EQ("CBF68BF10F8003DB86B31343AFAC8C7175BD03FB5FC905650F8C80AF087443A8",
            expected_hash_hex);
  EXPECT_EQ(expected_hash_hex, base::HexEncode(hash.data(), hash.size()));
}

// Write data to the file multiple times, interrupt it, and continue using
// another file.  Calculate the resulting combined sha256 hash.
TEST_F(BaseFileTest, MultipleWritesInterruptedWithHash) {
  // Calculate the final hash.
  ResetHash();
  UpdateHash(kTestData1, kTestDataLength1);
  UpdateHash(kTestData2, kTestDataLength2);
  UpdateHash(kTestData3, kTestDataLength3);
  std::string expected_hash = GetFinalHash();
  std::string expected_hash_hex =
      base::HexEncode(expected_hash.data(), expected_hash.size());

  MakeFileWithHash();
  ASSERT_EQ(net::OK, base_file_->Initialize());
  // Write some data
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData2));
  // Get the hash state and file name.
  std::string hash_state;
  hash_state = base_file_->GetHashState();
  // Finish the file.
  base_file_->Finish();

  // Create another file
  linked_ptr<net::FileStream> second_stream;
  BaseFile second_file(FilePath(),
                       GURL(),
                       GURL(),
                       base_file_->bytes_so_far(),
                       true,
                       hash_state,
                       second_stream,
                       net::BoundNetLog());
  ASSERT_EQ(net::OK, second_file.Initialize());
  std::string data(kTestData3);
  EXPECT_EQ(net::OK, second_file.AppendDataToFile(data.data(), data.size()));
  second_file.Finish();

  std::string hash;
  EXPECT_TRUE(second_file.GetHash(&hash));
  // This will fail until getting the hash state is supported in SecureHash.
  EXPECT_STREQ(expected_hash_hex.c_str(),
               base::HexEncode(hash.data(), hash.size()).c_str());
}

// Rename the file after all writes to it.
TEST_F(BaseFileTest, WriteThenRename) {
  ASSERT_EQ(net::OK, base_file_->Initialize());

  FilePath initial_path(base_file_->full_path());
  EXPECT_TRUE(file_util::PathExists(initial_path));
  FilePath new_path(temp_dir_.path().AppendASCII("NewFile"));
  EXPECT_FALSE(file_util::PathExists(new_path));

  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));

  EXPECT_EQ(net::OK, base_file_->Rename(new_path));
  EXPECT_FALSE(file_util::PathExists(initial_path));
  EXPECT_TRUE(file_util::PathExists(new_path));

  base_file_->Finish();
}

// Rename the file while the download is still in progress.
TEST_F(BaseFileTest, RenameWhileInProgress) {
  ASSERT_EQ(net::OK, base_file_->Initialize());

  FilePath initial_path(base_file_->full_path());
  EXPECT_TRUE(file_util::PathExists(initial_path));
  FilePath new_path(temp_dir_.path().AppendASCII("NewFile"));
  EXPECT_FALSE(file_util::PathExists(new_path));

  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));

  EXPECT_TRUE(base_file_->in_progress());
  EXPECT_EQ(net::OK, base_file_->Rename(new_path));
  EXPECT_FALSE(file_util::PathExists(initial_path));
  EXPECT_TRUE(file_util::PathExists(new_path));

  ASSERT_EQ(net::OK, AppendDataToFile(kTestData2));

  base_file_->Finish();
}

// Test that a failed rename reports the correct error.
TEST_F(BaseFileTest, RenameWithError) {
  ASSERT_EQ(net::OK, base_file_->Initialize());

  // TestDir is a subdirectory in |temp_dir_| that we will make read-only so
  // that the rename will fail.
  FilePath test_dir(temp_dir_.path().AppendASCII("TestDir"));
  ASSERT_TRUE(file_util::CreateDirectory(test_dir));

  FilePath new_path(test_dir.AppendASCII("TestFile"));
  EXPECT_FALSE(file_util::PathExists(new_path));

  {
    file_util::PermissionRestorer restore_permissions_for(test_dir);
    ASSERT_TRUE(file_util::MakeFileUnwritable(test_dir));
    EXPECT_EQ(net::ERR_ACCESS_DENIED, base_file_->Rename(new_path));
  }

  base_file_->Finish();
}

// Write data to the file multiple times.
TEST_F(BaseFileTest, MultipleWritesWithError) {
  ASSERT_TRUE(OpenMockFileStream());
  base_file_.reset(new BaseFile(mock_file_stream_->get_path(),
                                GURL(),
                                GURL(),
                                0,
                                false,
                                "",
                                mock_file_stream_,
                                net::BoundNetLog()));
  EXPECT_EQ(net::OK, base_file_->Initialize());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData2));
  ForceError(net::ERR_ACCESS_DENIED);
  ASSERT_NE(net::OK, AppendDataToFile(kTestData3));
  std::string hash;
  EXPECT_FALSE(base_file_->GetHash(&hash));
  base_file_->Finish();
}

// Try to write to uninitialized file.
TEST_F(BaseFileTest, UninitializedFile) {
  expect_in_progress_ = false;
  EXPECT_EQ(net::ERR_INVALID_HANDLE, AppendDataToFile(kTestData1));
}

// Create two |BaseFile|s with the same file, and attempt to write to both.
// Overwrite base_file_ with another file with the same name and
// non-zero contents, and make sure the last file to close 'wins'.
TEST_F(BaseFileTest, DuplicateBaseFile) {
  EXPECT_EQ(net::OK, base_file_->Initialize());

  // Create another |BaseFile| referring to the file that |base_file_| owns.
  CreateFileWithName(base_file_->full_path());

  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  base_file_->Finish();
}

// Create a file and append to it.
TEST_F(BaseFileTest, AppendToBaseFile) {
  // Create a new file.
  FilePath existing_file_name = CreateTestFile();

  set_expected_data(kTestData4);

  // Use the file we've just created.
  base_file_.reset(new BaseFile(existing_file_name,
                                GURL(),
                                GURL(),
                                kTestDataLength4,
                                false,
                                "",
                                file_stream_,
                                net::BoundNetLog()));

  EXPECT_EQ(net::OK, base_file_->Initialize());

  const FilePath file_name = base_file_->full_path();
  EXPECT_NE(FilePath::StringType(), file_name.value());

  // Write into the file.
  EXPECT_EQ(net::OK, AppendDataToFile(kTestData1));

  base_file_->Finish();
  base_file_->Detach();
  expect_file_survives_ = true;
}

// Create a read-only file and attempt to write to it.
TEST_F(BaseFileTest, ReadonlyBaseFile) {
  // Create a new file.
  FilePath readonly_file_name = CreateTestFile();

  // Restore permissions to the file when we are done with this test.
  file_util::PermissionRestorer restore_permissions(readonly_file_name);

  // Make it read-only.
  EXPECT_TRUE(file_util::MakeFileUnwritable(readonly_file_name));

  // Try to overwrite it.
  base_file_.reset(new BaseFile(readonly_file_name,
                                GURL(),
                                GURL(),
                                0,
                                false,
                                "",
                                file_stream_,
                                net::BoundNetLog()));

  expect_in_progress_ = false;

  int init_error = base_file_->Initialize();
  DVLOG(1) << " init_error = " << init_error;
  EXPECT_NE(net::OK, init_error);

  const FilePath file_name = base_file_->full_path();
  EXPECT_NE(FilePath::StringType(), file_name.value());

  // Write into the file.
  EXPECT_NE(net::OK, AppendDataToFile(kTestData1));

  base_file_->Finish();
  base_file_->Detach();
  expect_file_survives_ = true;
}

TEST_F(BaseFileTest, IsEmptyHash) {
  std::string empty(BaseFile::kSha256HashLen, '\x00');
  EXPECT_TRUE(BaseFile::IsEmptyHash(empty));
  std::string not_empty(BaseFile::kSha256HashLen, '\x01');
  EXPECT_FALSE(BaseFile::IsEmptyHash(not_empty));
  EXPECT_FALSE(BaseFile::IsEmptyHash(""));
}

// Test that calculating speed after no writes.
TEST_F(BaseFileTest, SpeedWithoutWrite) {
  ASSERT_EQ(net::OK, base_file_->Initialize());
  base::TimeTicks current = StartTick() + kElapsedTimeDelta;
  ASSERT_EQ(0, CurrentSpeedAtTime(current));
  base_file_->Finish();
}

// Test that calculating speed after a single write.
TEST_F(BaseFileTest, SpeedAfterSingleWrite) {
  ASSERT_EQ(net::OK, base_file_->Initialize());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  base::TimeTicks current = StartTick() + kElapsedTimeDelta;
  int64 expected_speed = kTestDataLength1 / kElapsedTimeSeconds;
  ASSERT_EQ(expected_speed, CurrentSpeedAtTime(current));
  base_file_->Finish();
}

// Test that calculating speed after a multiple writes.
TEST_F(BaseFileTest, SpeedAfterMultipleWrite) {
  ASSERT_EQ(net::OK, base_file_->Initialize());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData2));
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData3));
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData4));
  base::TimeTicks current = StartTick() + kElapsedTimeDelta;
  int64 expected_speed = (kTestDataLength1 + kTestDataLength2 +
      kTestDataLength3 + kTestDataLength4) / kElapsedTimeSeconds;
  ASSERT_EQ(expected_speed, CurrentSpeedAtTime(current));
  base_file_->Finish();
}

// Test that calculating speed after no delay - should not divide by 0.
TEST_F(BaseFileTest, SpeedAfterNoElapsedTime) {
  ASSERT_EQ(net::OK, base_file_->Initialize());
  ASSERT_EQ(net::OK, AppendDataToFile(kTestData1));
  ASSERT_EQ(0, CurrentSpeedAtTime(StartTick()));
  base_file_->Finish();
}
