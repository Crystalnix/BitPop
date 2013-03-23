// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/cros/onc_network_parser.h"

#include <cert.h>
#include <keyhi.h>
#include <pk11pub.h>

#include "base/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_string_value_serializer.h"
#include "base/lazy_instance.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/cros/certificate_pattern.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/login/mock_user_manager.h"
#include "chrome/browser/net/pref_proxy_config_tracker_impl.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/net/x509_certificate_model.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_pref_service.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/network/onc/onc_constants.h"
#include "chromeos/network/onc/onc_utils.h"
#include "content/public/test/test_browser_thread.h"
#include "crypto/nss_util.h"
#include "net/base/cert_type.h"
#include "net/base/crypto_module.h"
#include "net/base/nss_cert_database.h"
#include "net/base/x509_certificate.h"
#include "net/proxy/proxy_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

using ::testing::AnyNumber;
using ::testing::Return;

namespace chromeos {

namespace {

const char g_token_name[] = "OncNetworkParserTest token";

net::CertType GetCertType(const net::X509Certificate* cert) {
  DCHECK(cert);
  return x509_certificate_model::GetType(cert->os_cert_handle());
}

}  // namespace

class OncNetworkParserTest : public testing::Test {
 public:
  virtual void SetUp() {
    ASSERT_TRUE(test_nssdb_.is_open());

    slot_ = net::NSSCertDatabase::GetInstance()->GetPublicModule();

    // Don't run the test if the setup failed.
    ASSERT_TRUE(slot_->os_module_handle());

    // Test db should be empty at start of test.
    EXPECT_EQ(0U, ListCertsInSlot(slot_->os_module_handle()).size());
  }

  virtual void TearDown() {
    EXPECT_TRUE(CleanupSlotContents(slot_->os_module_handle()));
    EXPECT_EQ(0U, ListCertsInSlot(slot_->os_module_handle()).size());
  }

  virtual scoped_ptr<base::ListValue> ReadNetworkConfigs(
      const std::string& filename) {
      FilePath path;
      PathService::Get(chrome::DIR_TEST_DATA, &path);
      path = path.AppendASCII("chromeos").AppendASCII("cros").Append(filename);
      CHECK(file_util::PathExists(path))
        << "Couldn't find test data file " << path.value();
      std::string contents;
      CHECK(file_util::ReadFileToString(path, &contents))
        << "Unable to read test data file " << path.value();

      scoped_ptr<base::DictionaryValue> root =
          onc::ReadDictionaryFromJson(contents);
      CHECK(root.get() != NULL) << "ONC is not a valid JSON dictionary.";

      base::ListValue* network_configs;
      CHECK(root->GetListWithoutPathExpansion(onc::kNetworkConfigurations,
                                              &network_configs));
      return make_scoped_ptr(network_configs->DeepCopy());
  }

  const base::Value* GetExpectedProperty(const Network* network,
                                         PropertyIndex index,
                                         base::Value::Type expected_type);
  void CheckStringProperty(const Network* network,
                           PropertyIndex index,
                           const char* expected);
  void CheckBooleanProperty(const Network* network,
                            PropertyIndex index,
                            bool expected);

  void TestProxySettings(const std::string proxy_settings_blob,
                         net::ProxyConfig* net_config);

 protected:
  scoped_refptr<net::CryptoModule> slot_;

 private:
  net::CertificateList ListCertsInSlot(PK11SlotInfo* slot) {
    net::CertificateList result;
    CERTCertList* cert_list = PK11_ListCertsInSlot(slot);
    for (CERTCertListNode* node = CERT_LIST_HEAD(cert_list);
         !CERT_LIST_END(node, cert_list);
         node = CERT_LIST_NEXT(node)) {
      result.push_back(net::X509Certificate::CreateFromHandle(
          node->cert, net::X509Certificate::OSCertHandles()));
    }
    CERT_DestroyCertList(cert_list);

    // Sort the result so that test comparisons can be deterministic.
    std::sort(result.begin(), result.end(), net::X509Certificate::LessThan());
    return result;
  }

  bool CleanupSlotContents(PK11SlotInfo* slot) {
    bool ok = true;
    net::CertificateList certs = ListCertsInSlot(slot);
    for (size_t i = 0; i < certs.size(); ++i) {
      if (!net::NSSCertDatabase::GetInstance()->DeleteCertAndKey(certs[i]))
        ok = false;
    }
    return ok;
  }

