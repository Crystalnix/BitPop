// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_FLIMFLAM_NETWORK_CLIENT_H_
#define CHROMEOS_DBUS_FLIMFLAM_NETWORK_CLIENT_H_

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

// FlimflamNetworkClient is used to communicate with the Flimflam Network
// service.  All methods should be called from the origin thread which
// initializes the DBusThreadManager instance.
class CHROMEOS_EXPORT FlimflamNetworkClient {
 public:
  typedef FlimflamClientHelper::PropertyChangedHandler PropertyChangedHandler;
  typedef FlimflamClientHelper::DictionaryValueCallback DictionaryValueCallback;

  virtual ~FlimflamNetworkClient();

  // Factory function, creates a new instance which is owned by the caller.
  // For normal usage, access the singleton via DBusThreadManager::Get().
  static FlimflamNetworkClient* Create(DBusClientImplementationType type,
                                       dbus::Bus* bus);

  // Sets PropertyChanged signal handler.
  virtual void SetPropertyChangedHandler(
      const dbus::ObjectPath& network_path,
      const PropertyChangedHandler& handler) = 0;

  // Resets PropertyChanged signal handler.
  virtual void ResetPropertyChangedHandler(
      const dbus::ObjectPath& network_path) = 0;

  // Calls GetProperties method.
  // |callback| is called after the method call succeeds.
  virtual void GetProperties(const dbus::ObjectPath& network_path,
                             const DictionaryValueCallback& callback) = 0;

  // DEPRECATED DO NOT USE: Calls GetProperties method and blocks until the
  // method call finishes.  The caller is responsible to delete the result.
  // Thie method returns NULL when method call fails.
  //
  // TODO(hashimoto): Refactor CrosGetWifiAccessPoints and remove this method.
  // crosbug.com/29902
  virtual base::DictionaryValue* CallGetPropertiesAndBlock(
      const dbus::ObjectPath& network_path) = 0;

 protected:
  // Create() should be used instead.
  FlimflamNetworkClient();

 private:
  DISALLOW_COPY_AND_ASSIGN(FlimflamNetworkClient);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_FLIMFLAM_NETWORK_CLIENT_H_
