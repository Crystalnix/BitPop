// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_TEST_GPU_GPU_TEST_EXPECTATIONS_PARSER_H_
#define CONTENT_TEST_GPU_GPU_TEST_EXPECTATIONS_PARSER_H_
#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "content/test/gpu/gpu_test_config.h"

class GPUTestExpectationsParser {
 public:
  enum GPUTestExpectation {
    kGpuTestPass = 1 << 0,
    kGpuTestFail = 1 << 1,
    kGpuTestFlaky = 1 << 2,
    kGpuTestTimeout = 1 << 3,
  };

  GPUTestExpectationsParser();
  ~GPUTestExpectationsParser();

  // Parse the text expectations, and if no error is encountered,
  // save all the entries. Otherwise, generate error messages.
  // Return true if parsing succeeds.
  bool LoadTestExpectations(const std::string& data);
  bool LoadTestExpectations(const FilePath& path);

  // Query error messages from the last LoadTestExpectations() call.
  const std::vector<std::string>& GetErrorMessages() const;

  // Get the test expectation of a given test on a given bot.
  int32 GetTestExpectation(const std::string& test_name,
                           const GPUTestBotConfig& bot_config) const;

 private:
  struct GPUTestExpectationEntry {
    GPUTestExpectationEntry();

    std::string test_name;
    GPUTestConfig test_config;
    int32 test_expectation;
    size_t line_number;
  };

  // Parse a line of text. If we have a valid entry, save it; otherwise,
  // generate error messages.
  bool ParseLine(const std::string& line_data, size_t line_number);

  // Update OS/GPUVendor/BuildType modifiers. May generate an error message.
  bool UpdateTestConfig(
      GPUTestConfig* config, int32 token, size_t line_number);

  // Update GPUDeviceID modifier. May generate an error message.
  bool UpdateTestConfig(GPUTestConfig* config,
                        const std::string & gpu_device_id,
                        size_t line_number);

  // Check if two entries' config overlap with each other. May generate an
  // error message.
  bool DetectConflictsBetweenEntries();

  // Save an error message, which can be queried later.
  void PushErrorMessage(const std::string& message, size_t line_number);
  void PushErrorMessage(const std::string& message,
                        size_t entry1_line_number,
                        size_t entry2_line_number);

  std::vector<GPUTestExpectationEntry> entries_;
  std::vector<std::string> error_messages_;
};

#endif  // CONTENT_TEST_GPU_GPU_TEST_EXPECTATIONS_PARSER_H_