  ScopedStubCrosEnabler stub_cros_enabler_;
  crypto::ScopedTestNSSDB test_nssdb_;
};

const base::Value* OncNetworkParserTest::GetExpectedProperty(
    const Network* network,
    PropertyIndex index,
    base::Value::Type expected_type) {
  const base::Value* value;
  if (!network->GetProperty(index, &value)) {
    ADD_FAILURE() << "Property " << index << " does not exist";
    return NULL;
  }
  if (!value->IsType(expected_type)) {
    ADD_FAILURE() << "Property " << index << " expected type "
                  << expected_type << " actual type "
                  << value->GetType();
    return NULL;
  }
  return value;
}

void OncNetworkParserTest::CheckStringProperty(const Network* network,
                                               PropertyIndex index,
                                               const char* expected) {
  const base::Value* value =
    GetExpectedProperty(network, index, base::Value::TYPE_STRING);
  if (!value)
    return;
  std::string string_value;
  value->GetAsString(&string_value);
  EXPECT_EQ(expected, string_value);
}

void OncNetworkParserTest::CheckBooleanProperty(const Network* network,
                                                PropertyIndex index,
                                                bool expected) {
  const base::Value* value =
    GetExpectedProperty(network, index, base::Value::TYPE_BOOLEAN);
  if (!value)
    return;
  bool bool_value = false;
  value->GetAsBoolean(&bool_value);
  EXPECT_EQ(expected, bool_value);
}

void OncNetworkParserTest::TestProxySettings(const std::string filename,
                                             net::ProxyConfig* net_config) {
  // Parse Network Configuration including ProxySettings dictionary.
  scoped_ptr<base::ListValue> network_configs = ReadNetworkConfigs(filename);
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);

  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get());
  EXPECT_FALSE(network->proxy_config().empty());

  // Deserialize ProxyConfig string property of Network into
  // ProxyConfigDictionary and decode into net::ProxyConfig.
  JSONStringValueSerializer serializer(network->proxy_config());
  scoped_ptr<Value> value(serializer.Deserialize(NULL, NULL));
  ASSERT_TRUE(value.get());
  EXPECT_TRUE(value->GetType() == Value::TYPE_DICTIONARY);
  DictionaryValue* dict = static_cast<DictionaryValue*>(value.get());
  ProxyConfigDictionary proxy_dict(dict);
  EXPECT_TRUE(PrefProxyConfigTrackerImpl::PrefConfigToNetConfig(proxy_dict,
                                                                net_config));
}

TEST_F(OncNetworkParserTest, TestCreateNetworkWifi) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("network-wifi.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);

  EXPECT_EQ(1, parser.GetNetworkConfigsSize());
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get());

  EXPECT_EQ(chromeos::TYPE_WIFI, network->type());
  WifiNetwork* wifi = static_cast<WifiNetwork*>(network.get());
  EXPECT_EQ(chromeos::SECURITY_WEP, wifi->encryption());
  CheckStringProperty(wifi, PROPERTY_INDEX_SECURITY, flimflam::kSecurityWep);
  EXPECT_EQ("ssid", wifi->name());
  CheckStringProperty(wifi, PROPERTY_INDEX_SSID, "ssid");
  EXPECT_FALSE(wifi->auto_connect());
  EXPECT_EQ("0x1234567890", wifi->passphrase());
  CheckStringProperty(wifi, PROPERTY_INDEX_PASSPHRASE, "0x1234567890");
}

TEST_F(OncNetworkParserTest, TestCreateNetworkEthernet) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("network-ethernet.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);

  EXPECT_GE(parser.GetNetworkConfigsSize(), 1);
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get());

  EXPECT_EQ(chromeos::TYPE_ETHERNET, network->type());
  EthernetNetwork* ethernet = static_cast<EthernetNetwork*>(network.get());
  EXPECT_EQ(ethernet->unique_id(), "{485d6076-dd44-6b6d-69787465725f5045}");
}

