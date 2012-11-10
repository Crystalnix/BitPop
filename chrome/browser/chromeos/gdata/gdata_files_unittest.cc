// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/gdata/gdata_files.h"

#include <string>
#include <utility>
#include <vector>

#include "base/sequenced_task_runner.h"
#include "base/string_number_conversions.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/message_loop.h"
#include "chrome/browser/chromeos/gdata/gdata.pb.h"
#include "chrome/browser/chromeos/gdata/gdata_cache.h"
#include "chrome/browser/chromeos/gdata/gdata_test_util.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace gdata {
namespace {

// See gdata.proto for the difference between the two URLs.
const char kResumableEditMediaUrl[] = "http://resumable-edit-media/";
const char kResumableCreateMediaUrl[] = "http://resumable-create-media/";

// Add a directory to |parent| and return that directory. The name and
// resource_id are determined by the incrementing counter |sequence_id|.
GDataDirectory* AddDirectory(GDataDirectory* parent,
                             GDataDirectoryService* directory_service,
                             int sequence_id) {
  GDataDirectory* dir = new GDataDirectory(parent, directory_service);
  const std::string dir_name = "dir" + base::IntToString(sequence_id);
  const std::string resource_id = std::string("dir_resource_id:") +
                                  dir_name;
  dir->set_title(dir_name);
  dir->set_resource_id(resource_id);
  GDataFileError error = GDATA_FILE_ERROR_FAILED;
  directory_service->AddEntryToDirectory(
      parent->GetFilePath(),
      dir,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(GDATA_FILE_OK, error);
  return dir;
}

// Add a file to |parent| and return that file. The name and
// resource_id are determined by the incrementing counter |sequence_id|.
GDataFile* AddFile(GDataDirectory* parent,
                   GDataDirectoryService* directory_service,
                   int sequence_id) {
  GDataFile* file = new GDataFile(parent, directory_service);
  const std::string title = "file" + base::IntToString(sequence_id);
  const std::string resource_id = std::string("file_resource_id:") +
                                  title;
  file->set_title(title);
  file->set_resource_id(resource_id);
  file->set_file_md5(std::string("file_md5:") + title);
  GDataFileError error = GDATA_FILE_ERROR_FAILED;
  directory_service->AddEntryToDirectory(
      parent->GetFilePath(),
      file,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(GDATA_FILE_OK, error);
  return file;
}

// Creates the following files/directories
// drive/dir1/
// drive/dir2/
// drive/dir1/dir3/
// drive/dir1/file4
// drive/dir1/file5
// drive/dir2/file6
// drive/dir2/file7
// drive/dir2/file8
// drive/dir1/dir3/file9
// drive/dir1/dir3/file10
void InitDirectoryService(GDataDirectoryService* directory_service) {
  int sequence_id = 1;
  GDataDirectory* dir1 = AddDirectory(directory_service->root(),
      directory_service, sequence_id++);
  GDataDirectory* dir2 = AddDirectory(directory_service->root(),
      directory_service, sequence_id++);
  GDataDirectory* dir3 = AddDirectory(dir1, directory_service, sequence_id++);

  AddFile(dir1, directory_service, sequence_id++);
  AddFile(dir1, directory_service, sequence_id++);

  AddFile(dir2, directory_service, sequence_id++);
  AddFile(dir2, directory_service, sequence_id++);
  AddFile(dir2, directory_service, sequence_id++);

  AddFile(dir3, directory_service, sequence_id++);
  AddFile(dir3, directory_service, sequence_id++);
}

// Find directory by path.
GDataDirectory* FindDirectory(GDataDirectoryService* directory_service,
                              const char* path) {
  return directory_service->FindEntryByPathSync(
      FilePath(path))->AsGDataDirectory();
}

// Find file by path.
GDataFile* FindFile(GDataDirectoryService* directory_service,
                    const char* path) {
  return directory_service->FindEntryByPathSync(FilePath(path))->AsGDataFile();
}

// Verify that the recreated directory service matches what we created in
// InitDirectoryService.
void VerifyDirectoryService(GDataDirectoryService* directory_service) {
  ASSERT_TRUE(directory_service->root());

  GDataDirectory* dir1 = FindDirectory(directory_service, "drive/dir1");
  ASSERT_TRUE(dir1);
  GDataDirectory* dir2 = FindDirectory(directory_service, "drive/dir2");
  ASSERT_TRUE(dir2);
  GDataDirectory* dir3 = FindDirectory(directory_service, "drive/dir1/dir3");
  ASSERT_TRUE(dir3);

  GDataFile* file4 = FindFile(directory_service, "drive/dir1/file4");
  ASSERT_TRUE(file4);
  EXPECT_EQ(file4->parent(), dir1);

  GDataFile* file5 = FindFile(directory_service, "drive/dir1/file5");
  ASSERT_TRUE(file5);
  EXPECT_EQ(file5->parent(), dir1);

  GDataFile* file6 = FindFile(directory_service, "drive/dir2/file6");
  ASSERT_TRUE(file6);
  EXPECT_EQ(file6->parent(), dir2);

  GDataFile* file7 = FindFile(directory_service, "drive/dir2/file7");
  ASSERT_TRUE(file7);
  EXPECT_EQ(file7->parent(), dir2);

  GDataFile* file8 = FindFile(directory_service, "drive/dir2/file8");
  ASSERT_TRUE(file8);
  EXPECT_EQ(file8->parent(), dir2);

  GDataFile* file9 = FindFile(directory_service, "drive/dir1/dir3/file9");
  ASSERT_TRUE(file9);
  EXPECT_EQ(file9->parent(), dir3);

  GDataFile* file10 = FindFile(directory_service, "drive/dir1/dir3/file10");
  ASSERT_TRUE(file10);
  EXPECT_EQ(file10->parent(), dir3);

  EXPECT_EQ(dir1, directory_service->GetEntryByResourceId(
      "dir_resource_id:dir1"));
  EXPECT_EQ(dir2, directory_service->GetEntryByResourceId(
      "dir_resource_id:dir2"));
  EXPECT_EQ(dir3, directory_service->GetEntryByResourceId(
      "dir_resource_id:dir3"));
  EXPECT_EQ(file4, directory_service->GetEntryByResourceId(
      "file_resource_id:file4"));
  EXPECT_EQ(file5, directory_service->GetEntryByResourceId(
      "file_resource_id:file5"));
  EXPECT_EQ(file6, directory_service->GetEntryByResourceId(
      "file_resource_id:file6"));
  EXPECT_EQ(file7, directory_service->GetEntryByResourceId(
      "file_resource_id:file7"));
  EXPECT_EQ(file8, directory_service->GetEntryByResourceId(
      "file_resource_id:file8"));
  EXPECT_EQ(file9, directory_service->GetEntryByResourceId(
      "file_resource_id:file9"));
  EXPECT_EQ(file10, directory_service->GetEntryByResourceId(
      "file_resource_id:file10"));
}

// Callback for GDataDirectoryService::InitFromDB.
void InitFromDBCallback(GDataFileError expected_error,
                        GDataFileError actual_error) {
  EXPECT_EQ(expected_error, actual_error);
}

}  // namespace

TEST(GDataEntryTest, FromProto_DetectBadUploadUrl) {
  GDataEntryProto proto;
  proto.set_title("test.txt");

  GDataEntry entry(NULL, NULL);
  // This should fail as the upload URL is empty.
  ASSERT_FALSE(entry.FromProto(proto));

  // Set a upload URL.
  proto.set_upload_url(kResumableEditMediaUrl);

  // This should succeed as the upload URL is set.
  ASSERT_TRUE(entry.FromProto(proto));
  EXPECT_EQ(kResumableEditMediaUrl, entry.upload_url().spec());
}

TEST(GDataRootDirectoryTest, VersionCheck) {
  // Set up the root directory.
  GDataRootDirectoryProto proto;
  GDataEntryProto* mutable_entry =
      proto.mutable_gdata_directory()->mutable_gdata_entry();
  mutable_entry->mutable_file_info()->set_is_directory(true);
  mutable_entry->set_resource_id(kGDataRootDirectoryResourceId);
  mutable_entry->set_upload_url(kResumableCreateMediaUrl);
  mutable_entry->set_title("drive");

  GDataDirectoryService directory_service;

  std::string serialized_proto;
  ASSERT_TRUE(proto.SerializeToString(&serialized_proto));
  // This should fail as the version is emtpy.
  ASSERT_FALSE(directory_service.ParseFromString(serialized_proto));

  // Set an older version, and serialize.
  proto.set_version(kProtoVersion - 1);
  ASSERT_TRUE(proto.SerializeToString(&serialized_proto));
  // This should fail as the version is older.
  ASSERT_FALSE(directory_service.ParseFromString(serialized_proto));

  // Set the current version, and serialize.
  proto.set_version(kProtoVersion);
  ASSERT_TRUE(proto.SerializeToString(&serialized_proto));
  // This should succeed as the version matches the current number.
  ASSERT_TRUE(directory_service.ParseFromString(serialized_proto));

  // Set a newer version, and serialize.
  proto.set_version(kProtoVersion + 1);
  ASSERT_TRUE(proto.SerializeToString(&serialized_proto));
  // This should fail as the version is newer.
  ASSERT_FALSE(directory_service.ParseFromString(serialized_proto));
}

TEST(GDataRootDirectoryTest, ParseFromString_DetectBadTitle) {
  GDataRootDirectoryProto proto;
  proto.set_version(kProtoVersion);

  GDataEntryProto* mutable_entry =
      proto.mutable_gdata_directory()->mutable_gdata_entry();
  mutable_entry->mutable_file_info()->set_is_directory(true);
  mutable_entry->set_resource_id(kGDataRootDirectoryResourceId);
  mutable_entry->set_upload_url(kResumableCreateMediaUrl);

  std::string serialized_proto;
  ASSERT_TRUE(proto.SerializeToString(&serialized_proto));

  GDataDirectoryService directory_service;
  GDataDirectory* root(directory_service.root());
  // This should fail as the title is empty.
  // root.title() should be unchanged.
  ASSERT_FALSE(directory_service.ParseFromString(serialized_proto));
  ASSERT_EQ(kGDataRootDirectory, root->title());

  // Setting the title to "gdata".
  mutable_entry->set_title("gdata");
  ASSERT_TRUE(proto.SerializeToString(&serialized_proto));

  // This should fail as the title is not kGDataRootDirectory.
  // root.title() should be unchanged.
  ASSERT_FALSE(directory_service.ParseFromString(serialized_proto));
  ASSERT_EQ(kGDataRootDirectory, root->title());

  // Setting the title to kGDataRootDirectory.
  mutable_entry->set_title(kGDataRootDirectory);
  ASSERT_TRUE(proto.SerializeToString(&serialized_proto));

  // This should succeed as the title is kGDataRootDirectory.
  ASSERT_TRUE(directory_service.ParseFromString(serialized_proto));
  ASSERT_EQ(kGDataRootDirectory, root->title());
}

TEST(GDataRootDirectoryTest, ParseFromString_DetectBadResourceID) {
  GDataRootDirectoryProto proto;
  proto.set_version(kProtoVersion);

  GDataEntryProto* mutable_entry =
      proto.mutable_gdata_directory()->mutable_gdata_entry();
  mutable_entry->mutable_file_info()->set_is_directory(true);
  mutable_entry->set_title(kGDataRootDirectory);
  mutable_entry->set_upload_url(kResumableCreateMediaUrl);

  std::string serialized_proto;
  ASSERT_TRUE(proto.SerializeToString(&serialized_proto));

  GDataDirectoryService directory_service;
  GDataDirectory* root(directory_service.root());
  // This should fail as the resource ID is empty.
  // root.resource_id() should be unchanged.
  ASSERT_FALSE(directory_service.ParseFromString(serialized_proto));
  EXPECT_EQ(kGDataRootDirectoryResourceId, root->resource_id());

  // Set the correct resource ID.
  mutable_entry->set_resource_id(kGDataRootDirectoryResourceId);
  ASSERT_TRUE(proto.SerializeToString(&serialized_proto));

  // This should succeed as the resource ID is correct.
  ASSERT_TRUE(directory_service.ParseFromString(serialized_proto));
  EXPECT_EQ(kGDataRootDirectoryResourceId, root->resource_id());
}

// We have a similar test in FromProto_DetectBadUploadUrl, but the test here
// is to ensure that an error in GDataFile::FromProto() is properly
// propagated to GDataRootDirectory::ParseFromString().
TEST(GDataRootDirectoryTest, ParseFromString_DetectNoUploadUrl) {
  // Set up the root directory properly.
  GDataRootDirectoryProto root_directory_proto;
  root_directory_proto.set_version(kProtoVersion);

  GDataEntryProto* mutable_entry =
      root_directory_proto.mutable_gdata_directory()->mutable_gdata_entry();
  mutable_entry->mutable_file_info()->set_is_directory(true);
  mutable_entry->set_title(kGDataRootDirectory);
  mutable_entry->set_resource_id(kGDataRootDirectoryResourceId);
  mutable_entry->set_upload_url(kResumableCreateMediaUrl);

  // Add an empty sub directory under the root directory. This directory is
  // added to ensure that nothing is left when the parsing failed.
  GDataDirectoryProto* sub_directory_proto =
      root_directory_proto.mutable_gdata_directory()->add_child_directories();
  sub_directory_proto->mutable_gdata_entry()->mutable_file_info()->
      set_is_directory(true);
  sub_directory_proto->mutable_gdata_entry()->set_title("empty");
  sub_directory_proto->mutable_gdata_entry()->set_upload_url(
      kResumableCreateMediaUrl);

  // Add a sub directory under the root directory.
  sub_directory_proto =
      root_directory_proto.mutable_gdata_directory()->add_child_directories();
  sub_directory_proto->mutable_gdata_entry()->mutable_file_info()->
      set_is_directory(true);
  sub_directory_proto->mutable_gdata_entry()->set_title("dir");
  sub_directory_proto->mutable_gdata_entry()->set_upload_url(
      kResumableCreateMediaUrl);

  // Add a new file under the sub directory "dir".
  GDataEntryProto* entry_proto =
      sub_directory_proto->add_child_files();
  entry_proto->set_title("test.txt");
  entry_proto->mutable_file_specific_info()->set_file_md5("md5");

  GDataDirectoryService directory_service;
  GDataDirectory* root(directory_service.root());
  // The origin is set to UNINITIALIZED by default.
  ASSERT_EQ(UNINITIALIZED, directory_service.origin());
  std::string serialized_proto;
  // Serialize the proto and check if it's loaded.
  // This should fail as the upload URL is not set for |entry_proto|.
  ASSERT_TRUE(root_directory_proto.SerializeToString(&serialized_proto));
  ASSERT_FALSE(directory_service.ParseFromString(serialized_proto));
  // Nothing should be added to the root directory if the parse failed.
  ASSERT_TRUE(root->child_files().empty());
  ASSERT_TRUE(root->child_directories().empty());
  // The origin should remain UNINITIALIZED because the loading failed.
  ASSERT_EQ(UNINITIALIZED, directory_service.origin());

  // Set an upload URL.
  entry_proto->set_upload_url(kResumableEditMediaUrl);

  // Serialize the proto and check if it's loaded.
  // This should succeed as the upload URL is set for |entry_proto|.
  ASSERT_TRUE(root_directory_proto.SerializeToString(&serialized_proto));
  ASSERT_TRUE(directory_service.ParseFromString(serialized_proto));
  // No file should be added to the root directory.
  ASSERT_TRUE(root->child_files().empty());
  // Two directories ("empty", "dir") should be added to the root directory.
  ASSERT_EQ(2U, root->child_directories().size());
  // The origin should change to FROM_CACHE because we loaded from the cache.
  ASSERT_EQ(FROM_CACHE, directory_service.origin());
}

TEST(GDataRootDirectoryTest, RefreshFile) {
  MessageLoopForUI message_loop;
  GDataDirectoryService directory_service;
  GDataDirectory* root(directory_service.root());
  // Add a directory to the file system.
  GDataDirectory* directory_entry = new GDataDirectory(root,
                                                       &directory_service);
  directory_entry->set_resource_id("folder:directory_resource_id");
  directory_entry->set_title("directory");
  directory_entry->SetBaseNameFromTitle();
  GDataFileError error = GDATA_FILE_ERROR_FAILED;
  directory_service.AddEntryToDirectory(
      FilePath(kGDataRootDirectory),
      directory_entry,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  test_util::RunBlockingPoolTask();
  ASSERT_EQ(GDATA_FILE_OK, error);

  // Add a new file to the directory.
  GDataFile* initial_file_entry = new GDataFile(NULL, &directory_service);
  initial_file_entry->set_resource_id("file:file_resource_id");
  initial_file_entry->set_title("file");
  initial_file_entry->SetBaseNameFromTitle();
  directory_service.AddEntryToDirectory(
      directory_entry->GetFilePath(),
      initial_file_entry,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  test_util::RunBlockingPoolTask();
  ASSERT_EQ(GDATA_FILE_OK, error);

  ASSERT_EQ(directory_entry, initial_file_entry->parent());

  // Initial file system state set, let's try refreshing entries.

  // New value for the entry with resource id "file:file_resource_id".
  GDataFile* new_file_entry = new GDataFile(NULL, &directory_service);
  new_file_entry->set_resource_id("file:file_resource_id");
  directory_service.RefreshFile(scoped_ptr<GDataFile>(new_file_entry).Pass());
  // Root should have |new_file_entry|, not |initial_file_entry|.
  // If this is not true, |new_file_entry| has probably been destroyed, hence
  // ASSERT (we're trying to access |new_file_entry| later on).
  ASSERT_EQ(new_file_entry,
      directory_service.GetEntryByResourceId("file:file_resource_id"));
  // We have just verified new_file_entry exists inside root, so accessing
  // |new_file_entry->parent()| should be safe.
  EXPECT_EQ(directory_entry, new_file_entry->parent());

  // Let's try refreshing file that didn't prviously exist.
  GDataFile* non_existent_entry = new GDataFile(NULL, &directory_service);
  non_existent_entry->set_resource_id("file:does_not_exist");
  directory_service.RefreshFile(
      scoped_ptr<GDataFile>(non_existent_entry).Pass());
  // File with non existent resource id should not be added.
  EXPECT_FALSE(directory_service.GetEntryByResourceId("file:does_not_exist"));
}

TEST(GDataRootDirectoryTest, GetEntryByResourceId_RootDirectory) {
  GDataDirectoryService directory_service;
  // Look up the root directory by its resource ID.
  GDataEntry* entry = directory_service.GetEntryByResourceId(
      kGDataRootDirectoryResourceId);
  ASSERT_TRUE(entry);
  EXPECT_EQ(kGDataRootDirectoryResourceId, entry->resource_id());
}

TEST(GDataRootDirectoryTest, DBTest) {
  MessageLoopForUI message_loop;
  content::TestBrowserThread ui_thread(content::BrowserThread::UI,
                                       &message_loop);

  scoped_ptr<TestingProfile> profile(new TestingProfile);
  scoped_refptr<base::SequencedWorkerPool> pool =
      content::BrowserThread::GetBlockingPool();
  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner =
      pool->GetSequencedTaskRunner(pool->GetSequenceToken());

  GDataDirectoryService directory_service;
  FilePath db_path(GDataCache::GetCacheRootPath(profile.get()).
      AppendASCII("meta").AppendASCII("resource_metadata.db"));
  // InitFromDB should fail with GDATA_FILE_ERROR_NOT_FOUND since the db
  // doesn't exist.
  directory_service.InitFromDB(db_path, blocking_task_runner,
      base::Bind(&InitFromDBCallback, GDATA_FILE_ERROR_NOT_FOUND));
  test_util::RunBlockingPoolTask();
  InitDirectoryService(&directory_service);

  // Write the filesystem to db.
  directory_service.SaveToDB();
  test_util::RunBlockingPoolTask();

  GDataDirectoryService directory_service2;
  // InitFromDB should succeed with GDATA_FILE_OK as the db now exists.
  directory_service2.InitFromDB(db_path, blocking_task_runner,
      base::Bind(&InitFromDBCallback, GDATA_FILE_OK));
  test_util::RunBlockingPoolTask();

  VerifyDirectoryService(&directory_service2);
}

}  // namespace gdata
