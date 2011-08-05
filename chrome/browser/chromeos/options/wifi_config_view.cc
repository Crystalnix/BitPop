// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/options/wifi_config_view.h"

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/options/wifi_config_model.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "views/controls/button/checkbox.h"
#include "views/controls/button/image_button.h"
#include "views/controls/label.h"
#include "views/controls/textfield/textfield.h"
#include "views/layout/grid_layout.h"
#include "views/layout/layout_constants.h"
#include "views/window/window.h"

namespace chromeos {

namespace {

// Returns true if network is known to require 802.1x.
bool Is8021x(const WifiNetwork* wifi) {
  return wifi && wifi->encrypted() && wifi->encryption() == SECURITY_8021X;
}

enum SecurityComboboxIndex {
  SECURITY_INDEX_NONE  = 0,
  SECURITY_INDEX_WEP   = 1,
  SECURITY_INDEX_PSK   = 2,
  SECURITY_INDEX_COUNT = 3
};

class SecurityComboboxModel : public ui::ComboboxModel {
 public:
  SecurityComboboxModel() {}
  virtual ~SecurityComboboxModel() {}
  virtual int GetItemCount() {
    return SECURITY_INDEX_COUNT;
  }
  virtual string16 GetItemAt(int index) {
    if (index == SECURITY_INDEX_NONE)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_NONE);
    else if (index == SECURITY_INDEX_WEP)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_WEP);
    else if (index == SECURITY_INDEX_PSK)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_PSK);
    NOTREACHED();
    return string16();
  }
 private:
  DISALLOW_COPY_AND_ASSIGN(SecurityComboboxModel);
};

// Methods in alphabetical order.
enum EAPMethodComboboxIndex {
  EAP_METHOD_INDEX_NONE  = 0,
  EAP_METHOD_INDEX_LEAP  = 1,
  EAP_METHOD_INDEX_PEAP  = 2,
  EAP_METHOD_INDEX_TLS   = 3,
  EAP_METHOD_INDEX_TTLS  = 4,
  EAP_METHOD_INDEX_COUNT = 5
};

class EAPMethodComboboxModel : public ui::ComboboxModel {
 public:
  EAPMethodComboboxModel() {}
  virtual ~EAPMethodComboboxModel() {}
  virtual int GetItemCount() {
    return EAP_METHOD_INDEX_COUNT;
  }
  virtual string16 GetItemAt(int index) {
    if (index == EAP_METHOD_INDEX_NONE)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_NONE);
    else if (index == EAP_METHOD_INDEX_LEAP)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_LEAP);
    else if (index == EAP_METHOD_INDEX_PEAP)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_PEAP);
    else if (index == EAP_METHOD_INDEX_TLS)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_TLS);
    else if (index == EAP_METHOD_INDEX_TTLS)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD_TTLS);
    NOTREACHED();
    return string16();
  }
 private:
  DISALLOW_COPY_AND_ASSIGN(EAPMethodComboboxModel);
};

enum Phase2AuthComboboxIndex {
  PHASE_2_AUTH_INDEX_AUTO     = 0, // LEAP, EAP-TLS have only this auth.
  PHASE_2_AUTH_INDEX_MD5      = 1,
  PHASE_2_AUTH_INDEX_MSCHAPV2 = 2, // PEAP has up to this auth.
  PHASE_2_AUTH_INDEX_MSCHAP   = 3,
  PHASE_2_AUTH_INDEX_PAP      = 4,
  PHASE_2_AUTH_INDEX_CHAP     = 5, // EAP-TTLS has up to this auth.
  PHASE_2_AUTH_INDEX_COUNT    = 6
};