TEST_F(OncNetworkParserTest, TestLoadWifiCertificatePattern) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("cert-pattern.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);
  ASSERT_TRUE(parser.parse_error().empty());
  EXPECT_EQ(1, parser.GetNetworkConfigsSize());
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get());

  EXPECT_EQ(chromeos::TYPE_WIFI, network->type());
  WifiNetwork* wifi = static_cast<WifiNetwork*>(network.get());
  EXPECT_EQ(chromeos::SECURITY_8021X, wifi->encryption());
  EXPECT_EQ("WirelessNetwork", wifi->name());
  EXPECT_FALSE(wifi->auto_connect());
  EXPECT_EQ("", wifi->passphrase());
  EXPECT_EQ(chromeos::EAP_METHOD_TLS, wifi->eap_method());
  EXPECT_EQ(chromeos::CLIENT_CERT_TYPE_PATTERN, wifi->client_cert_type());
  EXPECT_EQ("Google, Inc.",
            wifi->client_cert_pattern().issuer().organization());
  ASSERT_EQ(2ul, wifi->client_cert_pattern().enrollment_uri_list().size());
  EXPECT_EQ("http://youtu.be/dQw4w9WgXcQ",
            wifi->client_cert_pattern().enrollment_uri_list()[0]);
  EXPECT_EQ("chrome-extension://abc/keygen-cert.html",
            wifi->client_cert_pattern().enrollment_uri_list()[1]);
}


TEST_F(OncNetworkParserTest, TestLoadVPNCertificatePattern) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("cert-pattern-vpn.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);
  ASSERT_TRUE(parser.parse_error().empty());
  EXPECT_EQ(1, parser.GetNetworkConfigsSize());
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get());

  EXPECT_EQ(chromeos::TYPE_VPN, network->type());
  VirtualNetwork* vpn = static_cast<VirtualNetwork*>(network.get());
  EXPECT_EQ("MyVPN", vpn->name());
  EXPECT_FALSE(vpn->auto_connect());
  EXPECT_EQ(chromeos::CLIENT_CERT_TYPE_PATTERN, vpn->client_cert_type());
  EXPECT_EQ("Google, Inc.",
            vpn->client_cert_pattern().issuer().organization());
  ASSERT_EQ(2ul, vpn->client_cert_pattern().enrollment_uri_list().size());
  EXPECT_EQ("http://youtu.be/dQw4w9WgXcQ",
            vpn->client_cert_pattern().enrollment_uri_list()[0]);
  EXPECT_EQ("chrome-extension://abc/keygen-cert.html",
            vpn->client_cert_pattern().enrollment_uri_list()[1]);
}

TEST_F(OncNetworkParserTest, TestNoCertificatePatternForDevicePolicy) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("cert-pattern.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_DEVICE_POLICY);

  // Make sure we fail when parsing a certificate pattern from a device policy
  // ONC file.
  ASSERT_TRUE(parser.parse_error().empty());
  EXPECT_EQ(1, parser.GetNetworkConfigsSize());
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get());

  EXPECT_EQ(chromeos::TYPE_WIFI, network->type());
  WifiNetwork* wifi = static_cast<WifiNetwork*>(network.get());
  EXPECT_EQ(chromeos::SECURITY_8021X, wifi->encryption());
  EXPECT_EQ("WirelessNetwork", wifi->name());
  EXPECT_FALSE(wifi->auto_connect());
  EXPECT_EQ("", wifi->passphrase());
  EXPECT_EQ(chromeos::EAP_METHOD_TLS, wifi->eap_method());
  EXPECT_EQ(chromeos::CLIENT_CERT_TYPE_PATTERN, wifi->client_cert_type());
  EXPECT_EQ("", wifi->client_cert_pattern().issuer().organization());
  ASSERT_EQ(0ul, wifi->client_cert_pattern().enrollment_uri_list().size());
}

TEST_F(OncNetworkParserTest, TestCreateNetworkWifiEAP1) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("network-wifi-eap1.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);

  EXPECT_EQ(1, parser.GetNetworkConfigsSize());
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get());

  EXPECT_EQ(chromeos::TYPE_WIFI, network->type());
  WifiNetwork* wifi = static_cast<WifiNetwork*>(network.get());
  EXPECT_EQ(chromeos::SECURITY_8021X, wifi->encryption());
  CheckStringProperty(wifi, PROPERTY_INDEX_SECURITY, flimflam::kSecurity8021x);
  EXPECT_EQ("ssid", wifi->name());
  EXPECT_EQ(true, wifi->auto_connect());
  CheckBooleanProperty(wifi, PROPERTY_INDEX_AUTO_CONNECT, true);
  EXPECT_EQ(EAP_METHOD_PEAP, wifi->eap_method());
  CheckStringProperty(wifi, PROPERTY_INDEX_EAP_METHOD,
                      flimflam::kEapMethodPEAP);
  EXPECT_FALSE(wifi->eap_use_system_cas());
}

