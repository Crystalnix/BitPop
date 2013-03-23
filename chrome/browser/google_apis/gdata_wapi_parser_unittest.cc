// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/google_apis/gdata_wapi_parser.h"

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/string16.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/browser/google_apis/time_util.h"
#include "chrome/common/chrome_paths.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/libxml/chromium/libxml_utils.h"

using base::Value;
using base::DictionaryValue;
using base::ListValue;

namespace google_apis {

class GDataWAPIParserTest : public testing::Test {
 protected:
  static ResourceEntry* LoadResourceEntryFromXml(const std::string& filename) {
    FilePath path;
    std::string error;
    PathService::Get(chrome::DIR_TEST_DATA, &path);
    path = path.AppendASCII("chromeos")
        .AppendASCII("gdata")
        .AppendASCII(filename.c_str());
    EXPECT_TRUE(file_util::PathExists(path)) <<
        "Couldn't find " << path.value();
    std::string contents;
    EXPECT_TRUE(file_util::ReadFileToString(path, &contents));
    XmlReader reader;
    if (!reader.Load(contents)) {
      NOTREACHED() << "Invalid xml:\n" << contents;
      return NULL;
    }
    scoped_ptr<ResourceEntry> entry;
    while (reader.Read()) {
      if (reader.NodeName() == "entry") {
        entry = ResourceEntry::CreateFromXml(&reader);
        break;
      }
    }
    return entry.release();
  }
};

// TODO(nhiroki): Move json files to out of 'chromeos' directory
// (http://crbug.com/149788).
// Test document feed parsing.
TEST_F(GDataWAPIParserTest, ResourceListJsonParser) {
  std::string error;
  scoped_ptr<Value> document =
      test_util::LoadJSONFile("gdata/basic_feed.json");
  ASSERT_TRUE(document.get());
  ASSERT_EQ(Value::TYPE_DICTIONARY, document->GetType());
  scoped_ptr<ResourceList> feed(ResourceList::ExtractAndParse(*document));
  ASSERT_TRUE(feed.get());

  base::Time update_time;
  ASSERT_TRUE(util::GetTimeFromString("2011-12-14T01:03:21.151Z",
                                                   &update_time));

  EXPECT_EQ(1, feed->start_index());
  EXPECT_EQ(1000, feed->items_per_page());
  EXPECT_EQ(update_time, feed->updated_time());

  // Check authors.
  ASSERT_EQ(1U, feed->authors().size());
  EXPECT_EQ(ASCIIToUTF16("tester"), feed->authors()[0]->name());
  EXPECT_EQ("tester@testing.com", feed->authors()[0]->email());

  // Check links.
  ASSERT_EQ(6U, feed->links().size());
  const Link* self_link = feed->GetLinkByType(Link::LINK_SELF);
  ASSERT_TRUE(self_link);
  EXPECT_EQ("https://self_link/", self_link->href().spec());
  EXPECT_EQ("application/atom+xml", self_link->mime_type());


  const Link* resumable_link =
      feed->GetLinkByType(Link::LINK_RESUMABLE_CREATE_MEDIA);
  ASSERT_TRUE(resumable_link);
  EXPECT_EQ("https://resumable_create_media_link/",
            resumable_link->href().spec());
  EXPECT_EQ("application/atom+xml", resumable_link->mime_type());

  // Check entries.
  ASSERT_EQ(4U, feed->entries().size());

  // Check a folder entry.
  const ResourceEntry* folder_entry = feed->entries()[0];
  ASSERT_TRUE(folder_entry);
  EXPECT_EQ(ENTRY_KIND_FOLDER, folder_entry->kind());
  EXPECT_EQ("\"HhMOFgcNHSt7ImBr\"", folder_entry->etag());
  EXPECT_EQ("folder:sub_sub_directory_folder_id", folder_entry->resource_id());
  EXPECT_EQ("https://1_folder_id", folder_entry->id());
  EXPECT_EQ(ASCIIToUTF16("Entry 1 Title"), folder_entry->title());
  base::Time entry1_update_time;
  base::Time entry1_publish_time;
  ASSERT_TRUE(util::GetTimeFromString("2011-04-01T18:34:08.234Z",
                                                   &entry1_update_time));
  ASSERT_TRUE(util::GetTimeFromString("2010-11-07T05:03:54.719Z",
                                                   &entry1_publish_time));
  EXPECT_EQ(entry1_update_time, folder_entry->updated_time());
  EXPECT_EQ(entry1_publish_time, folder_entry->published_time());

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
  ASSERT_EQ(FeedLink::FEED_LINK_ACL, feed_link->type());

  const Link* entry1_alternate_link =
      folder_entry->GetLinkByType(Link::LINK_ALTERNATE);
  ASSERT_TRUE(entry1_alternate_link);
  EXPECT_EQ("https://1_folder_alternate_link/",
            entry1_alternate_link->href().spec());
  EXPECT_EQ("text/html", entry1_alternate_link->mime_type());

  const Link* entry1_edit_link = folder_entry->GetLinkByType(Link::LINK_EDIT);
  ASSERT_TRUE(entry1_edit_link);
  EXPECT_EQ("https://1_edit_link/", entry1_edit_link->href().spec());
  EXPECT_EQ("application/atom+xml", entry1_edit_link->mime_type());

  // Check a file entry.
  const ResourceEntry* file_entry = feed->entries()[1];
  ASSERT_TRUE(file_entry);
  EXPECT_EQ(ENTRY_KIND_FILE, file_entry->kind());
  EXPECT_EQ(ASCIIToUTF16("filename.m4a"), file_entry->filename());
  EXPECT_EQ(ASCIIToUTF16("sugg_file_name.m4a"),
            file_entry->suggested_filename());
  EXPECT_EQ("3b4382ebefec6e743578c76bbd0575ce", file_entry->file_md5());
  EXPECT_EQ(892721, file_entry->file_size());
  const Link* file_parent_link = file_entry->GetLinkByType(Link::LINK_PARENT);
  ASSERT_TRUE(file_parent_link);
  EXPECT_EQ("https://file_link_parent/", file_parent_link->href().spec());
  EXPECT_EQ("application/atom+xml", file_parent_link->mime_type());
  EXPECT_EQ(ASCIIToUTF16("Medical"), file_parent_link->title());
  const Link* file_open_with_link =
    file_entry->GetLinkByType(Link::LINK_OPEN_WITH);
  ASSERT_TRUE(file_open_with_link);
  EXPECT_EQ("https://xml_file_entry_open_with_link/",
            file_open_with_link->href().spec());
  EXPECT_EQ("application/atom+xml", file_open_with_link->mime_type());
  EXPECT_EQ("the_app_id", file_open_with_link->app_id());
  EXPECT_EQ(654321, file_entry->changestamp());

  const Link* file_unknown_link = file_entry->GetLinkByType(Link::LINK_UNKNOWN);
  ASSERT_TRUE(file_unknown_link);
  EXPECT_EQ("https://xml_file_fake_entry_open_with_link/",
            file_unknown_link->href().spec());
  EXPECT_EQ("application/atom+xml", file_unknown_link->mime_type());
  EXPECT_EQ("", file_unknown_link->app_id());

  // Check a file entry.
  const ResourceEntry* resource_entry = feed->entries()[2];
  ASSERT_TRUE(resource_entry);
  EXPECT_EQ(ENTRY_KIND_DOCUMENT, resource_entry->kind());
  EXPECT_TRUE(resource_entry->is_hosted_document());
  EXPECT_TRUE(resource_entry->is_google_document());
  EXPECT_FALSE(resource_entry->is_external_document());

  // Check an external document entry.
  const ResourceEntry* app_entry = feed->entries()[3];
  ASSERT_TRUE(app_entry);
  EXPECT_EQ(ENTRY_KIND_EXTERNAL_APP, app_entry->kind());
  EXPECT_TRUE(app_entry->is_hosted_document());
  EXPECT_TRUE(app_entry->is_external_document());
  EXPECT_FALSE(app_entry->is_google_document());
}


// Test document feed parsing.
TEST_F(GDataWAPIParserTest, ResourceEntryXmlParser) {
  scoped_ptr<ResourceEntry> entry(LoadResourceEntryFromXml("entry.xml"));
  ASSERT_TRUE(entry.get());

  EXPECT_EQ(ENTRY_KIND_FILE, entry->kind());
  EXPECT_EQ("\"HhMOFgcNHSt7ImBr\"", entry->etag());
  EXPECT_EQ("file:xml_file_resource_id", entry->resource_id());
  EXPECT_EQ("https://xml_file_id", entry->id());
  EXPECT_EQ(ASCIIToUTF16("Xml Entry File Title.tar"), entry->title());
  base::Time entry1_update_time;
  base::Time entry1_publish_time;
  ASSERT_TRUE(util::GetTimeFromString("2011-04-01T18:34:08.234Z",
                                                   &entry1_update_time));
  ASSERT_TRUE(util::GetTimeFromString("2010-11-07T05:03:54.719Z",
                                                   &entry1_publish_time));
  EXPECT_EQ(entry1_update_time, entry->updated_time());
  EXPECT_EQ(entry1_publish_time, entry->published_time());

  EXPECT_EQ(1U, entry->authors().size());
  EXPECT_EQ(ASCIIToUTF16("entry_tester"), entry->authors()[0]->name());
  EXPECT_EQ("entry_tester@testing.com", entry->authors()[0]->email());
  EXPECT_EQ("https://1_xml_file_entry_content_url/",
            entry->content_url().spec());
  EXPECT_EQ("application/x-tar",
            entry->content_mime_type());

  // Check feed links.
  ASSERT_EQ(2U, entry->feed_links().size());
  const FeedLink* feed_link_1 = entry->feed_links()[0];
  ASSERT_TRUE(feed_link_1);
  EXPECT_EQ(FeedLink::FEED_LINK_ACL, feed_link_1->type());

  const FeedLink* feed_link_2 = entry->feed_links()[1];
  ASSERT_TRUE(feed_link_2);
  EXPECT_EQ(FeedLink::FEED_LINK_REVISIONS, feed_link_2->type());

  // Check links.
  ASSERT_EQ(9U, entry->links().size());
  const Link* entry1_alternate_link =
      entry->GetLinkByType(Link::LINK_ALTERNATE);
  ASSERT_TRUE(entry1_alternate_link);
  EXPECT_EQ("https://xml_file_entry_id_alternate_link/",
            entry1_alternate_link->href().spec());
  EXPECT_EQ("text/html", entry1_alternate_link->mime_type());

  const Link* entry1_edit_link = entry->GetLinkByType(Link::LINK_EDIT_MEDIA);
  ASSERT_TRUE(entry1_edit_link);
  EXPECT_EQ("https://xml_file_entry_id_edit_media_link/",
            entry1_edit_link->href().spec());
  EXPECT_EQ("application/x-tar", entry1_edit_link->mime_type());

  const Link* entry1_self_link = entry->GetLinkByType(Link::LINK_SELF);
  ASSERT_TRUE(entry1_self_link);
  EXPECT_EQ("https://xml_file_entry_id_self_link/",
            entry1_self_link->href().spec());
  EXPECT_EQ("application/atom+xml", entry1_self_link->mime_type());
  EXPECT_EQ("", entry1_self_link->app_id());

  const Link* entry1_open_with_link =
      entry->GetLinkByType(Link::LINK_OPEN_WITH);
  ASSERT_TRUE(entry1_open_with_link);
  EXPECT_EQ("https://xml_file_entry_open_with_link/",
            entry1_open_with_link->href().spec());
  EXPECT_EQ("application/atom+xml", entry1_open_with_link->mime_type());
  EXPECT_EQ("the_app_id", entry1_open_with_link->app_id());

  const Link* entry1_unknown_link = entry->GetLinkByType(Link::LINK_UNKNOWN);
  ASSERT_TRUE(entry1_unknown_link);
  EXPECT_EQ("https://xml_file_fake_entry_open_with_link/",
            entry1_unknown_link->href().spec());
  EXPECT_EQ("application/atom+xml", entry1_unknown_link->mime_type());
  EXPECT_EQ("", entry1_unknown_link->app_id());

  // Check a file properties.
  EXPECT_EQ(ENTRY_KIND_FILE, entry->kind());
  EXPECT_EQ(ASCIIToUTF16("Xml Entry File Name.tar"), entry->filename());
  EXPECT_EQ(ASCIIToUTF16("Xml Entry Suggested File Name.tar"),
            entry->suggested_filename());
  EXPECT_EQ("e48f4d5c46a778de263e0e3f4b3d2a7d", entry->file_md5());
  EXPECT_EQ(26562560, entry->file_size());
}

TEST_F(GDataWAPIParserTest, AccountMetadataFeedParser) {
  scoped_ptr<Value> document =
      test_util::LoadJSONFile("gdata/account_metadata.json");
  ASSERT_TRUE(document.get());
  ASSERT_EQ(Value::TYPE_DICTIONARY, document->GetType());
  DictionaryValue* entry_value = NULL;
  ASSERT_TRUE(reinterpret_cast<DictionaryValue*>(document.get())->GetDictionary(
      std::string("entry"), &entry_value));
  ASSERT_TRUE(entry_value);

  scoped_ptr<AccountMetadataFeed> feed(
      AccountMetadataFeed::CreateFrom(*document));
  ASSERT_TRUE(feed.get());
  EXPECT_EQ(GG_LONGLONG(6789012345), feed->quota_bytes_used());
  EXPECT_EQ(GG_LONGLONG(9876543210), feed->quota_bytes_total());
  EXPECT_EQ(654321, feed->largest_changestamp());
  EXPECT_EQ(2U, feed->installed_apps().size());
  const InstalledApp* first_app = feed->installed_apps()[0];
  const InstalledApp* second_app = feed->installed_apps()[1];

  ASSERT_TRUE(first_app);
  EXPECT_EQ("Drive App 1", UTF16ToUTF8(first_app->app_name()));
  EXPECT_EQ("Drive App Object 1", UTF16ToUTF8(first_app->object_type()));
  EXPECT_TRUE(first_app->supports_create());
  EXPECT_EQ("https://chrome.google.com/webstore/detail/abcdefabcdef",
            first_app->GetProductUrl().spec());

  ASSERT_EQ(2U, first_app->primary_mimetypes().size());
  EXPECT_EQ("application/test_type_1",
            *first_app->primary_mimetypes()[0]);
  EXPECT_EQ("application/vnd.google-apps.drive-sdk.11111111",
            *first_app->primary_mimetypes()[1]);

  ASSERT_EQ(1U, first_app->secondary_mimetypes().size());
  EXPECT_EQ("image/jpeg", *first_app->secondary_mimetypes()[0]);

  ASSERT_EQ(2U, first_app->primary_extensions().size());
  EXPECT_EQ("ext_1", *first_app->primary_extensions()[0]);
  EXPECT_EQ("ext_2", *first_app->primary_extensions()[1]);

  ASSERT_EQ(1U, first_app->secondary_extensions().size());
  EXPECT_EQ("ext_3", *first_app->secondary_extensions()[0]);

  ASSERT_EQ(1U, first_app->app_icons().size());
  EXPECT_EQ(AppIcon::ICON_DOCUMENT, first_app->app_icons()[0]->category());
  EXPECT_EQ(16, first_app->app_icons()[0]->icon_side_length());
  GURL icon_url = first_app->app_icons()[0]->GetIconURL();
  EXPECT_EQ("https://www.google.com/images/srpr/logo3w.png", icon_url.spec());
  InstalledApp::IconList icons =
    first_app->GetIconsForCategory(AppIcon::ICON_DOCUMENT);
  EXPECT_EQ("https://www.google.com/images/srpr/logo3w.png",
            icons[0].second.spec());
  icons = first_app->GetIconsForCategory(AppIcon::ICON_SHARED_DOCUMENT);
  EXPECT_TRUE(icons.empty());

  ASSERT_TRUE(second_app);
  EXPECT_EQ("Drive App 2", UTF16ToUTF8(second_app->app_name()));
  EXPECT_EQ("Drive App Object 2", UTF16ToUTF8(second_app->object_type()));
  EXPECT_EQ("https://chrome.google.com/webstore/detail/deadbeefdeadbeef",
            second_app->GetProductUrl().spec());
  EXPECT_FALSE(second_app->supports_create());
  EXPECT_EQ(2U, second_app->primary_mimetypes().size());
  EXPECT_EQ(0U, second_app->secondary_mimetypes().size());
  EXPECT_EQ(1U, second_app->primary_extensions().size());
  EXPECT_EQ(0U, second_app->secondary_extensions().size());
}

// Test file extension checking in ResourceEntry::HasDocumentExtension().
TEST_F(GDataWAPIParserTest, ResourceEntryHasDocumentExtension) {
  EXPECT_TRUE(ResourceEntry::HasHostedDocumentExtension(
      FilePath(FILE_PATH_LITERAL("Test.gdoc"))));
  EXPECT_TRUE(ResourceEntry::HasHostedDocumentExtension(
      FilePath(FILE_PATH_LITERAL("Test.gsheet"))));
  EXPECT_TRUE(ResourceEntry::HasHostedDocumentExtension(
      FilePath(FILE_PATH_LITERAL("Test.gslides"))));
  EXPECT_TRUE(ResourceEntry::HasHostedDocumentExtension(
      FilePath(FILE_PATH_LITERAL("Test.gdraw"))));
  EXPECT_TRUE(ResourceEntry::HasHostedDocumentExtension(
      FilePath(FILE_PATH_LITERAL("Test.gtable"))));
  EXPECT_FALSE(ResourceEntry::HasHostedDocumentExtension(
      FilePath(FILE_PATH_LITERAL("Test.tar.gz"))));
  EXPECT_FALSE(ResourceEntry::HasHostedDocumentExtension(
      FilePath(FILE_PATH_LITERAL("Test.txt"))));
  EXPECT_FALSE(ResourceEntry::HasHostedDocumentExtension(
      FilePath(FILE_PATH_LITERAL("Test"))));
  EXPECT_FALSE(ResourceEntry::HasHostedDocumentExtension(
      FilePath(FILE_PATH_LITERAL(""))));
}

TEST_F(GDataWAPIParserTest, ResourceEntryClassifyEntryKind) {
  EXPECT_EQ(ResourceEntry::KIND_OF_NONE,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_UNKNOWN));
  EXPECT_EQ(ResourceEntry::KIND_OF_NONE,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_ITEM));
  EXPECT_EQ(ResourceEntry::KIND_OF_NONE,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_SITE));
  EXPECT_EQ(ResourceEntry::KIND_OF_GOOGLE_DOCUMENT |
            ResourceEntry::KIND_OF_HOSTED_DOCUMENT,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_DOCUMENT));
  EXPECT_EQ(ResourceEntry::KIND_OF_GOOGLE_DOCUMENT |
            ResourceEntry::KIND_OF_HOSTED_DOCUMENT,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_SPREADSHEET));
  EXPECT_EQ(ResourceEntry::KIND_OF_GOOGLE_DOCUMENT |
            ResourceEntry::KIND_OF_HOSTED_DOCUMENT,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_PRESENTATION));
  EXPECT_EQ(ResourceEntry::KIND_OF_GOOGLE_DOCUMENT |
            ResourceEntry::KIND_OF_HOSTED_DOCUMENT,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_DRAWING));
  EXPECT_EQ(ResourceEntry::KIND_OF_GOOGLE_DOCUMENT |
            ResourceEntry::KIND_OF_HOSTED_DOCUMENT,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_TABLE));
  EXPECT_EQ(ResourceEntry::KIND_OF_EXTERNAL_DOCUMENT |
            ResourceEntry::KIND_OF_HOSTED_DOCUMENT,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_EXTERNAL_APP));
  EXPECT_EQ(ResourceEntry::KIND_OF_FOLDER,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_FOLDER));
  EXPECT_EQ(ResourceEntry::KIND_OF_FILE,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_FILE));
  EXPECT_EQ(ResourceEntry::KIND_OF_FILE,
            ResourceEntry::ClassifyEntryKind(ENTRY_KIND_PDF));
}

}  // namespace google_apis
