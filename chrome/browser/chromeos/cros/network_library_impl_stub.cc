// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/cros/network_library_impl_stub.h"

#include "base/bind.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace chromeos {

NetworkLibraryImplStub::NetworkLibraryImplStub()
    : ip_address_("1.1.1.1"),
      hardware_address_("01:23:45:67:89:ab"),
      pin_(""),
      pin_required_(false),
      pin_entered_(false),
      connect_delay_ms_(0),
      network_priority_order_(0) {
  // Emulate default setting of the CheckPortalList when OOBE is done.
  check_portal_list_ = "ethernet,wifi,cellular";
}

NetworkLibraryImplStub::~NetworkLibraryImplStub() {
  disabled_wifi_networks_.clear();
  disabled_cellular_networks_.clear();
  disabled_wimax_networks_.clear();
}

void NetworkLibraryImplStub::Init() {
  is_locked_ = false;

  // Devices
  int devices =
      (1 << TYPE_ETHERNET) | (1 << TYPE_WIFI) | (1 << TYPE_CELLULAR) |
      (1 << TYPE_WIMAX);
  available_devices_ = devices;
  enabled_devices_ = devices;
  connected_devices_ = devices;

  NetworkDevice* cellular = new NetworkDevice("cellular");
  cellular->type_ = TYPE_CELLULAR;
  cellular->imsi_ = "123456789012345";
  device_map_["cellular"] = cellular;

  CellularApn apn;
  apn.apn = "apn";
  apn.network_id = "network_id";
  apn.username = "username";
  apn.password = "password";
  apn.name = "name";
  apn.localized_name = "localized_name";
  apn.language = "language";

  CellularApnList apn_list;
  apn_list.push_back(apn);

  NetworkDevice* cellular_gsm = new NetworkDevice("cellular_gsm");
  cellular_gsm->type_ = TYPE_CELLULAR;
  cellular_gsm->set_technology_family(TECHNOLOGY_FAMILY_GSM);
  cellular_gsm->imsi_ = "123456789012345";
  cellular_gsm->set_sim_pin_required(SIM_PIN_REQUIRED);
  cellular_gsm->set_provider_apn_list(apn_list);
  device_map_["cellular_gsm"] = cellular_gsm;

  // Profiles
  AddProfile("default", PROFILE_SHARED);
  AddProfile("user", PROFILE_USER);

  // Networks
  // If these change, the expectations in network_library_unittest and
  // network_menu_icon_unittest need to be changed also.

  Network* ethernet = new EthernetNetwork("eth1");
  ethernet->set_name("Fake Ethernet");
  ethernet->set_connected();
  AddStubNetwork(ethernet, PROFILE_SHARED);
  ethernet->set_is_active(ethernet->connected());

  WifiNetwork* wifi1 = new WifiNetwork("wifi1");
  wifi1->set_name("Fake WiFi1");
  wifi1->set_strength(100);
  wifi1->set_connected();
  wifi1->set_encryption(SECURITY_NONE);
  AddStubNetwork(wifi1, PROFILE_SHARED);

  WifiNetwork* wifi2 = new WifiNetwork("wifi2");
  wifi2->set_name("Fake WiFi2");
  wifi2->set_strength(70);
  wifi2->set_encryption(SECURITY_NONE);
  AddStubNetwork(wifi2, PROFILE_SHARED);

  WifiNetwork* wifi3 = new WifiNetwork("wifi3");
  wifi3->set_name("Fake WiFi3 Encrypted with a long name");
  wifi3->set_strength(60);
  wifi3->set_encryption(SECURITY_WEP);
  wifi3->set_passphrase_required(true);
  AddStubNetwork(wifi3, PROFILE_USER);

  WifiNetwork* wifi_cert_pattern = new WifiNetwork("wifi_cert_pattern");
  wifi_cert_pattern->set_name("Fake WiFi CertPattern 802.1x");
  wifi_cert_pattern->set_strength(50);
  wifi_cert_pattern->set_connectable(false);
  wifi_cert_pattern->set_encryption(SECURITY_8021X);
  wifi_cert_pattern->SetEAPMethod(EAP_METHOD_TLS);
  wifi_cert_pattern->SetEAPUseSystemCAs(true);
  wifi_cert_pattern->SetEAPIdentity("user@example.com");
  wifi_cert_pattern->SetEAPPhase2Auth(EAP_PHASE_2_AUTH_AUTO);
  wifi_cert_pattern->set_client_cert_type(CLIENT_CERT_TYPE_PATTERN);
  CertificatePattern pattern;
  IssuerSubjectPattern subject;
  subject.set_organization("Google Inc");
  pattern.set_subject(subject);
  std::vector<std::string> enrollment_uris;
  enrollment_uris.push_back("http://www.google.com/chromebook");
  pattern.set_enrollment_uri_list(enrollment_uris);
  wifi_cert_pattern->set_client_cert_pattern(pattern);
  wifi_cert_pattern->set_eap_save_credentials(true);

  AddStubNetwork(wifi_cert_pattern, PROFILE_USER);

  WifiNetwork* wifi4 = new WifiNetwork("wifi4");
  wifi4->set_name("Fake WiFi4 802.1x");
  wifi4->set_strength(50);
  wifi4->set_connectable(false);
  wifi4->set_encryption(SECURITY_8021X);
  wifi4->SetEAPMethod(EAP_METHOD_PEAP);
  wifi4->SetEAPIdentity("nobody@google.com");
  wifi4->SetEAPPassphrase("password");
  AddStubNetwork(wifi4, PROFILE_NONE);

  WifiNetwork* wifi5 = new WifiNetwork("wifi5");
  wifi5->set_name("Fake WiFi5 UTF-8 SSID ");
  wifi5->SetSsid("Fake WiFi5 UTF-8 SSID \u3042\u3044\u3046");
  wifi5->set_strength(25);
  AddStubNetwork(wifi5, PROFILE_NONE);

  WifiNetwork* wifi6 = new WifiNetwork("wifi6");
  wifi6->set_name("Fake WiFi6 latin-1 SSID ");
  wifi6->SetSsid("Fake WiFi6 latin-1 SSID \xc0\xcb\xcc\xd6\xfb");
  wifi6->set_strength(20);
  AddStubNetwork(wifi6, PROFILE_NONE);

  WifiNetwork* wifi7 = new WifiNetwork("wifi7");
  wifi7->set_name("Fake Wifi7 (policy-managed)");
  wifi7->set_strength(100);
  wifi7->set_connectable(false);
  wifi7->set_passphrase_required(true);
  wifi7->set_encryption(SECURITY_8021X);
  wifi7->SetEAPMethod(EAP_METHOD_PEAP);
  wifi7->SetEAPIdentity("enterprise@example.com");
  wifi7->SetEAPPassphrase("password");
  NetworkUIData wifi7_ui_data;
  wifi7_ui_data.set_onc_source(NetworkUIData::ONC_SOURCE_DEVICE_POLICY);
  wifi7->set_ui_data(wifi7_ui_data);
  AddStubNetwork(wifi7, PROFILE_USER);

  CellularNetwork* cellular1 = new CellularNetwork("cellular1");
  cellular1->set_name("Fake Cellular 1");
  cellular1->set_strength(100);
  cellular1->set_connected();
  cellular1->set_activation_state(ACTIVATION_STATE_ACTIVATED);
  cellular1->set_payment_url(std::string("http://www.google.com"));
  cellular1->set_usage_url(std::string("http://www.google.com"));
  cellular1->set_network_technology(NETWORK_TECHNOLOGY_EVDO);
  AddStubNetwork(cellular1, PROFILE_NONE);

  CellularNetwork* cellular2 = new CellularNetwork("/cellular2");
  cellular2->set_name("Fake Cellular 2");
  cellular2->set_strength(50);
  cellular2->set_activation_state(ACTIVATION_STATE_NOT_ACTIVATED);
  cellular2->set_network_technology(NETWORK_TECHNOLOGY_UMTS);
  cellular2->set_roaming_state(ROAMING_STATE_ROAMING);
  cellular2->set_payment_url(std::string("http://www.google.com"));
  cellular2->set_usage_url(std::string("http://www.google.com"));
  AddStubNetwork(cellular2, PROFILE_NONE);

  CellularNetwork* cellular3 = new CellularNetwork("cellular3");
  cellular3->set_name("Fake Cellular 3 (policy-managed)");
  cellular3->set_device_path(cellular->device_path());
  cellular3->set_activation_state(ACTIVATION_STATE_ACTIVATED);
  cellular3->set_network_technology(NETWORK_TECHNOLOGY_EVDO);
  NetworkUIData cellular3_ui_data;
  cellular3_ui_data.set_onc_source(NetworkUIData::ONC_SOURCE_USER_POLICY);
  cellular3->set_ui_data(cellular3_ui_data);
  AddStubNetwork(cellular3, PROFILE_NONE);

  CellularNetwork* cellular4 = new CellularNetwork("cellular4");
  cellular4->set_name("Fake Cellular 4 (policy-managed)");
  cellular4->set_device_path(cellular_gsm->device_path());
  cellular4->set_activation_state(ACTIVATION_STATE_ACTIVATED);
  cellular4->set_network_technology(NETWORK_TECHNOLOGY_GSM);
  NetworkUIData cellular4_ui_data;
  cellular4_ui_data.set_onc_source(NetworkUIData::ONC_SOURCE_USER_POLICY);
  cellular4->set_ui_data(cellular4_ui_data);
  AddStubNetwork(cellular4, PROFILE_NONE);

  CellularNetwork* cellular5 = new CellularNetwork("cellular5");
  cellular5->set_name("Fake Cellular Low Data");
  cellular5->set_strength(100);
  cellular5->set_activation_state(ACTIVATION_STATE_ACTIVATED);
  cellular5->set_payment_url(std::string("http://www.google.com"));
  cellular5->set_usage_url(std::string("http://www.google.com"));
  cellular5->set_network_technology(NETWORK_TECHNOLOGY_EVDO);
  cellular5->set_data_left(CellularNetwork::DATA_LOW);
  AddStubNetwork(cellular5, PROFILE_NONE);

  CellularDataPlan* base_plan = new CellularDataPlan();
  base_plan->plan_name = "Base plan";
  base_plan->plan_type = CELLULAR_DATA_PLAN_METERED_BASE;
  base_plan->plan_data_bytes = 100ll * 1024 * 1024;
  base_plan->data_bytes_used = base_plan->plan_data_bytes / 4;

  CellularDataPlan* paid_plan = new CellularDataPlan();
  paid_plan->plan_name = "Paid plan";
  paid_plan->plan_type = CELLULAR_DATA_PLAN_METERED_PAID;
  paid_plan->plan_data_bytes = 5ll * 1024 * 1024 * 1024;
  paid_plan->data_bytes_used = paid_plan->plan_data_bytes / 2;

  CellularDataPlanVector* data_plan_vector1 = new CellularDataPlanVector;
  data_plan_vector1->push_back(base_plan);
  data_plan_vector1->push_back(paid_plan);
  UpdateCellularDataPlan(cellular1->service_path(), data_plan_vector1);

  CellularDataPlan* low_data_plan = new CellularDataPlan();
  low_data_plan->plan_name = "Low Data plan";
  low_data_plan->plan_type = CELLULAR_DATA_PLAN_METERED_PAID;
  low_data_plan->plan_data_bytes = 5ll * 1024 * 1024 * 1024;
  low_data_plan->data_bytes_used =
      low_data_plan->plan_data_bytes - kCellularDataVeryLowBytes;

  CellularDataPlanVector* data_plan_vector2 = new CellularDataPlanVector;
  data_plan_vector2->push_back(low_data_plan);
  UpdateCellularDataPlan(cellular5->service_path(), data_plan_vector2);

  WimaxNetwork* wimax1 = new WimaxNetwork("wimax1");
  wimax1->set_name("Fake WiMAX Protected");
  wimax1->set_strength(75);
  wimax1->set_connectable(true);
  wimax1->set_eap_identity("WiMAX User 1");
  wimax1->set_passphrase_required(true);
  AddStubNetwork(wimax1, PROFILE_NONE);

  WimaxNetwork* wimax2 = new WimaxNetwork("wimax2");
  wimax2->set_name("Fake WiMAX Open");
  wimax2->set_strength(50);
  wimax2->set_connected();
  wimax2->set_passphrase_required(false);
  AddStubNetwork(wimax2, PROFILE_NONE);

  VirtualNetwork* vpn1 = new VirtualNetwork("vpn1");
  vpn1->set_name("Fake VPN1");
  vpn1->set_server_hostname("vpn1server.fake.com");
  vpn1->set_provider_type(PROVIDER_TYPE_L2TP_IPSEC_PSK);
  vpn1->set_username("VPN User 1");
  AddStubNetwork(vpn1, PROFILE_USER);

  VirtualNetwork* vpn2 = new VirtualNetwork("vpn2");
  vpn2->set_name("Fake VPN2");
  vpn2->set_server_hostname("vpn2server.fake.com");
  vpn2->set_provider_type(PROVIDER_TYPE_L2TP_IPSEC_USER_CERT);
  vpn2->set_username("VPN User 2");
  AddStubNetwork(vpn2, PROFILE_USER);

  VirtualNetwork* vpn3 = new VirtualNetwork("vpn3");
  vpn3->set_name("Fake VPN3");
  vpn3->set_server_hostname("vpn3server.fake.com");
  vpn3->set_provider_type(PROVIDER_TYPE_OPEN_VPN);
  AddStubNetwork(vpn3, PROFILE_USER);

  VirtualNetwork* vpn4 = new VirtualNetwork("vpn4");
  vpn4->set_name("Fake VPN4 (policy-managed)");
  vpn4->set_server_hostname("vpn4server.fake.com");
  vpn4->set_provider_type(PROVIDER_TYPE_OPEN_VPN);
  NetworkUIData vpn4_ui_data;
  vpn4_ui_data.set_onc_source(NetworkUIData::ONC_SOURCE_DEVICE_POLICY);
  vpn4->set_ui_data(vpn4_ui_data);
  AddStubNetwork(vpn4, PROFILE_USER);

  wifi_scanning_ = false;
  offline_mode_ = false;

  // Ensure our active network is connected and vice versa, otherwise our
  // autotest browser_tests sometimes conclude the device is offline.
  CHECK(active_network()->connected())
      << "Active: " << active_network()->name();
  CHECK(connected_network()->is_active());

  std::string test_blob(
        "{"
        "  \"NetworkConfigurations\": ["
        "    {"
        "      \"GUID\": \"guid\","
        "      \"Type\": \"VPN\","
        "      \"Name\": \"VPNtest\","
        "      \"VPN\": {"
        "        \"Host\": \"172.22.12.98\","
        "        \"Type\": \"L2TP-IPsec\","
        "        \"IPsec\": {"
        "          \"AuthenticationType\": \"PSK\","
        "          \"IKEVersion\": 2,"
        "          \"PSK\": \"chromeos\","
        "        },"
        "        \"L2TP\": {"
        "          \"Username\": \"vpntest\","
        "        }"
        "      }"
        "    }"
        "  ],"
        "  \"Certificates\": []"
        "}");
//  LoadOncNetworks(test_blob, "", NetworkUIData::ONC_SOURCE_USER_IMPORT, NULL);
}

