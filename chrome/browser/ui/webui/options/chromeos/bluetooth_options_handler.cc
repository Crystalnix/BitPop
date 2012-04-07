// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/chromeos/bluetooth_options_handler.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/bluetooth/bluetooth_device.h"
#include "chrome/browser/chromeos/system/runtime_environment.h"
#include "chrome/browser/ui/webui/options/chromeos/system_settings_provider.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/web_ui.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// |UpdateDeviceCallback| takes a variable length list as an argument. The
// value stored in each list element is indicated by the following constants.
const int kUpdateDeviceAddressIndex = 0;
const int kUpdateDeviceCommandIndex = 1;
const int kUpdateDevicePasskeyIndex = 2;

}  // namespace

namespace chromeos {

BluetoothOptionsHandler::BluetoothOptionsHandler() {
}

BluetoothOptionsHandler::~BluetoothOptionsHandler() {
  if (!CommandLine::ForCurrentProcess()
      ->HasSwitch(switches::kEnableBluetooth)) {
    return;
  }

  chromeos::BluetoothManager* bluetooth_manager =
      chromeos::BluetoothManager::GetInstance();
  DCHECK(bluetooth_manager);

  chromeos::BluetoothAdapter* default_adapter =
      bluetooth_manager->DefaultAdapter();

  if (default_adapter != NULL) {
    default_adapter->RemoveObserver(this);
  }

  bluetooth_manager->RemoveObserver(this);
}

void BluetoothOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  localized_strings->SetString("bluetooth",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_SECTION_TITLE_BLUETOOTH));
  localized_strings->SetString("disableBluetooth",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_DISABLE));
  localized_strings->SetString("enableBluetooth",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_ENABLE));
  localized_strings->SetString("addBluetoothDevice",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_ADD_BLUETOOTH_DEVICE));
  localized_strings->SetString("bluetoothAddDeviceTitle",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_ADD_DEVICE_TITLE));
  localized_strings->SetString("bluetoothOptionsPageTabTitle",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_ADD_DEVICE_TITLE));
  localized_strings->SetString("findBluetoothDevices",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_FIND_BLUETOOTH_DEVICES));
  localized_strings->SetString("bluetoothNoDevices",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_NO_DEVICES));
  localized_strings->SetString("bluetoothNoDevicesFound",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_NO_DEVICES_FOUND));
  localized_strings->SetString("bluetoothScanning",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_SCANNING));
  localized_strings->SetString("bluetoothDeviceConnected",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_CONNECTED));
  localized_strings->SetString("bluetoothDeviceNotConnected",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_NOT_CONNECTED));
  localized_strings->SetString("bluetoothConnectDevice",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_CONNECT));
  localized_strings->SetString("bluetoothDisconnectDevice",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_DISCONNECT));
  localized_strings->SetString("bluetoothForgetDevice",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_FORGET));
  localized_strings->SetString("bluetoothCancel",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_CANCEL));
  localized_strings->SetString("bluetoothEnterKey",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_BLUETOOTH_ENTER_KEY));
  localized_strings->SetString("bluetoothAcceptPasskey",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_ACCEPT_PASSKEY));
  localized_strings->SetString("bluetoothRejectPasskey",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_REJECT_PASSKEY));
  localized_strings->SetString("bluetoothConfirmPasskey",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_CONFIRM_PASSKEY_REQUEST));
  localized_strings->SetString("bluetoothEnterPasskey",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_ENTER_PASSKEY_REQUEST));
  localized_strings->SetString("bluetoothRemotePasskey",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_REMOTE_PASSKEY_REQUEST));
  localized_strings->SetString("bluetoothDismissError",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_DISMISS_ERROR));
  localized_strings->SetString("bluetoothErrorNoDevice",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_CONNECTION_FAILED_NO_DEVICE));
  localized_strings->SetString("bluetoothErrorIncorrectPin",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_CONNECTION_FAILED_INCORRECT_PIN));
  localized_strings->SetString("bluetoothErrorTimeout",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_CONNECTION_FAILED_TIMEOUT));
  localized_strings->SetString("bluetoothErrorConnectionFailed",
      l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_BLUETOOTH_CONNECTION_FAILED));
}

void BluetoothOptionsHandler::Initialize() {
  // Bluetooth support is a work in progress.  Supress the feature unless
  // explicitly enabled via a command line flag.
  if (!CommandLine::ForCurrentProcess()
      ->HasSwitch(switches::kEnableBluetooth)) {
    return;
  }

  web_ui()->CallJavascriptFunction(
      "options.SystemOptions.showBluetoothSettings");

  // TODO(kevers): Determine whether bluetooth adapter is powered.
  bool bluetooth_on = false;
  base::FundamentalValue checked(bluetooth_on);
  web_ui()->CallJavascriptFunction(
      "options.SystemOptions.setBluetoothState", checked);

  chromeos::BluetoothManager* bluetooth_manager =
      chromeos::BluetoothManager::GetInstance();
  DCHECK(bluetooth_manager);
  bluetooth_manager->AddObserver(this);

  chromeos::BluetoothAdapter* default_adapter =
      bluetooth_manager->DefaultAdapter();
  DefaultAdapterChanged(default_adapter);
}

void BluetoothOptionsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("bluetoothEnableChange",
      base::Bind(&BluetoothOptionsHandler::EnableChangeCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("findBluetoothDevices",
      base::Bind(&BluetoothOptionsHandler::FindDevicesCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("updateBluetoothDevice",
      base::Bind(&BluetoothOptionsHandler::UpdateDeviceCallback,
                 base::Unretained(this)));
}

void BluetoothOptionsHandler::EnableChangeCallback(
    const ListValue* args) {
  bool bluetooth_enabled;
  args->GetBoolean(0, &bluetooth_enabled);
  // TODO(kevers): Call Bluetooth API to enable or disable.
  base::FundamentalValue checked(bluetooth_enabled);
  web_ui()->CallJavascriptFunction(
      "options.SystemOptions.setBluetoothState", checked);
}

void BluetoothOptionsHandler::FindDevicesCallback(
    const ListValue* args) {
  // We only initiate a scan if we're running on Chrome OS. Otherwise, we
  // generate a fake device list.
  if (!chromeos::system::runtime_environment::IsRunningOnChromeOS()) {
    GenerateFakeDeviceList();
    return;
  }

  chromeos::BluetoothManager* bluetooth_manager =
      chromeos::BluetoothManager::GetInstance();
  DCHECK(bluetooth_manager);

  chromeos::BluetoothAdapter* default_adapter =
      bluetooth_manager->DefaultAdapter();

  ValidateDefaultAdapter(default_adapter);

  if (default_adapter == NULL) {
    VLOG(1) << "FindDevicesCallback: no default adapter";
    return;
  }

  default_adapter->StartDiscovery();
}

void BluetoothOptionsHandler::UpdateDeviceCallback(
    const ListValue* args) {
  // TODO(kevers): Trigger connect/disconnect.
  int size = args->GetSize();
  std::string address;
  std::string command;
  args->GetString(kUpdateDeviceAddressIndex, &address);
  args->GetString(kUpdateDeviceCommandIndex, &command);
  if (size > kUpdateDevicePasskeyIndex) {
    // Passkey confirmation as part of the pairing process.
    std::string passkey;
    args->GetString(kUpdateDevicePasskeyIndex, &passkey);
    DVLOG(1) << "UpdateDeviceCallback: " << address << ": " << command
            << " [" << passkey << "]";
  } else {
    // Initiating a device connection or disconnecting
    DVLOG(1) << "UpdateDeviceCallback: " << address << ": " << command;
  }
}

void BluetoothOptionsHandler::SendDeviceNotification(
    chromeos::BluetoothDevice* device,
    base::DictionaryValue* params) {
  // Retrieve properties of the bluetooth device.  The properties names are
  // in title case.  Convert to camel case in accordance with our Javascript
  // naming convention.
  const DictionaryValue& properties = device->AsDictionary();
  base::DictionaryValue js_properties;
  for (DictionaryValue::key_iterator it = properties.begin_keys();
      it != properties.end_keys(); ++it) {
    base::Value* child = NULL;
    properties.GetWithoutPathExpansion(*it, &child);
    if (child) {
      std::string js_key = *it;
      js_key[0] = tolower(js_key[0]);
      js_properties.SetWithoutPathExpansion(js_key, child->DeepCopy());
    }
  }
  if (params) {
    js_properties.MergeDictionary(params);
  }
  web_ui()->CallJavascriptFunction(
      "options.SystemOptions.addBluetoothDevice",
      js_properties);
}

void BluetoothOptionsHandler::RequestConfirmation(
    chromeos::BluetoothDevice* device,
    int passkey) {
  DictionaryValue params;
  params.SetString("pairing", "bluetoothConfirmPasskey");
  params.SetInteger("passkey", passkey);
  SendDeviceNotification(device, &params);
}

void BluetoothOptionsHandler::DisplayPasskey(
    chromeos::BluetoothDevice* device,
    int passkey,
    int entered) {
  DictionaryValue params;
  params.SetString("pairing", "bluetoothRemotePasskey");
  params.SetInteger("passkey", passkey);
  params.SetInteger("entered", entered);
  SendDeviceNotification(device, &params);
}

void BluetoothOptionsHandler::RequestPasskey(
    chromeos::BluetoothDevice* device) {
  DictionaryValue params;
  params.SetString("pairing", "bluetoothEnterPasskey");
  SendDeviceNotification(device, &params);
}

void BluetoothOptionsHandler::ReportError(
    chromeos::BluetoothDevice* device,
    ConnectionError error) {
  std::string errorCode;
  switch (error) {
  case DEVICE_NOT_FOUND:
    errorCode = "bluetoothErrorNoDevice";
    break;
  case INCORRECT_PIN:
    errorCode = "bluetoothErrorIncorrectPin";
    break;
  case CONNECTION_TIMEOUT:
    errorCode = "bluetoothErrorTimeout";
    break;
  case CONNECTION_REJECTED:
    errorCode = "bluetoothErrorConnectionFailed";
    break;
  }
  DictionaryValue params;
  params.SetString("pairing", errorCode);
  SendDeviceNotification(device, &params);
}

void BluetoothOptionsHandler::DefaultAdapterChanged(
    chromeos::BluetoothAdapter* adapter) {
  std::string old_default_adapter_id = default_adapter_id_;

  if (adapter == NULL) {
    default_adapter_id_.clear();
    VLOG(2) << "DefaultAdapterChanged: no default bluetooth adapter";
  } else {
    default_adapter_id_ = adapter->Id();
    VLOG(2) << "DefaultAdapterChanged: " << default_adapter_id_;
  }

  if (default_adapter_id_ == old_default_adapter_id) {
    return;
  }

  if (adapter != NULL) {
    adapter->AddObserver(this);
  }

  // TODO(vlaviano): Respond to adapter change.
}

void BluetoothOptionsHandler::DiscoveryStarted(const std::string& adapter_id) {
  VLOG(2) << "Discovery started on " << adapter_id;
}

void BluetoothOptionsHandler::DiscoveryEnded(const std::string& adapter_id) {
  VLOG(2) << "Discovery ended on " << adapter_id;
  web_ui()->CallJavascriptFunction(
      "options.SystemOptions.notifyBluetoothSearchComplete");

  // Stop the discovery session.
  // TODO(vlaviano): We may want to expose DeviceDisappeared, remove the
  // "Find devices" button, and let the discovery session continue throughout
  // the time that the page is visible rather than just doing a single discovery
  // cycle in response to a button click.
  chromeos::BluetoothManager* bluetooth_manager =
      chromeos::BluetoothManager::GetInstance();
  DCHECK(bluetooth_manager);

  chromeos::BluetoothAdapter* default_adapter =
      bluetooth_manager->DefaultAdapter();

  ValidateDefaultAdapter(default_adapter);

  if (default_adapter == NULL) {
    VLOG(1) << "DiscoveryEnded: no default adapter";
    return;
  }

  default_adapter->StopDiscovery();
}

void BluetoothOptionsHandler::DeviceFound(const std::string& adapter_id,
                                          chromeos::BluetoothDevice* device) {
  VLOG(2) << "Device found on " << adapter_id;
  DCHECK(device);
  SendDeviceNotification(device, NULL);
}

void BluetoothOptionsHandler::ValidateDefaultAdapter(
    chromeos::BluetoothAdapter* adapter) {
  if ((adapter == NULL && !default_adapter_id_.empty()) ||
      (adapter != NULL && default_adapter_id_ != adapter->Id())) {
    VLOG(1) << "unexpected default adapter change from \""
            << default_adapter_id_ << "\" to \"" << adapter->Id() << "\"";
    DefaultAdapterChanged(adapter);
  }
}

void BluetoothOptionsHandler::GenerateFakeDeviceList() {
  GenerateFakeDevice(
    "Fake Wireless Keyboard",
    "01-02-03-04-05-06",
    "input-keyboard",
    false,
    false,
    "");
  GenerateFakeDevice(
    "Fake Wireless Mouse",
    "02-03-04-05-06-01",
    "input-mouse",
    false,
    false,
    "");
  GenerateFakeDevice(
    "Fake Wireless Headset",
    "03-04-05-06-01-02",
    "headset",
    false,
    false,
    "");
  web_ui()->CallJavascriptFunction(
      "options.SystemOptions.notifyBluetoothSearchComplete");
}

void BluetoothOptionsHandler::GenerateFakeDevice(
    const std::string& name,
    const std::string& address,
    const std::string& icon,
    bool paired,
    bool connected,
    const std::string& pairing) {
  DictionaryValue properties;
  properties.SetString(bluetooth_device::kNameProperty, name);
  properties.SetString(bluetooth_device::kAddressProperty, address);
  properties.SetString(bluetooth_device::kIconProperty, icon);
  properties.SetBoolean(bluetooth_device::kPairedProperty, paired);
  properties.SetBoolean(bluetooth_device::kConnectedProperty, connected);
  properties.SetInteger(bluetooth_device::kClassProperty, 0);
  chromeos::BluetoothDevice* device =
      chromeos::BluetoothDevice::Create(properties);
  DeviceFound("FakeAdapter", device);
  if (pairing.compare("bluetoothRemotePasskey") == 0) {
    DisplayPasskey(device, 730119, 2);
  } else if (pairing.compare("bluetoothConfirmPasskey") == 0) {
    RequestConfirmation(device, 730119);
  } else if (pairing.compare("bluetoothEnterPasskey") == 0) {
    RequestPasskey(device);
  } else if (pairing.length() > 0) {
    // Sending an error notification.
    DictionaryValue params;
    params.SetString("pairing", pairing);
    SendDeviceNotification(device, &params);
  }
  delete device;
}

}  // namespace chromeos

