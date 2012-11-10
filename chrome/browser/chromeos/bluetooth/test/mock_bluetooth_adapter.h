// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_BLUETOOTH_TEST_MOCK_BLUETOOTH_ADAPTER_H_
#define CHROME_BROWSER_CHROMEOS_BLUETOOTH_TEST_MOCK_BLUETOOTH_ADAPTER_H_

#include <string>

#include "base/callback.h"
#include "chrome/browser/chromeos/bluetooth/bluetooth_adapter.h"
#include "chrome/browser/chromeos/bluetooth/bluetooth_device.h"
#include "chromeos/dbus/bluetooth_out_of_band_client.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class MockBluetoothAdapter : public BluetoothAdapter {
 public:
  MockBluetoothAdapter();

  MOCK_CONST_METHOD0(IsPresent, bool());
  MOCK_CONST_METHOD0(IsPowered, bool());
  MOCK_METHOD3(SetDiscovering,
               void(bool discovering,
                    const base::Closure& callback,
                    const BluetoothAdapter::ErrorCallback& error_callback));
  MOCK_CONST_METHOD0(GetDevices, ConstDeviceList());
  MOCK_METHOD1(GetDevice, BluetoothDevice*(const std::string& address));
  MOCK_CONST_METHOD1(GetDevice,
                     const BluetoothDevice*(const std::string& address));
  MOCK_METHOD2(
      ReadLocalOutOfBandPairingData,
      void(const BluetoothAdapter::BluetoothOutOfBandPairingDataCallback&
               callback,
           const BluetoothAdapter::ErrorCallback& error_callback));
 protected:
  virtual ~MockBluetoothAdapter();
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_BLUETOOTH_TEST_MOCK_BLUETOOTH_ADAPTER_H_