class Phase2AuthComboboxModel : public ui::ComboboxModel {
 public:
  explicit Phase2AuthComboboxModel(views::Combobox* eap_method_combobox)
      : eap_method_combobox_(eap_method_combobox) {}
  virtual ~Phase2AuthComboboxModel() {}
  virtual int GetItemCount() {
    switch (eap_method_combobox_->selected_item()) {
      case EAP_METHOD_INDEX_NONE:
      case EAP_METHOD_INDEX_TLS:
      case EAP_METHOD_INDEX_LEAP:
        return PHASE_2_AUTH_INDEX_AUTO + 1;
      case EAP_METHOD_INDEX_PEAP:
        return PHASE_2_AUTH_INDEX_MSCHAPV2 + 1;
      case EAP_METHOD_INDEX_TTLS:
        return PHASE_2_AUTH_INDEX_CHAP + 1;
    }
    NOTREACHED();
    return 0;
  }
  virtual string16 GetItemAt(int index) {
    if (index == PHASE_2_AUTH_INDEX_AUTO)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_AUTO);
    else if (index == PHASE_2_AUTH_INDEX_MD5)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_MD5);
    else if (index == PHASE_2_AUTH_INDEX_MSCHAPV2)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_MSCHAPV2);
    else if (index == PHASE_2_AUTH_INDEX_MSCHAP)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_MSCHAP);
    else if (index == PHASE_2_AUTH_INDEX_PAP)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_PAP);
    else if (index == PHASE_2_AUTH_INDEX_CHAP)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH_CHAP);
    NOTREACHED();
    return string16();
  }
 private:
  views::Combobox* eap_method_combobox_;
  DISALLOW_COPY_AND_ASSIGN(Phase2AuthComboboxModel);
};

// Combobox that supports a preferred width.  Used by Server CA combobox
// because the strings inside it are too wide.
class ComboboxWithWidth : public views::Combobox {
 public:
  ComboboxWithWidth(ui::ComboboxModel* model, int width)
      : Combobox(model),
        width_(width) {
  }
  virtual ~ComboboxWithWidth() {}
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    gfx::Size size = Combobox::GetPreferredSize();
    size.set_width(width_);
    return size;
  }
 private:
  int width_;
  DISALLOW_COPY_AND_ASSIGN(ComboboxWithWidth);
};

class ServerCACertComboboxModel : public ui::ComboboxModel {
 public:
  explicit ServerCACertComboboxModel(WifiConfigModel* wifi_config_model)
      : wifi_config_model_(wifi_config_model) {
  }
  virtual ~ServerCACertComboboxModel() {}
  virtual int GetItemCount() {
    // First "Default", then the certs, then "Do not check".
    return wifi_config_model_->GetServerCaCertCount() + 2;
  }
  virtual string16 GetItemAt(int combo_index) {
    if (combo_index == 0)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_SERVER_CA_DEFAULT);
    if (combo_index == GetItemCount() - 1)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_SERVER_CA_DO_NOT_CHECK);
    int cert_index = combo_index - 1;
    return wifi_config_model_->GetServerCaCertName(cert_index);
  }
 private:
  WifiConfigModel* wifi_config_model_;
  DISALLOW_COPY_AND_ASSIGN(ServerCACertComboboxModel);
};

class ClientCertComboboxModel : public ui::ComboboxModel {
 public:
  explicit ClientCertComboboxModel(WifiConfigModel* wifi_config_model)
      : wifi_config_model_(wifi_config_model) {
  }
  virtual ~ClientCertComboboxModel() {}
  virtual int GetItemCount() {
    // One initial item "None", then the certs.
    return 1 + wifi_config_model_->GetUserCertCount();
  }
  virtual string16 GetItemAt(int combo_index) {
    if (combo_index == 0)
      return l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_NONE);
    int cert_index = combo_index - 1;
    return wifi_config_model_->GetUserCertName(cert_index);
  }
 private:
  WifiConfigModel* wifi_config_model_;
  DISALLOW_COPY_AND_ASSIGN(ClientCertComboboxModel);
};

}  // namespace

WifiConfigView::WifiConfigView(NetworkConfigView* parent, WifiNetwork* wifi)
    : ChildNetworkConfigView(parent, wifi),
      wifi_config_model_(new WifiConfigModel()),
      ssid_textfield_(NULL),
      eap_method_combobox_(NULL),
      phase_2_auth_label_(NULL),
      phase_2_auth_combobox_(NULL),
      client_cert_label_(NULL),
      client_cert_combobox_(NULL),
      server_ca_cert_label_(NULL),
      server_ca_cert_combobox_(NULL),
      identity_label_(NULL),
      identity_textfield_(NULL),
      identity_anonymous_label_(NULL),
      identity_anonymous_textfield_(NULL),
      save_credentials_checkbox_(NULL),
      security_combobox_(NULL),
      passphrase_label_(NULL),
      passphrase_textfield_(NULL),
      passphrase_visible_button_(NULL),
      error_label_(NULL) {
  Init(wifi, Is8021x(wifi));
}

