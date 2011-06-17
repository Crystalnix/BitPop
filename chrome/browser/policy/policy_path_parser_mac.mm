// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/policy_path_parser.h"

#include "base/logging.h"
#include "base/sys_string_conversions.h"

#import <Cocoa/Cocoa.h>

#include <string>

namespace policy {

namespace path_parser {

const char* kUserNamePolicyVarName = "${user_name}";
const char* kMachineNamePolicyVarName = "${machine_name}";
const char* kMacUsersDirectory = "${users}";
const char* kMacDocumentsFolderVarName = "${documents}";

struct MacFolderNamesToSPDMaping {
  const char* name;
  NSSearchPathDirectory id;
};

// Mapping from variable names to MacOS NSSearchPathDirectory ids.
const MacFolderNamesToSPDMaping mac_folder_mapping[] = {
    { kMacUsersDirectory, NSUserDirectory},
    { kMacDocumentsFolderVarName, NSDocumentDirectory}
};

// Replaces all variable occurrences in the policy string with the respective
// system settings values.
FilePath::StringType ExpandPathVariables(
    const FilePath::StringType& untranslated_string) {
  FilePath::StringType result(untranslated_string);
  // First translate all path variables we recognize.
  for (size_t i = 0; i < arraysize(mac_folder_mapping); ++i) {
    size_t position = result.find(mac_folder_mapping[i].name);
    if (position != std::string::npos) {
      NSArray* searchpaths = NSSearchPathForDirectoriesInDomains(
          mac_folder_mapping[i].id, NSAllDomainsMask, true);
      if ([searchpaths count] > 0) {
        NSString *variable_value = [searchpaths objectAtIndex:0];
        result.replace(position, strlen(mac_folder_mapping[i].name),
                       base::SysNSStringToUTF8(variable_value));
      }
    }
  }
  // Next translate two special variables ${user_name} and ${machine_name}
  size_t position = result.find(kUserNamePolicyVarName);
  if (position != std::string::npos) {
    NSString* username = NSUserName();
    if (username) {
      result.replace(position, strlen(kUserNamePolicyVarName),
                     base::SysNSStringToUTF8(username));
    } else {
      LOG(ERROR) << "Username variable can not be resolved.";
    }
  }
  position = result.find(kMachineNamePolicyVarName);
  if (position != std::string::npos) {
    NSString* machinename = [[NSHost currentHost] name];
    if (machinename) {
      result.replace(position, strlen(kMachineNamePolicyVarName),
                     base::SysNSStringToUTF8(machinename));
    } else {
      LOG(ERROR) << "Machine name variable can not be resolved.";
    }
  }
  return result;
}

}  // namespace path_parser

}  // namespace policy
