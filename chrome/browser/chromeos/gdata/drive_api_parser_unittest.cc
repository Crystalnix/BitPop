// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "base/path_service.h"
#include "base/string16.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/gdata/drive_api_parser.h"
#include "chrome/browser/chromeos/gdata/gdata_util.h"
#include "chrome/common/chrome_paths.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Value;
using base::DictionaryValue;
using base::ListValue;

namespace gdata {

class DriveAPIParserTest : public testing::Test {
 protected:
  static Value* LoadJSONFile(const std::string& filename) {
    FilePath path;
    std::string error;
    // Test files for this unit test are located in
    // src/chrome/test/data/chromeos/drive/*
    PathService::Get(chrome::DIR_TEST_DATA, &path);
    path = path.AppendASCII("chromeos")
        .AppendASCII("drive")
        .AppendASCII(filename.c_str());
    EXPECT_TRUE(file_util::PathExists(path)) <<
        "Couldn't find " << path.value();

    JSONFileValueSerializer serializer(path);
    Value* value = serializer.Deserialize(NULL, &error);
    EXPECT_TRUE(value) << "Parse error " << path.value() << ": " << error;
    return value;
  }
};

// Test about resource parsing.
TEST_F(DriveAPIParserTest, AboutResourceParser) {
  std::string error;
  scoped_ptr<Value> document(LoadJSONFile("about.json"));
  ASSERT_TRUE(document.get());

  ASSERT_EQ(Value::TYPE_DICTIONARY, document->GetType());
  scoped_ptr<AboutResource> resource(new AboutResource());
  EXPECT_TRUE(resource->Parse(*document));

  EXPECT_EQ("0AIv7G8yEYAWHUk9123", resource->root_folder_id());
  EXPECT_EQ(5368709120LL, resource->quota_bytes_total());
  EXPECT_EQ(1073741824LL, resource->quota_bytes_used());
  EXPECT_EQ(8177LL, resource->largest_change_id());
}

// Test app list parsing.
TEST_F(DriveAPIParserTest, AppListParser) {
  std::string error;
  scoped_ptr<Value> document(LoadJSONFile("applist.json"));
  ASSERT_TRUE(document.get());

  ASSERT_EQ(Value::TYPE_DICTIONARY, document->GetType());
  scoped_ptr<AppList> applist(new AppList);
  EXPECT_TRUE(applist->Parse(*document));

  EXPECT_EQ("\"Jm4BaSnCWNND-noZsHINRqj4ABC/tuqRBw0lvjUdPtc_2msA1tN4XYZ\"",
            applist->etag());
  ASSERT_EQ(2U, applist->items().size());
  // Check Drive app 1
  const AppResource& app1 = *applist->items()[0];
  EXPECT_EQ("123456788192", app1.application_id());
  EXPECT_EQ("Drive app 1", app1.name());
  EXPECT_EQ("", app1.object_type());
  EXPECT_TRUE(app1.supports_create());
  EXPECT_TRUE(app1.supports_import());
  EXPECT_TRUE(app1.is_installed());
  EXPECT_FALSE(app1.is_authorized());
  EXPECT_EQ("https://chrome.google.com/webstore/detail/"
            "abcdefghabcdefghabcdefghabcdefgh",
            app1.product_url().spec());

  ASSERT_EQ(1U, app1.primary_mimetypes().size());
  EXPECT_EQ("application/vnd.google-apps.drive-sdk.123456788192",
            *app1.primary_mimetypes()[0]);

  ASSERT_EQ(2U, app1.secondary_mimetypes().size());
  EXPECT_EQ("text/html", *app1.secondary_mimetypes()[0]);
  EXPECT_EQ("text/plain", *app1.secondary_mimetypes()[1]);

  ASSERT_EQ(2U, app1.primary_file_extensions().size());
  EXPECT_EQ("exe", *app1.primary_file_extensions()[0]);
  EXPECT_EQ("com", *app1.primary_file_extensions()[1]);

  EXPECT_EQ(0U, app1.secondary_file_extensions().size());

  ASSERT_EQ(6U, app1.icons().size());
  const DriveAppIcon& icon1 = *app1.icons()[0];
  EXPECT_EQ(DriveAppIcon::APPLICATION, icon1.category());
  EXPECT_EQ(10, icon1.icon_side_length());
  EXPECT_EQ("http://www.example.com/10.png", icon1.icon_url().spec());

  const DriveAppIcon& icon6 = *app1.icons()[5];
  EXPECT_EQ(DriveAppIcon::SHARED_DOCUMENT, icon6.category());
  EXPECT_EQ(16, icon6.icon_side_length());
  EXPECT_EQ("http://www.example.com/ds16.png", icon6.icon_url().spec());

  // Check Drive app 2
  const AppResource& app2 = *applist->items()[1];
  EXPECT_EQ("876543210000", app2.application_id());
  EXPECT_EQ("Drive app 2", app2.name());
  EXPECT_EQ("", app2.object_type());
  EXPECT_FALSE(app2.supports_create());
  EXPECT_FALSE(app2.supports_import());
  EXPECT_TRUE(app2.is_installed());
  EXPECT_FALSE(app2.is_authorized());
  EXPECT_EQ("https://chrome.google.com/webstore/detail/"
            "hgfedcbahgfedcbahgfedcbahgfedcba",
            app2.product_url().spec());

  ASSERT_EQ(3U, app2.primary_mimetypes().size());
  EXPECT_EQ("image/jpeg", *app2.primary_mimetypes()[0]);
  EXPECT_EQ("image/png", *app2.primary_mimetypes()[1]);
  EXPECT_EQ("application/vnd.google-apps.drive-sdk.876543210000",
            *app2.primary_mimetypes()[2]);

  EXPECT_EQ(0U, app2.secondary_mimetypes().size());
  EXPECT_EQ(0U, app2.primary_file_extensions().size());
  EXPECT_EQ(0U, app2.secondary_file_extensions().size());

  ASSERT_EQ(3U, app2.icons().size());
  const DriveAppIcon& icon2 = *app2.icons()[1];
  EXPECT_EQ(DriveAppIcon::DOCUMENT, icon2.category());
  EXPECT_EQ(10, icon2.icon_side_length());
  EXPECT_EQ("http://www.example.com/d10.png", icon2.icon_url().spec());
}

// Test file list parsing.
TEST_F(DriveAPIParserTest, FileListParser) {
  std::string error;
  scoped_ptr<Value> document(LoadJSONFile("filelist.json"));
  ASSERT_TRUE(document.get());

  ASSERT_EQ(Value::TYPE_DICTIONARY, document->GetType());
  scoped_ptr<FileList> filelist(new FileList);
  EXPECT_TRUE(filelist->Parse(*document));

  EXPECT_EQ("\"WtRjAPZWbDA7_fkFjc5ojsEvDEF/zyHTfoHpnRHovyi8bWpwK0DXABC\"",
            filelist->etag());
  EXPECT_EQ("EAIaggELEgA6egpi96It9mH_____f_8AAP__AAD_okhU-cHLz83KzszMxsjMzs_Ry"
            "NGJnridyrbHs7u9tv8AAP__AP7__n__AP8AokhU-cHLz83KzszMxsjMzs_RyNGJnr"
            "idyrbHs7u9tv8A__4QZCEiXPTi_wtIgTkAAAAAngnSXUgCDEAAIgsJPgart10AAAA"
            "ABC", filelist->next_page_token());
  EXPECT_EQ(GURL("https://www.googleapis.com/drive/v2/files?pageToken=EAIaggEL"
                 "EgA6egpi96It9mH_____f_8AAP__AAD_okhU-cHLz83KzszMxsjMzs_RyNGJ"
                 "nridyrbHs7u9tv8AAP__AP7__n__AP8AokhU-cHLz83KzszMxsjMzs_RyNGJ"
                 "nridyrbHs7u9tv8A__4QZCEiXPTi_wtIgTkAAAAAngnSXUgCDEAAIgsJPgar"
                 "t10AAAAABC"), filelist->next_link());

  ASSERT_EQ(3U, filelist->items().size());
  // Check file 1 (a file)
  const FileResource& file1 = *filelist->items()[0];
  EXPECT_EQ("0B4v7G8yEYAWHUmRrU2lMS2hLABC", file1.file_id());
  EXPECT_EQ("\"WtRjAPZWbDA7_fkFjc5ojsEvDEF/MTM0MzM2NzgwMDIXYZ\"",
            file1.etag());
  EXPECT_EQ("My first file data", file1.title());
  EXPECT_EQ("application/octet-stream", file1.mime_type());

  base::Time modified_time;
  ASSERT_TRUE(gdata::util::GetTimeFromString("2012-07-27T05:43:20.269Z",
                                             &modified_time));
  EXPECT_EQ(modified_time, file1.modified_by_me_date());

  ASSERT_EQ(1U, file1.parents().size());
  EXPECT_EQ("0B4v7G8yEYAWHYW1OcExsUVZLABC", file1.parents()[0]->file_id());
  EXPECT_FALSE(file1.parents()[0]->is_root());

  EXPECT_EQ(GURL("https://www.example.com/download"), file1.download_url());
  EXPECT_EQ("ext", file1.file_extension());
  EXPECT_EQ("d41d8cd98f00b204e9800998ecf8427e", file1.md5_checksum());
  EXPECT_EQ(1000U, file1.file_size());

  // Check file 2 (a Google Document)
  const FileResource& file2 = *filelist->items()[1];
  EXPECT_EQ("Test Google Document", file2.title());
  EXPECT_EQ("application/vnd.google-apps.document", file2.mime_type());
  EXPECT_EQ(0U, file2.file_size());

  ASSERT_EQ(0U, file2.parents().size());

  // Check file 3 (a folder)
  const FileResource& file3 = *filelist->items()[2];
  EXPECT_EQ(0U, file3.file_size());
  EXPECT_EQ("TestFolder", file3.title());
  EXPECT_EQ("application/vnd.google-apps.folder", file3.mime_type());
  ASSERT_TRUE(file3.IsDirectory());

  ASSERT_EQ(1U, file3.parents().size());
  EXPECT_EQ("0AIv7G8yEYAWHUk9ABC", file3.parents()[0]->file_id());
  EXPECT_TRUE(file3.parents()[0]->is_root());
}

// Test change list parsing.
TEST_F(DriveAPIParserTest, ChangeListParser) {
  std::string error;
  scoped_ptr<Value> document(LoadJSONFile("changelist.json"));
  ASSERT_TRUE(document.get());

  ASSERT_EQ(Value::TYPE_DICTIONARY, document->GetType());
  scoped_ptr<ChangeList> changelist(new ChangeList);
  EXPECT_TRUE(changelist->Parse(*document));

  EXPECT_EQ("\"Lp2bjAtLP341hvGmYHhxjYyBPJ8/BWbu_eylt5f_aGtCN6mGRv9hABC\"",
            changelist->etag());
  EXPECT_EQ("8929", changelist->next_page_token());
  EXPECT_EQ("https://www.googleapis.com/drive/v2/changes?pageToken=8929",
            changelist->next_link().spec());
  EXPECT_EQ(13664, changelist->largest_change_id());

  ASSERT_EQ(3U, changelist->items().size());
  const ChangeResource& change1 = *changelist->items()[0];
  EXPECT_EQ(8421, change1.change_id());
  EXPECT_FALSE(change1.is_deleted());
  EXPECT_EQ(change1.file_id(), change1.file().file_id());

  const ChangeResource& change2 = *changelist->items()[1];
  EXPECT_EQ(8424, change2.change_id());
  EXPECT_FALSE(change2.is_deleted());
  EXPECT_EQ(change2.file_id(), change2.file().file_id());

  const ChangeResource& change3 = *changelist->items()[2];
  EXPECT_EQ(8429, change3.change_id());
  EXPECT_FALSE(change3.is_deleted());
  EXPECT_EQ(change3.file_id(), change3.file().file_id());
}

}  // namespace gdata
