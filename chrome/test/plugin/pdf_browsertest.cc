// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/window_sizer.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_type.h"
#include "net/test/test_server.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/gfx/codec/png_codec.h"

namespace {

// Include things like browser frame and scrollbar and make sure we're bigger
// than the test pdf document.
static const int kBrowserWidth = 1000;
static const int kBrowserHeight = 600;

class PDFBrowserTest : public InProcessBrowserTest,
                       public NotificationObserver {
 public:
  PDFBrowserTest()
      : snapshot_different_(true),
        next_dummy_search_value_(0),
        load_stop_notification_count_(0) {
    EnableDOMAutomation();

    pdf_test_server_.reset(new net::TestServer(
        net::TestServer::TYPE_HTTP,
        FilePath(FILE_PATH_LITERAL("pdf/test"))));
  }

 protected:
  // Use our own TestServer so that we can serve files from the pdf directory.
  net::TestServer* pdf_test_server() { return pdf_test_server_.get(); }

  int load_stop_notification_count() const {
    return load_stop_notification_count_;
  }

  FilePath GetPDFTestDir() {
    return FilePath(FilePath::kCurrentDirectory).AppendASCII("..").
        AppendASCII("..").AppendASCII("..").AppendASCII("pdf").
        AppendASCII("test");
  }

  void Load() {
    // Make sure to set the window size before rendering, as otherwise rendering
    // to a smaller window and then expanding leads to slight anti-aliasing
    // differences of the text and the pixel comparison fails.
    gfx::Rect bounds(gfx::Rect(0, 0, kBrowserWidth, kBrowserHeight));
    scoped_ptr<WindowSizer::MonitorInfoProvider> monitor_info(
        WindowSizer::CreateDefaultMonitorInfoProvider());
    gfx::Rect screen_bounds = monitor_info->GetPrimaryMonitorBounds();
    ASSERT_GT(screen_bounds.width(), kBrowserWidth);
    ASSERT_GT(screen_bounds.height(), kBrowserHeight);
    browser()->window()->SetBounds(bounds);

    GURL url(ui_test_utils::GetTestUrl(
        GetPDFTestDir(),
        FilePath(FILE_PATH_LITERAL("pdf_browsertest.pdf"))));
    ui_test_utils::NavigateToURL(browser(), url);
  }

  void VerifySnapshot(const std::string& expected_filename) {
    snapshot_different_ = true;
    expected_filename_ = expected_filename;
    TabContentsWrapper* wrapper =  browser()->GetSelectedTabContentsWrapper();
    wrapper->CaptureSnapshot();
    ui_test_utils::RegisterAndWait(this,
                                   NotificationType::TAB_SNAPSHOT_TAKEN,
                                   Source<TabContentsWrapper>(wrapper));
    ASSERT_FALSE(snapshot_different_) << "Rendering didn't match, see result "
        "at " << snapshot_filename_.value().c_str();
  }

  void WaitForResponse() {
    // Even if the plugin has loaded the data or scrolled, because of how
    // pepper painting works, we might not have the data.  One way to force this
    // to be flushed is to do a find operation, since on this two-page test
    // document, it'll wait for us to flush the renderer message loop twice and
    // also the browser's once, at which point we're guaranteed to have updated
    // the backingstore.  Hacky, but it works.
    // Note that we need to change the text each time, because if we don't the
    // renderer code will think the second message is to go to next result, but
    // there are none so the plugin will assert.

    string16 query = UTF8ToUTF16(
        std::string("xyzxyz" + base::IntToString(next_dummy_search_value_++)));
    ASSERT_EQ(0, ui_test_utils::FindInPage(
        browser()->GetSelectedTabContentsWrapper(), query, true, false, NULL));
  }

 private:
  // NotificationObserver
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) {
    if (type == NotificationType::TAB_SNAPSHOT_TAKEN) {
      MessageLoopForUI::current()->Quit();
      FilePath reference = ui_test_utils::GetTestFilePath(
          GetPDFTestDir(),
          FilePath().AppendASCII(expected_filename_));
      base::PlatformFileInfo info;
      ASSERT_TRUE(file_util::GetFileInfo(reference, &info));
      int size = static_cast<size_t>(info.size);
      scoped_array<char> data(new char[size]);
      ASSERT_EQ(size, file_util::ReadFile(reference, data.get(), size));

      int w, h;
      std::vector<unsigned char> decoded;
      ASSERT_TRUE(gfx::PNGCodec::Decode(
          reinterpret_cast<unsigned char*>(data.get()), size,
          gfx::PNGCodec::FORMAT_BGRA, &decoded, &w, &h));
      int32* ref_pixels = reinterpret_cast<int32*>(&decoded[0]);

      const SkBitmap* bitmap = Details<const SkBitmap>(details).ptr();
      int32* pixels = static_cast<int32*>(bitmap->getPixels());

      // Get the background color, and use it to figure out the x-offsets in
      // each image.  The reason is that depending on the theme in the OS, the
      // same browser width can lead to slightly different plugin sizes, so the
      // pdf content will start at different x offsets.
      // Also note that the images we saved are cut off before the scrollbar, as
      // that'll change depending on the theme, and also cut off vertically so
      // that the ui controls don't show up, as those fade-in and so the timing
      // will affect their transparency.
      int32 bg_color = ref_pixels[0];
      int ref_x_offset, snapshot_x_offset;
      for (ref_x_offset = 0; ref_x_offset < w; ++ref_x_offset) {
        if (ref_pixels[ref_x_offset] != bg_color)
          break;
      }

      for (snapshot_x_offset = 0; snapshot_x_offset < bitmap->width();
           ++snapshot_x_offset) {
        if (pixels[snapshot_x_offset] != bg_color)
          break;
      }

      int x_max = std::min(
          w - ref_x_offset, bitmap->width() - snapshot_x_offset);
      int y_max = std::min(h, bitmap->height());
      int stride = bitmap->rowBytes();
      snapshot_different_ = false;
      for (int y = 0; y < y_max && !snapshot_different_; ++y) {
        for (int x = 0; x < x_max && !snapshot_different_; ++x) {
          if (pixels[y * stride / sizeof(int32) + x + snapshot_x_offset] !=
              ref_pixels[y * w + x + ref_x_offset])
            snapshot_different_ = true;
        }
      }

      if (snapshot_different_) {
        std::vector<unsigned char> png_data;
        gfx::PNGCodec::EncodeBGRASkBitmap(*bitmap, false, &png_data);
        if (file_util::CreateTemporaryFile(&snapshot_filename_)) {
          file_util::WriteFile(snapshot_filename_,
              reinterpret_cast<char*>(&png_data[0]), png_data.size());
        }
      }
    } else if (type == NotificationType::LOAD_STOP) {
      load_stop_notification_count_++;
    }
  }