bool NetworkLibraryImplStub::IsCros() const {
  return false;
}

////////////////////////////////////////////////////////////////////////////
// NetworkLibraryImplStub private methods.

void NetworkLibraryImplStub::AddStubNetwork(
    Network* network, NetworkProfileType profile_type) {
  // Currently we don't prioritize networks in Shill so don't do so in the stub.
  // network->priority_order_ = network_priority_order_++;
  network->CalculateUniqueId();
  if (!network->unique_id().empty())
    network_unique_id_map_[network->unique_id()] = network;
  AddNetwork(network);
  UpdateActiveNetwork(network);
  SetProfileType(network, profile_type);
  AddStubRememberedNetwork(network);
}

// Add a remembered network to the appropriate profile if specified.
void NetworkLibraryImplStub::AddStubRememberedNetwork(Network* network) {
  if (network->profile_type() == PROFILE_NONE)
    return;

  Network* remembered = FindRememberedFromNetwork(network);
  if (remembered) {
    // This network is already in the rememebred list. Check to see if the
    // type has changed.
    if (remembered->profile_type() == network->profile_type())
      return;  // Same type, nothing to do.
    // Delete the existing remembered network from the previous profile.
    DeleteRememberedNetwork(remembered->service_path());
    remembered = NULL;
  }

  NetworkProfile* profile = GetProfileForType(network->profile_type());
  if (profile) {
    profile->services.insert(network->service_path());
  } else {
    LOG(ERROR) << "No profile type: " << network->profile_type();
    return;
  }

  if (network->type() == TYPE_WIFI) {
    WifiNetwork* remembered_wifi = new WifiNetwork(network->service_path());
    remembered_wifi->set_encryption(remembered_wifi->encryption());
    NetworkUIData wifi_ui_data;
    wifi_ui_data.set_onc_source(network->ui_data().onc_source());
    remembered_wifi->set_ui_data(wifi_ui_data);
    remembered = remembered_wifi;
  } else if (network->type() == TYPE_VPN) {
    VirtualNetwork* remembered_vpn =
        new VirtualNetwork(network->service_path());
    remembered_vpn->set_server_hostname("vpnserver.fake.com");
    remembered_vpn->set_provider_type(PROVIDER_TYPE_L2TP_IPSEC_USER_CERT);
    NetworkUIData vpn_ui_data;
    vpn_ui_data.set_onc_source(network->ui_data().onc_source());
    remembered_vpn->set_ui_data(vpn_ui_data);
    remembered = remembered_vpn;
  }
  if (remembered) {
    remembered->set_name(network->name());
    remembered->set_unique_id(network->unique_id());
    // ValidateAndAddRememberedNetwork will insert the network into the matching
    // profile and set the profile type + path.
    if (!ValidateAndAddRememberedNetwork(remembered))
      NOTREACHED();
  }
}

