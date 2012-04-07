// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/forms/form_data.h"

#include "base/string_util.h"

namespace webkit {
namespace forms {

FormData::FormData()
    : user_submitted(false) {
}

FormData::FormData(const FormData& data)
    : name(data.name),
      method(data.method),
      origin(data.origin),
      action(data.action),
      user_submitted(data.user_submitted),
      fields(data.fields) {
}

FormData::~FormData() {
}

bool FormData::operator==(const FormData& form) const {
  return (name == form.name &&
          StringToLowerASCII(method) == StringToLowerASCII(form.method) &&
          origin == form.origin &&
          action == form.action &&
          user_submitted == form.user_submitted &&
          fields == form.fields);
}

}  // namespace forms
}  // namespace webkit
