// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/json/json_reader.h"
#include "base/message_loop_proxy.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/stringprintf.h"
#include "chrome/browser/google_apis/gdata_wapi_operations.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "chrome/browser/google_apis/gdata_wapi_url_generator.h"
#include "chrome/browser/google_apis/operation_registry.h"
#include "chrome/browser/google_apis/test_server/http_server.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "net/base/escape.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace google_apis {

namespace {

const char kTestGDataAuthToken[] = "testtoken";
const char kTestUserAgent[] = "test-user-agent";

// Copies the results from GetDataCallback and quit the message loop.
void CopyResultsFromGetDataCallbackAndQuit(
    GDataErrorCode* out_result_code,
    scoped_ptr<base::Value>* out_result_data,
    GDataErrorCode result_code,
    scoped_ptr<base::Value> result_data) {
  *out_result_code = result_code;
  *out_result_data = result_data.Pass();
  MessageLoop::current()->Quit();
}

// Copies the results from DownloadActionCallback and quit the message loop.
// The contents of the download cache file are copied to a string, and the
// file is removed.
void CopyResultsFromDownloadActionCallbackAndQuit(
    GDataErrorCode* out_result_code,
    std::string* contents,
    GDataErrorCode result_code,
    const FilePath& cache_file_path) {
  *out_result_code = result_code;
  file_util::ReadFileToString(cache_file_path, contents);
  file_util::Delete(cache_file_path, false);
  MessageLoop::current()->Quit();
}

// Copies the result from EntryActionCallback and quit the message loop.
void CopyResultFromEntryActionCallbackAndQuit(
    GDataErrorCode* out_result_code,
    GDataErrorCode result_code) {
  *out_result_code = result_code;
  MessageLoop::current()->Quit();
}

// Copies the result from InitiateUploadCallback and quit the message loop.
void CopyResultFromInitiateUploadCallbackAndQuit(
    GDataErrorCode* out_result_code,
    GURL* out_upload_url,
    GDataErrorCode result_code,
    const GURL& upload_url) {
  *out_result_code = result_code;
  *out_upload_url = upload_url;
  MessageLoop::current()->Quit();
}

// Copies the result from ResumeUploadCallback and quit the message loop.
void CopyResultFromResumeUploadCallbackAndQuit(
    ResumeUploadResponse* out_response,
    scoped_ptr<ResourceEntry>* out_new_entry,
    const ResumeUploadResponse& response,
    scoped_ptr<ResourceEntry> new_entry) {
  *out_response = response;
  *out_new_entry = new_entry.Pass();
  MessageLoop::current()->Quit();
}

// Returns true if |json_data| equals to JSON data in |expected_json_file_path|.
bool VerifyJsonData(const FilePath& expected_json_file_path,
                    const base::Value* json_data) {
  std::string expected_contents;
  if (!file_util::ReadFileToString(expected_json_file_path, &expected_contents))
    return false;

  scoped_ptr<base::Value> expected_data(
      base::JSONReader::Read(expected_contents));
  return base::Value::Equals(expected_data.get(), json_data);
}

// Removes |prefix| from |input| and stores the result in |output|. Returns
// true if the prefix is removed.
bool RemovePrefix(const std::string& input,
                  const std::string& prefix,
                  std::string* output) {
  if (!StartsWithASCII(input, prefix, true /* case sensitive */))
    return false;

  *output = input.substr(prefix.size());
  return true;
}

// Parses a value of Content-Range header, which looks like
// "bytes <start_position>-<end_position>/<length>".
// Returns true on success.
bool ParseContentRangeHeader(const std::string& value,
                             int64* start_position,
                             int64* end_position,
                             int64* length) {
  DCHECK(start_position);
  DCHECK(end_position);
  DCHECK(length);

  std::string remaining;
  if (!RemovePrefix(value, "bytes ", &remaining))
    return false;

  std::vector<std::string> parts;
  base::SplitString(remaining, '/', &parts);
  if (parts.size() != 2U)
    return false;

  const std::string range = parts[0];
  if (!base::StringToInt64(parts[1], length))
    return false;

  parts.clear();
  base::SplitString(range, '-', &parts);
  if (parts.size() != 2U)
    return false;

  return (base::StringToInt64(parts[0], start_position) &&
          base::StringToInt64(parts[1], end_position));
}

// Does nothing for ReAuthenticateCallback(). This function should not be
// reached as there won't be any authentication failures in the test.
void DoNothingForReAuthenticateCallback(
    AuthenticatedOperationInterface* /* operation */) {
  NOTREACHED();
}

class GDataWapiOperationsTest : public testing::Test {
 public:
  GDataWapiOperationsTest()
      : ui_thread_(content::BrowserThread::UI, &message_loop_),
        file_thread_(content::BrowserThread::FILE),
        io_thread_(content::BrowserThread::IO) {
  }