TEST_F(OncNetworkParserTest, TestCreateNetworkWifiEAP2) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("network-wifi-eap2.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);

  EXPECT_EQ(1, parser.GetNetworkConfigsSize());
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get());

  EXPECT_EQ(chromeos::TYPE_WIFI, network->type());
  WifiNetwork* wifi = static_cast<WifiNetwork*>(network.get());
  EXPECT_EQ(chromeos::SECURITY_8021X, wifi->encryption());
  EXPECT_EQ("ssid", wifi->name());
  EXPECT_FALSE(wifi->auto_connect());
  EXPECT_EQ(EAP_METHOD_LEAP, wifi->eap_method());
  EXPECT_EQ(true, wifi->eap_use_system_cas());
  EXPECT_EQ("user", wifi->eap_identity());
  CheckStringProperty(wifi, PROPERTY_INDEX_EAP_IDENTITY, "user");
  EXPECT_EQ("pass", wifi->eap_passphrase());
  CheckStringProperty(wifi, PROPERTY_INDEX_EAP_PASSWORD, "pass");
  EXPECT_EQ("anon", wifi->eap_anonymous_identity());
  CheckStringProperty(wifi, PROPERTY_INDEX_EAP_ANONYMOUS_IDENTITY, "anon");
}

TEST_F(OncNetworkParserTest, TestCreateNetworkUnknownFields) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("network-unknown-fields.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get());

  EXPECT_EQ(chromeos::TYPE_WIFI, network->type());
  WifiNetwork* wifi = static_cast<WifiNetwork*>(network.get());
  EXPECT_EQ(chromeos::SECURITY_WEP, wifi->encryption());
  EXPECT_EQ("ssid", wifi->name());
  EXPECT_EQ("z123456789012", wifi->passphrase());
}

TEST_F(OncNetworkParserTest, TestCreateNetworkOpenVPN) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("network-openvpn.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);

  EXPECT_EQ(1, parser.GetNetworkConfigsSize());
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network.get() != NULL);

  EXPECT_EQ(chromeos::TYPE_VPN, network->type());
  CheckStringProperty(network.get(), PROPERTY_INDEX_TYPE, flimflam::kTypeVPN);
  VirtualNetwork* vpn = static_cast<VirtualNetwork*>(network.get());
  EXPECT_EQ("MyVPN", vpn->name());
  EXPECT_EQ(PROVIDER_TYPE_OPEN_VPN, vpn->provider_type());
  CheckStringProperty(vpn, PROPERTY_INDEX_PROVIDER_TYPE,
                      flimflam::kProviderOpenVpn);
  EXPECT_EQ("vpn.acme.org", vpn->server_hostname());
  CheckStringProperty(vpn, PROPERTY_INDEX_PROVIDER_HOST, "vpn.acme.org");
  CheckStringProperty(vpn, PROPERTY_INDEX_VPN_DOMAIN, "");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_AUTHRETRY, "interact");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_CACERT,
                      "{55ca78f6-0842-4e1b-96a3-09a9e1a26ef5}");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_COMPLZO, "true");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_KEYDIRECTION, "1");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_PORT, "443");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_PROTO, "udp");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_PUSHPEERINFO, "true");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_REMOTECERTEKU,
                      "TLS Web Server Authentication");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_REMOTECERTKU, "eo");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_REMOTECERTTLS, "server");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_RENEGSEC, "0");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_SERVERPOLLTIMEOUT, "10");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_STATICCHALLENGE,
                      "My static challenge");
  // Check the default properties are set.
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_AUTHUSERPASS, "");
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_MGMT_ENABLE, "");

  std::string tls_auth_contents;
  const Value* tls_auth_value =
    GetExpectedProperty(vpn, PROPERTY_INDEX_OPEN_VPN_TLSAUTHCONTENTS,
                        base::Value::TYPE_STRING);
  if (tls_auth_value != NULL) {
    tls_auth_value->GetAsString(&tls_auth_contents);
    EXPECT_NE(std::string::npos,
              tls_auth_contents.find("END OpenVPN Static key V1-----\n"));
    EXPECT_NE(std::string::npos,
              tls_auth_contents.find(
                  "-----BEGIN OpenVPN Static key V1-----\n"));
  }
  CheckStringProperty(vpn, PROPERTY_INDEX_OPEN_VPN_TLSREMOTE,
                      "MyOpenVPNServer");
  EXPECT_FALSE(vpn->save_credentials());
  EXPECT_EQ("{55ca78f6-0842-4e1b-96a3-09a9e1a26ef5}", vpn->ca_cert_nss());
}

