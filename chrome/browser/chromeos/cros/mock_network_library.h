// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CROS_MOCK_NETWORK_LIBRARY_H_
#define CHROME_BROWSER_CHROMEOS_CROS_MOCK_NETWORK_LIBRARY_H_
#pragma once

#include <string>

#include "chrome/browser/chromeos/cros/network_library.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class MockNetworkLibrary : public NetworkLibrary {
 public:
  MockNetworkLibrary();
  virtual ~MockNetworkLibrary();

  MOCK_METHOD1(AddNetworkManagerObserver, void(NetworkManagerObserver*));
  MOCK_METHOD1(RemoveNetworkManagerObserver, void(NetworkManagerObserver*));
  MOCK_METHOD2(AddNetworkObserver, void(const std::string&, NetworkObserver*));
  MOCK_METHOD2(RemoveNetworkObserver, void(const std::string&,
                                           NetworkObserver*));
  MOCK_METHOD1(RemoveObserverForAllNetworks, void(NetworkObserver*));
  MOCK_METHOD2(AddNetworkDeviceObserver, void(const std::string&,
                                              NetworkDeviceObserver*));
  MOCK_METHOD2(RemoveNetworkDeviceObserver, void(const std::string&,
                                                 NetworkDeviceObserver*));
  MOCK_METHOD1(AddCellularDataPlanObserver, void(CellularDataPlanObserver*));
  MOCK_METHOD1(RemoveCellularDataPlanObserver, void(CellularDataPlanObserver*));
  MOCK_METHOD1(AddPinOperationObserver, void(PinOperationObserver*));
  MOCK_METHOD1(RemovePinOperationObserver, void(PinOperationObserver*));
  MOCK_METHOD1(AddUserActionObserver, void(UserActionObserver*));
  MOCK_METHOD1(RemoveUserActionObserver, void(UserActionObserver*));
  MOCK_METHOD0(Lock, void(void));
  MOCK_METHOD0(Unlock, void(void));
  MOCK_METHOD0(IsLocked, bool(void));
  MOCK_CONST_METHOD0(ethernet_network, const EthernetNetwork*(void));
  MOCK_CONST_METHOD0(ethernet_connecting, bool(void));
  MOCK_CONST_METHOD0(ethernet_connected, bool(void));

  MOCK_CONST_METHOD0(wifi_network, const WifiNetwork*(void));
  MOCK_CONST_METHOD0(wifi_connecting, bool(void));
  MOCK_CONST_METHOD0(wifi_connected, bool(void));

  MOCK_CONST_METHOD0(cellular_network, const CellularNetwork*(void));
  MOCK_CONST_METHOD0(cellular_connecting, bool(void));
  MOCK_CONST_METHOD0(cellular_connected, bool(void));

  MOCK_CONST_METHOD0(virtual_network, const VirtualNetwork*(void));
  MOCK_CONST_METHOD0(virtual_network_connecting, bool(void));
  MOCK_CONST_METHOD0(virtual_network_connected, bool(void));

  MOCK_CONST_METHOD0(Connected, bool(void));
  MOCK_CONST_METHOD0(Connecting, bool(void));

  MOCK_CONST_METHOD0(IPAddress, const std::string&(void));
  MOCK_CONST_METHOD0(wifi_networks, const WifiNetworkVector&(void));
  MOCK_CONST_METHOD0(remembered_wifi_networks, const WifiNetworkVector&(void));
  MOCK_CONST_METHOD0(cellular_networks, const CellularNetworkVector&(void));
  MOCK_CONST_METHOD0(virtual_networks, const VirtualNetworkVector&(void));
  MOCK_CONST_METHOD0(remembered_virtual_networks,
                     const VirtualNetworkVector&(void));

  MOCK_CONST_METHOD1(FindNetworkDeviceByPath,
                     NetworkDevice*(const std::string&));
  MOCK_CONST_METHOD0(FindCellularDevice, const NetworkDevice*(void));
  MOCK_CONST_METHOD0(FindWifiDevice, const NetworkDevice*(void));
  MOCK_CONST_METHOD0(FindEthernetDevice, const NetworkDevice*(void));
  MOCK_CONST_METHOD1(FindNetworkByPath, Network*(const std::string&));
  MOCK_CONST_METHOD1(FindWifiNetworkByPath, WifiNetwork*(const std::string&));
  MOCK_CONST_METHOD1(FindCellularNetworkByPath,
                     CellularNetwork*(const std::string&));
  MOCK_CONST_METHOD1(FindVirtualNetworkByPath,
                     VirtualNetwork*(const std::string&));
  MOCK_CONST_METHOD1(FindNetworkFromRemembered,Network*(const Network*));
  MOCK_CONST_METHOD1(FindRememberedNetworkByPath, Network*(const std::string&));
  MOCK_CONST_METHOD1(GetDataPlans,
                     CellularDataPlanVector*(const std::string&));
  MOCK_CONST_METHOD1(GetSignificantDataPlan,
                     CellularDataPlan*(const std::string&));

  MOCK_METHOD2(ChangePin, void(const std::string&, const std::string&));
  MOCK_METHOD2(ChangeRequirePin, void(bool, const std::string&));
  MOCK_METHOD1(EnterPin, void(const std::string&));
  MOCK_METHOD2(UnblockPin, void(const std::string&, const std::string&));

  MOCK_METHOD0(RequestCellularScan, void());
  MOCK_METHOD1(RequestCellularRegister, void(const std::string&));
  MOCK_METHOD1(SetCellularDataRoamingAllowed, void(bool));

  MOCK_METHOD0(RequestNetworkScan, void(void));
  MOCK_CONST_METHOD1(HasProfileType, bool(NetworkProfileType));
  MOCK_METHOD1(GetWifiAccessPoints, bool(WifiAccessPointVector*));
  MOCK_CONST_METHOD1(CanConnectToNetwork, bool(const Network*));
  MOCK_METHOD1(ConnectToWifiNetwork, void(WifiNetwork*));
  MOCK_METHOD1(ConnectToWifiNetwork, void(const std::string&));
  MOCK_METHOD3(ConnectToWifiNetwork, void(const std::string&,
                                          ConnectionSecurity security,
                                          const std::string&));
  MOCK_METHOD10(ConnectToWifiNetwork8021x, void(const std::string&,
                                                EAPMethod,
                                                EAPPhase2Auth,
                                                const std::string&,
                                                bool,
                                                const std::string&,
                                                const std::string&,
                                                const std::string&,
                                                const std::string&,
                                                bool));
  MOCK_METHOD1(ConnectToCellularNetwork, void(CellularNetwork*));
  MOCK_METHOD1(ConnectToVirtualNetwork, void(VirtualNetwork*));
  MOCK_METHOD5(ConnectToVirtualNetworkPSK, void(const std::string&,
                                                const std::string&,
                                                const std::string&,
                                                const std::string&,
                                                const std::string&));
  MOCK_METHOD6(ConnectToVirtualNetworkCert, void(const std::string&,
                                                 const std::string&,
                                                 const std::string&,
                                                 const std::string&,
                                                 const std::string&,
                                                 const std::string&));
  MOCK_METHOD0(SignalCellularPlanPayment, void(void));
  MOCK_METHOD0(HasRecentCellularPlanPayment, bool(void));

  MOCK_METHOD1(DisconnectFromNetwork, void(const Network*));
  MOCK_METHOD1(ForgetNetwork, void(const std::string&));
  MOCK_METHOD2(SetNetworkProfile, void(const std::string&,
                                       NetworkProfileType));
  MOCK_CONST_METHOD0(GetCellularHomeCarrierId, std::string(void));

  MOCK_CONST_METHOD0(ethernet_available, bool(void));
  MOCK_CONST_METHOD0(wifi_available, bool(void));
  MOCK_CONST_METHOD0(cellular_available, bool(void));

  MOCK_CONST_METHOD0(ethernet_enabled, bool(void));
  MOCK_CONST_METHOD0(wifi_enabled, bool(void));
  MOCK_CONST_METHOD0(cellular_enabled, bool(void));

  MOCK_CONST_METHOD0(wifi_scanning, bool(void));

  MOCK_CONST_METHOD0(active_network, const Network*(void));
  MOCK_CONST_METHOD0(connected_network, const Network*(void));

  MOCK_CONST_METHOD0(offline_mode, bool(void));

  MOCK_METHOD1(EnableEthernetNetworkDevice, void(bool));
  MOCK_METHOD1(EnableWifiNetworkDevice, void(bool));
  MOCK_METHOD1(EnableCellularNetworkDevice, void(bool));
  MOCK_METHOD1(EnableOfflineMode, void(bool));
  MOCK_METHOD3(GetIPConfigs, NetworkIPConfigVector(const std::string&,
                                                   std::string*,
                                                   HardwareAddressFormat));
  MOCK_METHOD1(SetIPConfig, void(const NetworkIPConfig&));
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CROS_MOCK_NETWORK_LIBRARY_H_
