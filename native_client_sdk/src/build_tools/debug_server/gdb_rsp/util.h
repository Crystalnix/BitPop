/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef NATIVE_CLIENT_GDB_RSP_UTIL_H_
#define NATIVE_CLIENT_GDB_RSP_UTIL_H_ 1

#include <sstream>
#include <string>
#include <vector>

#include "native_client/src/debug_server/port/std_types.h"

namespace gdb_rsp {

typedef std::vector<std::string> stringvec;

// Convert from ASCII (0-9,a-f,A-F) to 4b unsigned or return
// false if the input char is unexpected.
bool NibbleToInt(char inChar, int *outInt);

// Convert from 0-15 to ASCII (0-9,a-f) or return false
// if the input is not a value from 0-15.
bool IntToNibble(int inInt, char *outChar);

// Convert a pair of nibbles to a value from 0-255 or return
// false if ethier input character is not a valid nibble.
bool NibblesToByte(const char *inStr, int *outInt);

#ifdef WIN32
int snprintf(char *str, size_t size, const char *fmt, ...);
#endif

stringvec StringSplit(const std::string& instr, const char *delim);

}  // namespace gdb_rsp

#endif  // NATIVE_CLIENT_GDB_RSP_UTIL_H_