void NetworkLibraryImplStub::ConnectToNetwork(Network* network) {
  std::string passphrase;
  if (network->type() == TYPE_WIFI) {
    WifiNetwork* wifi = static_cast<WifiNetwork*>(network);
    if (wifi->passphrase_required())
      passphrase = wifi->passphrase();
  } else if (network->type() == TYPE_WIMAX) {
    WimaxNetwork* wimax = static_cast<WimaxNetwork*>(network);
    if (wimax->passphrase_required())
      passphrase = wimax->eap_passphrase();
  }
  if (!passphrase.empty()) {
    if (passphrase.find("bad") == 0) {
      NetworkConnectCompleted(network, CONNECT_BAD_PASSPHRASE);
      return;
    } else if (passphrase.find("error") == 0) {
      NetworkConnectCompleted(network, CONNECT_FAILED);
      return;
    }
  }

  // Disconnect ethernet when connecting to a new network (for UI testing).
  if (network->type() != TYPE_VPN) {
    ethernet_->set_is_active(false);
    ethernet_->set_disconnected();
  }

  // Set connected state.
  network->set_connected();
  network->set_connection_started(false);

  // Make the connected network the highest priority network.
  // Set all other networks of the same type to disconnected + inactive;
  int old_priority_order = network->priority_order_;
  network->priority_order_ = 0;
  for (NetworkMap::iterator iter = network_map_.begin();
       iter != network_map_.end(); ++iter) {
    Network* other = iter->second;
    if (other == network)
      continue;
    if (other->priority_order_ < old_priority_order)
      other->priority_order_++;
    if (other->type() == network->type()) {
      other->set_is_active(false);
      other->set_disconnected();
    }
  }

  // Cycle data left to trigger notifications.
  if (network->type() == TYPE_CELLULAR) {
    if (network->name().find("Low Data") != std::string::npos) {
      CellularNetwork* cellular = static_cast<CellularNetwork*>(network);
      // Simulate a transition to very low data.
      cellular->set_data_left(CellularNetwork::DATA_LOW);
      NotifyCellularDataPlanChanged();
      cellular->set_data_left(CellularNetwork::DATA_VERY_LOW);
      active_cellular_ = cellular;
      NotifyCellularDataPlanChanged();
    }
  }

  // Remember connected network.
  if (network->profile_type() == PROFILE_NONE) {
    NetworkProfileType profile_type = PROFILE_USER;
    if (network->type() == TYPE_WIFI) {
      WifiNetwork* wifi = static_cast<WifiNetwork*>(network);
      if (!wifi->encrypted())
        profile_type = PROFILE_SHARED;
    }
    SetProfileType(network, profile_type);
  }
  AddStubRememberedNetwork(network);

  // Call Completed and signal observers.
  NetworkConnectCompleted(network, CONNECT_SUCCESS);
}

