// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PAPPI_TESTS_TEST_DIRECTORY_READER_H_
#define PAPPI_TESTS_TEST_DIRECTORY_READER_H_

#include <string>

#include "ppapi/tests/test_case.h"

class TestDirectoryReader : public TestCase {
 public:
  explicit TestDirectoryReader(TestingInstance* instance)
      : TestCase(instance) {}

  // TestCase implementation.
  virtual bool Init();
  virtual void RunTest();

 private:
  std::string TestGetNextFile();
};

#endif  // PAPPI_TESTS_TEST_DIRECTORY_READER_H_
