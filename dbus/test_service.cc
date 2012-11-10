// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus/test_service.h"

#include "base/bind.h"
#include "base/test/test_timeouts.h"
#include "base/threading/platform_thread.h"
#include "dbus/bus.h"
#include "dbus/exported_object.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/property.h"

namespace dbus {

// Echo, SlowEcho, AsyncEcho, BrokenMethod, GetAll, Get, Set.
const int TestService::kNumMethodsToExport = 7;

TestService::Options::Options() {
}

TestService::Options::~Options() {
}

TestService::TestService(const Options& options)
    : base::Thread("TestService"),
      dbus_thread_message_loop_proxy_(options.dbus_thread_message_loop_proxy),
      on_all_methods_exported_(false, false),
      num_exported_methods_(0) {
}

TestService::~TestService() {
  Stop();
}

bool TestService::StartService() {
  base::Thread::Options thread_options;
  thread_options.message_loop_type = MessageLoop::TYPE_IO;
  return StartWithOptions(thread_options);
}

bool TestService::WaitUntilServiceIsStarted() {
  const base::TimeDelta timeout(TestTimeouts::action_max_timeout());
  // Wait until all methods are exported.
  return on_all_methods_exported_.TimedWait(timeout);
}

void TestService::ShutdownAndBlock() {
  message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&TestService::ShutdownAndBlockInternal,
                 base::Unretained(this)));
}

bool TestService::HasDBusThread() {
  return bus_->HasDBusThread();
}

void TestService::ShutdownAndBlockInternal() {
  if (HasDBusThread())
    bus_->ShutdownOnDBusThreadAndBlock();
  else
    bus_->ShutdownAndBlock();
}

void TestService::SendTestSignal(const std::string& message) {
  message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&TestService::SendTestSignalInternal,
                 base::Unretained(this),
                 message));
}

void TestService::SendTestSignalFromRoot(const std::string& message) {
  message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&TestService::SendTestSignalFromRootInternal,
                 base::Unretained(this),
                 message));
}

void TestService::SendTestSignalInternal(const std::string& message) {
  dbus::Signal signal("org.chromium.TestInterface", "Test");
  dbus::MessageWriter writer(&signal);
  writer.AppendString(message);
  exported_object_->SendSignal(&signal);
}

void TestService::SendTestSignalFromRootInternal(const std::string& message) {
  dbus::Signal signal("org.chromium.TestInterface", "Test");
  dbus::MessageWriter writer(&signal);
  writer.AppendString(message);

  bus_->RequestOwnership("org.chromium.TestService",
                         base::Bind(&TestService::OnOwnership,
                                    base::Unretained(this)));

  // Use "/" just like dbus-send does.
  ExportedObject* root_object =
      bus_->GetExportedObject(dbus::ObjectPath("/"));
  root_object->SendSignal(&signal);
}

void TestService::OnOwnership(const std::string& service_name,
                              bool success) {
  LOG_IF(ERROR, !success) << "Failed to own: " << service_name;
}

void TestService::OnExported(const std::string& interface_name,
                             const std::string& method_name,
                             bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to export: " << interface_name << "."
               << method_name;
    // Returning here will make WaitUntilServiceIsStarted() to time out
    // and return false.
    return;
  }

  ++num_exported_methods_;
  if (num_exported_methods_ == kNumMethodsToExport)
    on_all_methods_exported_.Signal();
}