WifiConfigView::WifiConfigView(NetworkConfigView* parent, bool show_8021x)
    : ChildNetworkConfigView(parent),
      wifi_config_model_(new WifiConfigModel()),
      ssid_textfield_(NULL),
      eap_method_combobox_(NULL),
      phase_2_auth_label_(NULL),
      phase_2_auth_combobox_(NULL),
      client_cert_label_(NULL),
      client_cert_combobox_(NULL),
      server_ca_cert_label_(NULL),
      server_ca_cert_combobox_(NULL),
      identity_label_(NULL),
      identity_textfield_(NULL),
      identity_anonymous_label_(NULL),
      identity_anonymous_textfield_(NULL),
      save_credentials_checkbox_(NULL),
      security_combobox_(NULL),
      passphrase_label_(NULL),
      passphrase_textfield_(NULL),
      passphrase_visible_button_(NULL),
      error_label_(NULL) {
  Init(NULL, show_8021x);
}

WifiConfigView::~WifiConfigView() {
}

string16 WifiConfigView::GetTitle() {
  return l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_JOIN_WIFI_NETWORKS);
}

bool WifiConfigView::CanLogin() {
  static const size_t kMinWirelessPasswordLen = 5;

  // We either have an existing wifi network or the user entered an SSID.
  if (service_path_.empty() && GetSsid().empty())
    return false;

  // If the network requires a passphrase, make sure it is the right length.
  if (passphrase_textfield_ != NULL
      && passphrase_textfield_->IsEnabled()
      && passphrase_textfield_->text().length() < kMinWirelessPasswordLen)
    return false;

  // If we're using EAP, we must have a method.
  if (eap_method_combobox_
      && eap_method_combobox_->IsEnabled()
      && eap_method_combobox_->selected_item() == EAP_METHOD_INDEX_NONE)
    return false;

  return true;
}

void WifiConfigView::UpdateDialogButtons() {
  parent_->GetDialogClientView()->UpdateDialogButtons();
}

void WifiConfigView::RefreshEapFields() {
  int selected = eap_method_combobox_->selected_item();

  // If EAP method changes, the phase 2 auth choices may have changed also.
  phase_2_auth_combobox_->ModelChanged();
  phase_2_auth_combobox_->SetSelectedItem(0);
  phase_2_auth_combobox_->SetEnabled(
      phase_2_auth_combobox_->model()->GetItemCount() > 1);
  phase_2_auth_label_->SetEnabled(phase_2_auth_combobox_->IsEnabled());

  // No password for EAP-TLS
  passphrase_textfield_->SetEnabled(selected != EAP_METHOD_INDEX_NONE &&
                                    selected != EAP_METHOD_INDEX_TLS);
  passphrase_label_->SetEnabled(passphrase_textfield_->IsEnabled());
  if (!passphrase_textfield_->IsEnabled())
    passphrase_textfield_->SetText(string16());

  // Client certs only for EAP-TLS
  if (client_cert_combobox_) {
    client_cert_combobox_->SetEnabled(selected == EAP_METHOD_INDEX_TLS);
    client_cert_label_->SetEnabled(client_cert_combobox_->IsEnabled());
  }

  // No server CA certs for LEAP
  server_ca_cert_combobox_->SetEnabled(selected != EAP_METHOD_INDEX_NONE &&
                                       selected != EAP_METHOD_INDEX_LEAP);
  server_ca_cert_label_->SetEnabled(server_ca_cert_combobox_->IsEnabled());

  // No anonymous identity if no phase 2 auth.
  identity_anonymous_textfield_->SetEnabled(
      phase_2_auth_combobox_->IsEnabled());
  identity_anonymous_label_->SetEnabled(
      identity_anonymous_textfield_->IsEnabled());
  if (!identity_anonymous_textfield_->IsEnabled())
    identity_anonymous_textfield_->SetText(string16());
}

