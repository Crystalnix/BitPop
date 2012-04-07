// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_MOCK_BUS_H_
#define DBUS_MOCK_BUS_H_
#pragma once

#include "dbus/bus.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace dbus {

// Mock for Bus class. Along with MockObjectProxy and MockExportedObject,
// the mock classes can be used to write unit tests without issuing real
// D-Bus calls.
class MockBus : public Bus {
 public:
  MockBus(Bus::Options& options);
  virtual ~MockBus();

  MOCK_METHOD2(GetObjectProxy, ObjectProxy*(const std::string& service_name,
                                            const std::string& object_path));
  MOCK_METHOD2(GetExportedObject, ExportedObject*(
      const std::string& service_name,
      const std::string& object_path));
  MOCK_METHOD0(ShutdownAndBlock, void());
  MOCK_METHOD0(ShutdownOnDBusThreadAndBlock, void());
  MOCK_METHOD0(Connect, bool());
  MOCK_METHOD1(RequestOwnership, bool(const std::string& service_name));
  MOCK_METHOD1(ReleaseOwnership, bool(const std::string& service_name));
  MOCK_METHOD0(SetUpAsyncOperations, bool());
  MOCK_METHOD3(SendWithReplyAndBlock, DBusMessage*(DBusMessage* request,
                                                   int timeout_ms,
                                                   DBusError* error));
  MOCK_METHOD3(SendWithReply, void(DBusMessage* request,
                                   DBusPendingCall** pending_call,
                                   int timeout_ms));
  MOCK_METHOD2(Send, void(DBusMessage* request,
                          uint32* serial));
  MOCK_METHOD2(AddFilter, void(DBusHandleMessageFunction handle_message,
                               void* user_data));
  MOCK_METHOD2(RemoveFilter, void(DBusHandleMessageFunction handle_message,
                                  void* user_data));
  MOCK_METHOD2(AddMatch, void(const std::string& match_rule,
                              DBusError* error));
  MOCK_METHOD2(RemoveMatch, void(const std::string& match_rule,
                                 DBusError* error));
  MOCK_METHOD4(TryRegisterObjectPath, bool(const std::string& object_path,
                                           const DBusObjectPathVTable* vtable,
                                           void* user_data,
                                           DBusError* error));
  MOCK_METHOD1(UnregisterObjectPath, void(const std::string& object_path));
  MOCK_METHOD2(PostTaskToOriginThread, void(
      const tracked_objects::Location& from_here,
      const base::Closure& task));
  MOCK_METHOD2(PostTaskToDBusThread, void(
      const tracked_objects::Location& from_here,
      const base::Closure& task));
  MOCK_METHOD3(PostDelayedTaskToDBusThread, void(
      const tracked_objects::Location& from_here,
      const base::Closure& task,
      int delay_ms));
  MOCK_METHOD0(HasDBusThread, bool());
  MOCK_METHOD0(AssertOnOriginThread, void());
  MOCK_METHOD0(AssertOnDBusThread, void());
};

}  // namespace dbus

#endif  // DBUS_MOCK_BUS_H_
