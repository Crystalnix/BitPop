// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PAPPI_TESTS_TEST_SCROLLBAR_H_
#define PAPPI_TESTS_TEST_SCROLLBAR_H_

#include "ppapi/cpp/dev/scrollbar_dev.h"
#include "ppapi/cpp/dev/widget_client_dev.h"
#include "ppapi/cpp/dev/widget_dev.h"
#include "ppapi/tests/test_case.h"

class TestScrollbar : public TestCase,
                      public pp::WidgetClient_Dev {
 public:
  TestScrollbar(TestingInstance* instance);

  // TestCase implementation.
  virtual void RunTest();

 private:
  std::string TestHandleEvent();

  virtual void InvalidateWidget(pp::Widget_Dev widget,
                                const pp::Rect& dirty_rect);
  virtual void ScrollbarValueChanged(pp::Scrollbar_Dev scrollbar,
                                     uint32_t value);

  pp::Scrollbar_Dev scrollbar_;
  bool scrollbar_value_changed_;
};

#endif  // PAPPI_TESTS_TEST_SCROLLBAR_H_