void TestService::Run(MessageLoop* message_loop) {
  Bus::Options bus_options;
  bus_options.bus_type = Bus::SESSION;
  bus_options.connection_type = Bus::PRIVATE;
  bus_options.dbus_thread_message_loop_proxy = dbus_thread_message_loop_proxy_;
  bus_ = new Bus(bus_options);

  bus_->RequestOwnership("org.chromium.TestService",
                         base::Bind(&TestService::OnOwnership,
                                    base::Unretained(this)));

  exported_object_ = bus_->GetExportedObject(
      dbus::ObjectPath("/org/chromium/TestObject"));

  int num_methods = 0;
  exported_object_->ExportMethod(
      "org.chromium.TestInterface",
      "Echo",
      base::Bind(&TestService::Echo,
                 base::Unretained(this)),
      base::Bind(&TestService::OnExported,
                 base::Unretained(this)));
  ++num_methods;

  exported_object_->ExportMethod(
      "org.chromium.TestInterface",
      "SlowEcho",
      base::Bind(&TestService::SlowEcho,
                 base::Unretained(this)),
      base::Bind(&TestService::OnExported,
                 base::Unretained(this)));
  ++num_methods;

  exported_object_->ExportMethod(
      "org.chromium.TestInterface",
      "AsyncEcho",
      base::Bind(&TestService::AsyncEcho,
                 base::Unretained(this)),
      base::Bind(&TestService::OnExported,
                 base::Unretained(this)));
  ++num_methods;

  exported_object_->ExportMethod(
      "org.chromium.TestInterface",
      "BrokenMethod",
      base::Bind(&TestService::BrokenMethod,
                 base::Unretained(this)),
      base::Bind(&TestService::OnExported,
                 base::Unretained(this)));
  ++num_methods;

  exported_object_->ExportMethod(
       kPropertiesInterface,
       kPropertiesGetAll,
       base::Bind(&TestService::GetAllProperties,
                  base::Unretained(this)),
       base::Bind(&TestService::OnExported,
                  base::Unretained(this)));
  ++num_methods;

  exported_object_->ExportMethod(
       kPropertiesInterface,
       kPropertiesGet,
       base::Bind(&TestService::GetProperty,
                  base::Unretained(this)),
       base::Bind(&TestService::OnExported,
                  base::Unretained(this)));
  ++num_methods;

  exported_object_->ExportMethod(
       kPropertiesInterface,
       kPropertiesSet,
       base::Bind(&TestService::SetProperty,
                  base::Unretained(this)),
       base::Bind(&TestService::OnExported,
                  base::Unretained(this)));
  ++num_methods;

  // Just print an error message as we don't want to crash tests.
  // Tests will fail at a call to WaitUntilServiceIsStarted().
  if (num_methods != kNumMethodsToExport) {
    LOG(ERROR) << "The number of methods does not match";
  }
  message_loop->Run();
}

void TestService::Echo(MethodCall* method_call,
                       dbus::ExportedObject::ResponseSender response_sender) {
  MessageReader reader(method_call);
  std::string text_message;
  if (!reader.PopString(&text_message)) {
    response_sender.Run(NULL);
    return;
  }

  Response* response = Response::FromMethodCall(method_call);
  MessageWriter writer(response);
  writer.AppendString(text_message);
  response_sender.Run(response);
}

void TestService::SlowEcho(
    MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  base::PlatformThread::Sleep(TestTimeouts::tiny_timeout());
  Echo(method_call, response_sender);
}

void TestService::AsyncEcho(
    MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  // Schedule a call to Echo() to send an asynchronous response after we return.
  message_loop()->PostDelayedTask(FROM_HERE,
                                  base::Bind(&TestService::Echo,
                                             base::Unretained(this),
                                             method_call,
                                             response_sender),
                                  TestTimeouts::tiny_timeout());
}

void TestService::BrokenMethod(
    MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  response_sender.Run(NULL);
}


void TestService::GetAllProperties(
    MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  MessageReader reader(method_call);
  std::string interface;
  if (!reader.PopString(&interface)) {
    response_sender.Run(NULL);
    return;
  }

  // The properties response is a dictionary of strings identifying the
  // property and a variant containing the property value. We return all
  // of the properties, thus the response is:
  //
  // {
  //   "Name": Variant<"TestService">,
  //   "Version": Variant<10>,
  //   "Methods": Variant<["Echo", "SlowEcho", "AsyncEcho", "BrokenMethod"]>,
  //   "Objects": Variant<[objectpath:"/TestObjectPath"]>
  // ]

  Response* response = Response::FromMethodCall(method_call);
  MessageWriter writer(response);

  MessageWriter array_writer(NULL);
  MessageWriter dict_entry_writer(NULL);
  MessageWriter variant_writer(NULL);
  MessageWriter variant_array_writer(NULL);

  writer.OpenArray("{sv}", &array_writer);

  array_writer.OpenDictEntry(&dict_entry_writer);
  dict_entry_writer.AppendString("Name");
  dict_entry_writer.AppendVariantOfString("TestService");
  array_writer.CloseContainer(&dict_entry_writer);

  array_writer.OpenDictEntry(&dict_entry_writer);
  dict_entry_writer.AppendString("Version");
  dict_entry_writer.AppendVariantOfInt16(10);
  array_writer.CloseContainer(&dict_entry_writer);

  array_writer.OpenDictEntry(&dict_entry_writer);
  dict_entry_writer.AppendString("Methods");
  dict_entry_writer.OpenVariant("as", &variant_writer);
  variant_writer.OpenArray("s", &variant_array_writer);
  variant_array_writer.AppendString("Echo");
  variant_array_writer.AppendString("SlowEcho");
  variant_array_writer.AppendString("AsyncEcho");
  variant_array_writer.AppendString("BrokenMethod");
  variant_writer.CloseContainer(&variant_array_writer);
  dict_entry_writer.CloseContainer(&variant_writer);
  array_writer.CloseContainer(&dict_entry_writer);

  array_writer.OpenDictEntry(&dict_entry_writer);
  dict_entry_writer.AppendString("Objects");
  dict_entry_writer.OpenVariant("ao", &variant_writer);
  variant_writer.OpenArray("o", &variant_array_writer);
  variant_array_writer.AppendObjectPath(dbus::ObjectPath("/TestObjectPath"));
  variant_writer.CloseContainer(&variant_array_writer);
  dict_entry_writer.CloseContainer(&variant_writer);
  array_writer.CloseContainer(&dict_entry_writer);

  writer.CloseContainer(&array_writer);

  response_sender.Run(response);
}

