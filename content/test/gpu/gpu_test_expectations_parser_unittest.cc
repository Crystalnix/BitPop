// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_paths.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "content/test/gpu/gpu_test_expectations_parser.h"
#include "testing/gtest/include/gtest/gtest.h"

class GPUTestExpectationsParserTest : public testing::Test {
 public:
  GPUTestExpectationsParserTest() { }

  virtual ~GPUTestExpectationsParserTest() { }

  const GPUTestBotConfig& bot_config() const {
    return bot_config_;
  }

 protected:
  void SetUp() {
    bot_config_.set_os(GPUTestConfig::kOsWin7);
    bot_config_.set_build_type(GPUTestConfig::kBuildTypeRelease);
    bot_config_.AddGPUVendor(0x10de);
    bot_config_.set_gpu_device_id(0x0640);
    ASSERT_TRUE(bot_config_.IsValid());
  }

  void TearDown() { }

 private:
  GPUTestBotConfig bot_config_;
};

TEST_F(GPUTestExpectationsParserTest, CommentOnly) {
  const std::string text =
      "  \n"
      "// This is just some comment\n"
      "";
  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(text));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
  EXPECT_EQ(GPUTestExpectationsParser::kGpuTestPass,
            parser.GetTestExpectation("some_test", bot_config()));
}

TEST_F(GPUTestExpectationsParserTest, ValidFullEntry) {
  const std::string text =
      "BUG12345 WIN7 RELEASE NVIDIA 0x0640 : MyTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(text));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
  EXPECT_EQ(GPUTestExpectationsParser::kGpuTestFail,
            parser.GetTestExpectation("MyTest", bot_config()));
}

TEST_F(GPUTestExpectationsParserTest, ValidPartialEntry) {
  const std::string text =
      "BUG12345 WIN NVIDIA : MyTest = TIMEOUT";

  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(text));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
  EXPECT_EQ(GPUTestExpectationsParser::kGpuTestTimeout,
            parser.GetTestExpectation("MyTest", bot_config()));
}

TEST_F(GPUTestExpectationsParserTest, ValidUnrelatedOsEntry) {
  const std::string text =
      "BUG12345 LEOPARD : MyTest = TIMEOUT";

  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(text));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
  EXPECT_EQ(GPUTestExpectationsParser::kGpuTestPass,
            parser.GetTestExpectation("MyTest", bot_config()));
}

TEST_F(GPUTestExpectationsParserTest, ValidUnrelatedTestEntry) {
  const std::string text =
      "BUG12345 WIN7 RELEASE NVIDIA 0x0640 : AnotherTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(text));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
  EXPECT_EQ(GPUTestExpectationsParser::kGpuTestPass,
            parser.GetTestExpectation("MyTest", bot_config()));
}

TEST_F(GPUTestExpectationsParserTest, AllModifiers) {
  const std::string text =
      "BUG12345 XP VISTA WIN7 LEOPARD SNOWLEOPARD LION LINUX CHROMEOS "
      "NVIDIA INTEL AMD RELEASE DEBUG : MyTest = PASS FAIL FLAKY TIMEOUT";

  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(text));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
  EXPECT_EQ(GPUTestExpectationsParser::kGpuTestPass |
            GPUTestExpectationsParser::kGpuTestFail |
            GPUTestExpectationsParser::kGpuTestFlaky |
            GPUTestExpectationsParser::kGpuTestTimeout,
            parser.GetTestExpectation("MyTest", bot_config()));
}

TEST_F(GPUTestExpectationsParserTest, DuplicateModifiers) {
  const std::string text =
      "BUG12345 WIN7 WIN7 RELEASE NVIDIA 0x0640 : MyTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_FALSE(parser.LoadTestExpectations(text));
  EXPECT_NE(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, AllModifiersLowerCase) {
  const std::string text =
      "BUG12345 xp vista win7 leopard snowleopard lion linux chromeos "
      "nvidia intel amd release debug : MyTest = pass fail flaky timeout";

  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(text));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
  EXPECT_EQ(GPUTestExpectationsParser::kGpuTestPass |
            GPUTestExpectationsParser::kGpuTestFail |
            GPUTestExpectationsParser::kGpuTestFlaky |
            GPUTestExpectationsParser::kGpuTestTimeout,
            parser.GetTestExpectation("MyTest", bot_config()));
}

TEST_F(GPUTestExpectationsParserTest, MissingColon) {
  const std::string text =
      "BUG12345 XP MyTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_FALSE(parser.LoadTestExpectations(text));
  EXPECT_NE(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, MissingEqual) {
  const std::string text =
      "BUG12345 XP : MyTest FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_FALSE(parser.LoadTestExpectations(text));
  EXPECT_NE(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, IllegalModifier) {
  const std::string text =
      "BUG12345 XP XXX : MyTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_FALSE(parser.LoadTestExpectations(text));
  EXPECT_NE(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, OsConflicts) {
  const std::string text =
      "BUG12345 XP WIN : MyTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_FALSE(parser.LoadTestExpectations(text));
  EXPECT_NE(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, InvalidModifierCombination) {
  const std::string text =
      "BUG12345 XP NVIDIA INTEL 0x0640 : MyTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_FALSE(parser.LoadTestExpectations(text));
  EXPECT_NE(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, BadGpuDeviceID) {
  const std::string text =
      "BUG12345 XP NVIDIA 0xU07X : MyTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_FALSE(parser.LoadTestExpectations(text));
  EXPECT_NE(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, MoreThanOneGpuDeviceID) {
  const std::string text =
      "BUG12345 XP NVIDIA 0x0640 0x0641 : MyTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_FALSE(parser.LoadTestExpectations(text));
  EXPECT_NE(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, MultipleEntriesConflicts) {
  const std::string text =
      "BUG12345 WIN7 RELEASE NVIDIA 0x0640 : MyTest = FAIL\n"
      "BUG12345 WIN : MyTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_FALSE(parser.LoadTestExpectations(text));
  EXPECT_NE(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, MultipleTests) {
  const std::string text =
      "BUG12345 WIN7 RELEASE NVIDIA 0x0640 : MyTest = FAIL\n"
      "BUG12345 WIN : AnotherTest = FAIL";

  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(text));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
}

TEST_F(GPUTestExpectationsParserTest, ValidMultipleEntries) {
  const std::string text =
      "BUG12345 WIN7 RELEASE NVIDIA 0x0640 : MyTest = FAIL\n"
      "BUG12345 LINUX : MyTest = TIMEOUT";

  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(text));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
  EXPECT_EQ(GPUTestExpectationsParser::kGpuTestFail,
            parser.GetTestExpectation("MyTest", bot_config()));
}

TEST_F(GPUTestExpectationsParserTest, WebGLTestExpectationsValidation) {
  FilePath path;
  ASSERT_TRUE(PathService::Get(base::DIR_SOURCE_ROOT, &path));
  path = path.Append(FILE_PATH_LITERAL("chrome"))
             .Append(FILE_PATH_LITERAL("test"))
             .Append(FILE_PATH_LITERAL("gpu"))
             .Append(FILE_PATH_LITERAL(
                 "webgl_conformance_test_expectations.txt"));
  ASSERT_TRUE(file_util::PathExists(path));

  GPUTestExpectationsParser parser;
  EXPECT_TRUE(parser.LoadTestExpectations(path));
  EXPECT_EQ(0u, parser.GetErrorMessages().size());
  for (size_t i = 0; i < parser.GetErrorMessages().size(); ++i)
    LOG(ERROR) << parser.GetErrorMessages()[i];
}