TEST_F(OncNetworkParserTest, TestCreateNetworkL2TPIPsec) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("network-l2tp-ipsec.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);

  EXPECT_EQ(1, parser.GetNetworkConfigsSize());
  scoped_ptr<Network> network(parser.ParseNetwork(0, NULL));
  ASSERT_TRUE(network != NULL);

  EXPECT_EQ(chromeos::TYPE_VPN, network->type());
  CheckStringProperty(network.get(), PROPERTY_INDEX_TYPE, flimflam::kTypeVPN);
  VirtualNetwork* vpn = static_cast<VirtualNetwork*>(network.get());
  EXPECT_EQ("MyL2TPVPN", vpn->name());
  EXPECT_EQ(PROVIDER_TYPE_L2TP_IPSEC_PSK, vpn->provider_type());
  CheckStringProperty(vpn, PROPERTY_INDEX_PROVIDER_TYPE,
                      flimflam::kProviderL2tpIpsec);
  EXPECT_EQ("l2tp.acme.org", vpn->server_hostname());
  CheckStringProperty(vpn, PROPERTY_INDEX_PROVIDER_HOST, "l2tp.acme.org");
  CheckStringProperty(vpn, PROPERTY_INDEX_VPN_DOMAIN, "");
  EXPECT_EQ("passphrase", vpn->psk_passphrase());
  CheckStringProperty(vpn, PROPERTY_INDEX_L2TPIPSEC_PSK, "passphrase");
  CheckStringProperty(vpn, PROPERTY_INDEX_IPSEC_IKEVERSION, "1");
  EXPECT_FALSE(vpn->save_credentials());
}

TEST_F(OncNetworkParserTest, TestProxySettingsDirect) {
  net::ProxyConfig net_config;
  TestProxySettings("network-wifi-proxy-direct.onc", &net_config);
  EXPECT_EQ(net::ProxyConfig::ProxyRules::TYPE_NO_RULES,
            net_config.proxy_rules().type);
  EXPECT_FALSE(net_config.HasAutomaticSettings());
}

TEST_F(OncNetworkParserTest, TestProxySettingsWpad) {
  net::ProxyConfig net_config;
  TestProxySettings("network-wifi-proxy-wpad.onc", &net_config);
  EXPECT_EQ(net::ProxyConfig::ProxyRules::TYPE_NO_RULES,
            net_config.proxy_rules().type);
  EXPECT_TRUE(net_config.HasAutomaticSettings());
  EXPECT_TRUE(net_config.auto_detect());
  EXPECT_FALSE(net_config.has_pac_url());
}

TEST_F(OncNetworkParserTest, TestProxySettingsPac) {
  const std::string kPacUrl("http://proxyconfig.corp.google.com/wpad.dat");
  net::ProxyConfig net_config;
  TestProxySettings("network-wifi-proxy-pac.onc", &net_config);
  EXPECT_EQ(net::ProxyConfig::ProxyRules::TYPE_NO_RULES,
            net_config.proxy_rules().type);
  EXPECT_TRUE(net_config.HasAutomaticSettings());
  EXPECT_FALSE(net_config.auto_detect());
  EXPECT_TRUE(net_config.has_pac_url());
  EXPECT_EQ(GURL(kPacUrl), net_config.pac_url());
}