//////////////////////////////////////////////////////////////////////////////
// NetworkLibraryImplBase implementation.

void NetworkLibraryImplStub::MonitorNetworkStart(
    const std::string& service_path) {}

void NetworkLibraryImplStub::MonitorNetworkStop(
    const std::string& service_path) {}

void NetworkLibraryImplStub::MonitorNetworkDeviceStart(
    const std::string& device_path) {}

void NetworkLibraryImplStub::MonitorNetworkDeviceStop(
    const std::string& device_path) {}

void NetworkLibraryImplStub::CallConfigureService(
    const std::string& identifier,
    const DictionaryValue* info) {}

void NetworkLibraryImplStub::CallConnectToNetwork(Network* network) {
  // Immediately set the network to active to mimic flimflam's behavior.
  SetActiveNetwork(network->type(), network->service_path());
  // If a delay has been set (i.e. we are interactive), delay the call to
  // ConnectToNetwork (but signal observers since we changed connecting state).
  if (connect_delay_ms_) {
    // This class is a Singleton and won't be deleted until this callbacks has
    // run.
    BrowserThread::PostDelayedTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&NetworkLibraryImplStub::ConnectToNetwork,
                   base::Unretained(this), network),
        base::TimeDelta::FromMilliseconds(connect_delay_ms_));
    SignalNetworkManagerObservers();
    NotifyNetworkChanged(network);
  } else {
    ConnectToNetwork(network);
  }
}

