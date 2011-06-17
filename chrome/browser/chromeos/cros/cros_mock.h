// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CROS_CROS_MOCK_H_
#define CHROME_BROWSER_CHROMEOS_CROS_CROS_MOCK_H_

#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/test/in_process_browser_test.h"
#include "third_party/cros/chromeos_input_method.h"

namespace chromeos {

class MockCryptohomeLibrary;
class MockKeyboardLibrary;
class MockInputMethodLibrary;
class MockLibraryLoader;
class MockNetworkLibrary;
class MockPowerLibrary;
class MockScreenLockLibrary;
class MockSpeechSynthesisLibrary;
class MockTouchpadLibrary;

// Class for initializing mocks for some parts of CrosLibrary. Once you mock
// part of CrosLibrary it will be considered as successfully loaded and
// libraries that compose CrosLibrary will be created. CrosMock also defines a
// minimum set of mocks that is used by status area elements (network,
// input language, power).
class CrosMock {
 public:
  CrosMock();
  virtual ~CrosMock();

  // This method sets up basic mocks that are used by status area items:
  // LibraryLoader, Language, Network, Power, Touchpad libraries.
  // Add a call to this method at the beginning of your
  // SetUpInProcessBrowserTestFixture.
  void InitStatusAreaMocks();

  // Initialization of CrosLibrary mock loader. If you intend calling
  // separate init methods for mocks call this one first.
  void InitMockLibraryLoader();

  // Initialization of mocks.
  void InitMockCryptohomeLibrary();
  void InitMockKeyboardLibrary();
  void InitMockInputMethodLibrary();
  void InitMockNetworkLibrary();
  void InitMockPowerLibrary();
  void InitMockScreenLockLibrary();
  void InitMockSpeechSynthesisLibrary();
  void InitMockTouchpadLibrary();

  // Get mocks.
  MockCryptohomeLibrary* mock_cryptohome_library();
  MockKeyboardLibrary* mock_keyboard_library();
  MockInputMethodLibrary* mock_input_method_library();
  MockNetworkLibrary* mock_network_library();
  MockPowerLibrary* mock_power_library();
  MockScreenLockLibrary* mock_screen_lock_library();
  MockSpeechSynthesisLibrary* mock_speech_synthesis_library();
  MockTouchpadLibrary* mock_touchpad_library();

  // This method sets up corresponding expectations for basic mocks that
  // are used by status area items.
  // Make sure that InitStatusAreaMocks was called before.
  // Add a call to this method in your SetUpInProcessBrowserTestFixture.
  // They are all configured with RetiresOnSaturation().
  // Once such expectation is used it won't block expectations you've defined.
  void SetStatusAreaMocksExpectations();

  // Methods to setup minimal mocks expectations for status area.
  void SetKeyboardLibraryStatusAreaExpectations();
  void SetInputMethodLibraryStatusAreaExpectations();
  void SetNetworkLibraryStatusAreaExpectations();
  void SetPowerLibraryStatusAreaExpectations();
  void SetPowerLibraryExpectations();
  void SetSpeechSynthesisLibraryExpectations();
  void SetTouchpadLibraryExpectations();

  void TearDownMocks();

  // Creates input method descriptors. This is a helper function for
  // SetInputMethodLibraryStatusAreaExpectations().
  static InputMethodDescriptors* CreateInputMethodDescriptors();

  // TestApi gives access to CrosLibrary private members.
  chromeos::CrosLibrary::TestApi* test_api();

 private:
  // Mocks, destroyed by CrosLibrary class.
  MockLibraryLoader* loader_;
  MockCryptohomeLibrary* mock_cryptohome_library_;
  MockKeyboardLibrary* mock_keyboard_library_;
  MockInputMethodLibrary* mock_input_method_library_;
  MockNetworkLibrary* mock_network_library_;
  MockPowerLibrary* mock_power_library_;
  MockScreenLockLibrary* mock_screen_lock_library_;
  MockSpeechSynthesisLibrary* mock_speech_synthesis_library_;
  MockTouchpadLibrary* mock_touchpad_library_;

  ImePropertyList ime_properties_;
  InputMethodDescriptor current_input_method_;
  InputMethodDescriptor previous_input_method_;
  WifiNetworkVector wifi_networks_;
  CellularNetworkVector cellular_networks_;
  VirtualNetworkVector virtual_networks_;
  std::string empty_string_;

  DISALLOW_COPY_AND_ASSIGN(CrosMock);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CROS_CROS_MOCK_H_
