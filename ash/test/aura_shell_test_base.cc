// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/test/aura_shell_test_base.h"

#include "ash/shell.h"
#include "ash/test/test_shell_delegate.h"

namespace ash {
namespace test {

AuraShellTestBase::AuraShellTestBase() {
}

AuraShellTestBase::~AuraShellTestBase() {
}

void AuraShellTestBase::SetUp() {
  aura::test::AuraTestBase::SetUp();

  // Creates Shell and hook with Desktop.
  ash::Shell::CreateInstance(new TestShellDelegate);
}

void AuraShellTestBase::TearDown() {
  // Flush the message loop to finish pending release tasks.
  RunAllPendingInMessageLoop();

  // Tear down the shell.
  ash::Shell::DeleteInstance();

  aura::test::AuraTestBase::TearDown();
}

}  // namespace test
}  // namespace ash
