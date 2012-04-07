// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_TESTS_TEST_IMAGE_DATA_H_
#define PPAPI_TESTS_TEST_IMAGE_DATA_H_

#include "ppapi/c/ppb_image_data.h"
#include "ppapi/tests/test_case.h"

class TestImageData : public TestCase {
 public:
  TestImageData(TestingInstance* instance) : TestCase(instance) {}

  // TestCase implementation.
  virtual bool Init();
  virtual void RunTests(const std::string& filter);

 private:
  std::string TestInvalidFormat();
  std::string TestInvalidSize();
  std::string TestHugeSize();
  std::string TestInitToZero();
  std::string TestIsImageData();

  // Used by the tests that access the C API directly.
  const PPB_ImageData* image_data_interface_;
};

#endif  // PPAPI_TESTS_TEST_IMAGE_DATA_H_
