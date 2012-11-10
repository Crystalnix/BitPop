// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/shared_impl/file_path.h"

#include <string>

#if defined(OS_WIN)
#include "base/utf_string_conversions.h"
#endif

namespace ppapi {

namespace {

FilePath GetFilePathFromUTF8(const std::string& utf8_path) {
#if defined(OS_WIN)
  return FilePath(UTF8ToUTF16(utf8_path));
#else
  return FilePath(utf8_path);
#endif
}

}  // namespace

PepperFilePath::PepperFilePath()
    : domain_(DOMAIN_INVALID),
      path_() {
}

PepperFilePath::PepperFilePath(Domain domain, const FilePath& path)
    : domain_(domain),
      path_(path) {
  // TODO(viettrungluu): Should we DCHECK() some things here?
}

// static
PepperFilePath PepperFilePath::MakeAbsolute(const FilePath& path) {
  return PepperFilePath(DOMAIN_ABSOLUTE, path);
}

// static
PepperFilePath PepperFilePath::MakeModuleLocal(const std::string& name,
                                               const char* utf8_path) {
  FilePath full_path = GetFilePathFromUTF8(name).Append(
      GetFilePathFromUTF8(utf8_path));
  return PepperFilePath(DOMAIN_MODULE_LOCAL, full_path);
}

}  // namespace ppapi
