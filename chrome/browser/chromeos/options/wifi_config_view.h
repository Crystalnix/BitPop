// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
#pragma once

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/string16.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/options/network_config_view.h"
#include "ui/base/models/combobox_model.h"
#include "views/controls/button/button.h"
#include "views/controls/combobox/combobox.h"
#include "views/controls/textfield/textfield_controller.h"
#include "views/view.h"

namespace views {
class Checkbox;
class ImageButton;
class Label;
}
class FilePath;

namespace chromeos {

class WifiConfigModel;

// A dialog box for showing a password textfield.
class WifiConfigView : public ChildNetworkConfigView,
                       public views::TextfieldController,
                       public views::ButtonListener,
                       public views::Combobox::Listener {
 public:
  // Wifi login dialog for wifi network |wifi|. |wifi| must be a non NULL
  // pointer to a WifiNetwork in NetworkLibrary.
  WifiConfigView(NetworkConfigView* parent, WifiNetwork* wifi);
  // Wifi login dialog for "Joining other network..."
  WifiConfigView(NetworkConfigView* parent, bool show_8021x);
  virtual ~WifiConfigView();

  // views::TextfieldController:
  virtual void ContentsChanged(views::Textfield* sender,
                               const string16& new_contents);
  virtual bool HandleKeyEvent(views::Textfield* sender,
                              const views::KeyEvent& key_event);

  // views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

  // views::Combobox::Listener:
  virtual void ItemChanged(views::Combobox* combo_box,
                           int prev_index, int new_index);

  // ChildNetworkConfigView implementation.
  virtual string16 GetTitle() OVERRIDE;
  virtual bool CanLogin() OVERRIDE;
  virtual bool Login() OVERRIDE;
  virtual void Cancel() OVERRIDE;
  virtual void InitFocus() OVERRIDE;

 private:
  // Initializes UI.  If |show_8021x| includes 802.1x config options.
  void Init(WifiNetwork* wifi, bool show_8021x);

  // Get the typed in SSID.
  std::string GetSsid() const;

  // Get the typed in passphrase.
  std::string GetPassphrase() const;

  // Get various 802.1X EAP values from the widgets.
  EAPMethod GetEapMethod() const;
  EAPPhase2Auth GetEapPhase2Auth() const;
  std::string GetEapServerCaCertNssNickname() const;
  bool GetEapUseSystemCas() const;
  std::string GetEapClientCertPkcs11Id() const;
  std::string GetEapIdentity() const;
  std::string GetEapAnonymousIdentity() const;
  bool GetSaveCredentials() const;

  // Updates state of the Login button.
  void UpdateDialogButtons();

  // Enable/Disable EAP fields as appropriate based on selected EAP method.
  void RefreshEapFields();

  // Updates the error text label.
  void UpdateErrorLabel();

  scoped_ptr<WifiConfigModel> wifi_config_model_;

  views::Textfield* ssid_textfield_;
  views::Combobox* eap_method_combobox_;
  views::Label* phase_2_auth_label_;
  views::Combobox* phase_2_auth_combobox_;
  views::Label* client_cert_label_;
  views::Combobox* client_cert_combobox_;
  views::Label* server_ca_cert_label_;
  views::Combobox* server_ca_cert_combobox_;
  views::Label* identity_label_;
  views::Textfield* identity_textfield_;
  views::Label* identity_anonymous_label_;
  views::Textfield* identity_anonymous_textfield_;
  views::Checkbox* save_credentials_checkbox_;
  views::Combobox* security_combobox_;
  views::Label* passphrase_label_;
  views::Textfield* passphrase_textfield_;
  views::ImageButton* passphrase_visible_button_;
  views::Label* error_label_;

  DISALLOW_COPY_AND_ASSIGN(WifiConfigView);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