  virtual void SetUp() OVERRIDE {
    file_thread_.Start();
    io_thread_.StartIOThread();
    profile_.reset(new TestingProfile);

    request_context_getter_ = new net::TestURLRequestContextGetter(
        content::BrowserThread::GetMessageLoopProxyForThread(
            content::BrowserThread::IO));

    ASSERT_TRUE(test_server_.InitializeAndWaitUntilReady());
    test_server_.RegisterRequestHandler(
        base::Bind(&GDataWapiOperationsTest::HandleDownloadRequest,
                   base::Unretained(this)));
    test_server_.RegisterRequestHandler(
        base::Bind(&GDataWapiOperationsTest::HandleResourceFeedRequest,
                   base::Unretained(this)));
    test_server_.RegisterRequestHandler(
        base::Bind(&GDataWapiOperationsTest::HandleMetadataFeedRequest,
                   base::Unretained(this)));
    test_server_.RegisterRequestHandler(
        base::Bind(&GDataWapiOperationsTest::HandleCreateSessionRequest,
                   base::Unretained(this)));
    test_server_.RegisterRequestHandler(
        base::Bind(&GDataWapiOperationsTest::HandleUploadRequest,
                   base::Unretained(this)));

    url_generator_.reset(new GDataWapiUrlGenerator(
        GDataWapiUrlGenerator::GetBaseUrlForTesting(test_server_.port())));
  }

  virtual void TearDown() OVERRIDE {
    test_server_.ShutdownAndWaitUntilComplete();
    request_context_getter_ = NULL;
  }

 protected:
  // Returns a temporary file path suitable for storing the cache file.
  FilePath GetTestCachedFilePath(const FilePath& file_name) {
    return profile_->GetPath().Append(file_name);
  }

  // Handles a request for downloading a file. Reads a file from the test
  // directory and returns the content.
  scoped_ptr<test_server::HttpResponse> HandleDownloadRequest(
      const test_server::HttpRequest& request) {
    http_request_ = request;

    const GURL absolute_url = test_server_.GetURL(request.relative_url);
    std::string remaining_path;
    if (!RemovePrefix(absolute_url.path(), "/files/", &remaining_path))
      return scoped_ptr<test_server::HttpResponse>();

    return test_util::CreateHttpResponseFromFile(
        test_util::GetTestFilePath(remaining_path));
  }

  // Handles a request for fetching a resource feed.
  scoped_ptr<test_server::HttpResponse> HandleResourceFeedRequest(
      const test_server::HttpRequest& request) {
    http_request_ = request;

    const GURL absolute_url = test_server_.GetURL(request.relative_url);
    std::string remaining_path;
    if (absolute_url.path() == "/feeds/default/private/full" &&
        request.method == test_server::METHOD_POST) {
      // This is a request for copying a document.
      // TODO(satorux): we should generate valid JSON data for the newly
      // copied document but for now, just return "file_entry.json"
      return test_util::CreateHttpResponseFromFile(
          test_util::GetTestFilePath("gdata/file_entry.json"));
    }

    if (!RemovePrefix(absolute_url.path(),
                      "/feeds/default/private/full/",
                      &remaining_path)) {
      return scoped_ptr<test_server::HttpResponse>();
    }

    if (remaining_path == "-/mine") {
      // Process the default feed.
      return test_util::CreateHttpResponseFromFile(
          test_util::GetTestFilePath("gdata/root_feed.json"));
    } else {
      // Process a feed for a single resource ID.
      const std::string resource_id = net::UnescapeURLComponent(
          remaining_path, net::UnescapeRule::URL_SPECIAL_CHARS);
      if (resource_id == "file:2_file_resource_id") {
        // Check if this is an authorization request for an app.
        if (request.method == test_server::METHOD_PUT &&
            request.content.find("<docs:authorizedApp>") != std::string::npos) {
          return test_util::CreateHttpResponseFromFile(
              test_util::GetTestFilePath("gdata/basic_feed.json"));
        }

        return test_util::CreateHttpResponseFromFile(
            test_util::GetTestFilePath("gdata/file_entry.json"));
      } else if (resource_id == "folder:root" &&
                 request.method == test_server::METHOD_POST) {
        // This is a request for creating a directory in the root directory.
        // TODO(satorux): we should generate valid JSON data for the newly
        // created directory but for now, just return "directory_entry.json"
        return test_util::CreateHttpResponseFromFile(
            test_util::GetTestFilePath("gdata/directory_entry.json"));
      } else if (resource_id == "folder:root/file:2_file_resource_id" &&
                 request.method == test_server::METHOD_DELETE) {
        // This is a request for deleting a file from the root directory.
        // TODO(satorux): Investigate what's returned from the server, and
        // copy it. For now, just return a random file, as the contents don't
        // matter.
        return test_util::CreateHttpResponseFromFile(
            test_util::GetTestFilePath("gdata/testfile.txt"));
      }
    }

    return scoped_ptr<test_server::HttpResponse>();
  }

  // Handles a request for fetching a metadata feed.
  scoped_ptr<test_server::HttpResponse> HandleMetadataFeedRequest(
      const test_server::HttpRequest& request) {
    http_request_ = request;

    const GURL absolute_url = test_server_.GetURL(request.relative_url);
    if (absolute_url.path() != "/feeds/metadata/default")
      return scoped_ptr<test_server::HttpResponse>();

    return test_util::CreateHttpResponseFromFile(
        test_util::GetTestFilePath("gdata/account_metadata.json"));
  }

