// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_FLIMFLAM_IPCONFIG_CLIENT_H_
#define CHROMEOS_DBUS_FLIMFLAM_IPCONFIG_CLIENT_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "chromeos/chromeos_export.h"
#include "chromeos/dbus/dbus_client_implementation_type.h"
#include "chromeos/dbus/flimflam_client_helper.h"

namespace base {

class Value;
class DictionaryValue;

}  // namespace base

namespace dbus {

class Bus;
class ObjectPath;

}  // namespace dbus

namespace chromeos {

// FlimflamIPConfigClient is used to communicate with the Flimflam IPConfig
// service.  All methods should be called from the origin thread which
// initializes the DBusThreadManager instance.
class CHROMEOS_EXPORT FlimflamIPConfigClient {
 public:
  typedef FlimflamClientHelper::PropertyChangedHandler PropertyChangedHandler;
  typedef FlimflamClientHelper::DictionaryValueCallback DictionaryValueCallback;
  virtual ~FlimflamIPConfigClient();

  // Factory function, creates a new instance which is owned by the caller.
  // For normal usage, access the singleton via DBusThreadManager::Get().
  static FlimflamIPConfigClient* Create(DBusClientImplementationType type,
                                        dbus::Bus* bus);

  // Sets PropertyChanged signal handler.
  virtual void SetPropertyChangedHandler(
      const dbus::ObjectPath& ipconfig_path,
      const PropertyChangedHandler& handler) = 0;

  // Resets PropertyChanged signal handler.
  virtual void ResetPropertyChangedHandler(
      const dbus::ObjectPath& ipconfig_path) = 0;

  // Calls GetProperties method.
  // |callback| is called after the method call succeeds.
  virtual void GetProperties(const dbus::ObjectPath& ipconfig_path,
                             const DictionaryValueCallback& callback) = 0;

  // DEPRECATED DO NOT USE: Calls GetProperties method and blocks until the
  // method call finishes.  The caller is responsible to delete the result.
  // Thie method returns NULL when method call fails.
  //
  // TODO(hashimoto): Refactor CrosListIPConfigs to remove this method.
  // crosbug.com/29902
  virtual base::DictionaryValue* CallGetPropertiesAndBlock(
      const dbus::ObjectPath& ipconfig_path) = 0;

  // Calls SetProperty method.
  // |callback| is called after the method call succeeds.
  virtual void SetProperty(const dbus::ObjectPath& ipconfig_path,
                           const std::string& name,
                           const base::Value& value,
                           const VoidDBusMethodCallback& callback) = 0;

  // Calls ClearProperty method.
  // |callback| is called after the method call succeeds.
  virtual void ClearProperty(const dbus::ObjectPath& ipconfig_path,
                             const std::string& name,
                             const VoidDBusMethodCallback& callback) = 0;

  // Calls Remove method.
  // |callback| is called after the method call succeeds.
  virtual void Remove(const dbus::ObjectPath& ipconfig_path,
                      const VoidDBusMethodCallback& callback) = 0;

  // DEPRECATED DO NOT USE: Calls Remove method and blocks until the method call
  // finishes.
  //
  // TODO(hashimoto): Refactor CrosRemoveIPConfig to remove this method.
  // crosbug.com/29902
  virtual bool CallRemoveAndBlock(const dbus::ObjectPath& ipconfig_path) = 0;

 protected:
  // Create() should be used instead.
  FlimflamIPConfigClient();

 private:
  DISALLOW_COPY_AND_ASSIGN(FlimflamIPConfigClient);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_FLIMFLAM_IPCONFIG_CLIENT_H_