void NetworkLibraryImplStub::CallRequestWifiNetworkAndConnect(
    const std::string& ssid, ConnectionSecurity security) {
  WifiNetwork* wifi = new WifiNetwork(ssid);
  wifi->set_name(ssid);
  wifi->set_encryption(security);
  AddNetwork(wifi);
  ConnectToWifiNetworkUsingConnectData(wifi);
  SignalNetworkManagerObservers();
}

void NetworkLibraryImplStub::CallRequestVirtualNetworkAndConnect(
    const std::string& service_name,
    const std::string& server_hostname,
    ProviderType provider_type) {
  VirtualNetwork* vpn = new VirtualNetwork(service_name);
  vpn->set_name(service_name);
  vpn->set_server_hostname(server_hostname);
  vpn->set_provider_type(provider_type);
  AddNetwork(vpn);
  ConnectToVirtualNetworkUsingConnectData(vpn);
  SignalNetworkManagerObservers();
}

void NetworkLibraryImplStub::CallDeleteRememberedNetwork(
    const std::string& profile_path,
    const std::string& service_path) {}

void NetworkLibraryImplStub::CallEnableNetworkDeviceType(
    ConnectionType device, bool enable) {
  if (enable) {
    if (device == TYPE_WIFI && !wifi_enabled()) {
      wifi_networks_.swap(disabled_wifi_networks_);
      disabled_wifi_networks_.clear();
    } else if (device == TYPE_WIMAX && !wimax_enabled()) {
      wimax_networks_.swap(disabled_wimax_networks_);
      disabled_wimax_networks_.clear();
    } else if (device == TYPE_CELLULAR && !cellular_enabled()) {
      cellular_networks_.swap(disabled_cellular_networks_);
      disabled_cellular_networks_.clear();
    }
    enabled_devices_ |= (1 << device);
  } else {
    if (device == TYPE_WIFI && wifi_enabled()) {
      wifi_networks_.swap(disabled_wifi_networks_);
      wifi_networks_.clear();
      if (active_wifi_)
        DisconnectFromNetwork(active_wifi_);
    } else if (device == TYPE_WIMAX && wimax_enabled()) {
        wimax_networks_.swap(disabled_wimax_networks_);
        wimax_networks_.clear();
        if (active_wimax_)
          DisconnectFromNetwork(active_wimax_);
    } else if (device == TYPE_CELLULAR && cellular_enabled()) {
      cellular_networks_.swap(disabled_cellular_networks_);
      cellular_networks_.clear();
      if (active_cellular_)
        DisconnectFromNetwork(active_cellular_);
    }
    enabled_devices_ &= ~(1 << device);
  }
  SignalNetworkManagerObservers();
}