  // Handles a request for creating a session for uploading.
  scoped_ptr<test_server::HttpResponse> HandleCreateSessionRequest(
      const test_server::HttpRequest& request) {
    http_request_ = request;

    const GURL absolute_url = test_server_.GetURL(request.relative_url);
    if (absolute_url.path() ==
        // This is an upload URL of the root directory.
        "/feeds/upload/create-session/default/private/full" ||
        absolute_url.path() ==
        // This is an upload URL of an existing file.
        "/feeds/upload/create-session/default/private/full/file:foo") {
      scoped_ptr<test_server::HttpResponse> http_response(
          new test_server::HttpResponse);
      http_response->set_code(test_server::SUCCESS);
      GURL upload_url;
      // POST is used for a new file, and PUT is used for an existing file.
      if (request.method == test_server::METHOD_POST) {
        upload_url = test_server_.GetURL("/upload_new_file");
      } else if (request.method == test_server::METHOD_PUT) {
        upload_url = test_server_.GetURL("/upload_existing_file");
      } else {
        return scoped_ptr<test_server::HttpResponse>();
      }
      http_response->AddCustomHeader("Location", upload_url.spec());
      return http_response.Pass();
    }

    return scoped_ptr<test_server::HttpResponse>();
  }

  // Handles a request for uploading content.
  scoped_ptr<test_server::HttpResponse> HandleUploadRequest(
      const test_server::HttpRequest& request) {
    http_request_ = request;

    const GURL absolute_url = test_server_.GetURL(request.relative_url);
    if (absolute_url.path() != "/upload_new_file" &&
        absolute_url.path() != "/upload_existing_file") {
      return scoped_ptr<test_server::HttpResponse>();
    }

    // TODO(satorux): We should create a correct JSON data for the uploaded
    // file, but for now, just return file_entry.json.
    scoped_ptr<test_server::HttpResponse> response =
        test_util::CreateHttpResponseFromFile(
            test_util::GetTestFilePath("gdata/file_entry.json"));
    // response.code() is set to SUCCESS. Change it to CREATED if it's a new
    // file.
    if (absolute_url.path() == "/upload_new_file")
      response->set_code(test_server::CREATED);

    // Check if the Content-Range header is present. This must be present if
    // the request body is not empty.
    if (!request.content.empty()) {
      std::map<std::string, std::string>::const_iterator iter =
          request.headers.find("Content-Range");
      if (iter == request.headers.end())
        return scoped_ptr<test_server::HttpResponse>();
      int64 start_position = 0;
      int64 end_position = 0;
      int64 length = 0;
      if (!ParseContentRangeHeader(iter->second,
                                   &start_position,
                                   &end_position,
                                   &length)) {
        return scoped_ptr<test_server::HttpResponse>();
      }

      // Add Range header to the response, based on the values of
      // Content-Range header in the request.
      response->AddCustomHeader(
          "Range",
          "bytes=" +
          base::Int64ToString(start_position) + "-" +
          base::Int64ToString(end_position));

      // Change the code to RESUME_INCOMPLETE if upload is not complete.
      if (end_position + 1 < length)
        response->set_code(test_server::RESUME_INCOMPLETE);
    }

    return response.Pass();
  }

  MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;
  content::TestBrowserThread io_thread_;
  test_server::HttpServer test_server_;
  scoped_ptr<TestingProfile> profile_;
  OperationRegistry operation_registry_;
  scoped_ptr<GDataWapiUrlGenerator> url_generator_;
  scoped_refptr<net::TestURLRequestContextGetter> request_context_getter_;

  // The incoming HTTP request is saved so tests can verify the request
  // parameters like HTTP method (ex. some operations should use DELETE
  // instead of GET).
  test_server::HttpRequest http_request_;
};

}  // namespace

TEST_F(GDataWapiOperationsTest, GetResourceListOperation_DefaultFeed) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  GetResourceListOperation* operation = new GetResourceListOperation(
      &operation_registry_,
      request_context_getter_.get(),
      *url_generator_,
      GURL(),  // Pass an empty URL to use the default feed
      0,  // start changestamp
      "",  // search string
      false,  // shared with me
      "",  // directory resource ID
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data));
  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full/-/mine?v=3&alt=json&showfolders=true"
            "&max-results=500&include-installed-apps=true",
            http_request_.relative_url);
  ASSERT_TRUE(result_data);
  EXPECT_TRUE(VerifyJsonData(
      test_util::GetTestFilePath("gdata/root_feed.json"),
      result_data.get()));
}

TEST_F(GDataWapiOperationsTest, GetResourceListOperation_ValidFeed) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  GetResourceListOperation* operation = new GetResourceListOperation(
      &operation_registry_,
      request_context_getter_.get(),
      *url_generator_,
      test_server_.GetURL("/files/gdata/root_feed.json"),
      0,  // start changestamp
      "",  // search string
      false,  // shared with me
      "",  // directory resource ID
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data));
  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/files/gdata/root_feed.json?v=3&alt=json&showfolders=true"
            "&max-results=500&include-installed-apps=true",
            http_request_.relative_url);
  ASSERT_TRUE(result_data);
  EXPECT_TRUE(VerifyJsonData(
      test_util::GetTestFilePath("gdata/root_feed.json"),
      result_data.get()));
}

