// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/flimflam_manager_client.h"

#include "base/bind.h"
#include "base/chromeos/chromeos_version.h"
#include "base/message_loop.h"
#include "base/values.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "dbus/values_util.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

// Returns whether the properties have the required keys or not.
bool AreServicePropertiesValid(const base::DictionaryValue& properties) {
  if (properties.HasKey(flimflam::kGuidProperty))
    return true;
  return properties.HasKey(flimflam::kTypeProperty) &&
      properties.HasKey(flimflam::kSecurityProperty) &&
      properties.HasKey(flimflam::kSSIDProperty);
}

// Appends a string-to-variant dictionary to the writer.
void AppendServicePropertiesDictionary(
    dbus::MessageWriter* writer,
    const base::DictionaryValue& dictionary) {
  dbus::MessageWriter array_writer(NULL);
  writer->OpenArray("{sv}", &array_writer);
  for (base::DictionaryValue::Iterator it(dictionary);
       it.HasNext();
       it.Advance()) {
    dbus::MessageWriter entry_writer(NULL);
    array_writer.OpenDictEntry(&entry_writer);
    entry_writer.AppendString(it.key());
    FlimflamClientHelper::AppendValueDataAsVariant(&entry_writer, it.value());
    array_writer.CloseContainer(&entry_writer);
  }
  writer->CloseContainer(&array_writer);
}

// The FlimflamManagerClient implementation.
class FlimflamManagerClientImpl : public FlimflamManagerClient {
 public:
  explicit FlimflamManagerClientImpl(dbus::Bus* bus)
      : proxy_(bus->GetObjectProxy(
          flimflam::kFlimflamServiceName,
          dbus::ObjectPath(flimflam::kFlimflamServicePath))),
        helper_(bus, proxy_) {
    helper_.MonitorPropertyChanged(flimflam::kFlimflamManagerInterface);
  }

  // FlimflamManagerClient overrides:
  virtual void SetPropertyChangedHandler(
      const PropertyChangedHandler& handler) OVERRIDE {
    helper_.SetPropertyChangedHandler(handler);
  }

  virtual void ResetPropertyChangedHandler() OVERRIDE {
    helper_.ResetPropertyChangedHandler();
  }

  virtual void GetProperties(const DictionaryValueCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kGetPropertiesFunction);
    helper_.CallDictionaryValueMethod(&method_call, callback);
  }

  virtual base::DictionaryValue* CallGetPropertiesAndBlock() OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kGetPropertiesFunction);
    return helper_.CallDictionaryValueMethodAndBlock(&method_call);
  }

  virtual void SetProperty(const std::string& name,
                           const base::Value& value,
                           const VoidDBusMethodCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kSetPropertyFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(name);
    FlimflamClientHelper::AppendValueDataAsVariant(&writer, value);
    helper_.CallVoidMethod(&method_call, callback);
  }

  virtual void RequestScan(const std::string& type,
                           const VoidDBusMethodCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kRequestScanFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(type);
    helper_.CallVoidMethod(&method_call, callback);
  }

  virtual void EnableTechnology(
      const std::string& type,
      const VoidDBusMethodCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kEnableTechnologyFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(type);
    helper_.CallVoidMethod(&method_call, callback);
  }

  virtual void DisableTechnology(
      const std::string& type,
      const VoidDBusMethodCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kDisableTechnologyFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(type);
    helper_.CallVoidMethod(&method_call, callback);
  }

  virtual void ConfigureService(
      const base::DictionaryValue& properties,
      const VoidDBusMethodCallback& callback) OVERRIDE {
    DCHECK(AreServicePropertiesValid(properties));
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kConfigureServiceFunction);
    dbus::MessageWriter writer(&method_call);
    AppendServicePropertiesDictionary(&writer, properties);
    helper_.CallVoidMethod(&method_call, callback);
  }

  virtual void GetService(
      const base::DictionaryValue& properties,
      const ObjectPathDBusMethodCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kGetServiceFunction);
    dbus::MessageWriter writer(&method_call);
    AppendServicePropertiesDictionary(&writer, properties);
    helper_.CallObjectPathMethod(&method_call, callback);
  }

 private:
  dbus::ObjectProxy* proxy_;
  FlimflamClientHelper helper_;

  DISALLOW_COPY_AND_ASSIGN(FlimflamManagerClientImpl);
};

