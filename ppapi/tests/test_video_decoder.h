// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_TESTS_TEST_VIDEO_DECODER_H_
#define PPAPI_TESTS_TEST_VIDEO_DECODER_H_

#include "ppapi/c/pp_stdint.h"
#include "ppapi/tests/test_case.h"

struct PPB_Testing_Dev;
struct PPB_Var;
struct PPB_VideoDecoder_Dev;

class TestVideoDecoder : public TestCase {
 public:
  TestVideoDecoder(TestingInstance* instance) : TestCase(instance) {}

  // TestCase implementation.
  virtual bool Init();
  virtual void RunTest();

  void QuitMessageLoop();

 private:
  std::string TestCreate();

  // Used by the tests that access the C API directly.
  const PPB_VideoDecoder_Dev* video_decoder_interface_;
  const PPB_Var* var_interface_;
  const PPB_Testing_Dev* testing_interface_;
};

#endif  // PPAPI_TESTS_TEST_VIDEO_DECODER_H_