TEST_F(GDataWapiOperationsTest, GetResourceListOperation_InvalidFeed) {
  // testfile.txt exists but the response is not JSON, so it should
  // emit a parse error instead.
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  GetResourceListOperation* operation = new GetResourceListOperation(
      &operation_registry_,
      request_context_getter_.get(),
      *url_generator_,
      test_server_.GetURL("/files/gdata/testfile.txt"),
      0,  // start changestamp
      "",  // search string
      false,  // shared with me
      "",  // directory resource ID
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data));
  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(GDATA_PARSE_ERROR, result_code);
  EXPECT_EQ(test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/files/gdata/testfile.txt?v=3&alt=json&showfolders=true"
            "&max-results=500&include-installed-apps=true",
            http_request_.relative_url);
  EXPECT_FALSE(result_data);
}

TEST_F(GDataWapiOperationsTest, GetResourceEntryOperation_ValidResourceId) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  GetResourceEntryOperation* operation = new GetResourceEntryOperation(
      &operation_registry_,
      request_context_getter_.get(),
      *url_generator_,
      "file:2_file_resource_id",  // resource ID
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data));
  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full/file%3A2_file_resource_id"
            "?v=3&alt=json",
            http_request_.relative_url);
  ASSERT_TRUE(result_data);
  EXPECT_TRUE(VerifyJsonData(
      test_util::GetTestFilePath("gdata/file_entry.json"),
      result_data.get()));
}

TEST_F(GDataWapiOperationsTest, GetResourceEntryOperation_InvalidResourceId) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  GetResourceEntryOperation* operation = new GetResourceEntryOperation(
      &operation_registry_,
      request_context_getter_.get(),
      *url_generator_,
      "<invalid>",  // resource ID
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data));
  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_NOT_FOUND, result_code);
  EXPECT_EQ(test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full/%3Cinvalid%3E?v=3&alt=json",
            http_request_.relative_url);
  ASSERT_FALSE(result_data);
}

TEST_F(GDataWapiOperationsTest, GetAccountMetadataOperation) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  GetAccountMetadataOperation* operation = new GetAccountMetadataOperation(
      &operation_registry_,
      request_context_getter_.get(),
      *url_generator_,
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data));
  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/feeds/metadata/default?v=3&alt=json&include-installed-apps=true",
            http_request_.relative_url);
  EXPECT_TRUE(VerifyJsonData(
      test_util::GetTestFilePath("gdata/account_metadata.json"),
      result_data.get()));
}

TEST_F(GDataWapiOperationsTest, DownloadFileOperation_ValidFile) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  std::string contents;
  DownloadFileOperation* operation = new DownloadFileOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultsFromDownloadActionCallbackAndQuit,
                 &result_code,
                 &contents),
      GetContentCallback(),
      test_server_.GetURL("/files/gdata/testfile.txt"),
      FilePath::FromUTF8Unsafe("/dummy/gdata/testfile.txt"),
      GetTestCachedFilePath(FilePath::FromUTF8Unsafe("cached_testfile.txt")));
  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/files/gdata/testfile.txt", http_request_.relative_url);

  const FilePath expected_path =
      test_util::GetTestFilePath("gdata/testfile.txt");
  std::string expected_contents;
  file_util::ReadFileToString(expected_path, &expected_contents);
  EXPECT_EQ(expected_contents, contents);
}

TEST_F(GDataWapiOperationsTest, DownloadFileOperation_NonExistentFile) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  std::string contents;
  DownloadFileOperation* operation = new DownloadFileOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultsFromDownloadActionCallbackAndQuit,
                 &result_code,
                 &contents),
      GetContentCallback(),
      test_server_.GetURL("/files/gdata/no-such-file.txt"),
      FilePath::FromUTF8Unsafe("/dummy/gdata/no-such-file.txt"),
      GetTestCachedFilePath(
          FilePath::FromUTF8Unsafe("cache_no-such-file.txt")));
  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_NOT_FOUND, result_code);
  EXPECT_EQ(test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/files/gdata/no-such-file.txt", http_request_.relative_url);
  // Do not verify the not found message.
}

TEST_F(GDataWapiOperationsTest, DeleteResourceOperation) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;

  DeleteResourceOperation* operation = new DeleteResourceOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultFromEntryActionCallbackAndQuit,
                 &result_code),
      test_server_.GetURL(
          "/feeds/default/private/full/file:2_file_resource_id"));

  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_DELETE, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full/file:2_file_resource_id?v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("*", http_request_.headers["If-Match"]);
}