// A stub implementation of FlimflamManagerClient.
// Implemented: Stub cellular DeviceList entry for SMS testing.
class FlimflamManagerClientStubImpl : public FlimflamManagerClient {
 public:
  FlimflamManagerClientStubImpl() : weak_ptr_factory_(this) {
    base::ListValue* device_list = new base::ListValue;
    // Note: names match Device stub map.
    const char kStubCellular1[] = "stub_cellular1";
    const char kStubCellular2[] = "stub_cellular2";
    device_list->Append(base::Value::CreateStringValue(kStubCellular1));
    device_list->Append(base::Value::CreateStringValue(kStubCellular2));
    stub_properties_.Set(flimflam::kDevicesProperty, device_list);
  }

  virtual ~FlimflamManagerClientStubImpl() {}

  // FlimflamManagerClient override.
  virtual void SetPropertyChangedHandler(
      const PropertyChangedHandler& handler) OVERRIDE {}

  // FlimflamManagerClient override.
  virtual void ResetPropertyChangedHandler() OVERRIDE {}

  // FlimflamManagerClient override.
  virtual void GetProperties(const DictionaryValueCallback& callback) OVERRIDE {
    MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(
            &FlimflamManagerClientStubImpl::PassStubProperties,
            weak_ptr_factory_.GetWeakPtr(),
            callback));
  }

  // FlimflamManagerClient override.
  virtual base::DictionaryValue* CallGetPropertiesAndBlock() OVERRIDE {
    return new base::DictionaryValue;
  }

  // FlimflamManagerClient override.
  virtual void SetProperty(const std::string& name,
                           const base::Value& value,
                           const VoidDBusMethodCallback& callback) OVERRIDE {
    stub_properties_.Set(name, value.DeepCopy());
    MessageLoop::current()->PostTask(FROM_HERE,
                                     base::Bind(callback,
                                                DBUS_METHOD_CALL_SUCCESS));
  }

  // FlimflamManagerClient override.
  virtual void RequestScan(const std::string& type,
                           const VoidDBusMethodCallback& callback) OVERRIDE {
    MessageLoop::current()->PostTask(FROM_HERE,
                                     base::Bind(callback,
                                                DBUS_METHOD_CALL_SUCCESS));
  }

  // FlimflamManagerClient override.
  virtual void EnableTechnology(
      const std::string& type,
      const VoidDBusMethodCallback& callback) OVERRIDE {
    MessageLoop::current()->PostTask(FROM_HERE,
                                     base::Bind(callback,
                                                DBUS_METHOD_CALL_SUCCESS));
  }

  // FlimflamManagerClient override.
  virtual void DisableTechnology(
      const std::string& type,
      const VoidDBusMethodCallback& callback) OVERRIDE {
    MessageLoop::current()->PostTask(FROM_HERE,
                                     base::Bind(callback,
                                                DBUS_METHOD_CALL_SUCCESS));
  }

  // FlimflamManagerClient override.
  virtual void ConfigureService(
      const base::DictionaryValue& properties,
      const VoidDBusMethodCallback& callback) OVERRIDE {
    MessageLoop::current()->PostTask(FROM_HERE,
                                     base::Bind(callback,
                                                DBUS_METHOD_CALL_SUCCESS));
  }

  // FlimflamManagerClient override.
  virtual void GetService(
      const base::DictionaryValue& properties,
      const ObjectPathDBusMethodCallback& callback) OVERRIDE {
    MessageLoop::current()->PostTask(FROM_HERE,
                                     base::Bind(callback,
                                                DBUS_METHOD_CALL_SUCCESS,
                                                dbus::ObjectPath()));
  }

 private:
  void PassStubProperties(const DictionaryValueCallback& callback) const {
    callback.Run(DBUS_METHOD_CALL_SUCCESS, stub_properties_);
  }

  base::WeakPtrFactory<FlimflamManagerClientStubImpl> weak_ptr_factory_;
  base::DictionaryValue stub_properties_;

  DISALLOW_COPY_AND_ASSIGN(FlimflamManagerClientStubImpl);
};

}  // namespace

FlimflamManagerClient::FlimflamManagerClient() {}

FlimflamManagerClient::~FlimflamManagerClient() {}

// static
FlimflamManagerClient* FlimflamManagerClient::Create(
    DBusClientImplementationType type,
    dbus::Bus* bus) {
  if (type == REAL_DBUS_CLIENT_IMPLEMENTATION)
    return new FlimflamManagerClientImpl(bus);
  DCHECK_EQ(STUB_DBUS_CLIENT_IMPLEMENTATION, type);
  return new FlimflamManagerClientStubImpl();
}

}  // namespace chromeos
