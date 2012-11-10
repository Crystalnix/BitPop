// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/path_service.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/string16.h"
#include "base/utf_string_conversions.h"
#include "content/browser/accessibility/browser_accessibility.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#include "content/browser/accessibility/dump_accessibility_tree_helper.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/port/browser/render_widget_host_view_port.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_paths.h"
#include "content/public/test/test_utils.h"
#include "content/test/content_browser_test.h"
#include "content/test/content_browser_test_utils.h"
#include "content/shell/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
  static const char kCommentToken = '#';
  static const char* kMarkSkipFile = "#<skip";
  static const char* kMarkEndOfFile = "<-- End-of-file -->";
  static const char* kSignalDiff = "*";
} // namespace

namespace content {

// This test takes a snapshot of the platform BrowserAccessibility tree and
// tests it against an expected baseline.
//
// The flow of the test is as outlined below.
// 1. Load an html file from chrome/test/data/accessibility.
// 2. Read the expectation.
// 3. Browse to the page and serialize the platform specific tree into a human
//    readable string.
// 4. Perform a comparison between actual and expected and fail if they do not
//    exactly match.
class DumpAccessibilityTreeTest : public ContentBrowserTest {
 public:
  // Utility helper that does a comment aware equality check.
  // Returns array of lines from expected file which are different.
  std::vector<int> DiffLines(std::vector<std::string>& expected_lines,
                             std::vector<std::string>& actual_lines) {
    int actual_lines_count = actual_lines.size();
    int expected_lines_count = expected_lines.size();
    std::vector<int> diff_lines;
    int i = 0, j = 0;
    while (i < actual_lines_count && j < expected_lines_count) {
      if (expected_lines[j].size() == 0 ||
          expected_lines[j][0] == kCommentToken) {
        // Skip comment lines and blank lines in expected output.
        ++j;
        continue;
      }

      if (actual_lines[i] != expected_lines[j])
        diff_lines.push_back(j);
      ++i;
      ++j;
    }

    // Actual file has been fully checked.
    return diff_lines;
  }

  void AddDefaultFilters(std::set<string16>* allow_filters,
                         std::set<string16>* deny_filters) {
    allow_filters->insert(ASCIIToUTF16("FOCUSABLE"));
    allow_filters->insert(ASCIIToUTF16("READONLY"));
  }

  void ParseFilters(const std::string& test_html,
                    std::set<string16>* allow_filters,
                    std::set<string16>* deny_filters) {
    std::vector<std::string> lines;
    base::SplitString(test_html, '\n', &lines);
    for (std::vector<std::string>::const_iterator iter = lines.begin();
         iter != lines.end();
         ++iter) {
      const std::string& line = *iter;
      const std::string& allow_str = helper_.GetAllowString();
      const std::string& deny_str = helper_.GetDenyString();
      if (StartsWithASCII(line, allow_str, true))
        allow_filters->insert(UTF8ToUTF16(line.substr(allow_str.size())));
      else if (StartsWithASCII(line, deny_str, true))
        deny_filters->insert(UTF8ToUTF16(line.substr(deny_str.size())));
    }
  }

  void RunTest(const FilePath::CharType* file_path);

