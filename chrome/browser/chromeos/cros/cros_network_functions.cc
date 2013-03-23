// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/cros/cros_network_functions.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_tokenizer.h"
#include "base/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/chromeos/cros/sms_watcher.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/shill_device_client.h"
#include "chromeos/dbus/shill_ipconfig_client.h"
#include "chromeos/dbus/shill_manager_client.h"
#include "chromeos/dbus/shill_network_client.h"
#include "chromeos/dbus/shill_profile_client.h"
#include "chromeos/dbus/shill_property_changed_observer.h"
#include "chromeos/dbus/shill_service_client.h"
#include "dbus/object_path.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

using base::DoNothing;

namespace chromeos {

namespace {

// Class to watch network manager's properties without Libcros.
class NetworkManagerPropertiesWatcher
    : public CrosNetworkWatcher,
      public ShillPropertyChangedObserver {
 public:
  NetworkManagerPropertiesWatcher(
      const NetworkPropertiesWatcherCallback& callback)
    : callback_(callback) {
    DBusThreadManager::Get()->GetShillManagerClient()->
        AddPropertyChangedObserver(this);
  }

  virtual ~NetworkManagerPropertiesWatcher() {
    DBusThreadManager::Get()->GetShillManagerClient()->
        RemovePropertyChangedObserver(this);
  }

  virtual void OnPropertyChanged(const std::string& name,
                                 const base::Value& value) {
    callback_.Run(flimflam::kFlimflamServicePath, name, value);
  }
 private:
  NetworkPropertiesWatcherCallback callback_;
};

// Class to watch network service's properties without Libcros.
class NetworkServicePropertiesWatcher
    : public CrosNetworkWatcher,
      public ShillPropertyChangedObserver {
 public:
  NetworkServicePropertiesWatcher(
      const NetworkPropertiesWatcherCallback& callback,
      const std::string& service_path) : service_path_(service_path),
                                         callback_(callback) {
    DBusThreadManager::Get()->GetShillServiceClient()->
        AddPropertyChangedObserver(dbus::ObjectPath(service_path), this);
  }

  virtual ~NetworkServicePropertiesWatcher() {
    DBusThreadManager::Get()->GetShillServiceClient()->
        RemovePropertyChangedObserver(dbus::ObjectPath(service_path_), this);
  }

  virtual void OnPropertyChanged(const std::string& name,
                                 const base::Value& value) {
    callback_.Run(service_path_, name, value);
  }

 private:
  std::string service_path_;
  NetworkPropertiesWatcherCallback callback_;
};

// Class to watch network device's properties without Libcros.
class NetworkDevicePropertiesWatcher
    : public CrosNetworkWatcher,
      public ShillPropertyChangedObserver {
 public:
  NetworkDevicePropertiesWatcher(
      const NetworkPropertiesWatcherCallback& callback,
      const std::string& device_path) : device_path_(device_path),
                                        callback_(callback) {
    DBusThreadManager::Get()->GetShillDeviceClient()->
        AddPropertyChangedObserver(dbus::ObjectPath(device_path), this);
  }

  virtual ~NetworkDevicePropertiesWatcher() {
    DBusThreadManager::Get()->GetShillDeviceClient()->
        RemovePropertyChangedObserver(dbus::ObjectPath(device_path_), this);
  }

  virtual void OnPropertyChanged(const std::string& name,
                                 const base::Value& value) {
    callback_.Run(device_path_, name, value);
  }

 private:
  std::string device_path_;
  NetworkPropertiesWatcherCallback callback_;
};

// Gets a string property from dictionary.
bool GetStringProperty(const base::DictionaryValue& dictionary,
                       const std::string& key,
                       std::string* out) {
  const bool result = dictionary.GetStringWithoutPathExpansion(key, out);
  LOG_IF(WARNING, !result) << "Cannnot get property " << key;
  return result;
}

// Gets an int64 property from dictionary.
bool GetInt64Property(const base::DictionaryValue& dictionary,
                      const std::string& key,
                      int64* out) {
  // Int64 value is stored as a double because it cannot be fitted in int32.
  double value_double = 0;
  const bool result = dictionary.GetDoubleWithoutPathExpansion(key,
                                                               &value_double);
  if (result)
    *out = value_double;
  else
    LOG(WARNING) << "Cannnot get property " << key;
  return result;
}

// Gets a base::Time property from dictionary.
bool GetTimeProperty(const base::DictionaryValue& dictionary,
                     const std::string& key,
                     base::Time* out) {
  int64 value_int64 = 0;
  if (!GetInt64Property(dictionary, key, &value_int64))
    return false;
  *out = base::Time::FromInternalValue(value_int64);
  return true;
}

// Does nothing. Used as a callback.
void DoNothingWithCallStatus(DBusMethodCallStatus call_status) {}

// Ignores errors.
void IgnoreErrors(const std::string& error_name,
                  const std::string& error_message) {}

// A callback used to implement CrosRequest*Properties functions.
void RunCallbackWithDictionaryValue(const NetworkPropertiesCallback& callback,
                                    const std::string& path,
                                    DBusMethodCallStatus call_status,
                                    const base::DictionaryValue& value) {
  callback.Run(path, call_status == DBUS_METHOD_CALL_SUCCESS ? &value : NULL);
}

// A callback used to implement CrosRequest*Properties functions.
void RunCallbackWithDictionaryValueNoStatus(
    const NetworkPropertiesCallback& callback,
    const std::string& path,
    const base::DictionaryValue& value) {
  callback.Run(path, &value);
}

// A callback used to implement the error callback for CrosRequest*Properties
// functions.
void RunCallbackWithDictionaryValueError(
    const NetworkPropertiesCallback& callback,
    const std::string& path,
    const std::string& error_name,
    const std::string& error_message) {
  callback.Run(path, NULL);
}

// Used as a callback for ShillManagerClient::GetService
void OnGetService(const NetworkPropertiesCallback& callback,
                  const dbus::ObjectPath& service_path) {
  VLOG(1) << "OnGetServiceService: " << service_path.value();
  DBusThreadManager::Get()->GetShillServiceClient()->GetProperties(
      service_path, base::Bind(&RunCallbackWithDictionaryValue,
                               callback,
                               service_path.value()));
}

// A callback used to call a NetworkOperationCallback on error.
void OnNetworkActionError(const NetworkOperationCallback& callback,
                          const std::string& path,
                          const std::string& error_name,
                          const std::string& error_message) {
  if (error_name.empty())
    callback.Run(path, NETWORK_METHOD_ERROR_LOCAL, "");
  else
    callback.Run(path, NETWORK_METHOD_ERROR_REMOTE, error_message);
}

IPConfigType ParseIPConfigType(const std::string& type) {
  if (type == flimflam::kTypeIPv4)
    return IPCONFIG_TYPE_IPV4;
  if (type == flimflam::kTypeIPv6)
    return IPCONFIG_TYPE_IPV6;
  if (type == flimflam::kTypeDHCP)
    return IPCONFIG_TYPE_DHCP;
  if (type == flimflam::kTypeBOOTP)
    return IPCONFIG_TYPE_BOOTP;
  if (type == flimflam::kTypeZeroConf)
    return IPCONFIG_TYPE_ZEROCONF;
  if (type == flimflam::kTypeDHCP6)
    return IPCONFIG_TYPE_DHCP6;
  if (type == flimflam::kTypePPP)
    return IPCONFIG_TYPE_PPP;
  return IPCONFIG_TYPE_UNKNOWN;
}

// Converts a list of name servers to a string.
std::string ConvertNameSerersListToString(const base::ListValue& name_servers) {
  std::string result;
  for (size_t i = 0; i != name_servers.GetSize(); ++i) {
    std::string name_server;
    if (!name_servers.GetString(i, &name_server)) {
      LOG(ERROR) << "name_servers[" << i << "] is not a string.";
      continue;
    }
    if (!result.empty())
      result += ",";
    result += name_server;
  }
  return result;
}

// Gets NetworkIPConfigVector populated with data from a
// given DBus object path.
//
// returns true on success.
bool ParseIPConfig(const std::string& device_path,
                   const std::string& ipconfig_path,
                   NetworkIPConfigVector* ipconfig_vector) {
  ShillIPConfigClient* ipconfig_client =
      DBusThreadManager::Get()->GetShillIPConfigClient();
  // TODO(hashimoto): Remove this blocking D-Bus method call. crosbug.com/29902
  scoped_ptr<base::DictionaryValue> properties(
      ipconfig_client->CallGetPropertiesAndBlock(
          dbus::ObjectPath(ipconfig_path)));
  if (!properties.get())
    return false;

  std::string type_string;
  properties->GetStringWithoutPathExpansion(flimflam::kMethodProperty,
                                            &type_string);
  std::string address;
  properties->GetStringWithoutPathExpansion(flimflam::kAddressProperty,
                                            &address);
  int32 prefix_len = 0;
  properties->GetIntegerWithoutPathExpansion(flimflam::kPrefixlenProperty,
                                             &prefix_len);
  std::string gateway;
  properties->GetStringWithoutPathExpansion(flimflam::kGatewayProperty,
                                            &gateway);
  base::ListValue* name_servers = NULL;
  std::string name_servers_string;
  // store nameservers as a comma delimited list
  if (properties->GetListWithoutPathExpansion(flimflam::kNameServersProperty,
                                              &name_servers)) {
    name_servers_string = ConvertNameSerersListToString(*name_servers);
  } else {
    LOG(ERROR) << "Cannot get name servers.";
  }
  ipconfig_vector->push_back(
      NetworkIPConfig(device_path,
                      ParseIPConfigType(type_string),
                      address,
                      CrosPrefixLengthToNetmask(prefix_len),
                      gateway,
                      name_servers_string));
  return true;
}

void ListIPConfigsCallback(const NetworkGetIPConfigsCallback& callback,
                           const std::string& device_path,
                           DBusMethodCallStatus call_status,
                           const base::DictionaryValue& properties) {
  NetworkIPConfigVector ipconfig_vector;
  std::string hardware_address;
  const ListValue* ips = NULL;
  if (call_status != DBUS_METHOD_CALL_SUCCESS ||
      !properties.GetListWithoutPathExpansion(flimflam::kIPConfigsProperty,
                                              &ips)) {
    callback.Run(ipconfig_vector, hardware_address);
    return;
  }

  for (size_t i = 0; i < ips->GetSize(); i++) {
    std::string ipconfig_path;
    if (!ips->GetString(i, &ipconfig_path)) {
      LOG(WARNING) << "Found NULL ip for device " << device_path;
      continue;
    }
    ParseIPConfig(device_path, ipconfig_path, &ipconfig_vector);
  }
  // Get the hardware address as well.
  properties.GetStringWithoutPathExpansion(flimflam::kAddressProperty,
                                           &hardware_address);

  callback.Run(ipconfig_vector, hardware_address);
}

}  // namespace

SMS::SMS()
    : validity(0),
      msgclass(0) {
}

SMS::~SMS() {}

bool CrosActivateCellularModem(const std::string& service_path,
                               const std::string& carrier) {
  return DBusThreadManager::Get()->GetShillServiceClient()->
      CallActivateCellularModemAndBlock(dbus::ObjectPath(service_path),
                                        carrier);
}

void CrosSetNetworkServiceProperty(const std::string& service_path,
                                   const std::string& property,
                                   const base::Value& value) {
  DBusThreadManager::Get()->GetShillServiceClient()->SetProperty(
      dbus::ObjectPath(service_path), property, value,
      base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

void CrosClearNetworkServiceProperty(const std::string& service_path,
                                     const std::string& property) {
  DBusThreadManager::Get()->GetShillServiceClient()->ClearProperty(
      dbus::ObjectPath(service_path), property, base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

void CrosSetNetworkDeviceProperty(const std::string& device_path,
                                  const std::string& property,
                                  const base::Value& value) {
  DBusThreadManager::Get()->GetShillDeviceClient()->SetProperty(
      dbus::ObjectPath(device_path), property, value, base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

void CrosSetNetworkIPConfigProperty(const std::string& ipconfig_path,
                                    const std::string& property,
                                    const base::Value& value) {
  DBusThreadManager::Get()->GetShillIPConfigClient()->SetProperty(
      dbus::ObjectPath(ipconfig_path), property, value,
      base::Bind(&DoNothingWithCallStatus));
}

void CrosSetNetworkManagerProperty(const std::string& property,
                                   const base::Value& value) {
  DBusThreadManager::Get()->GetShillManagerClient()->SetProperty(
      property, value, base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

void CrosDeleteServiceFromProfile(const std::string& profile_path,
                                  const std::string& service_path) {
  DBusThreadManager::Get()->GetShillProfileClient()->DeleteEntry(
      dbus::ObjectPath(profile_path),
      service_path,
      base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

CrosNetworkWatcher* CrosMonitorNetworkManagerProperties(
    const NetworkPropertiesWatcherCallback& callback) {
  return new NetworkManagerPropertiesWatcher(callback);
}

CrosNetworkWatcher* CrosMonitorNetworkServiceProperties(
    const NetworkPropertiesWatcherCallback& callback,
    const std::string& service_path) {
  return new NetworkServicePropertiesWatcher(callback, service_path);
}

CrosNetworkWatcher* CrosMonitorNetworkDeviceProperties(
    const NetworkPropertiesWatcherCallback& callback,
    const std::string& device_path) {
  return new NetworkDevicePropertiesWatcher(callback, device_path);
}

CrosNetworkWatcher* CrosMonitorSMS(const std::string& modem_device_path,
                                   MonitorSMSCallback callback) {
  return new SMSWatcher(modem_device_path, callback);
}

void CrosRequestNetworkServiceConnect(
    const std::string& service_path,
    const NetworkOperationCallback& callback) {
  DBusThreadManager::Get()->GetShillServiceClient()->Connect(
      dbus::ObjectPath(service_path),
      base::Bind(callback, service_path, NETWORK_METHOD_ERROR_NONE,
                 std::string()),
      base::Bind(&OnNetworkActionError, callback, service_path));
}

void CrosRequestNetworkManagerProperties(
    const NetworkPropertiesCallback& callback) {
  DBusThreadManager::Get()->GetShillManagerClient()->GetProperties(
      base::Bind(&RunCallbackWithDictionaryValue,
                 callback,
                 flimflam::kFlimflamServicePath));
}

void CrosRequestNetworkServiceProperties(
    const std::string& service_path,
    const NetworkPropertiesCallback& callback) {
  DBusThreadManager::Get()->GetShillServiceClient()->GetProperties(
      dbus::ObjectPath(service_path),
      base::Bind(&RunCallbackWithDictionaryValue, callback, service_path));
}

void CrosRequestNetworkDeviceProperties(
    const std::string& device_path,
    const NetworkPropertiesCallback& callback) {
  DBusThreadManager::Get()->GetShillDeviceClient()->GetProperties(
      dbus::ObjectPath(device_path),
      base::Bind(&RunCallbackWithDictionaryValue, callback, device_path));
}

void CrosRequestNetworkProfileProperties(
    const std::string& profile_path,
    const NetworkPropertiesCallback& callback) {
  DBusThreadManager::Get()->GetShillProfileClient()->GetProperties(
      dbus::ObjectPath(profile_path),
      base::Bind(&RunCallbackWithDictionaryValueNoStatus,
                 callback, profile_path),
      base::Bind(&RunCallbackWithDictionaryValueError, callback, profile_path));
}

void CrosRequestNetworkProfileEntryProperties(
    const std::string& profile_path,
    const std::string& profile_entry_path,
    const NetworkPropertiesCallback& callback) {
  DBusThreadManager::Get()->GetShillProfileClient()->GetEntry(
      dbus::ObjectPath(profile_path),
      profile_entry_path,
      base::Bind(&RunCallbackWithDictionaryValueNoStatus,
                 callback,
                 profile_entry_path),
      base::Bind(&RunCallbackWithDictionaryValueError,
                 callback,
                 profile_entry_path));
}

void CrosRequestHiddenWifiNetworkProperties(
    const std::string& ssid,
    const std::string& security,
    const NetworkPropertiesCallback& callback) {
  base::DictionaryValue properties;
  properties.SetWithoutPathExpansion(
      flimflam::kModeProperty,
      new base::StringValue(flimflam::kModeManaged));
  properties.SetWithoutPathExpansion(
      flimflam::kTypeProperty,
      new base::StringValue(flimflam::kTypeWifi));
  properties.SetWithoutPathExpansion(
      flimflam::kSSIDProperty,
      new base::StringValue(ssid));
  properties.SetWithoutPathExpansion(
      flimflam::kSecurityProperty,
      new base::StringValue(security));
  // shill.Manger.GetService() will apply the property changes in
  // |properties| and return a new or existing service to OnGetService().
  // OnGetService will then call GetProperties which will then call callback.
  DBusThreadManager::Get()->GetShillManagerClient()->GetService(
      properties, base::Bind(&OnGetService, callback),
      base::Bind(&IgnoreErrors));
}

void CrosRequestVirtualNetworkProperties(
    const std::string& service_name,
    const std::string& server_hostname,
    const std::string& provider_type,
    const NetworkPropertiesCallback& callback) {
  base::DictionaryValue properties;
  properties.SetWithoutPathExpansion(
      flimflam::kTypeProperty,
      new base::StringValue(flimflam::kTypeVPN));
  properties.SetWithoutPathExpansion(
      flimflam::kProviderNameProperty,
      new base::StringValue(service_name));
  properties.SetWithoutPathExpansion(
      flimflam::kProviderHostProperty,
      new base::StringValue(server_hostname));
  properties.SetWithoutPathExpansion(
      flimflam::kProviderTypeProperty,
      new base::StringValue(provider_type));
  // The actual value of Domain does not matter, so just use service_name.
  properties.SetWithoutPathExpansion(
      flimflam::kVPNDomainProperty,
      new base::StringValue(service_name));

  // shill.Manger.GetService() will apply the property changes in
  // |properties| and pass a new or existing service to OnGetService().
  // OnGetService will then call GetProperties which will then call callback.
  DBusThreadManager::Get()->GetShillManagerClient()->GetService(
      properties, base::Bind(&OnGetService, callback),
      base::Bind(&IgnoreErrors));
}

void CrosRequestNetworkServiceDisconnect(const std::string& service_path) {
  DBusThreadManager::Get()->GetShillServiceClient()->Disconnect(
      dbus::ObjectPath(service_path), base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

void CrosRequestRemoveNetworkService(const std::string& service_path) {
  DBusThreadManager::Get()->GetShillServiceClient()->Remove(
      dbus::ObjectPath(service_path), base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

void CrosRequestNetworkScan(const std::string& network_type) {
  DBusThreadManager::Get()->GetShillManagerClient()->RequestScan(
      network_type, base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

void CrosRequestNetworkDeviceEnable(const std::string& network_type,
                                    bool enable) {
  if (enable) {
    DBusThreadManager::Get()->GetShillManagerClient()->EnableTechnology(
        network_type, base::Bind(&DoNothing),
        base::Bind(&IgnoreErrors));
  } else {
    DBusThreadManager::Get()->GetShillManagerClient()->DisableTechnology(
        network_type, base::Bind(&DoNothing),
        base::Bind(&IgnoreErrors));
  }
}

void CrosRequestRequirePin(const std::string& device_path,
                           const std::string& pin,
                           bool enable,
                           const NetworkOperationCallback& callback) {
  DBusThreadManager::Get()->GetShillDeviceClient()->RequirePin(
      dbus::ObjectPath(device_path), pin, enable,
      base::Bind(callback, device_path, NETWORK_METHOD_ERROR_NONE,
                 std::string()),
      base::Bind(&OnNetworkActionError, callback, device_path));
}

void CrosRequestEnterPin(const std::string& device_path,
                         const std::string& pin,
                         const NetworkOperationCallback& callback) {
  DBusThreadManager::Get()->GetShillDeviceClient()->EnterPin(
      dbus::ObjectPath(device_path), pin,
      base::Bind(callback, device_path, NETWORK_METHOD_ERROR_NONE,
                 std::string()),
      base::Bind(&OnNetworkActionError, callback, device_path));
}

void CrosRequestUnblockPin(const std::string& device_path,
                           const std::string& unblock_code,
                           const std::string& pin,
                           const NetworkOperationCallback& callback) {
  DBusThreadManager::Get()->GetShillDeviceClient()->UnblockPin(
      dbus::ObjectPath(device_path), unblock_code, pin,
      base::Bind(callback, device_path, NETWORK_METHOD_ERROR_NONE,
                 std::string()),
      base::Bind(&OnNetworkActionError, callback, device_path));
}

void CrosRequestChangePin(const std::string& device_path,
                          const std::string& old_pin,
                          const std::string& new_pin,
                          const NetworkOperationCallback& callback) {
  DBusThreadManager::Get()->GetShillDeviceClient()->ChangePin(
      dbus::ObjectPath(device_path), old_pin, new_pin,
      base::Bind(callback, device_path, NETWORK_METHOD_ERROR_NONE,
                 std::string()),
      base::Bind(&OnNetworkActionError, callback, device_path));
}

void CrosProposeScan(const std::string& device_path) {
  DBusThreadManager::Get()->GetShillDeviceClient()->ProposeScan(
      dbus::ObjectPath(device_path), base::Bind(&DoNothingWithCallStatus));
}

void CrosRequestCellularRegister(const std::string& device_path,
                                 const std::string& network_id,
                                 const NetworkOperationCallback& callback) {
  DBusThreadManager::Get()->GetShillDeviceClient()->Register(
      dbus::ObjectPath(device_path), network_id,
      base::Bind(callback, device_path, NETWORK_METHOD_ERROR_NONE,
                 std::string()),
      base::Bind(&OnNetworkActionError, callback, device_path));
}

bool CrosSetOfflineMode(bool offline) {
  base::FundamentalValue value(offline);
  DBusThreadManager::Get()->GetShillManagerClient()->SetProperty(
      flimflam::kOfflineModeProperty, value, base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
  return true;
}

void CrosListIPConfigs(const std::string& device_path,
                       const NetworkGetIPConfigsCallback& callback) {
  const dbus::ObjectPath device_object_path(device_path);
  DBusThreadManager::Get()->GetShillDeviceClient()->GetProperties(
      device_object_path,
      base::Bind(&ListIPConfigsCallback, callback, device_path));
}

bool CrosListIPConfigsAndBlock(const std::string& device_path,
                               NetworkIPConfigVector* ipconfig_vector,
                               std::vector<std::string>* ipconfig_paths,
                               std::string* hardware_address) {
  if (hardware_address)
    hardware_address->clear();
  const dbus::ObjectPath device_object_path(device_path);
  ShillDeviceClient* device_client =
      DBusThreadManager::Get()->GetShillDeviceClient();
  // TODO(hashimoto): Remove this blocking D-Bus method call.
  // crosbug.com/29902
  scoped_ptr<base::DictionaryValue> properties(
      device_client->CallGetPropertiesAndBlock(device_object_path));
  if (!properties.get())
    return false;

  ListValue* ips = NULL;
  if (!properties->GetListWithoutPathExpansion(
          flimflam::kIPConfigsProperty, &ips))
    return false;

  for (size_t i = 0; i < ips->GetSize(); i++) {
    std::string ipconfig_path;
    if (!ips->GetString(i, &ipconfig_path)) {
      LOG(WARNING) << "Found NULL ip for device " << device_path;
      continue;
    }
    ParseIPConfig(device_path, ipconfig_path, ipconfig_vector);
    if (ipconfig_paths)
      ipconfig_paths->push_back(ipconfig_path);
  }
  // Store the hardware address as well.
  if (hardware_address)
    properties->GetStringWithoutPathExpansion(flimflam::kAddressProperty,
                                              hardware_address);
  return true;
}

void CrosRequestIPConfigRefresh(const std::string& ipconfig_path) {
  DBusThreadManager::Get()->GetShillIPConfigClient()->Refresh(
      dbus::ObjectPath(ipconfig_path),
      base::Bind(&DoNothingWithCallStatus));
}

bool CrosGetWifiAccessPoints(WifiAccessPointVector* result) {
  scoped_ptr<base::DictionaryValue> manager_properties(
      DBusThreadManager::Get()->GetShillManagerClient()->
      CallGetPropertiesAndBlock());
  if (!manager_properties.get()) {
    LOG(WARNING) << "Couldn't read managers's properties";
    return false;
  }

  base::ListValue* devices = NULL;
  if (!manager_properties->GetListWithoutPathExpansion(
          flimflam::kDevicesProperty, &devices)) {
    LOG(WARNING) << flimflam::kDevicesProperty << " property not found";
    return false;
  }
  const base::Time now = base::Time::Now();
  bool found_at_least_one_device = false;
  result->clear();
  for (size_t i = 0; i < devices->GetSize(); i++) {
    std::string device_path;
    if (!devices->GetString(i, &device_path)) {
      LOG(WARNING) << "Couldn't get devices[" << i << "]";
      continue;
    }
    scoped_ptr<base::DictionaryValue> device_properties(
        DBusThreadManager::Get()->GetShillDeviceClient()->
        CallGetPropertiesAndBlock(dbus::ObjectPath(device_path)));
    if (!device_properties.get()) {
      LOG(WARNING) << "Couldn't read device's properties " << device_path;
      continue;
    }

    base::ListValue* networks = NULL;
    if (!device_properties->GetListWithoutPathExpansion(
            flimflam::kNetworksProperty, &networks))
      continue;  // Some devices do not list networks, e.g. ethernet.

    base::Value* device_powered_value = NULL;
    bool device_powered = false;
    if (device_properties->GetWithoutPathExpansion(
            flimflam::kPoweredProperty, &device_powered_value) &&
        device_powered_value->GetAsBoolean(&device_powered) &&
        !device_powered)
      continue;  // Skip devices that are not powered up.

    int scan_interval = 0;
    device_properties->GetIntegerWithoutPathExpansion(
        flimflam::kScanIntervalProperty, &scan_interval);

    found_at_least_one_device = true;
    for (size_t j = 0; j < networks->GetSize(); j++) {
      std::string network_path;
      if (!networks->GetString(j, &network_path)) {
        LOG(WARNING) << "Couldn't get networks[" << j << "]";
        continue;
      }

      scoped_ptr<base::DictionaryValue> network_properties(
          DBusThreadManager::Get()->GetShillNetworkClient()->
          CallGetPropertiesAndBlock(dbus::ObjectPath(network_path)));
      if (!network_properties.get()) {
        LOG(WARNING) << "Couldn't read network's properties " << network_path;
        continue;
      }

      // Using the scan interval as a proxy for approximate age.
      // TODO(joth): Replace with actual age, when available from dbus.
      const int age_seconds = scan_interval;
      WifiAccessPoint ap;
      network_properties->GetStringWithoutPathExpansion(
          flimflam::kAddressProperty, &ap.mac_address);
      network_properties->GetStringWithoutPathExpansion(
          flimflam::kNameProperty, &ap.name);
      ap.timestamp = now - base::TimeDelta::FromSeconds(age_seconds);
      network_properties->GetIntegerWithoutPathExpansion(
          flimflam::kSignalStrengthProperty, &ap.signal_strength);
      network_properties->GetIntegerWithoutPathExpansion(
          flimflam::kWifiChannelProperty, &ap.channel);
      result->push_back(ap);
    }
  }
  if (!found_at_least_one_device)
    return false;  // No powered device found that has a 'Networks' array.
  return true;
}

void CrosConfigureService(const base::DictionaryValue& properties) {
  DBusThreadManager::Get()->GetShillManagerClient()->ConfigureService(
      properties, base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

std::string CrosPrefixLengthToNetmask(int32 prefix_length) {
  std::string netmask;
  // Return the empty string for invalid inputs.
  if (prefix_length < 0 || prefix_length > 32)
    return netmask;
  for (int i = 0; i < 4; i++) {
    int remainder = 8;
    if (prefix_length >= 8) {
      prefix_length -= 8;
    } else {
      remainder = prefix_length;
      prefix_length = 0;
    }
    if (i > 0)
      netmask += ".";
    int value = remainder == 0 ? 0 :
        ((2L << (remainder - 1)) - 1) << (8 - remainder);
    netmask += StringPrintf("%d", value);
  }
  return netmask;
}

int32 CrosNetmaskToPrefixLength(const std::string& netmask) {
  int count = 0;
  int prefix_length = 0;
  StringTokenizer t(netmask, ".");
  while (t.GetNext()) {
    // If there are more than 4 numbers, then it's invalid.
    if (count == 4)
      return -1;

    std::string token = t.token();
    // If we already found the last mask and the current one is not
    // "0" then the netmask is invalid. For example, 255.224.255.0
    if (prefix_length / 8 != count) {
      if (token != "0")
        return -1;
    } else if (token == "255") {
      prefix_length += 8;
    } else if (token == "254") {
      prefix_length += 7;
    } else if (token == "252") {
      prefix_length += 6;
    } else if (token == "248") {
      prefix_length += 5;
    } else if (token == "240") {
      prefix_length += 4;
    } else if (token == "224") {
      prefix_length += 3;
    } else if (token == "192") {
      prefix_length += 2;
    } else if (token == "128") {
      prefix_length += 1;
    } else if (token == "0") {
      prefix_length += 0;
    } else {
      // mask is not a valid number.
      return -1;
    }
    count++;
  }
  if (count < 4)
    return -1;
  return prefix_length;
}

// Changes the active cellular carrier.
void CrosSetCarrier(const std::string& device_path,
                    const std::string& carrier,
                    const NetworkOperationCallback& callback) {
  DBusThreadManager::Get()->GetShillDeviceClient()->SetCarrier(
      dbus::ObjectPath(device_path), carrier,
      base::Bind(callback, device_path, NETWORK_METHOD_ERROR_NONE,
                 std::string()),
      base::Bind(&OnNetworkActionError, callback, device_path));
}

// Resets the device.
void CrosReset(const std::string& device_path) {
  DBusThreadManager::Get()->GetShillDeviceClient()->Reset(
      dbus::ObjectPath(device_path),
      base::Bind(&DoNothing),
      base::Bind(&IgnoreErrors));
}

}  // namespace chromeos