  // True if the snapshot differed from the expected value.
  bool snapshot_different_;
  // Internal variable used to synchronize to the renderer.
  int next_dummy_search_value_;
  // The filename of the bitmap to compare the snapshot to.
  std::string expected_filename_;
  // If the snapshot is different, holds the location where it's saved.
  FilePath snapshot_filename_;
  // How many times we've seen NotificationType::LOAD_STOP.
  int load_stop_notification_count_;

  scoped_ptr<net::TestServer> pdf_test_server_;
};

#if defined(OS_CHROMEOS)
// TODO(sanjeevr): http://crbug.com/79837
#define MAYBE_Basic DISABLED_Basic
#else
#define MAYBE_Basic Basic
#endif
// Tests basic PDF rendering.  This can be broken depending on bad merges with
// the vendor, so it's important that we have basic sanity checking.
IN_PROC_BROWSER_TEST_F(PDFBrowserTest, MAYBE_Basic) {
  ASSERT_NO_FATAL_FAILURE(Load());
  ASSERT_NO_FATAL_FAILURE(WaitForResponse());
  ASSERT_NO_FATAL_FAILURE(VerifySnapshot("pdf_browsertest.png"));
}

#if defined(OS_CHROMEOS)
// TODO(sanjeevr): http://crbug.com/79837
#define MAYBE_Scroll DISABLED_Scroll
#else
#define MAYBE_Scroll Scroll
#endif
// Tests that scrolling works.
IN_PROC_BROWSER_TEST_F(PDFBrowserTest, MAYBE_Scroll) {
  ASSERT_NO_FATAL_FAILURE(Load());

  // We use wheel mouse event since that's the only one we can easily push to
  // the renderer.  There's no way to push a cross-platform keyboard event at
  // the moment.
  WebKit::WebMouseWheelEvent wheel_event;
  wheel_event.type = WebKit::WebInputEvent::MouseWheel;
  wheel_event.deltaY = -200;
  wheel_event.wheelTicksY = -2;
  TabContents* tab_contents = browser()->GetSelectedTabContents();
  tab_contents->render_view_host()->ForwardWheelEvent(wheel_event);
  ASSERT_NO_FATAL_FAILURE(WaitForResponse());

  int y_offset = 0;
  ASSERT_TRUE(ui_test_utils::ExecuteJavaScriptAndExtractInt(
      browser()->GetSelectedTabContents()->render_view_host(),
      std::wstring(),
      L"window.domAutomationController.send(plugin.pageYOffset())",
      &y_offset));
  ASSERT_GT(y_offset, 0);
}