  DumpAccessibilityTreeHelper helper_;
};

void DumpAccessibilityTreeTest::RunTest(const FilePath::CharType* file_path) {
  NavigateToURL(shell(), GURL("about:blank"));
  RenderWidgetHostViewPort* host_view = static_cast<RenderWidgetHostViewPort*>(
      shell()->web_contents()->GetRenderWidgetHostView());
  RenderWidgetHostImpl* host =
      RenderWidgetHostImpl::From(host_view->GetRenderWidgetHost());
  RenderViewHostImpl* view_host = static_cast<RenderViewHostImpl*>(host);
  view_host->set_save_accessibility_tree_for_testing(true);
  view_host->SetAccessibilityMode(AccessibilityModeComplete);

  // Setup test paths.
  FilePath dir_test_data;
  EXPECT_TRUE(PathService::Get(DIR_TEST_DATA, &dir_test_data));
  FilePath test_path(dir_test_data.Append(FILE_PATH_LITERAL("accessibility")));
  EXPECT_TRUE(file_util::PathExists(test_path))
      << test_path.LossyDisplayName();

  FilePath html_file = test_path.Append(FilePath(file_path));
  // Output the test path to help anyone who encounters a failure and needs
  // to know where to look.
  printf("Testing: %s\n", html_file.MaybeAsASCII().c_str());

  std::string html_contents;
  file_util::ReadFileToString(html_file, &html_contents);

  // Parse filters in the test file.
  std::set<string16> allow_filters;
  std::set<string16> deny_filters;
  AddDefaultFilters(&allow_filters, &deny_filters);
  ParseFilters(html_contents, &allow_filters, &deny_filters);
  helper_.SetFilters(allow_filters, deny_filters);

  // Read the expected file.
  std::string expected_contents_raw;
  FilePath expected_file =
    FilePath(html_file.RemoveExtension().value() +
             helper_.GetExpectedFileSuffix());
  file_util::ReadFileToString(
      expected_file,
      &expected_contents_raw);

  // Tolerate Windows-style line endings (\r\n) in the expected file:
  // normalize by deleting all \r from the file (if any) to leave only \n.
  std::string expected_contents;
  RemoveChars(expected_contents_raw, "\r", &expected_contents);

  if (!expected_contents.compare(0, strlen(kMarkSkipFile), kMarkSkipFile)) {
    printf("Skipping this test on this platform.\n");
    return;
  }

  // Load the page.
  WindowedNotificationObserver tree_updated_observer(
      NOTIFICATION_RENDER_VIEW_HOST_ACCESSIBILITY_TREE_UPDATED,
      NotificationService::AllSources());
  string16 html_contents16;
  html_contents16 = UTF8ToUTF16(html_contents);
  GURL url = GetTestUrl("accessibility",
                        html_file.BaseName().MaybeAsASCII().c_str());
  NavigateToURL(shell(), url);

  // Wait for the tree.
  tree_updated_observer.Wait();

  // Perform a diff (or write the initial baseline).
  string16 actual_contents_utf16;
  helper_.DumpAccessibilityTree(
      host_view->GetBrowserAccessibilityManager()->GetRoot(),
      &actual_contents_utf16);
  std::string actual_contents = UTF16ToUTF8(actual_contents_utf16);
  std::vector<std::string> actual_lines, expected_lines;
  Tokenize(actual_contents, "\n", &actual_lines);
  Tokenize(expected_contents, "\n", &expected_lines);
  // Marking the end of the file with a line of text ensures that
  // file length differences are found.
  expected_lines.push_back(kMarkEndOfFile);
  actual_lines.push_back(kMarkEndOfFile);

  std::vector<int> diff_lines = DiffLines(expected_lines, actual_lines);
  bool is_different = diff_lines.size() > 0;
  EXPECT_FALSE(is_different);
  if (is_different) {
    // Mark the expected lines which did not match actual output with a *.
    printf("* Line Expected\n");
    printf("- ---- --------\n");
    for (int line = 0, diff_index = 0;
         line < static_cast<int>(expected_lines.size());
         ++line) {
      bool is_diff = false;
      if (diff_index < static_cast<int>(diff_lines.size()) &&
          diff_lines[diff_index] == line) {
        is_diff = true;
        ++ diff_index;
      }
      printf("%1s %4d %s\n", is_diff? kSignalDiff : "", line + 1,
             expected_lines[line].c_str());
    }
    printf("\nActual\n");
    printf("------\n");
    printf("%s\n", actual_contents.c_str());
  }

  if (!file_util::PathExists(expected_file)) {
    FilePath actual_file =
      FilePath(html_file.RemoveExtension().value() +
               helper_.GetActualFileSuffix());

    EXPECT_TRUE(file_util::WriteFile(
        actual_file, actual_contents.c_str(), actual_contents.size()));

    ADD_FAILURE() << "No expectation found. Create it by doing:\n"
                  << "mv " << actual_file.LossyDisplayName() << " "
                  << expected_file.LossyDisplayName();
  }
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest, AccessibilityA) {
  RunTest(FILE_PATH_LITERAL("a.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest, AccessibilityAName) {
  RunTest(FILE_PATH_LITERAL("a-name.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest, AccessibilityAOnclick) {
  RunTest(FILE_PATH_LITERAL("a-onclick.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest,
                       AccessibilityAriaApplication) {
  RunTest(FILE_PATH_LITERAL("aria-application.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest, AccessibilityAWithImg) {
  RunTest(FILE_PATH_LITERAL("a-with-img.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest, AccessibilityButtonNameCalc) {
  RunTest(FILE_PATH_LITERAL("button-name-calc.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest,
                       AccessibilityCheckboxNameCalc) {
  RunTest(FILE_PATH_LITERAL("checkbox-name-calc.html"));
}

// TODO(dimich): Started to fail in Chrome r149732 (crbug 140397)
#if defined(OS_WIN)
#define MAYBE_AccessibilityContenteditableDescendants \
    DISABLED_AccessibilityContenteditableDescendants
#else
#define MAYBE_AccessibilityContenteditableDescendants \
    AccessibilityContenteditableDescendants
#endif

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest,
                       MAYBE_AccessibilityContenteditableDescendants) {
  RunTest(FILE_PATH_LITERAL("contenteditable-descendants.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest, AccessibilityFooter) {
  RunTest(FILE_PATH_LITERAL("footer.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest,
                       AccessibilityInputTextNameCalc) {
  RunTest(FILE_PATH_LITERAL("input-text-name-calc.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest, AccessibilityListMarkers) {
  RunTest(FILE_PATH_LITERAL("list-markers.html"));
}

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest, AccessibilityUl) {
  RunTest(FILE_PATH_LITERAL("ul.html"));
}

}  // namespace content
