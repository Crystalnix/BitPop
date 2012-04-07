// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ui/ui_test.h"

#include "base/file_util.h"
#include "chrome/test/automation/automation_proxy.h"
#include "chrome/test/automation/browser_proxy.h"
#include "chrome/test/automation/tab_proxy.h"
#include "net/test/test_server.h"
#include "sql/connection.h"
#include "sql/statement.h"

namespace {

class MultipartResponseUITest : public UITest {
};

#if defined(NDEBUG)
// http://code.google.com/p/chromium/issues/detail?id=37746
// Running this test only for release builds as it fails in debug test
// runs
TEST_F(MultipartResponseUITest, SingleVisit) {
  // Make sure that visiting a multipart/x-mixed-replace site only
  // creates one entry in the visits table.
  net::TestServer test_server(net::TestServer::TYPE_HTTP,
                              FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(test_server.Start());

  scoped_refptr<BrowserProxy> browser_proxy(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(browser_proxy.get());
  scoped_refptr<TabProxy> tab_proxy(browser_proxy->GetActiveTab());
  ASSERT_TRUE(tab_proxy.get());
  NavigateToURL(test_server.GetURL("multipart"));
  std::wstring title;
  EXPECT_TRUE(tab_proxy->GetTabTitle(&title));
  EXPECT_EQ(L"page 9", title);
  CloseBrowserAndServer();

  // The browser has shutdown now.  Check the contents of the history
  // table.  We should have only one visit for the URL even though it
  // had 10 parts.
  sql::Connection db;
  FilePath history =
      user_data_dir().AppendASCII("Default").AppendASCII("History");
  ASSERT_TRUE(file_util::PathExists(history));
  ASSERT_TRUE(db.Open(history));
  std::string query(
      "SELECT COUNT(1) FROM visits, urls WHERE visits.url = urls.id"
      " AND urls.url LIKE 'http://" +
      test_server.host_port_pair().HostForURL() + ":%/multipart'");
  {
    sql::Statement statement(db.GetUniqueStatement(query.c_str()));
    EXPECT_TRUE(statement);
    EXPECT_TRUE(statement.Step());
    EXPECT_EQ(1, statement.ColumnInt(0));
  }
  db.Close();
}
#endif

}  // namespace