#if defined(OS_CHROMEOS)
// TODO(sanjeevr): http://crbug.com/79837
#define MAYBE_FindAndCopy DISABLED_FindAndCopy
#else
#define MAYBE_FindAndCopy FindAndCopy
#endif
IN_PROC_BROWSER_TEST_F(PDFBrowserTest, MAYBE_FindAndCopy) {
  ASSERT_NO_FATAL_FAILURE(Load());
  // Verifies that find in page works.
  ASSERT_EQ(3, ui_test_utils::FindInPage(
      browser()->GetSelectedTabContentsWrapper(), UTF8ToUTF16("adipiscing"),
      true, false, NULL));

  // Verify that copying selected text works.
  ui::Clipboard clipboard;
  // Reset the clipboard first.
  ui::Clipboard::ObjectMap objects;
  ui::Clipboard::ObjectMapParams params;
  params.push_back(std::vector<char>());
  objects[ui::Clipboard::CBF_TEXT] = params;
  clipboard.WriteObjects(objects);

  browser()->GetSelectedTabContents()->render_view_host()->Copy();
  ASSERT_NO_FATAL_FAILURE(WaitForResponse());

  std::string text;
  clipboard.ReadAsciiText(ui::Clipboard::BUFFER_STANDARD, &text);
  ASSERT_EQ("adipiscing", text);
}

// Tests that loading async pdfs works correctly (i.e. document fully loads).
// This also loads all documents that used to crash, to ensure we don't have
// regressions.
// Flaky as per http://crbug.com/74548.
IN_PROC_BROWSER_TEST_F(PDFBrowserTest, FLAKY_SLOW_Loading) {
  ASSERT_TRUE(pdf_test_server()->Start());

  NavigationController* controller =
      &(browser()->GetSelectedTabContents()->controller());
  NotificationRegistrar registrar;
  registrar.Add(this,
                NotificationType::LOAD_STOP,
                Source<NavigationController>(controller));
  std::string base_url = std::string("files/");

  file_util::FileEnumerator file_enumerator(
      ui_test_utils::GetTestFilePath(GetPDFTestDir(), FilePath()),
      false,
      file_util::FileEnumerator::FILES,
      FILE_PATH_LITERAL("*.pdf"));
  for (FilePath file_path = file_enumerator.Next();
       !file_path.empty();
       file_path = file_enumerator.Next()) {
    std::string filename = file_path.BaseName().MaybeAsASCII();
    ASSERT_FALSE(filename.empty());

#if defined(OS_MACOSX) || defined(OS_LINUX)
    if (filename == "sample.pdf")
      continue;  // Crashes on Mac and Linux.  http://crbug.com/63549
#endif

    LOG(WARNING) << "PDFBrowserTest.Loading: " << filename;

    GURL url = pdf_test_server()->GetURL(base_url + filename);
    ui_test_utils::NavigateToURL(browser(), url);

    while (true) {
      int last_count = load_stop_notification_count();
      // We might get extraneous NotificationType::LOAD_STOP notifications when
      // doing async loading.  This happens when the first loader is cancelled
      // and before creating a byte-range request loader.
      bool complete = false;
      ASSERT_TRUE(ui_test_utils::ExecuteJavaScriptAndExtractBool(
          browser()->GetSelectedTabContents()->render_view_host(),
          std::wstring(),
          L"window.domAutomationController.send(plugin.documentLoadComplete())",
          &complete));
      if (complete)
        break;

      // Check if the LOAD_STOP notification could have come while we run a
      // nested message loop for the JS call.
      if (last_count != load_stop_notification_count())
        continue;
      ui_test_utils::WaitForLoadStop(browser()->GetSelectedTabContents());
    }
  }
}

// Flaky as per http://crbug.com/74549.
#if defined(OS_MACOSX)
#define MAYBE_OnLoadAndReload DISABLED_OnLoadAndReload
#else
#define MAYBE_OnLoadAndReload FLAKY_OnLoadAndReload
#endif
IN_PROC_BROWSER_TEST_F(PDFBrowserTest, MAYBE_OnLoadAndReload) {
  ASSERT_TRUE(pdf_test_server()->Start());

  GURL url = pdf_test_server()->GetURL("files/onload_reload.html");
  ui_test_utils::NavigateToURL(browser(), url);

  ASSERT_TRUE(ui_test_utils::ExecuteJavaScript(
      browser()->GetSelectedTabContents()->render_view_host(),
      std::wstring(),
      L"reloadPDF();"));

  ASSERT_TRUE(ui_test_utils::WaitForNavigationInCurrentTab(browser()));
  ASSERT_EQ("success", browser()->GetSelectedTabContents()->GetURL().query());
}

}  // namespace
