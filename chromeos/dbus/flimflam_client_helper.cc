// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/flimflam_client_helper.h"

#include "base/bind.h"
#include "base/values.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"
#include "dbus/values_util.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

FlimflamClientHelper::FlimflamClientHelper(dbus::Bus* bus,
                                           dbus::ObjectProxy* proxy)
    : weak_ptr_factory_(this),
      blocking_method_caller_(bus, proxy),
      proxy_(proxy) {
}

FlimflamClientHelper::~FlimflamClientHelper() {
}

void FlimflamClientHelper::SetPropertyChangedHandler(
    const PropertyChangedHandler& handler) {
  property_changed_handler_ = handler;
}

void FlimflamClientHelper::ResetPropertyChangedHandler() {
  property_changed_handler_.Reset();
}

void FlimflamClientHelper::MonitorPropertyChanged(
    const std::string& interface_name) {
  // We are not using dbus::PropertySet to monitor PropertyChanged signal
  // because the interface is not "org.freedesktop.DBus.Properties".
  proxy_->ConnectToSignal(interface_name,
                          flimflam::kMonitorPropertyChanged,
                          base::Bind(&FlimflamClientHelper::OnPropertyChanged,
                                     weak_ptr_factory_.GetWeakPtr()),
                          base::Bind(&FlimflamClientHelper::OnSignalConnected,
                                     weak_ptr_factory_.GetWeakPtr()));
}

void FlimflamClientHelper::CallVoidMethod(
    dbus::MethodCall* method_call,
    const VoidDBusMethodCallback& callback) {
  proxy_->CallMethod(method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                     base::Bind(&FlimflamClientHelper::OnVoidMethod,
                                weak_ptr_factory_.GetWeakPtr(),
                                callback));
}

void FlimflamClientHelper::CallObjectPathMethod(
    dbus::MethodCall* method_call,
    const ObjectPathDBusMethodCallback& callback) {
  proxy_->CallMethod(method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                     base::Bind(&FlimflamClientHelper::OnObjectPathMethod,
                                weak_ptr_factory_.GetWeakPtr(),
                                callback));
}

void FlimflamClientHelper::CallDictionaryValueMethod(
    dbus::MethodCall* method_call,
    const DictionaryValueCallback& callback) {
  proxy_->CallMethod(method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                     base::Bind(&FlimflamClientHelper::OnDictionaryValueMethod,
                                weak_ptr_factory_.GetWeakPtr(),
                                callback));
}

void FlimflamClientHelper::CallVoidMethodWithErrorCallback(
    dbus::MethodCall* method_call,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  proxy_->CallMethodWithErrorCallback(
      method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::Bind(&FlimflamClientHelper::OnVoidMethodWithErrorCallback,
                 weak_ptr_factory_.GetWeakPtr(),
                 callback),
      base::Bind(&FlimflamClientHelper::OnError,
                 weak_ptr_factory_.GetWeakPtr(),
                 error_callback));
}

void FlimflamClientHelper::CallDictionaryValueMethodWithErrorCallback(
    dbus::MethodCall* method_call,
    const DictionaryValueCallbackWithoutStatus& callback,
    const ErrorCallback& error_callback) {
  proxy_->CallMethodWithErrorCallback(
      method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::Bind(
          &FlimflamClientHelper::OnDictionaryValueMethodWithErrorCallback,
          weak_ptr_factory_.GetWeakPtr(),
          callback,
          error_callback),
      base::Bind(&FlimflamClientHelper::OnError,
                 weak_ptr_factory_.GetWeakPtr(),
                 error_callback));
}

bool FlimflamClientHelper::CallVoidMethodAndBlock(
    dbus::MethodCall* method_call) {
  scoped_ptr<dbus::Response> response(
      blocking_method_caller_.CallMethodAndBlock(method_call));
  if (!response.get())
    return false;
  return true;
}

dbus::ObjectPath FlimflamClientHelper::CallObjectPathMethodAndBlock(
    dbus::MethodCall* method_call) {
  scoped_ptr<dbus::Response> response(
      blocking_method_caller_.CallMethodAndBlock(method_call));
  if (!response.get())
    return dbus::ObjectPath();

  dbus::MessageReader reader(response.get());
  dbus::ObjectPath result;
  if (!reader.PopObjectPath(&result))
    return dbus::ObjectPath();

  return result;
}

base::DictionaryValue* FlimflamClientHelper::CallDictionaryValueMethodAndBlock(
    dbus::MethodCall* method_call) {
  scoped_ptr<dbus::Response> response(
      blocking_method_caller_.CallMethodAndBlock(method_call));
  if (!response.get())
    return NULL;

  dbus::MessageReader reader(response.get());
  base::Value* value = dbus::PopDataAsValue(&reader);
  base::DictionaryValue* result = NULL;
  if (!value || !value->GetAsDictionary(&result)) {
    delete value;
    return NULL;
  }
  return result;
}