TEST_F(OncNetworkParserTest, TestProxySettingsManual) {
  const std::string kHttpHost("http.example.com");
  const std::string kHttpsHost("https.example.com");
  const std::string kFtpHost("ftp.example.com");
  const std::string socks_host("socks5://socks.example.com");
  const uint16 kHttpPort = 1234;
  const uint16 kHttpsPort = 3456;
  const uint16 kFtpPort = 5678;
  const uint16 kSocksPort = 7890;

  net::ProxyConfig net_config;
  TestProxySettings("network-wifi-proxy-manual.onc", &net_config);
  const net::ProxyConfig::ProxyRules& rules = net_config.proxy_rules();
  EXPECT_EQ(net::ProxyConfig::ProxyRules::TYPE_PROXY_PER_SCHEME, rules.type);
  // Verify http proxy server.
  EXPECT_TRUE(rules.proxy_for_http.is_valid());
  EXPECT_EQ(rules.proxy_for_http,
            net::ProxyServer(net::ProxyServer::SCHEME_HTTP,
                             net::HostPortPair(kHttpHost, kHttpPort)));
  // Verify https proxy server.
  EXPECT_TRUE(rules.proxy_for_https.is_valid());
  EXPECT_EQ(rules.proxy_for_https,
            net::ProxyServer(net::ProxyServer::SCHEME_HTTP,
                             net::HostPortPair(kHttpsHost, kHttpsPort)));
  // Verify ftp proxy server.
  EXPECT_TRUE(rules.proxy_for_ftp.is_valid());
  EXPECT_EQ(rules.proxy_for_ftp,
            net::ProxyServer(net::ProxyServer::SCHEME_HTTP,
                             net::HostPortPair(kFtpHost, kFtpPort)));
  // Verify socks server.
  EXPECT_TRUE(rules.fallback_proxy.is_valid());
  EXPECT_EQ(rules.fallback_proxy,
            net::ProxyServer(net::ProxyServer::SCHEME_SOCKS5,
                             net::HostPortPair(socks_host, kSocksPort)));
  // Verify bypass rules.
  net::ProxyBypassRules expected_bypass_rules;
  expected_bypass_rules.AddRuleFromString("google.com");
  expected_bypass_rules.AddRuleToBypassLocal();
  EXPECT_TRUE(expected_bypass_rules.Equals(rules.bypass_rules));
}

TEST(OncNetworkParserUserExpansionTest, GetUserExpandedValue) {
  ScopedMockUserManagerEnabler mock_user_manager;
  mock_user_manager.user_manager()->SetLoggedInUser("onc@example.com");

  EXPECT_CALL(*mock_user_manager.user_manager(), IsUserLoggedIn())
      .Times(2)
      .WillRepeatedly(Return(false));

  onc::ONCSource source = onc::ONC_SOURCE_USER_IMPORT;

  // Setup environment needed by UserManager.
  MessageLoop loop;
  content::TestBrowserThread ui_thread(content::BrowserThread::UI, &loop);
  base::ShadowingAtExitManager at_exit_manager;
  ScopedTestingLocalState local_state(
      static_cast<TestingBrowserProcess*>(g_browser_process));

  base::StringValue login_id_pattern("a ${LOGIN_ID} b");
  base::StringValue login_email_pattern("a ${LOGIN_EMAIL} b");

  // No expansion if there is no user logged in.
  EXPECT_EQ("a ${LOGIN_ID} b",
            chromeos::OncNetworkParser::GetUserExpandedValue(
                login_id_pattern, source));
  EXPECT_EQ("a ${LOGIN_EMAIL} b",
            chromeos::OncNetworkParser::GetUserExpandedValue(
                login_email_pattern, source));

  // Log in a user and check that the expansions work as expected.
  EXPECT_CALL(*mock_user_manager.user_manager(), IsUserLoggedIn())
      .Times(2)
      .WillRepeatedly(Return(true));

  EXPECT_EQ("a onc b",
            chromeos::OncNetworkParser::GetUserExpandedValue(
                login_id_pattern, source));
  EXPECT_EQ("a onc@example.com b",
            chromeos::OncNetworkParser::GetUserExpandedValue(
                login_email_pattern, source));
}

TEST_F(OncNetworkParserTest, TestRemoveNetworkWifi) {
  scoped_ptr<base::ListValue> network_configs =
      ReadNetworkConfigs("network-wifi-remove.onc");
  OncNetworkParser parser(*network_configs,
                          onc::ONC_SOURCE_USER_IMPORT);
  EXPECT_EQ(1, parser.GetNetworkConfigsSize());
  bool marked_for_removal = false;
  scoped_ptr<Network> network(parser.ParseNetwork(0, &marked_for_removal));

  EXPECT_TRUE(marked_for_removal);
  EXPECT_EQ("{485d6076-dd44-6b6d-69787465725f5045}", network->unique_id());
}

}  // namespace chromeos