void WifiConfigView::UpdateErrorLabel() {
  std::string error_msg;
  if (!service_path_.empty()) {
    NetworkLibrary* cros = CrosLibrary::Get()->GetNetworkLibrary();
    const WifiNetwork* wifi = cros->FindWifiNetworkByPath(service_path_);
    if (wifi && wifi->failed()) {
      bool passphrase_empty = wifi->GetPassphrase().empty();
      switch (wifi->error()) {
        case ERROR_BAD_PASSPHRASE:
          if (!passphrase_empty) {
            error_msg = l10n_util::GetStringUTF8(
                IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_BAD_PASSPHRASE);
          }
          break;
        case ERROR_BAD_WEPKEY:
          if (!passphrase_empty) {
            error_msg = l10n_util::GetStringUTF8(
                IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_BAD_WEPKEY);
          }
          break;
        default:
          error_msg = wifi->GetErrorString();
          break;
      }
    }
  }
  if (!error_msg.empty()) {
    error_label_->SetText(UTF8ToWide(error_msg));
    error_label_->SetVisible(true);
  } else {
    error_label_->SetVisible(false);
  }
}

void WifiConfigView::ContentsChanged(views::Textfield* sender,
                                     const string16& new_contents) {
  UpdateDialogButtons();
}

bool WifiConfigView::HandleKeyEvent(views::Textfield* sender,
                                    const views::KeyEvent& key_event) {
  if (sender == passphrase_textfield_ &&
      key_event.key_code() == ui::VKEY_RETURN) {
    parent_->GetDialogClientView()->AcceptWindow();
  }
  return false;
}

void WifiConfigView::ButtonPressed(views::Button* sender,
                                   const views::Event& event) {
  if (sender == passphrase_visible_button_) {
    if (passphrase_textfield_)
      passphrase_textfield_->SetPassword(!passphrase_textfield_->IsPassword());
  } else {
    NOTREACHED();
  }
}

void WifiConfigView::ItemChanged(views::Combobox* combo_box,
                                 int prev_index, int new_index) {
  if (new_index == prev_index)
    return;
  if (combo_box == security_combobox_) {
    // If changed to no security, then disable combobox and clear it.
    // Otherwise, enable it. Also, update can login.
    if (new_index == SECURITY_INDEX_NONE) {
      passphrase_label_->SetEnabled(false);
      passphrase_textfield_->SetEnabled(false);
      passphrase_textfield_->SetText(string16());
    } else {
      passphrase_label_->SetEnabled(true);
      passphrase_textfield_->SetEnabled(true);
    }
  } else if (combo_box == eap_method_combobox_) {
    RefreshEapFields();
  }
  UpdateDialogButtons();
}

bool WifiConfigView::Login() {
  NetworkLibrary* cros = CrosLibrary::Get()->GetNetworkLibrary();
  if (service_path_.empty()) {
    if (!eap_method_combobox_) {
      // Hidden ordinary Wi-Fi connection.
      ConnectionSecurity sec = SECURITY_UNKNOWN;
      switch (security_combobox_->selected_item()) {
        case SECURITY_INDEX_NONE:
          sec = SECURITY_NONE;
          break;
        case SECURITY_INDEX_WEP:
          sec = SECURITY_WEP;
          break;
        case SECURITY_INDEX_PSK:
          sec = SECURITY_PSK;
          break;
      }
      cros->ConnectToWifiNetwork(GetSsid(), sec, GetPassphrase());
    } else {
      // Hidden 802.1X EAP Wi-Fi connection.
      cros->ConnectToWifiNetwork8021x(
          GetSsid(), GetEapMethod(), GetEapPhase2Auth(),
          GetEapServerCaCertNssNickname(), GetEapUseSystemCas(),
          GetEapClientCertPkcs11Id(), GetEapIdentity(),
          GetEapAnonymousIdentity(), GetPassphrase(),
          GetSaveCredentials());
    }
  } else {
    WifiNetwork* wifi = cros->FindWifiNetworkByPath(service_path_);
    if (!wifi) {
      // Flimflam no longer knows about this wifi network (edge case).
      // TODO(stevenjb): Add a notification (chromium-os13225).
      LOG(WARNING) << "Wifi network: " << service_path_ << " no longer exists.";
      return true;
    }
    if (eap_method_combobox_) {
      // Visible 802.1X EAP Wi-Fi connection.
      EAPMethod method = GetEapMethod();
      DCHECK(method != EAP_METHOD_UNKNOWN);
      wifi->SetEAPMethod(method);
      wifi->SetEAPPhase2Auth(GetEapPhase2Auth());
      wifi->SetEAPServerCaCertNssNickname(GetEapServerCaCertNssNickname());
      wifi->SetEAPUseSystemCAs(GetEapUseSystemCas());
      wifi->SetEAPClientCertPkcs11Id(GetEapClientCertPkcs11Id());
      wifi->SetEAPIdentity(GetEapIdentity());
      wifi->SetEAPAnonymousIdentity(GetEapAnonymousIdentity());
      wifi->SetEAPPassphrase(GetPassphrase());
      wifi->SetSaveCredentials(GetSaveCredentials());
    } else {
      // Visible ordinary Wi-Fi connection.
      const std::string passphrase = GetPassphrase();
      if (passphrase != wifi->passphrase())
        wifi->SetPassphrase(passphrase);
    }

    cros->ConnectToWifiNetwork(wifi);
    // Connection failures are responsible for updating the UI, including
    // reopening dialogs.
  }
  return true;  // dialog will be closed
}

