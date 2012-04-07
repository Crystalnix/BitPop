// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/content_test_suite.h"
#include "content/test/unittest_test_suite.h"

int main(int argc, char** argv) {
  return UnitTestTestSuite(new ContentTestSuite(argc, argv)).Run();
}