void NetworkLibraryImplStub::CallRemoveNetwork(const Network* network) {}

/////////////////////////////////////////////////////////////////////////////
// NetworkLibrary implementation.

void NetworkLibraryImplStub::SetCheckPortalList(const
    std::string& check_portal_list) {
  check_portal_list_ = check_portal_list;
}

void NetworkLibraryImplStub::SetDefaultCheckPortalList() {
  SetCheckPortalList("ethernet,wifi,cellular");
}

void NetworkLibraryImplStub::ChangePin(const std::string& old_pin,
                                       const std::string& new_pin) {
  sim_operation_ = SIM_OPERATION_CHANGE_PIN;
  if (!pin_required_ || old_pin == pin_) {
    pin_ = new_pin;
    NotifyPinOperationCompleted(PIN_ERROR_NONE);
  } else {
    NotifyPinOperationCompleted(PIN_ERROR_INCORRECT_CODE);
  }
}

void NetworkLibraryImplStub::ChangeRequirePin(bool require_pin,
                                              const std::string& pin) {
  sim_operation_ = SIM_OPERATION_CHANGE_REQUIRE_PIN;
  if (!pin_required_ || pin == pin_) {
    pin_required_ = require_pin;
    NotifyPinOperationCompleted(PIN_ERROR_NONE);
  } else {
    NotifyPinOperationCompleted(PIN_ERROR_INCORRECT_CODE);
  }
}