EAPMethod WifiConfigView::GetEapMethod() const {
  DCHECK(eap_method_combobox_);
  switch (eap_method_combobox_->selected_item()) {
    case EAP_METHOD_INDEX_NONE:
      return EAP_METHOD_UNKNOWN;
    case EAP_METHOD_INDEX_PEAP:
      return EAP_METHOD_PEAP;
    case EAP_METHOD_INDEX_TLS:
      return EAP_METHOD_TLS;
    case EAP_METHOD_INDEX_TTLS:
      return EAP_METHOD_TTLS;
    case EAP_METHOD_INDEX_LEAP:
      return EAP_METHOD_LEAP;
    default:
      return EAP_METHOD_UNKNOWN;
  }
}

EAPPhase2Auth WifiConfigView::GetEapPhase2Auth() const {
  DCHECK(phase_2_auth_combobox_);
  switch (phase_2_auth_combobox_->selected_item()) {
    case PHASE_2_AUTH_INDEX_AUTO:
      return EAP_PHASE_2_AUTH_AUTO;
    case PHASE_2_AUTH_INDEX_MD5:
      return EAP_PHASE_2_AUTH_MD5;
    case PHASE_2_AUTH_INDEX_MSCHAPV2:
      return EAP_PHASE_2_AUTH_MSCHAPV2;
    case PHASE_2_AUTH_INDEX_MSCHAP:
      return EAP_PHASE_2_AUTH_MSCHAP;
    case PHASE_2_AUTH_INDEX_PAP:
      return EAP_PHASE_2_AUTH_PAP;
    case PHASE_2_AUTH_INDEX_CHAP:
      return EAP_PHASE_2_AUTH_CHAP;
    default:
      return EAP_PHASE_2_AUTH_AUTO;
  }
}

std::string WifiConfigView::GetEapServerCaCertNssNickname() const {
  DCHECK(server_ca_cert_combobox_);
  int selected = server_ca_cert_combobox_->selected_item();
  if (selected == 0) {
    // First item is "Default".
    return std::string();
  } else if (selected ==
      server_ca_cert_combobox_->model()->GetItemCount() - 1) {
    // Last item is "Do not check".
    return std::string();
  } else {
    int cert_index = selected - 1;
    return wifi_config_model_->GetServerCaCertNssNickname(cert_index);
  }
}

bool WifiConfigView::GetEapUseSystemCas() const {
  DCHECK(server_ca_cert_combobox_);
  // Only use system CAs if the first item ("Default") is selected.
  return server_ca_cert_combobox_->selected_item() == 0;
}

std::string WifiConfigView::GetEapClientCertPkcs11Id() const {
  DCHECK(client_cert_combobox_);
  int selected = client_cert_combobox_->selected_item();
  if (selected == 0) {
    // First item is "None".
    return std::string();
  } else {
    // Otherwise certificates are in a list.
    int cert_index = selected - 1;
    return wifi_config_model_->GetUserCertPkcs11Id(cert_index);
  }
}

std::string WifiConfigView::GetEapIdentity() const {
  DCHECK(identity_textfield_);
  return UTF16ToUTF8(identity_textfield_->text());
}

std::string WifiConfigView::GetEapAnonymousIdentity() const {
  DCHECK(identity_anonymous_textfield_);
  return UTF16ToUTF8(identity_anonymous_textfield_->text());
}

bool WifiConfigView::GetSaveCredentials() const {
  DCHECK(save_credentials_checkbox_);
  return save_credentials_checkbox_->checked();
}

void WifiConfigView::Cancel() {
}

