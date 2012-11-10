// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/gestures/velocity_calculator.h"

namespace ui {
namespace test {

namespace {

static void AddPoints(VelocityCalculator* velocity_calculator,
                      float x_increment,
                      float y_increment,
                      float time_increment_seconds,
                      int num_points) {
  float x = 0;
  float y = 0;
  double time = 0;

  for (int i = 0; i < num_points; ++i) {
    velocity_calculator->PointSeen(x, y, time);
    x += x_increment;
    y += y_increment;
    time += time_increment_seconds * 1000000;
  }
}

}  // namespace

// Test that the velocity returned is reasonable
TEST(VelocityCalculatorTest, ReturnsReasonableVelocity) {
  VelocityCalculator velocity_calculator(5);
  AddPoints(&velocity_calculator, 10, -10, 1, 7);

  EXPECT_GT(velocity_calculator.XVelocity(), 9.9);
  EXPECT_LT(velocity_calculator.XVelocity(), 10.1);
  EXPECT_GT(velocity_calculator.YVelocity(), -10.1);
  EXPECT_LT(velocity_calculator.YVelocity(), -9.9);

  velocity_calculator.PointSeen(9, -11, 5500000);
  velocity_calculator.PointSeen(21, -19, 6000000);
  velocity_calculator.PointSeen(30, -32, 6500000);
  velocity_calculator.PointSeen(38, -40, 7000000);
  velocity_calculator.PointSeen(50, -51, 7500000);

  EXPECT_GT(velocity_calculator.XVelocity(), 19);
  EXPECT_LT(velocity_calculator.XVelocity(), 21);
  EXPECT_GT(velocity_calculator.YVelocity(), -21);
  EXPECT_LT(velocity_calculator.YVelocity(), -19);

  // Significantly larger difference in position
  velocity_calculator.PointSeen(70, -70, 8000000);

  EXPECT_GT(velocity_calculator.XVelocity(), 20);
  EXPECT_LT(velocity_calculator.XVelocity(), 25);
  EXPECT_GT(velocity_calculator.YVelocity(), -25);
  EXPECT_LT(velocity_calculator.YVelocity(), -20);
}

TEST(VelocityCalculatorTest, IsAccurateWithLargeTimes) {
  VelocityCalculator velocity_calculator(5);
  int64 start_time = 0;
  velocity_calculator.PointSeen(9, -11, start_time);
  velocity_calculator.PointSeen(21, -19, start_time + 8);
  velocity_calculator.PointSeen(30, -32, start_time + 16);
  velocity_calculator.PointSeen(38, -40, start_time + 24);
  velocity_calculator.PointSeen(50, -51, start_time + 32);

  EXPECT_GT(velocity_calculator.XVelocity(), 1230000);
  EXPECT_LT(velocity_calculator.XVelocity(), 1260000);
  EXPECT_GT(velocity_calculator.YVelocity(), -1270000);
  EXPECT_LT(velocity_calculator.YVelocity(), -1240000);

  start_time = GG_LONGLONG(1223372036800000000);
  velocity_calculator.PointSeen(9, -11, start_time);
  velocity_calculator.PointSeen(21, -19, start_time + 8);
  velocity_calculator.PointSeen(30, -32, start_time + 16);
  velocity_calculator.PointSeen(38, -40, start_time + 24);
  velocity_calculator.PointSeen(50, -51, start_time + 32);

  EXPECT_GT(velocity_calculator.XVelocity(), 1230000);
  EXPECT_LT(velocity_calculator.XVelocity(), 1260000);
  EXPECT_GT(velocity_calculator.YVelocity(), -1270000);
  EXPECT_LT(velocity_calculator.YVelocity(), -124000);
}

// Check that the velocity returned is 0 if the velocity calculator
// doesn't have enough data
TEST(VelocityCalculatorTest, RequiresEnoughData) {
  VelocityCalculator velocity_calculator(5);
  EXPECT_EQ(velocity_calculator.XVelocity(), 0);
  EXPECT_EQ(velocity_calculator.YVelocity(), 0);

  AddPoints(&velocity_calculator, 10, 10, 1, 4);

  // We've only seen 4 points, the buffer size is 5
  // Since the buffer isn't full, return 0
  EXPECT_EQ(velocity_calculator.XVelocity(), 0);
  EXPECT_EQ(velocity_calculator.YVelocity(), 0);

  AddPoints(&velocity_calculator, 10, 10, 1, 1);

  EXPECT_GT(velocity_calculator.XVelocity(), 9.9);
  EXPECT_GT(velocity_calculator.YVelocity(), 9.9);
}

// Ensures ClearHistory behaves correctly
TEST(VelocityCalculatorTest, ClearsHistory) {
  VelocityCalculator velocity_calculator(5);
  AddPoints(&velocity_calculator, 10, -10, 1, 7);

  EXPECT_GT(velocity_calculator.XVelocity(), 9.9);
  EXPECT_LT(velocity_calculator.XVelocity(), 10.1);
  EXPECT_GT(velocity_calculator.YVelocity(), -10.1);
  EXPECT_LT(velocity_calculator.YVelocity(), -9.9);

  velocity_calculator.ClearHistory();

  EXPECT_EQ(velocity_calculator.XVelocity(), 0);
  EXPECT_EQ(velocity_calculator.YVelocity(), 0);
}

// Ensure data older than the buffer size is ignored
TEST(VelocityCalculatorTest, IgnoresOldData) {
  VelocityCalculator velocity_calculator(5);
  AddPoints(&velocity_calculator, 10, -10, 1, 7);

  EXPECT_GT(velocity_calculator.XVelocity(), 9.9);
  EXPECT_LT(velocity_calculator.XVelocity(), 10.1);
  EXPECT_GT(velocity_calculator.YVelocity(), -10.1);
  EXPECT_LT(velocity_calculator.YVelocity(), -9.9);

  AddPoints(&velocity_calculator, 0, 0, 1, 5);

  EXPECT_EQ(velocity_calculator.XVelocity(), 0);
  EXPECT_EQ(velocity_calculator.YVelocity(), 0);
}

}  // namespace test
}  // namespace ui
