// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtk/gtk.h>

#include "base/string_number_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/gtk/browser_toolbar_gtk.h"
#include "chrome/browser/ui/gtk/browser_window_gtk.h"
#include "chrome/browser/ui/gtk/location_bar_view_gtk.h"
#include "chrome/browser/ui/gtk/view_id_util.h"
#include "chrome/browser/ui/zoom/zoom_controller.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_zoom.h"
#include "grit/theme_resources.h"
#include "testing/gtest/include/gtest/gtest.h"

// TODO(dbeam): share some testing code with ZoomBubbleGtkTest.

namespace {

int GetZoomPercent(content::WebContents* contents) {
  bool dummy;
  return contents->GetZoomPercent(&dummy, &dummy);
}

void ExpectZoomedIn(content::WebContents* contents) {
  EXPECT_GT(GetZoomPercent(contents), 100);
}

void ExpectZoomedOut(content::WebContents* contents) {
  EXPECT_LT(GetZoomPercent(contents), 100);
}

void ExpectAtDefaultZoom(content::WebContents* contents) {
  EXPECT_EQ(GetZoomPercent(contents), 100);
}

}

class LocationBarViewGtkZoomTest : public InProcessBrowserTest {
 public:
  LocationBarViewGtkZoomTest() {}
  virtual ~LocationBarViewGtkZoomTest() {}

 protected:
  void ExpectTooltipContainsZoom() {
    gchar* text = gtk_widget_get_tooltip_text(GetZoomWidget());
    std::string tooltip(text);
    g_free(text);
    content::WebContents* contents = chrome::GetActiveWebContents(browser());
    std::string zoom_percent = base::IntToString(GetZoomPercent(contents));
    EXPECT_FALSE(tooltip.find(zoom_percent) == std::string::npos);
  }

  bool ZoomIconIsShowing() {
    return gtk_widget_get_visible(GetZoomWidget());
  }

  void ExpectIconIsResource(int resource_id) {
    // TODO(dbeam): actually compare the image bits with gfx::test::IsEqual?
    content::WebContents* contents = chrome::GetActiveWebContents(browser());
    ZoomController* zoom_controller = ZoomController::FromWebContents(contents);
    EXPECT_EQ(resource_id, zoom_controller->GetResourceForZoomLevel());
  }

  void ResetZoom() {
    WaitForZoom(content::PAGE_ZOOM_RESET);
  }

  content::WebContents* SetUpTest() {
    content::WebContents* contents = chrome::GetActiveWebContents(browser());
    ResetZoom();
    ExpectAtDefaultZoom(contents);
    return contents;
  }

  void ZoomIn() {
    WaitForZoom(content::PAGE_ZOOM_IN);
  }

  void ZoomOut() {
    WaitForZoom(content::PAGE_ZOOM_OUT);
  }

 private:
  GtkWidget* GetZoomWidget() {
    gfx::NativeWindow window = browser()->window()->GetNativeWindow();
    return ViewIDUtil::GetWidget(GTK_WIDGET(window), VIEW_ID_ZOOM_BUTTON);
  }

  void WaitForZoom(content::PageZoom zoom_action) {
    content::WindowedNotificationObserver zoom_observer(
        content::NOTIFICATION_ZOOM_LEVEL_CHANGED,
        content::NotificationService::AllSources());
    chrome::Zoom(browser(), zoom_action);
    zoom_observer.Wait();
  }

  DISALLOW_COPY_AND_ASSIGN(LocationBarViewGtkZoomTest);
};

IN_PROC_BROWSER_TEST_F(LocationBarViewGtkZoomTest, DefaultToZoomedInAndBack) {
  content::WebContents* contents = SetUpTest();

  ZoomIn();
  ExpectZoomedIn(contents);
  EXPECT_TRUE(ZoomIconIsShowing());
  ExpectIconIsResource(IDR_ZOOM_PLUS);
  ExpectTooltipContainsZoom();

  ZoomOut();  // Back to default, in theory.
  ExpectAtDefaultZoom(contents);
  EXPECT_FALSE(ZoomIconIsShowing());
}

IN_PROC_BROWSER_TEST_F(LocationBarViewGtkZoomTest, ZoomInTwiceAndReset) {
  content::WebContents* contents = SetUpTest();

  ZoomIn();
  int zoom_level = GetZoomPercent(contents);
  ZoomIn();
  DCHECK_GT(GetZoomPercent(contents), zoom_level);

  ExpectZoomedIn(contents);
  EXPECT_TRUE(ZoomIconIsShowing());
  ExpectIconIsResource(IDR_ZOOM_PLUS);
  ExpectTooltipContainsZoom();

  ResetZoom();
  ExpectAtDefaultZoom(contents);
  EXPECT_FALSE(ZoomIconIsShowing());
}

IN_PROC_BROWSER_TEST_F(LocationBarViewGtkZoomTest, DefaultToZoomedOutAndBack) {
  content::WebContents* contents = SetUpTest();

  ZoomOut();
  ExpectZoomedOut(contents);
  EXPECT_TRUE(ZoomIconIsShowing());
  ExpectIconIsResource(IDR_ZOOM_MINUS);
  ExpectTooltipContainsZoom();

  ZoomIn();
  ExpectAtDefaultZoom(contents);
  EXPECT_FALSE(ZoomIconIsShowing());
}

IN_PROC_BROWSER_TEST_F(LocationBarViewGtkZoomTest, ZoomOutTwiceAndReset) {
  content::WebContents* contents = SetUpTest();

  ZoomOut();
  int zoom_level = GetZoomPercent(contents);
  ZoomOut();
  DCHECK_LT(GetZoomPercent(contents), zoom_level);
  ExpectZoomedOut(contents);
  EXPECT_TRUE(ZoomIconIsShowing());
  ExpectIconIsResource(IDR_ZOOM_MINUS);
  ExpectTooltipContainsZoom();

  ResetZoom();
  ExpectAtDefaultZoom(contents);
  EXPECT_FALSE(ZoomIconIsShowing());
}
