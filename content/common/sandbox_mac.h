// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_SANDBOX_MAC_H_
#define CONTENT_COMMON_SANDBOX_MAC_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "base/gtest_prod_util.h"

class FilePath;

#if __OBJC__
@class NSArray;
@class NSString;
#else
class NSArray;
class NSString;
#endif

namespace sandbox {

// Class representing a substring of the sandbox profile tagged with its type.
class SandboxSubstring {
 public:
  enum SandboxSubstringType {
    PLAIN,    // Just a plain string, no escaping necessary.
    LITERAL,  // Escape for use in (literal ...) expression.
    REGEX,    // Escape for use in (regex ...) expression.
  };

  SandboxSubstring() {}

  explicit SandboxSubstring(const std::string& value)
      : value_(value),
        type_(PLAIN) {}

  SandboxSubstring(const std::string& value, SandboxSubstringType type)
      : value_(value),
        type_(type) {}

  const std::string& value() { return value_; }
  SandboxSubstringType type() { return type_; }

 private:
  std::string value_;
  SandboxSubstringType type_;
};

class Sandbox {
 public:
  // A map of variable name -> string to substitute in its place.
  typedef base::hash_map<std::string, SandboxSubstring>
      SandboxVariableSubstitions;

  enum SandboxProcessType {
    SANDBOX_TYPE_FIRST_TYPE,  // Placeholder to ease iteration.

    SANDBOX_TYPE_RENDERER = SANDBOX_TYPE_FIRST_TYPE,

    // The worker process uses the most restrictive sandbox which has almost
    // *everything* locked down. Only a couple of /System/Library/ paths and
    // some other very basic operations (e.g., reading metadata to allow
    // following symlinks) are permitted.
    SANDBOX_TYPE_WORKER,

    // Utility process is as restrictive as the worker process except full
    // access is allowed to one configurable directory.
    SANDBOX_TYPE_UTILITY,

    // Native Client sandbox for the user's untrusted code.
    SANDBOX_TYPE_NACL_LOADER,

    // GPU process.
    SANDBOX_TYPE_GPU,

    SANDBOX_AFTER_TYPE_LAST_TYPE,  // Placeholder to ease iteration.
  };

  // Warm up System APIs that empirically need to be accessed before the Sandbox
  // is turned on. |sandbox_type| is the type of sandbox to warm up.
  static void SandboxWarmup(SandboxProcessType sandbox_type);

  // Turns on the OS X sandbox for this process.
  // |sandbox_type| - type of Sandbox to use.
  // |allowed_dir| - directory to allow access to, currently the only sandbox
  // profile that supports this is SANDBOX_TYPE_UTILITY .
  //
  // Returns true on success, false if an error occurred enabling the sandbox.
  static bool EnableSandbox(SandboxProcessType sandbox_type,
                            const FilePath& allowed_dir);


  // Exposed for testing purposes, used by an accessory function of our tests
  // so we can't use FRIEND_TEST.

  // Build the Sandbox command necessary to allow access to a named directory
  // indicated by |allowed_dir|.
  // Returns a string containing the sandbox profile commands necessary to allow
  // access to that directory or nil if an error occured.

  // The header comment for PostProcessSandboxProfile() explains how variable
  // substition works in sandbox templates.
  // The returned string contains embedded variables. The function fills in
  // |substitutions| to contain the values for these variables.
  static NSString* BuildAllowDirectoryAccessSandboxString(
                       const FilePath& allowed_dir,
                       SandboxVariableSubstitions* substitutions);

  // Assemble the final sandbox profile from a template by removing comments
  // and substituting variables.
  //
  // |sandbox_template| is a string which contains 2 entitites to operate on:
  //
  // - Comments - The sandbox comment syntax is used to make the OS sandbox
  // optionally ignore commands it doesn't support. e.g.
  // ;10.6_ONLY (foo)
  // Where (foo) is some command that is only supported on OS X 10.6.
  // The ;10.6_ONLY comment can then be removed from the template to enable
  // (foo) as appropriate.
  //
  // - Variables - denoted by @variable_name@ .  These are defined in the
  // sandbox template in cases where another string needs to be substituted at
  // runtime. e.g. @HOMEDIR_AS_LITERAL@ is substituted at runtime for the user's
  // home directory escaped appropriately for a (literal ...) expression.
  //
  // |comments_to_remove| is a list of NSStrings containing the comments to
  // remove.
  // |substitutions| is a hash of "variable name" -> "string to substitute".
  // Where the replacement string is tagged with information on how it is to be
  // escaped e.g. used as part of a regex string or a literal.
  //
  // On output |final_sandbox_profile_str| contains the final sandbox profile.
  // Returns true on success, false otherwise.
  static bool PostProcessSandboxProfile(
                  NSString* in_sandbox_data,
                  NSArray* comments_to_remove,
                  SandboxVariableSubstitions& substitutions,
                  std::string *final_sandbox_profile_str);

 private:
  // Escape |src_utf8| for use in a plain string variable in a sandbox
  // configuraton file.  On return |dst| is set to the quoted output.
  // Returns: true on success, false otherwise.
  static bool QuotePlainString(const std::string& src_utf8, std::string* dst);

  // Escape |str_utf8| for use in a regex literal in a sandbox
  // configuraton file.  On return |dst| is set to the utf-8 encoded quoted
  // output.
  //
  // The implementation of this function is based on empirical testing of the
  // OS X sandbox on 10.5.8 & 10.6.2 which is undocumented and subject to
  // change.
  //
  // Note: If str_utf8 contains any characters < 32 || >125 then the function
  // fails and false is returned.
  //
  // Returns: true on success, false otherwise.
  static bool QuoteStringForRegex(const std::string& str_utf8,
                                  std::string* dst);

  // Convert provided path into a "canonical" path matching what the Sandbox
  // expects i.e. one without symlinks.
  // This path is not necessarily unique e.g. in the face of hardlinks.
  static void GetCanonicalSandboxPath(FilePath* path);

  FRIEND_TEST(MacDirAccessSandboxTest, StringEscape);
  FRIEND_TEST(MacDirAccessSandboxTest, RegexEscape);
  FRIEND_TEST(MacDirAccessSandboxTest, SandboxAccess);

  DISALLOW_IMPLICIT_CONSTRUCTORS(Sandbox);
};

}  // namespace sandbox

#endif  // CONTENT_COMMON_SANDBOX_MAC_H_
