// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/values.h"
#include "chromeos/dbus/flimflam_client_unittest_base.h"
#include "chromeos/dbus/flimflam_network_client.h"
#include "dbus/message.h"
#include "dbus/values_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

const char kExampleNetworkPath[] = "/foo/bar";

}  // namespace

class FlimflamNetworkClientTest : public FlimflamClientUnittestBase {
 public:
  FlimflamNetworkClientTest()
      : FlimflamClientUnittestBase(
          flimflam::kFlimflamNetworkInterface,
          dbus::ObjectPath(kExampleNetworkPath)) {
  }

  virtual void SetUp() {
    FlimflamClientUnittestBase::SetUp();
    // Create a client with the mock bus.
    client_.reset(FlimflamNetworkClient::Create(REAL_DBUS_CLIENT_IMPLEMENTATION,
                                                mock_bus_));
    // Run the message loop to run the signal connection result callback.
    message_loop_.RunAllPending();
  }

  virtual void TearDown() {
    FlimflamClientUnittestBase::TearDown();
  }

 protected:
  scoped_ptr<FlimflamNetworkClient> client_;
};

TEST_F(FlimflamNetworkClientTest, PropertyChanged) {
  // Create a signal.
  const base::FundamentalValue kConnected(true);
  dbus::Signal signal(flimflam::kFlimflamNetworkInterface,
                      flimflam::kMonitorPropertyChanged);
  dbus::MessageWriter writer(&signal);
  writer.AppendString(flimflam::kConnectedProperty);
  dbus::AppendBasicTypeValueDataAsVariant(&writer, kConnected);

  // Set expectations.
  client_->SetPropertyChangedHandler(dbus::ObjectPath(kExampleNetworkPath),
                                     base::Bind(&ExpectPropertyChanged,
                                                flimflam::kConnectedProperty,
                                                &kConnected));
  // Run the signal callback.
  SendPropertyChangedSignal(&signal);

  // Reset the handler.
  client_->ResetPropertyChangedHandler(dbus::ObjectPath(kExampleNetworkPath));
}

TEST_F(FlimflamNetworkClientTest, GetProperties) {
  const char kAddress[] = "address";
  const char kName[] = "name";
  const uint8 kSignalStrength = 1;
  const uint32 kWifiChannel = 1;
  const bool kConnected = true;

  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  dbus::MessageWriter array_writer(NULL);
  writer.OpenArray("{sv}", &array_writer);
  dbus::MessageWriter entry_writer(NULL);
  // Append address.
  array_writer.OpenDictEntry(&entry_writer);
  entry_writer.AppendString(flimflam::kAddressProperty);
  entry_writer.AppendVariantOfString(kAddress);
  array_writer.CloseContainer(&entry_writer);
  // Append name.
  array_writer.OpenDictEntry(&entry_writer);
  entry_writer.AppendString(flimflam::kNameProperty);
  entry_writer.AppendVariantOfString(kName);
  array_writer.CloseContainer(&entry_writer);
  // Append signal strength.
  array_writer.OpenDictEntry(&entry_writer);
  entry_writer.AppendString(flimflam::kSignalStrengthProperty);
  entry_writer.AppendVariantOfByte(kSignalStrength);
  array_writer.CloseContainer(&entry_writer);
  // Append Wifi channel.
  array_writer.OpenDictEntry(&entry_writer);
  entry_writer.AppendString(flimflam::kWifiChannelProperty);
  entry_writer.AppendVariantOfUint32(kWifiChannel);
  array_writer.CloseContainer(&entry_writer);
  // Append connected.
  array_writer.OpenDictEntry(&entry_writer);
  entry_writer.AppendString(flimflam::kConnectedProperty);
  entry_writer.AppendVariantOfBool(kConnected);
  array_writer.CloseContainer(&entry_writer);
  writer.CloseContainer(&array_writer);

  // Create the expected value.
  base::DictionaryValue value;
  value.SetWithoutPathExpansion(flimflam::kAddressProperty,
                                base::Value::CreateStringValue(kAddress));
  value.SetWithoutPathExpansion(flimflam::kNameProperty,
                                base::Value::CreateStringValue(kName));
  value.SetWithoutPathExpansion(
      flimflam::kSignalStrengthProperty,
      base::Value::CreateIntegerValue(kSignalStrength));
  // WiFi.Channel is set as a double because uint32 is larger than int32.
  value.SetWithoutPathExpansion(flimflam::kWifiChannelProperty,
                                base::Value::CreateDoubleValue(kWifiChannel));
  value.SetWithoutPathExpansion(flimflam::kConnectedProperty,
                                base::Value::CreateBooleanValue(kConnected));

  // Set expectations.
  PrepareForMethodCall(flimflam::kGetPropertiesFunction,
                       base::Bind(&ExpectNoArgument),
                       response.get());
  // Call method.
  client_->GetProperties(dbus::ObjectPath(kExampleNetworkPath),
                         base::Bind(&ExpectDictionaryValueResult, &value));
  // Run the message loop.
  message_loop_.RunAllPending();
}

TEST_F(FlimflamNetworkClientTest, CallGetPropertiesAndBlock) {
  const char kName[] = "name";

  // Create response.
  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  dbus::MessageWriter array_writer(NULL);
  writer.OpenArray("{sv}", &array_writer);
  dbus::MessageWriter entry_writer(NULL);
  array_writer.OpenDictEntry(&entry_writer);
  entry_writer.AppendString(flimflam::kNameProperty);
  entry_writer.AppendVariantOfString(kName);
  array_writer.CloseContainer(&entry_writer);
  writer.CloseContainer(&array_writer);

  // Create the expected value.
  base::DictionaryValue value;
  value.SetWithoutPathExpansion(flimflam::kNameProperty,
                                base::Value::CreateStringValue(kName));

  // Set expectations.
  PrepareForMethodCall(flimflam::kGetPropertiesFunction,
                       base::Bind(&ExpectNoArgument),
                       response.get());
  // Call method.
  scoped_ptr<base::DictionaryValue> result(
      client_->CallGetPropertiesAndBlock(
          dbus::ObjectPath(kExampleNetworkPath)));

  ASSERT_TRUE(result.get());
  EXPECT_TRUE(result->Equals(&value));
}

}  // namespace chromeos