// static
void FlimflamClientHelper::AppendValueDataAsVariant(dbus::MessageWriter* writer,
                                                    const base::Value& value) {
  // Support basic types and string-to-string dictionary.
  switch (value.GetType()) {
    case base::Value::TYPE_DICTIONARY: {
      const base::DictionaryValue* dictionary = NULL;
      value.GetAsDictionary(&dictionary);
      dbus::MessageWriter variant_writer(NULL);
      writer->OpenVariant("a{ss}", &variant_writer);
      dbus::MessageWriter array_writer(NULL);
      variant_writer.OpenArray("{ss}", &array_writer);
      for (base::DictionaryValue::Iterator it(*dictionary);
           it.HasNext();
           it.Advance()) {
        dbus::MessageWriter entry_writer(NULL);
        array_writer.OpenDictEntry(&entry_writer);
        entry_writer.AppendString(it.key());
        const base::Value& value = it.value();
        std::string value_string;
        DLOG_IF(ERROR, value.GetType() != base::Value::TYPE_STRING)
            << "Unexpected type " << value.GetType();
        value.GetAsString(&value_string);
        entry_writer.AppendString(value_string);
        array_writer.CloseContainer(&entry_writer);
      }
      variant_writer.CloseContainer(&array_writer);
      writer->CloseContainer(&variant_writer);
      break;
    }
    case base::Value::TYPE_BOOLEAN:
    case base::Value::TYPE_INTEGER:
    case base::Value::TYPE_DOUBLE:
    case base::Value::TYPE_STRING:
      dbus::AppendBasicTypeValueDataAsVariant(writer, value);
      break;
    default:
      DLOG(ERROR) << "Unexpected type " << value.GetType();
  }

}

void FlimflamClientHelper::OnSignalConnected(const std::string& interface,
                                             const std::string& signal,
                                             bool success) {
  LOG_IF(ERROR, !success) << "Connect to " << interface << " " << signal
                          << " failed.";
}

void FlimflamClientHelper::OnPropertyChanged(dbus::Signal* signal) {
  if (property_changed_handler_.is_null())
    return;

  dbus::MessageReader reader(signal);
  std::string name;
  if (!reader.PopString(&name))
    return;
  scoped_ptr<base::Value> value(dbus::PopDataAsValue(&reader));
  if (!value.get())
    return;
  property_changed_handler_.Run(name, *value);
}

void FlimflamClientHelper::OnVoidMethod(const VoidDBusMethodCallback& callback,
                                        dbus::Response* response) {
  if (!response) {
    callback.Run(DBUS_METHOD_CALL_FAILURE);
    return;
  }
  callback.Run(DBUS_METHOD_CALL_SUCCESS);
}

void FlimflamClientHelper::OnObjectPathMethod(
    const ObjectPathDBusMethodCallback& callback,
    dbus::Response* response) {
  if (!response) {
    callback.Run(DBUS_METHOD_CALL_FAILURE, dbus::ObjectPath());
    return;
  }
  dbus::MessageReader reader(response);
  dbus::ObjectPath result;
  if (!reader.PopObjectPath(&result)) {
    callback.Run(DBUS_METHOD_CALL_FAILURE, dbus::ObjectPath());
    return;
  }
  callback.Run(DBUS_METHOD_CALL_SUCCESS, result);
}

void FlimflamClientHelper::OnDictionaryValueMethod(
    const DictionaryValueCallback& callback,
    dbus::Response* response) {
  if (!response) {
    base::DictionaryValue result;
    callback.Run(DBUS_METHOD_CALL_FAILURE, result);
    return;
  }
  dbus::MessageReader reader(response);
  scoped_ptr<base::Value> value(dbus::PopDataAsValue(&reader));
  base::DictionaryValue* result = NULL;
  if (!value.get() || !value->GetAsDictionary(&result)) {
    base::DictionaryValue result;
    callback.Run(DBUS_METHOD_CALL_FAILURE, result);
    return;
  }
  callback.Run(DBUS_METHOD_CALL_SUCCESS, *result);
}

void FlimflamClientHelper::OnVoidMethodWithErrorCallback(
    const base::Closure& callback,
    dbus::Response* response) {
  callback.Run();
}

void FlimflamClientHelper::OnDictionaryValueMethodWithErrorCallback(
    const DictionaryValueCallbackWithoutStatus& callback,
    const ErrorCallback& error_callback,
    dbus::Response* response) {
  dbus::MessageReader reader(response);
  scoped_ptr<base::Value> value(dbus::PopDataAsValue(&reader));
  base::DictionaryValue* result = NULL;
  if (!value.get() || !value->GetAsDictionary(&result)) {
    const std::string error_name;  // No error name.
    const std::string error_message = "Invalid response.";
    error_callback.Run(error_name, error_message);
    return;
  }
  callback.Run(*result);
}

void FlimflamClientHelper::OnError(const ErrorCallback& error_callback,
                                   dbus::ErrorResponse* response) {
  std::string error_name;
  std::string error_message;
  if (response) {
    // Error message may contain the error message as string.
    dbus::MessageReader reader(response);
    error_name = response->GetErrorName();
    reader.PopString(&error_message);
  }
  error_callback.Run(error_name, error_message);
}

}  // namespace chromeos