TEST_F(GDataWapiOperationsTest, CreateDirectoryOperation) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  // Create "new directory" in the root directory.
  CreateDirectoryOperation* operation = new CreateDirectoryOperation(
      &operation_registry_,
      request_context_getter_.get(),
      *url_generator_,
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data),
      test_server_.GetURL("/feeds/default/private/full/folder%3Aroot"),
      FILE_PATH_LITERAL("new directory"));

  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full/folder%3Aroot?v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("application/atom+xml", http_request_.headers["Content-Type"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("<?xml version=\"1.0\"?>\n"
            "<entry xmlns=\"http://www.w3.org/2005/Atom\">\n"
            " <category scheme=\"http://schemas.google.com/g/2005#kind\" "
            "term=\"http://schemas.google.com/docs/2007#folder\"/>\n"
            " <title>new directory</title>\n"
            "</entry>\n",
            http_request_.content);
}

TEST_F(GDataWapiOperationsTest, CopyHostedDocumentOperation) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  // Copy a document with a new name "New Document".
  CopyHostedDocumentOperation* operation = new CopyHostedDocumentOperation(
      &operation_registry_,
      request_context_getter_.get(),
      *url_generator_,
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data),
      "document:5_document_resource_id",  // source resource ID
      FILE_PATH_LITERAL("New Document"));

  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full?v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("application/atom+xml", http_request_.headers["Content-Type"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("<?xml version=\"1.0\"?>\n"
            "<entry xmlns=\"http://www.w3.org/2005/Atom\">\n"
            " <id>document:5_document_resource_id</id>\n"
            " <title>New Document</title>\n"
            "</entry>\n",
            http_request_.content);
}

TEST_F(GDataWapiOperationsTest, RenameResourceOperation) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;

  // Rename a file with a new name "New File".
  RenameResourceOperation* operation = new RenameResourceOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultFromEntryActionCallbackAndQuit,
                 &result_code),
      test_server_.GetURL(
          "/feeds/default/private/full/file:2_file_resource_id"),
      FILE_PATH_LITERAL("New File"));

  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_PUT, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full/file:2_file_resource_id?v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("application/atom+xml", http_request_.headers["Content-Type"]);
  EXPECT_EQ("*", http_request_.headers["If-Match"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("<?xml version=\"1.0\"?>\n"
            "<entry xmlns=\"http://www.w3.org/2005/Atom\">\n"
            " <title>New File</title>\n"
            "</entry>\n",
            http_request_.content);
}

TEST_F(GDataWapiOperationsTest, AuthorizeAppOperation_ValidFeed) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  // Authorize an app with APP_ID to access to a document.
  AuthorizeAppOperation* operation = new AuthorizeAppOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data),
      test_server_.GetURL(
          "/feeds/default/private/full/file:2_file_resource_id"),
      "APP_ID");

  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_PUT, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full/file:2_file_resource_id?v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("application/atom+xml", http_request_.headers["Content-Type"]);
  EXPECT_EQ("*", http_request_.headers["If-Match"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("<?xml version=\"1.0\"?>\n"
            "<entry xmlns=\"http://www.w3.org/2005/Atom\" "
            "xmlns:docs=\"http://schemas.google.com/docs/2007\">\n"
            " <docs:authorizedApp>APP_ID</docs:authorizedApp>\n"
            "</entry>\n",
            http_request_.content);
}

TEST_F(GDataWapiOperationsTest, AuthorizeAppOperation_InvalidFeed) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result_data;

  // Authorize an app with APP_ID to access to a document but an invalid feed.
  AuthorizeAppOperation* operation = new AuthorizeAppOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultsFromGetDataCallbackAndQuit,
                 &result_code,
                 &result_data),
      test_server_.GetURL("/files/gdata/testfile.txt"),
      "APP_ID");

  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(GDATA_PARSE_ERROR, result_code);
  EXPECT_EQ(test_server::METHOD_PUT, http_request_.method);
  EXPECT_EQ("/files/gdata/testfile.txt?v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("application/atom+xml", http_request_.headers["Content-Type"]);
  EXPECT_EQ("*", http_request_.headers["If-Match"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("<?xml version=\"1.0\"?>\n"
            "<entry xmlns=\"http://www.w3.org/2005/Atom\" "
            "xmlns:docs=\"http://schemas.google.com/docs/2007\">\n"
            " <docs:authorizedApp>APP_ID</docs:authorizedApp>\n"
            "</entry>\n",
            http_request_.content);
}

TEST_F(GDataWapiOperationsTest, AddResourceToDirectoryOperation) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;

  // Add a file to the root directory.
  AddResourceToDirectoryOperation* operation =
      new AddResourceToDirectoryOperation(
          &operation_registry_,
          request_context_getter_.get(),
          *url_generator_,
          base::Bind(&CopyResultFromEntryActionCallbackAndQuit,
                     &result_code),
          test_server_.GetURL("/feeds/default/private/full/folder%3Aroot"),
          test_server_.GetURL(
              "/feeds/default/private/full/file:2_file_resource_id"));

  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full/folder%3Aroot?v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("application/atom+xml", http_request_.headers["Content-Type"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("<?xml version=\"1.0\"?>\n"
            "<entry xmlns=\"http://www.w3.org/2005/Atom\">\n"
            " <id>http://127.0.0.1:8040/feeds/default/private/full/"
            "file:2_file_resource_id</id>\n"
            "</entry>\n",
            http_request_.content);
}

TEST_F(GDataWapiOperationsTest, RemoveResourceFromDirectoryOperation) {
  GDataErrorCode result_code = GDATA_OTHER_ERROR;

  // Remove a file from the root directory.
  RemoveResourceFromDirectoryOperation* operation =
      new RemoveResourceFromDirectoryOperation(
          &operation_registry_,
          request_context_getter_.get(),
          *url_generator_,
          base::Bind(&CopyResultFromEntryActionCallbackAndQuit,
                     &result_code),
          test_server_.GetURL("/feeds/default/private/full/folder%3Aroot"),
          "file:2_file_resource_id");

  operation->Start(kTestGDataAuthToken, kTestUserAgent,
                   base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  // DELETE method should be used, without the body content.
  EXPECT_EQ(test_server::METHOD_DELETE, http_request_.method);
  EXPECT_EQ("/feeds/default/private/full/folder%3Aroot/"
            "file%3A2_file_resource_id?v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("*", http_request_.headers["If-Match"]);
  EXPECT_FALSE(http_request_.has_content);
}

// This test exercises InitiateUploadOperation and ResumeUploadOperation for
// a scenario of uploading a new file.
TEST_F(GDataWapiOperationsTest, UploadNewFile) {
  const std::string kUploadContent = "hello";
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  GURL upload_url;

  // 1) Get the upload URL for uploading a new file.
  InitiateUploadParams initiate_params(
      UPLOAD_NEW_FILE,
      "New file",
      "text/plain",
      kUploadContent.size(),
      test_server_.GetURL("/feeds/upload/create-session/default/private/full"),
      FilePath::FromUTF8Unsafe("drive/newfile.txt"));

  InitiateUploadOperation* initiate_operation = new InitiateUploadOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultFromInitiateUploadCallbackAndQuit,
                 &result_code,
                 &upload_url),
      initiate_params);

  initiate_operation->Start(kTestGDataAuthToken, kTestUserAgent,
                            base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server_.GetURL("/upload_new_file"), upload_url);
  EXPECT_EQ(test_server::METHOD_POST, http_request_.method);
  // convert=false should be passed as files should be uploaded as-is.
  EXPECT_EQ("/feeds/upload/create-session/default/private/full"
            "?convert=false&v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("text/plain", http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ("application/atom+xml", http_request_.headers["Content-Type"]);
  EXPECT_EQ(base::Int64ToString(kUploadContent.size()),
            http_request_.headers["X-Upload-Content-Length"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("<?xml version=\"1.0\"?>\n"
            "<entry xmlns=\"http://www.w3.org/2005/Atom\" "
            "xmlns:docs=\"http://schemas.google.com/docs/2007\">\n"
            " <title>New file</title>\n"
            "</entry>\n",
            http_request_.content);

  // 2) Upload the content to the upload URL.
  scoped_refptr<net::IOBuffer> buffer = new net::StringIOBuffer(kUploadContent);
  ResumeUploadParams resume_params(
      UPLOAD_NEW_FILE,
      0,  // start_position
      kUploadContent.size(), // end_position (exclusive)
      kUploadContent.size(),  // content_length,
      "text/plain",  // content_type
      buffer,
      upload_url,
      FilePath::FromUTF8Unsafe("drive/newfile.txt"));

  ResumeUploadResponse response;
  scoped_ptr<ResourceEntry> new_entry;

  ResumeUploadOperation* resume_operation = new ResumeUploadOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultFromResumeUploadCallbackAndQuit,
                 &response,
                 &new_entry),
      resume_params);

  resume_operation->Start(kTestGDataAuthToken, kTestUserAgent,
                          base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  // METHOD_PUT should be used to upload data.
  EXPECT_EQ(test_server::METHOD_PUT, http_request_.method);
  // Request should go to the upload URL.
  EXPECT_EQ(upload_url.path(), http_request_.relative_url);
  // Content-Range header should be added.
  EXPECT_EQ("bytes 0-" +
            base::Int64ToString(kUploadContent.size() -1) + "/" +
            base::Int64ToString(kUploadContent.size()),
            http_request_.headers["Content-Range"]);
  // The upload content should be set in the HTTP request.
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ(kUploadContent, http_request_.content);

  // Check the response.
  EXPECT_EQ(HTTP_CREATED, response.code);  // Because it's a new file
  // The start and end positions should be set to -1, if an upload is complete.
  EXPECT_EQ(-1, response.start_position_received);
  EXPECT_EQ(-1, response.end_position_received);
}

// This test exercises InitiateUploadOperation and ResumeUploadOperation for
// a scenario of uploading a new *large* file, which requires multiple requests
// of ResumeUploadOperation.
TEST_F(GDataWapiOperationsTest, UploadNewLargeFile) {
  const size_t kMaxNumBytes = 10;
  // This is big enough to cause multiple requests of ResumeUploadOperation
  // as we are going to send at most kMaxNumBytes at a time.
  const std::string kUploadContent(kMaxNumBytes + 1, 'a');
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  GURL upload_url;

  // 1) Get the upload URL for uploading a new file.
  InitiateUploadParams initiate_params(
      UPLOAD_NEW_FILE,
      "New file",
      "text/plain",
      kUploadContent.size(),
      test_server_.GetURL("/feeds/upload/create-session/default/private/full"),
      FilePath::FromUTF8Unsafe("drive/newfile.txt"));

  InitiateUploadOperation* initiate_operation = new InitiateUploadOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultFromInitiateUploadCallbackAndQuit,
                 &result_code,
                 &upload_url),
      initiate_params);

  initiate_operation->Start(kTestGDataAuthToken, kTestUserAgent,
                            base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server_.GetURL("/upload_new_file"), upload_url);
  EXPECT_EQ(test_server::METHOD_POST, http_request_.method);
  // convert=false should be passed as files should be uploaded as-is.
  EXPECT_EQ("/feeds/upload/create-session/default/private/full?convert=false"
            "&v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("text/plain", http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ("application/atom+xml", http_request_.headers["Content-Type"]);
  EXPECT_EQ(base::Int64ToString(kUploadContent.size()),
            http_request_.headers["X-Upload-Content-Length"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("<?xml version=\"1.0\"?>\n"
            "<entry xmlns=\"http://www.w3.org/2005/Atom\" "
            "xmlns:docs=\"http://schemas.google.com/docs/2007\">\n"
            " <title>New file</title>\n"
            "</entry>\n",
            http_request_.content);

  // 2) Upload the content to the upload URL with multiple requests.
  size_t num_bytes_consumed = 0;
  for (size_t start_position = 0; start_position < kUploadContent.size();
       start_position += kMaxNumBytes) {
    SCOPED_TRACE(testing::Message("start_position: ") << start_position);

    // The payload is at most kMaxNumBytes.
    const size_t remaining_size = kUploadContent.size() - start_position;
    const std::string payload = kUploadContent.substr(
        start_position, std::min(kMaxNumBytes, remaining_size));
    num_bytes_consumed += payload.size();
    // The end position is exclusive.
    const size_t end_position = start_position + payload.size();

    scoped_refptr<net::IOBuffer> buffer = new net::StringIOBuffer(payload);
    ResumeUploadParams resume_params(
        UPLOAD_NEW_FILE,
        start_position,
        end_position,
        kUploadContent.size(),  // content_length,
        "text/plain",  // content_type
        buffer,
        upload_url,
        FilePath::FromUTF8Unsafe("drive/newfile.txt"));

    ResumeUploadResponse response;
    scoped_ptr<ResourceEntry> new_entry;

    ResumeUploadOperation* resume_operation = new ResumeUploadOperation(
        &operation_registry_,
        request_context_getter_.get(),
        base::Bind(&CopyResultFromResumeUploadCallbackAndQuit,
                   &response,
                   &new_entry),
        resume_params);

    resume_operation->Start(kTestGDataAuthToken, kTestUserAgent,
                            base::Bind(&DoNothingForReAuthenticateCallback));
    MessageLoop::current()->Run();

    // METHOD_PUT should be used to upload data.
    EXPECT_EQ(test_server::METHOD_PUT, http_request_.method);
    // Request should go to the upload URL.
    EXPECT_EQ(upload_url.path(), http_request_.relative_url);
    // Content-Range header should be added.
    EXPECT_EQ("bytes " +
              base::Int64ToString(start_position) + "-" +
              base::Int64ToString(end_position - 1) + "/" +
              base::Int64ToString(kUploadContent.size()),
              http_request_.headers["Content-Range"]);
    // The upload content should be set in the HTTP request.
    EXPECT_TRUE(http_request_.has_content);
    EXPECT_EQ(payload, http_request_.content);

    // Check the response.
    if (payload.size() == remaining_size) {
      EXPECT_EQ(HTTP_CREATED, response.code);  // Because it's a new file.
      // The start and end positions should be set to -1, if an upload is
      // complete.
      EXPECT_EQ(-1, response.start_position_received);
      EXPECT_EQ(-1, response.end_position_received);
    } else {
      EXPECT_EQ(HTTP_RESUME_INCOMPLETE, response.code);
      EXPECT_EQ(static_cast<int64>(start_position),
                response.start_position_received);
      EXPECT_EQ(static_cast<int64>(end_position),
                response.end_position_received);
    }
  }

  EXPECT_EQ(kUploadContent.size(), num_bytes_consumed);
}

// This test exercises InitiateUploadOperation and ResumeUploadOperation for
// a scenario of uploading a new *empty* file.
//
// The test is almost identical to UploadNewFile. The only difference is the
// expectation for the Content-Range header.
TEST_F(GDataWapiOperationsTest, UploadNewEmptyFile) {
  const std::string kUploadContent = "";
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  GURL upload_url;

  // 1) Get the upload URL for uploading a new file.
  InitiateUploadParams initiate_params(
      UPLOAD_NEW_FILE,
      "New file",
      "text/plain",
      kUploadContent.size(),
      test_server_.GetURL("/feeds/upload/create-session/default/private/full"),
      FilePath::FromUTF8Unsafe("drive/newfile.txt"));

  InitiateUploadOperation* initiate_operation = new InitiateUploadOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultFromInitiateUploadCallbackAndQuit,
                 &result_code,
                 &upload_url),
      initiate_params);

  initiate_operation->Start(kTestGDataAuthToken, kTestUserAgent,
                            base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server_.GetURL("/upload_new_file"), upload_url);
  EXPECT_EQ(test_server::METHOD_POST, http_request_.method);
  // convert=false should be passed as files should be uploaded as-is.
  EXPECT_EQ("/feeds/upload/create-session/default/private/full?convert=false"
            "&v=3&alt=json",
            http_request_.relative_url);
  EXPECT_EQ("text/plain", http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ("application/atom+xml", http_request_.headers["Content-Type"]);
  EXPECT_EQ(base::Int64ToString(kUploadContent.size()),
            http_request_.headers["X-Upload-Content-Length"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("<?xml version=\"1.0\"?>\n"
            "<entry xmlns=\"http://www.w3.org/2005/Atom\" "
            "xmlns:docs=\"http://schemas.google.com/docs/2007\">\n"
            " <title>New file</title>\n"
            "</entry>\n",
            http_request_.content);

  // 2) Upload the content to the upload URL.
  scoped_refptr<net::IOBuffer> buffer = new net::StringIOBuffer(kUploadContent);
  ResumeUploadParams resume_params(
      UPLOAD_NEW_FILE,
      0,  // start_position
      kUploadContent.size(), // end_position (exclusive)
      kUploadContent.size(),  // content_length,
      "text/plain",  // content_type
      buffer,
      upload_url,
      FilePath::FromUTF8Unsafe("drive/newfile.txt"));

  ResumeUploadResponse response;
  scoped_ptr<ResourceEntry> new_entry;

  ResumeUploadOperation* resume_operation = new ResumeUploadOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultFromResumeUploadCallbackAndQuit,
                 &response,
                 &new_entry),
      resume_params);

  resume_operation->Start(kTestGDataAuthToken, kTestUserAgent,
                          base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  // METHOD_PUT should be used to upload data.
  EXPECT_EQ(test_server::METHOD_PUT, http_request_.method);
  // Request should go to the upload URL.
  EXPECT_EQ(upload_url.path(), http_request_.relative_url);
  // Content-Range header should not exit if the content is empty.
  // We should not generate the header with an invalid value "bytes 0--1/0".
  EXPECT_EQ(0U, http_request_.headers.count("Content-Range"));
  // The upload content should be set in the HTTP request.
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ(kUploadContent, http_request_.content);

  // Check the response.
  EXPECT_EQ(HTTP_CREATED, response.code);  // Because it's a new file.
  // The start and end positions should be set to -1, if an upload is complete.
  EXPECT_EQ(-1, response.start_position_received);
  EXPECT_EQ(-1, response.end_position_received);
}

// This test exercises InitiateUploadOperation and ResumeUploadOperation for
// a scenario of updating an existing file.
TEST_F(GDataWapiOperationsTest, UploadExistingFile) {
  const std::string kUploadContent = "hello";
  GDataErrorCode result_code = GDATA_OTHER_ERROR;
  GURL upload_url;

  // 1) Get the upload URL for uploading an existing file.
  InitiateUploadParams initiate_params(
      UPLOAD_EXISTING_FILE,
      "Existing file",
      "text/plain",
      kUploadContent.size(),
      test_server_.GetURL(
          "/feeds/upload/create-session/default/private/full/file:foo"),
      FilePath::FromUTF8Unsafe("drive/existingfile.txt"));

  InitiateUploadOperation* initiate_operation = new InitiateUploadOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultFromInitiateUploadCallbackAndQuit,
                 &result_code,
                 &upload_url),
      initiate_params);

  initiate_operation->Start(kTestGDataAuthToken, kTestUserAgent,
                            base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, result_code);
  EXPECT_EQ(test_server_.GetURL("/upload_existing_file"), upload_url);
  // For updating an existing file, METHOD_PUT should be used.
  EXPECT_EQ(test_server::METHOD_PUT, http_request_.method);
  // convert=false should be passed as files should be uploaded as-is.
  EXPECT_EQ("/feeds/upload/create-session/default/private/full/file:foo"
            "?convert=false&v=3&alt=json",
            http_request_.relative_url);
  // Even though the body is empty, the content type should be set to
  // "text/plain".
  EXPECT_EQ("text/plain", http_request_.headers["Content-Type"]);
  EXPECT_EQ("text/plain", http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ(base::Int64ToString(kUploadContent.size()),
            http_request_.headers["X-Upload-Content-Length"]);
  // For updating an existing file, an empty body should be attached (PUT
  // requires a body)
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("", http_request_.content);

  // 2) Upload the content to the upload URL.
  scoped_refptr<net::IOBuffer> buffer = new net::StringIOBuffer(kUploadContent);
  ResumeUploadParams resume_params(
      UPLOAD_EXISTING_FILE,
      0,  // start_position
      kUploadContent.size(), // end_position (exclusive)
      kUploadContent.size(),  // content_length,
      "text/plain",  // content_type
      buffer,
      upload_url,
      FilePath::FromUTF8Unsafe("drive/existingfile.txt"));

  ResumeUploadResponse response;
  scoped_ptr<ResourceEntry> new_entry;

  ResumeUploadOperation* resume_operation = new ResumeUploadOperation(
      &operation_registry_,
      request_context_getter_.get(),
      base::Bind(&CopyResultFromResumeUploadCallbackAndQuit,
                 &response,
                 &new_entry),
      resume_params);

  resume_operation->Start(kTestGDataAuthToken, kTestUserAgent,
                          base::Bind(&DoNothingForReAuthenticateCallback));
  MessageLoop::current()->Run();

  // METHOD_PUT should be used to upload data.
  EXPECT_EQ(test_server::METHOD_PUT, http_request_.method);
  // Request should go to the upload URL.
  EXPECT_EQ(upload_url.path(), http_request_.relative_url);
  // Content-Range header should be added.
  EXPECT_EQ("bytes 0-" +
            base::Int64ToString(kUploadContent.size() -1) + "/" +
            base::Int64ToString(kUploadContent.size()),
            http_request_.headers["Content-Range"]);
  // The upload content should be set in the HTTP request.
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ(kUploadContent, http_request_.content);

  // Check the response.
  EXPECT_EQ(HTTP_SUCCESS, response.code);  // Because it's an existing file.
  // The start and end positions should be set to -1, if an upload is complete.
  EXPECT_EQ(-1, response.start_position_received);
  EXPECT_EQ(-1, response.end_position_received);
}

}  // namespace google_apis
