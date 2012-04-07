// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/json/json_value_serializer.h"
#include "base/path_service.h"
#include "base/string16.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/gdata/gdata_parser.h"
#include "chrome/common/chrome_paths.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Value;
using base::DictionaryValue;
using base::ListValue;
using gdata::DocumentEntry;
using gdata::DocumentFeed;
using gdata::FeedLink;
using gdata::GDataEntry;
using gdata::Link;

class GDataParserTest : public testing::Test {
 protected:
  static Value* LoadJSONFile(const std::string& filename) {
    FilePath path;
    std::string error;
    PathService::Get(chrome::DIR_TEST_DATA, &path);
    path = path.AppendASCII("chromeos")
        .AppendASCII("gdata")
        .AppendASCII(filename.c_str());
    EXPECT_TRUE(file_util::PathExists(path)) <<
        "Couldn't find " << path.value();

    JSONFileValueSerializer serializer(path);
    Value* value = serializer.Deserialize(NULL, &error);
    EXPECT_TRUE(value) <<
        "Parse error " << path.value() << ": " << error;
    return value;
  }
};

// Test document feed parsing.
TEST_F(GDataParserTest, DocumentFeedParser) {
  std::string error;
  scoped_ptr<Value> document(LoadJSONFile("basic_feed.json"));
  ASSERT_TRUE(document.get());
  ASSERT_TRUE(document->GetType() == Value::TYPE_DICTIONARY);
  Value* feed_value;
  ASSERT_TRUE(reinterpret_cast<DictionaryValue*>(document.get())->Get(
      std::string("feed"), &feed_value));
  ASSERT_TRUE(feed_value);
  scoped_ptr<DocumentFeed> feed(DocumentFeed::CreateFrom(feed_value));

  base::Time update_time;
  ASSERT_TRUE(GDataEntry::GetTimeFromString("2011-12-14T01:03:21.151Z",
                                            &update_time));

  EXPECT_EQ(1, feed->start_index());
  EXPECT_EQ(1000, feed->items_per_page());
  EXPECT_EQ(update_time, feed->updated_time());

  // Check authors.
  ASSERT_EQ(1U, feed->authors().size());
  EXPECT_EQ(ASCIIToUTF16("tester"), feed->authors()[0]->name());
  EXPECT_EQ("tester@testing.com", feed->authors()[0]->email());

  // Check links.
  ASSERT_EQ(feed->links().size(), 6U);
  const Link* self_link = feed->GetLinkByType(gdata::Link::SELF);
  ASSERT_TRUE(self_link);
  EXPECT_EQ("https://self_link/", self_link->href().spec());
  EXPECT_EQ("application/atom+xml", self_link->mime_type());

  const Link* resumable_link =
      feed->GetLinkByType(gdata::Link::RESUMABLE_CREATE_MEDIA);
  ASSERT_TRUE(resumable_link);
  EXPECT_EQ("https://resumable_create_media_link/",
            resumable_link->href().spec());
  EXPECT_EQ("application/atom+xml", resumable_link->mime_type());

  // Check entries.
  ASSERT_EQ(3U, feed->entries().size());

  // Check a folder entry.
  const DocumentEntry* folder_entry = feed->entries()[0];
  ASSERT_TRUE(folder_entry);
  EXPECT_EQ(gdata::DocumentEntry::FOLDER, folder_entry->kind());
  EXPECT_EQ("\"HhMOFgcNHSt7ImBr\"", folder_entry->etag());
  EXPECT_EQ("folder:1_folder_resouce_id", folder_entry->resource_id());
  EXPECT_EQ("https://1_folder_id", folder_entry->id());
  EXPECT_EQ(ASCIIToUTF16("Entry 1 Title"), folder_entry->title());
  base::Time entry1_update_time;
  base::Time entry1_publish_time;
  ASSERT_TRUE(GDataEntry::GetTimeFromString("2011-04-01T18:34:08.234Z",
                                              &entry1_update_time));
  ASSERT_TRUE(GDataEntry::GetTimeFromString("2010-11-07T05:03:54.719Z",
                                              &entry1_publish_time));
  ASSERT_EQ(entry1_update_time, folder_entry->updated_time());
  ASSERT_EQ(entry1_publish_time, folder_entry->published_time());

  ASSERT_EQ(1U, folder_entry->authors().size());
  EXPECT_EQ(ASCIIToUTF16("entry_tester"), folder_entry->authors()[0]->name());
  EXPECT_EQ("entry_tester@testing.com", folder_entry->authors()[0]->email());
  EXPECT_EQ("https://1_folder_content_url/",
            folder_entry->content_url().spec());
  EXPECT_EQ("application/atom+xml;type=feed",
            folder_entry->content_mime_type());

  ASSERT_EQ(1U, folder_entry->feed_links().size());
  const FeedLink* feed_link = folder_entry->feed_links()[0];
  ASSERT_TRUE(feed_link);
  ASSERT_EQ(gdata::FeedLink::ACL, feed_link->type());

  const Link* entry1_alternate_link =
      folder_entry->GetLinkByType(gdata::Link::ALTERNATE);
  ASSERT_TRUE(entry1_alternate_link);
  EXPECT_EQ("https://1_folder_alternate_link/",
            entry1_alternate_link->href().spec());
  EXPECT_EQ("text/html", entry1_alternate_link->mime_type());

  const Link* entry1_edit_link =
      folder_entry->GetLinkByType(gdata::Link::EDIT);
  ASSERT_TRUE(entry1_edit_link);
  EXPECT_EQ("https://1_edit_link/", entry1_edit_link->href().spec());
  EXPECT_EQ("application/atom+xml", entry1_edit_link->mime_type());

  // Check a file entry.
  const DocumentEntry* file_entry = feed->entries()[1];
  ASSERT_TRUE(file_entry);
  EXPECT_EQ(gdata::DocumentEntry::FILE, file_entry->kind());
  EXPECT_EQ(ASCIIToUTF16("filename.m4a"), file_entry->filename());
  EXPECT_EQ(ASCIIToUTF16("sugg_file_name.m4a"),
            file_entry->suggested_filename());
  EXPECT_EQ("3b4382ebefec6e743578c76bbd0575ce", file_entry->file_md5());
  EXPECT_EQ(892721, file_entry->file_size());
  const Link* file_parent_link =
      file_entry->GetLinkByType(gdata::Link::PARENT);
  ASSERT_TRUE(file_parent_link);
  EXPECT_EQ("https://file_link_parent/", file_parent_link->href().spec());
  EXPECT_EQ("application/atom+xml", file_parent_link->mime_type());
  EXPECT_EQ(ASCIIToUTF16("Medical"), file_parent_link->title());

  // Check a file entry.
  const DocumentEntry* document_entry = feed->entries()[2];
  ASSERT_TRUE(document_entry);
  EXPECT_EQ(gdata::DocumentEntry::DOCUMENT, document_entry->kind());
}