void TestService::GetProperty(
    MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  MessageReader reader(method_call);
  std::string interface;
  if (!reader.PopString(&interface)) {
    response_sender.Run(NULL);
    return;
  }

  std::string name;
  if (!reader.PopString(&name)) {
    response_sender.Run(NULL);
    return;
  }

  if (name == "Name") {
    // Return the previous value for the "Name" property:
    // Variant<"TestService">
    Response* response = Response::FromMethodCall(method_call);
    MessageWriter writer(response);

    writer.AppendVariantOfString("TestService");

    response_sender.Run(response);
  } else if (name == "Version") {
    // Return a new value for the "Version" property:
    // Variant<20>
    Response* response = Response::FromMethodCall(method_call);
    MessageWriter writer(response);

    writer.AppendVariantOfInt16(20);

    response_sender.Run(response);
  } else if (name == "Methods") {
    // Return the previous value for the "Methods" property:
    // Variant<["Echo", "SlowEcho", "AsyncEcho", "BrokenMethod"]>
    Response* response = Response::FromMethodCall(method_call);
    MessageWriter writer(response);
    MessageWriter variant_writer(NULL);
    MessageWriter variant_array_writer(NULL);

    writer.OpenVariant("as", &variant_writer);
    variant_writer.OpenArray("s", &variant_array_writer);
    variant_array_writer.AppendString("Echo");
    variant_array_writer.AppendString("SlowEcho");
    variant_array_writer.AppendString("AsyncEcho");
    variant_array_writer.AppendString("BrokenMethod");
    variant_writer.CloseContainer(&variant_array_writer);
    writer.CloseContainer(&variant_writer);

    response_sender.Run(response);
  } else if (name == "Objects") {
    // Return the previous value for the "Objects" property:
    // Variant<[objectpath:"/TestObjectPath"]>
    Response* response = Response::FromMethodCall(method_call);
    MessageWriter writer(response);
    MessageWriter variant_writer(NULL);
    MessageWriter variant_array_writer(NULL);

    writer.OpenVariant("ao", &variant_writer);
    variant_writer.OpenArray("o", &variant_array_writer);
    variant_array_writer.AppendObjectPath(dbus::ObjectPath("/TestObjectPath"));
    variant_writer.CloseContainer(&variant_array_writer);
    writer.CloseContainer(&variant_writer);

    response_sender.Run(response);
  } else {
    // Return error.
    response_sender.Run(NULL);
    return;
  }
}

void TestService::SetProperty(
    MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  MessageReader reader(method_call);
  std::string interface;
  if (!reader.PopString(&interface)) {
    response_sender.Run(NULL);
    return;
  }

  std::string name;
  if (!reader.PopString(&name)) {
    response_sender.Run(NULL);
    return;
  }

  if (name != "Name") {
    response_sender.Run(NULL);
    return;
  }

  std::string value;
  if (!reader.PopVariantOfString(&value)) {
    response_sender.Run(NULL);
    return;
  }

  SendPropertyChangedSignal(value);

  Response* response = Response::FromMethodCall(method_call);
  response_sender.Run(response);
}

void TestService::SendPropertyChangedSignal(const std::string& name) {
  message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&TestService::SendPropertyChangedSignalInternal,
                 base::Unretained(this),
                 name));
}

void TestService::SendPropertyChangedSignalInternal(const std::string& name) {
  dbus::Signal signal(kPropertiesInterface, kPropertiesChanged);
  dbus::MessageWriter writer(&signal);
  writer.AppendString("org.chromium.TestService");

  MessageWriter array_writer(NULL);
  MessageWriter dict_entry_writer(NULL);

  writer.OpenArray("{sv}", &array_writer);
  array_writer.OpenDictEntry(&dict_entry_writer);
  dict_entry_writer.AppendString("Name");
  dict_entry_writer.AppendVariantOfString(name);
  array_writer.CloseContainer(&dict_entry_writer);
  writer.CloseContainer(&array_writer);

  exported_object_->SendSignal(&signal);
}

}  // namespace dbus
