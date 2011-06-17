// Copyright 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ui/npapi_test_helper.h"

#include "base/file_util.h"
#include "base/test/test_file_util.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "webkit/glue/plugins/plugin_list.h"

namespace npapi_test {
const char kTestCompleteCookie[] = "status";
const char kTestCompleteSuccess[] = "OK";
}  // namespace npapi_test.

NPAPITesterBase::NPAPITesterBase() {
}

void NPAPITesterBase::SetUp() {
#if defined(OS_MACOSX)
  // The plugins directory isn't read by default on the Mac, so it needs to be
  // explicitly registered.
  launch_arguments_.AppendSwitchPath(switches::kExtraPluginDir,
                                     GetPluginsDirectory());
#endif

  UITest::SetUp();
}

FilePath NPAPITesterBase::GetPluginsDirectory() {
  FilePath plugins_directory = browser_directory_.AppendASCII("plugins");
  return plugins_directory;
}

// NPAPIVisiblePluginTester members.
void NPAPIVisiblePluginTester::SetUp() {
  show_window_ = true;
  NPAPITesterBase::SetUp();
}

// NPAPIIncognitoTester members.
void NPAPIIncognitoTester::SetUp() {
  launch_arguments_.AppendSwitch(switches::kIncognito);
  NPAPITesterBase::SetUp();
}