void NetworkLibraryImplStub::EnterPin(const std::string& pin) {
  sim_operation_ = SIM_OPERATION_ENTER_PIN;
  if (!pin_required_ || pin == pin_) {
    pin_entered_ = true;
    NotifyPinOperationCompleted(PIN_ERROR_NONE);
  } else {
    NotifyPinOperationCompleted(PIN_ERROR_INCORRECT_CODE);
  }
}

void NetworkLibraryImplStub::UnblockPin(const std::string& puk,
                                        const std::string& new_pin) {
  sim_operation_ = SIM_OPERATION_UNBLOCK_PIN;
  // TODO(stevenjb): something?
  NotifyPinOperationCompleted(PIN_ERROR_NONE);
}

void NetworkLibraryImplStub::RequestCellularScan() {}

void NetworkLibraryImplStub::RequestCellularRegister(
    const std::string& network_id) {}

void NetworkLibraryImplStub::SetCellularDataRoamingAllowed(bool new_value) {}

bool NetworkLibraryImplStub::IsCellularAlwaysInRoaming() {
  return false;
}

void NetworkLibraryImplStub::RequestNetworkScan() {
  // This is triggered by user interaction, so set a network connect delay.
  const int kConnectDelayMs = 4 * 1000;
  connect_delay_ms_ = kConnectDelayMs;
  SignalNetworkManagerObservers();
}

bool NetworkLibraryImplStub::GetWifiAccessPoints(
    WifiAccessPointVector* result) {
  *result = WifiAccessPointVector();
  return true;
}

void NetworkLibraryImplStub::DisconnectFromNetwork(const Network* network) {
  // Update the network state here since no network manager in stub impl.
  Network* modify_network = const_cast<Network*>(network);
  modify_network->set_is_active(false);
  modify_network->set_disconnected();
  if (network == active_wifi_)
    active_wifi_ = NULL;
  else if (network == active_cellular_)
    active_cellular_ = NULL;
  else if (network == active_virtual_)
    active_virtual_ = NULL;
  SignalNetworkManagerObservers();
  NotifyNetworkChanged(network);
}

void NetworkLibraryImplStub::EnableOfflineMode(bool enable) {
  if (enable != offline_mode_) {
    offline_mode_ = enable;
    CallEnableNetworkDeviceType(TYPE_WIFI, !enable);
    CallEnableNetworkDeviceType(TYPE_CELLULAR, !enable);
  }
}

NetworkIPConfigVector NetworkLibraryImplStub::GetIPConfigs(
    const std::string& device_path,
    std::string* hardware_address,
    HardwareAddressFormat format) {
  *hardware_address = hardware_address_;
  return ip_configs_;
}

void NetworkLibraryImplStub::SetIPConfig(const NetworkIPConfig& ipconfig) {
    ip_configs_.push_back(ipconfig);
}

}  // namespace chromeos
