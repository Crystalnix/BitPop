// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_CHROMEDRIVER_COMMANDS_H_
#define CHROME_TEST_CHROMEDRIVER_COMMANDS_H_

#include <map>
#include <string>

#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/test/chromedriver/command.h"
#include "chrome/test/chromedriver/session_map.h"

namespace base {
class DictionaryValue;
class Value;
}

class ChromeLauncher;
struct Session;

// Creates a new session.
Status ExecuteNewSession(
    SessionMap* session_map,
    ChromeLauncher* launcher,
    const base::DictionaryValue& params,
    const std::string& session_id,
    scoped_ptr<base::Value>* out_value,
    std::string* out_session_id);

// Quits all sessions.
Status ExecuteQuitAll(
    Command quit_command,
    SessionMap* session_map,
    const base::DictionaryValue& params,
    const std::string& session_id,
    scoped_ptr<base::Value>* out_value,
    std::string* out_session_id);

// Quits a particular session.
Status ExecuteQuit(
    SessionMap* session_map,
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value);

// Loads a URL.
Status ExecuteGet(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value);

// Evaluates a given script with arguments.
Status ExecuteExecuteScript(
    Session* session,
    const base::DictionaryValue& params,
    scoped_ptr<base::Value>* value);

#endif  // CHROME_TEST_CHROMEDRIVER_COMMANDS_H_