std::string WifiConfigView::GetSsid() const {
  std::string result;
  if (ssid_textfield_ != NULL) {
    std::string untrimmed = UTF16ToUTF8(ssid_textfield_->text());
    TrimWhitespaceASCII(untrimmed, TRIM_ALL, &result);
  }
  return result;
}

std::string WifiConfigView::GetPassphrase() const {
  std::string result;
  if (passphrase_textfield_ != NULL)
    result = UTF16ToUTF8(passphrase_textfield_->text());
  return result;
}

// This will initialize the view depending on if we have a wifi network or not.
// And if we are doing simple password encryption or the more complicated
// 802.1x encryption.
// If we are creating the "Join other network..." dialog, we will allow user
// to enter the data. And if they select the 802.1x encryption, we will show
// the 802.1x fields.
void WifiConfigView::Init(WifiNetwork* wifi, bool show_8021x) {
  views::GridLayout* layout = views::GridLayout::CreatePanel(this);
  SetLayoutManager(layout);

  int column_view_set_id = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(column_view_set_id);
  // Label
  column_set->AddColumn(views::GridLayout::LEADING, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0, 0);
  // Textfield
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0,
                        ChildNetworkConfigView::kPassphraseWidth);
  // Password visible button
  column_set->AddColumn(views::GridLayout::CENTER, views::GridLayout::FILL, 1,
                        views::GridLayout::USE_PREF, 0, 0);

  // SSID input
  layout->StartRow(0, column_view_set_id);
  layout->AddView(new views::Label(UTF16ToWide(l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_ID))));
  if (!wifi) {
    ssid_textfield_ = new views::Textfield(views::Textfield::STYLE_DEFAULT);
    ssid_textfield_->SetController(this);
    ssid_textfield_->SetAccessibleName(l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_ID));
    layout->AddView(ssid_textfield_);
  } else {
    views::Label* label = new views::Label(UTF8ToWide(wifi->name()));
    label->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
    layout->AddView(label);
  }
  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  // Security select
  if (!wifi && !show_8021x) {
    layout->StartRow(0, column_view_set_id);
    layout->AddView(new views::Label(UTF16ToWide(l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY))));
    security_combobox_ = new views::Combobox(new SecurityComboboxModel());
    security_combobox_->set_listener(this);
    layout->AddView(security_combobox_);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
  }

  if (show_8021x) {
    // Only enumerate certificates in the data model for 802.1X networks.
    wifi_config_model_->UpdateCertificates();

    // EAP method
    layout->StartRow(0, column_view_set_id);
    layout->AddView(new views::Label(UTF16ToWide(l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EAP_METHOD))));
    eap_method_combobox_ = new views::Combobox(new EAPMethodComboboxModel());
    eap_method_combobox_->set_listener(this);
    layout->AddView(eap_method_combobox_);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    // Phase 2 authentication
    layout->StartRow(0, column_view_set_id);
    phase_2_auth_label_ =
        new views::Label(UTF16ToWide(l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PHASE_2_AUTH)));
    layout->AddView(phase_2_auth_label_);
    phase_2_auth_combobox_ = new views::Combobox(
        new Phase2AuthComboboxModel(eap_method_combobox_));
    phase_2_auth_label_->SetEnabled(false);
    phase_2_auth_combobox_->SetEnabled(false);
    phase_2_auth_combobox_->set_listener(this);
    layout->AddView(phase_2_auth_combobox_);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    // Server CA certificate
    layout->StartRow(0, column_view_set_id);
    server_ca_cert_label_ =
        new views::Label(UTF16ToWide(l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_SERVER_CA)));
    layout->AddView(server_ca_cert_label_);
    server_ca_cert_combobox_ = new ComboboxWithWidth(
        new ServerCACertComboboxModel(wifi_config_model_.get()),
        ChildNetworkConfigView::kPassphraseWidth);
    server_ca_cert_label_->SetEnabled(false);
    server_ca_cert_combobox_->SetEnabled(false);
    server_ca_cert_combobox_->set_listener(this);
    layout->AddView(server_ca_cert_combobox_);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    // Client certificate
    layout->StartRow(0, column_view_set_id);
    client_cert_label_ = new views::Label(
        UTF16ToWide(l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT)));
    layout->AddView(client_cert_label_);
    client_cert_combobox_ = new views::Combobox(
        new ClientCertComboboxModel(wifi_config_model_.get()));
    client_cert_label_->SetEnabled(false);
    client_cert_combobox_->SetEnabled(false);
    client_cert_combobox_->set_listener(this);
    layout->AddView(client_cert_combobox_);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    // Identity
    layout->StartRow(0, column_view_set_id);
    identity_label_ = new views::Label(UTF16ToWide(l10n_util::GetStringUTF16(
        IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_IDENTITY)));
    layout->AddView(identity_label_);
    identity_textfield_ = new views::Textfield(
        views::Textfield::STYLE_DEFAULT);
    identity_textfield_->SetController(this);
    if (wifi && !wifi->identity().empty())
      identity_textfield_->SetText(UTF8ToUTF16(wifi->identity()));
    layout->AddView(identity_textfield_);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
  }

  // Passphrase input
  layout->StartRow(0, column_view_set_id);
  int label_text_id = IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PASSPHRASE;
  passphrase_label_ = new views::Label(
      UTF16ToWide(l10n_util::GetStringUTF16(label_text_id)));
  layout->AddView(passphrase_label_);
  passphrase_textfield_ = new views::Textfield(
      views::Textfield::STYLE_PASSWORD);
  passphrase_textfield_->SetController(this);
  if (wifi && !wifi->GetPassphrase().empty())
    passphrase_textfield_->SetText(UTF8ToUTF16(wifi->GetPassphrase()));
  // Disable passphrase input initially for other network.
  if (!wifi) {
    passphrase_label_->SetEnabled(false);
    passphrase_textfield_->SetEnabled(false);
  }
  passphrase_textfield_->SetAccessibleName(l10n_util::GetStringUTF16(
      label_text_id));
  layout->AddView(passphrase_textfield_);
  // Password visible button.
  passphrase_visible_button_ = new views::ImageButton(this);
  passphrase_visible_button_->SetImage(
      views::ImageButton::BS_NORMAL,
      ResourceBundle::GetSharedInstance().
      GetBitmapNamed(IDR_STATUSBAR_NETWORK_SECURE));
  passphrase_visible_button_->SetImageAlignment(
      views::ImageButton::ALIGN_CENTER, views::ImageButton::ALIGN_MIDDLE);
  layout->AddView(passphrase_visible_button_);
  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  if (show_8021x) {
    // Anonymous identity
    layout->StartRow(0, column_view_set_id);
    identity_anonymous_label_ =
        new views::Label(UTF16ToWide(l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_IDENTITY_ANONYMOUS)));
    layout->AddView(identity_anonymous_label_);
    identity_anonymous_textfield_ = new views::Textfield(
        views::Textfield::STYLE_DEFAULT);
    identity_anonymous_label_->SetEnabled(false);
    identity_anonymous_textfield_->SetEnabled(false);
    identity_anonymous_textfield_->SetController(this);
    layout->AddView(identity_anonymous_textfield_);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    // Save credentials
    layout->StartRow(0, column_view_set_id);
    save_credentials_checkbox_ = new views::Checkbox(
        UTF16ToWide(l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SAVE_CREDENTIALS)));
    layout->SkipColumns(1);
    layout->AddView(save_credentials_checkbox_);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
  }

  // After creating the fields, we set the values. Fields need to be created
  // first because RefreshEapFields() will enable/disable them as appropriate.
  if (show_8021x) {
    EAPMethod eap_method = (wifi ? wifi->eap_method() : EAP_METHOD_UNKNOWN);
    switch (eap_method) {
      case EAP_METHOD_PEAP:
        eap_method_combobox_->SetSelectedItem(EAP_METHOD_INDEX_PEAP);
        break;
      case EAP_METHOD_TTLS:
        eap_method_combobox_->SetSelectedItem(EAP_METHOD_INDEX_TTLS);
        break;
      case EAP_METHOD_TLS:
        eap_method_combobox_->SetSelectedItem(EAP_METHOD_INDEX_TLS);
        break;
      case EAP_METHOD_LEAP:
        eap_method_combobox_->SetSelectedItem(EAP_METHOD_INDEX_LEAP);
        break;
      default:
        break;
    }
    RefreshEapFields();

    // Phase 2 authentication
    if (phase_2_auth_combobox_->IsEnabled()) {
      EAPPhase2Auth eap_phase_2_auth =
          (wifi ? wifi->eap_phase_2_auth() : EAP_PHASE_2_AUTH_AUTO);
      switch (eap_phase_2_auth) {
        case EAP_PHASE_2_AUTH_MD5:
          phase_2_auth_combobox_->SetSelectedItem(PHASE_2_AUTH_INDEX_MD5);
          break;
        case EAP_PHASE_2_AUTH_MSCHAPV2:
          phase_2_auth_combobox_->SetSelectedItem(PHASE_2_AUTH_INDEX_MSCHAPV2);
          break;
        case EAP_PHASE_2_AUTH_MSCHAP:
          phase_2_auth_combobox_->SetSelectedItem(PHASE_2_AUTH_INDEX_MSCHAP);
          break;
        case EAP_PHASE_2_AUTH_PAP:
          phase_2_auth_combobox_->SetSelectedItem(PHASE_2_AUTH_INDEX_PAP);
          break;
        case EAP_PHASE_2_AUTH_CHAP:
          phase_2_auth_combobox_->SetSelectedItem(PHASE_2_AUTH_INDEX_CHAP);
          break;
        default:
          break;
      }
    }

    // Server CA certificate
    if (server_ca_cert_combobox_->IsEnabled()) {
      const std::string& nss_nickname =
          (wifi ? wifi->eap_server_ca_cert_nss_nickname() : std::string());
      if (nss_nickname.empty()) {
        if (wifi->eap_use_system_cas()) {
          // "Default"
          server_ca_cert_combobox_->SetSelectedItem(0);
        } else {
          // "Do not check"
          server_ca_cert_combobox_->SetSelectedItem(
              server_ca_cert_combobox_->model()->GetItemCount() - 1);
        }
      } else {
        // select the certificate if available
        int cert_index = wifi_config_model_->GetServerCaCertIndex(nss_nickname);
        if (cert_index >= 0) {
          // Skip item for "Default"
          server_ca_cert_combobox_->SetSelectedItem(1 + cert_index);
        }
      }
    }

    // Client certificate
    if (client_cert_combobox_ && client_cert_combobox_->IsEnabled()) {
      const std::string& pkcs11_id =
          (wifi ? wifi->eap_client_cert_pkcs11_id() : std::string());
      if (pkcs11_id.empty()) {
        // First item is "None".
        client_cert_combobox_->SetSelectedItem(0);
      } else {
        int cert_index = wifi_config_model_->GetUserCertIndex(pkcs11_id);
        if (cert_index >= 0) {
          // Skip item for "None"
          client_cert_combobox_->SetSelectedItem(1 + cert_index);
        }
      }
    }

    // Identity
    if (identity_textfield_->IsEnabled()) {
      const std::string& eap_identity =
          (wifi ? wifi->eap_identity() : std::string());
      identity_textfield_->SetText(UTF8ToUTF16(eap_identity));
    }

    // Anonymous identity
    if (identity_anonymous_textfield_->IsEnabled()) {
      const std::string& eap_anonymous_identity =
          (wifi ? wifi->eap_anonymous_identity() : std::string());
      identity_anonymous_textfield_->SetText(
          UTF8ToUTF16(eap_anonymous_identity));
    }

    // Passphrase
    if (passphrase_textfield_->IsEnabled()) {
      const std::string& eap_passphrase =
          (wifi ? wifi->eap_passphrase() : std::string());
      passphrase_textfield_->SetText(UTF8ToUTF16(eap_passphrase));
    }

    // Save credentials
    bool save_credentials = (wifi ? wifi->save_credentials() : false);
    save_credentials_checkbox_->SetChecked(save_credentials);
  }

  // Create an error label.
  layout->StartRow(0, column_view_set_id);
  layout->SkipColumns(1);
  error_label_ = new views::Label();
  error_label_->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  error_label_->SetColor(SK_ColorRED);
  layout->AddView(error_label_);

  // Set or hide the error text.
  UpdateErrorLabel();
}

void WifiConfigView::InitFocus() {
  // Set focus to a reasonable widget, depending on what we're showing.
  if (ssid_textfield_)
    ssid_textfield_->RequestFocus();
  else if (eap_method_combobox_)
    eap_method_combobox_->RequestFocus();
  else if (passphrase_textfield_ && passphrase_textfield_->IsEnabled())
    passphrase_textfield_->RequestFocus();
}

}  // namespace chromeos
