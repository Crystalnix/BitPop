// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_MOCK_EXPORTED_OBJECT_H_
#define DBUS_MOCK_EXPORTED_OBJECT_H_
#pragma once

#include <string>

#include "dbus/exported_object.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace dbus {

// Mock for ExportedObject.
class MockExportedObject : public ExportedObject {
 public:
  MockExportedObject(Bus* bus,
                     const std::string& service_name,
                     const std::string& object_path);
  virtual ~MockExportedObject();

  MOCK_METHOD3(ExportMethodAndBlock,
               bool(const std::string& interface_name,
                    const std::string& method_name,
                    MethodCallCallback method_call_callback));
  MOCK_METHOD4(ExportMethod,
               void(const std::string& interface_name,
                    const std::string& method_name,
                    MethodCallCallback method_call_callback,
                    OnExportedCallback on_exported_callback));
  MOCK_METHOD1(SendSignal, void(Signal* signal));
  MOCK_METHOD0(Unregister, void());
};

}  // namespace dbus

#endif  // DBUS_MOCK_EXPORTED_OBJECT_H_
