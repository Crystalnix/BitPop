// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "chrome/browser/extensions/file_reader.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/extensions/extension_test_util.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;

namespace {

class FileReaderTest : public testing::Test {
 public:
  FileReaderTest() : file_thread_(BrowserThread::FILE) {
    file_thread_.Start();
  }
 private:
  MessageLoop message_loop_;
  content::TestBrowserThread file_thread_;
};

class Receiver {
 public:
  Receiver() : succeeded_(false) {
  }

  FileReader::Callback NewCallback() {
    return base::Bind(&Receiver::DidReadFile, base::Unretained(this));
  }

  bool succeeded() const { return succeeded_; }
  const std::string& data() const { return data_; }

 private:
  void DidReadFile(bool success, const std::string& data) {
    succeeded_ = success;
    data_ = data;
    MessageLoop::current()->Quit();
  }

  bool succeeded_;
  std::string data_;
};

void RunBasicTest(const char* filename) {
  FilePath path;
  PathService::Get(chrome::DIR_TEST_DATA, &path);
  std::string extension_id = extension_test_util::MakeId("test");
  ExtensionResource resource(extension_id, path,
                             FilePath().AppendASCII(filename));
  path = path.AppendASCII(filename);

  std::string file_contents;
  bool file_exists = file_util::ReadFileToString(path, &file_contents);

  Receiver receiver;

  scoped_refptr<FileReader> file_reader(
      new FileReader(resource, receiver.NewCallback()));
  file_reader->Start();

  MessageLoop::current()->Run();

  EXPECT_EQ(file_exists, receiver.succeeded());
  EXPECT_EQ(file_contents, receiver.data());
}

TEST_F(FileReaderTest, SmallFile) {
  RunBasicTest("title1.html");
}

TEST_F(FileReaderTest, BiggerFile) {
  RunBasicTest("download-test1.lib");
}

TEST_F(FileReaderTest, NonExistantFile) {
  FilePath path;
  PathService::Get(chrome::DIR_TEST_DATA, &path);
  std::string extension_id = extension_test_util::MakeId("test");
  ExtensionResource resource(extension_id, path, FilePath(
      FILE_PATH_LITERAL("file_that_does_not_exist")));
  path = path.AppendASCII("file_that_does_not_exist");

  Receiver receiver;

  scoped_refptr<FileReader> file_reader(
      new FileReader(resource, receiver.NewCallback()));
  file_reader->Start();

  MessageLoop::current()->Run();

  EXPECT_FALSE(receiver.succeeded());
}

}  // namespace
