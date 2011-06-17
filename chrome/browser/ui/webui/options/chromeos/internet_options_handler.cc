// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/chromeos/internet_options_handler.h"

#include <ctype.h>

#include <map>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/basictypes.h"
#include "base/callback.h"
#include "base/i18n/time_formatting.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/choose_mobile_network_dialog.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/customization_document.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/sim_dialog_delegate.h"
#include "chrome/browser/chromeos/status/network_menu.h"
#include "chrome/browser/chromeos/user_cros_settings_provider.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/window.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/time_format.h"
#include "content/common/notification_service.h"
#include "content/common/notification_type.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "views/window/window.h"

static const char kOtherNetworksFakePath[] = "?";

InternetOptionsHandler::InternetOptionsHandler()
    : chromeos::CrosOptionsPageUIHandler(
          new chromeos::UserCrosSettingsProvider),
      use_settings_ui_(false) {
  registrar_.Add(this, NotificationType::REQUIRE_PIN_SETTING_CHANGE_ENDED,
      NotificationService::AllSources());
  registrar_.Add(this, NotificationType::ENTER_PIN_ENDED,
      NotificationService::AllSources());
  chromeos::NetworkLibrary* netlib =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  netlib->AddNetworkManagerObserver(this);
  netlib->AddCellularDataPlanObserver(this);
  MonitorNetworks(netlib);
}

InternetOptionsHandler::~InternetOptionsHandler() {
  chromeos::NetworkLibrary *netlib =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  netlib->RemoveNetworkManagerObserver(this);
  netlib->RemoveCellularDataPlanObserver(this);
  netlib->RemoveObserverForAllNetworks(this);
}

void InternetOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  RegisterTitle(localized_strings, "internetPage",
                IDS_OPTIONS_INTERNET_TAB_LABEL);

  localized_strings->SetString("wired_title",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_SECTION_TITLE_WIRED_NETWORK));
  localized_strings->SetString("wireless_title",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_SECTION_TITLE_WIRELESS_NETWORK));
  localized_strings->SetString("remembered_title",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_SECTION_TITLE_REMEMBERED_NETWORK));

  localized_strings->SetString("connect_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_CONNECT));
  localized_strings->SetString("disconnect_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_DISCONNECT));
  localized_strings->SetString("options_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_OPTIONS));
  localized_strings->SetString("forget_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_FORGET));
  localized_strings->SetString("activate_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_ACTIVATE));
  localized_strings->SetString("buyplan_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_BUY_PLAN));

  localized_strings->SetString("wifiNetworkTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_WIFI));
  localized_strings->SetString("cellularPlanTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_PLAN));
  localized_strings->SetString("cellularConnTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_CONNECTION));
  localized_strings->SetString("cellularDeviceTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_DEVICE));
  localized_strings->SetString("networkTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_NETWORK));
  localized_strings->SetString("securityTabLabel",
        l10n_util::GetStringUTF16(
            IDS_OPTIONS_SETTINGS_INTERNET_TAB_SECURITY));

  localized_strings->SetString("connectionState",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CONNECTION_STATE));
  localized_strings->SetString("inetAddress",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_ADDRESS));
  localized_strings->SetString("inetSubnetAddress",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SUBNETMASK));
  localized_strings->SetString("inetGateway",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_GATEWAY));
  localized_strings->SetString("inetDns",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_DNSSERVER));
  localized_strings->SetString("hardwareAddress",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_HARDWARE_ADDRESS));

  localized_strings->SetString("accessLockedMsg",
      l10n_util::GetStringUTF16(
          IDS_STATUSBAR_NETWORK_LOCKED));
  localized_strings->SetString("inetSsid",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_ID));
  localized_strings->SetString("inetPassProtected",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NET_PROTECTED));
  localized_strings->SetString("inetAutoConnectNetwork",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_AUTO_CONNECT));
  localized_strings->SetString("inetLogin",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_LOGIN));
  localized_strings->SetString("inetShowPass",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SHOWPASSWORD));
  localized_strings->SetString("inetSecurityNone",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_SELECT,
          l10n_util::GetStringUTF16(
              IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_NONE)));
  localized_strings->SetString("inetSecurityWEP",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_SELECT,
          l10n_util::GetStringUTF16(
              IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_WEP)));
  localized_strings->SetString("inetSecurityWPA",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_SELECT,
          l10n_util::GetStringUTF16(
              IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_WPA)));
  localized_strings->SetString("inetSecurityRSN",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_SELECT,
          l10n_util::GetStringUTF16(
              IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_RSN)));
  localized_strings->SetString("inetPassPrompt",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PASSWORD));
  localized_strings->SetString("inetSsidPrompt",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SSID));
  localized_strings->SetString("inetStatus",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_STATUS_TITLE));
  localized_strings->SetString("inetConnect",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CONNECT_TITLE));

  localized_strings->SetString("serviceName",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_SERVICE_NAME));
  localized_strings->SetString("networkTechnology",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_NETWORK_TECHNOLOGY));
  localized_strings->SetString("operatorName",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_OPERATOR));
  localized_strings->SetString("operatorCode",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_OPERATOR_CODE));
  localized_strings->SetString("activationState",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ACTIVATION_STATE));
  localized_strings->SetString("roamingState",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ROAMING_STATE));
  localized_strings->SetString("restrictedPool",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_RESTRICTED_POOL));
  localized_strings->SetString("errorState",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ERROR_STATE));
  localized_strings->SetString("manufacturer",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_MANUFACTURER));
  localized_strings->SetString("modelId",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_MODEL_ID));
  localized_strings->SetString("firmwareRevision",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_FIRMWARE_REVISION));
  localized_strings->SetString("hardwareRevision",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_HARDWARE_REVISION));
  localized_strings->SetString("prlVersion",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_PRL_VERSION));
  localized_strings->SetString("cellularApnLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN));
  localized_strings->SetString("cellularApnUsername",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN_USERNAME));
  localized_strings->SetString("cellularApnPassword",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN_PASSWORD));
  localized_strings->SetString("cellularApnClear",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN_CLEAR));
  localized_strings->SetString("cellularApnSet",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN_SET));

  localized_strings->SetString("accessSecurityTabLink",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ACCESS_SECURITY_TAB));
  localized_strings->SetString("lockSimCard",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_LOCK_SIM_CARD));
  localized_strings->SetString("changePinButton",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_CHANGE_PIN_BUTTON));

  localized_strings->SetString("planName",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELL_PLAN_NAME));
  localized_strings->SetString("planLoading",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_LOADING_PLAN));
  localized_strings->SetString("noPlansFound",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NO_PLANS_FOUND));
  localized_strings->SetString("purchaseMore",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PURCHASE_MORE));
  localized_strings->SetString("dataRemaining",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_DATA_REMAINING));
  localized_strings->SetString("planExpires",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EXPIRES));
  localized_strings->SetString("showPlanNotifications",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SHOW_MOBILE_NOTIFICATION));
  localized_strings->SetString("autoconnectCellular",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_AUTO_CONNECT));
  localized_strings->SetString("customerSupport",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CUSTOMER_SUPPORT));

  localized_strings->SetString("enableWifi",
      l10n_util::GetStringFUTF16(
          IDS_STATUSBAR_NETWORK_DEVICE_ENABLE,
          l10n_util::GetStringUTF16(IDS_STATUSBAR_NETWORK_DEVICE_WIFI)));
  localized_strings->SetString("disableWifi",
      l10n_util::GetStringFUTF16(
          IDS_STATUSBAR_NETWORK_DEVICE_DISABLE,
          l10n_util::GetStringUTF16(IDS_STATUSBAR_NETWORK_DEVICE_WIFI)));
  localized_strings->SetString("enableCellular",
      l10n_util::GetStringFUTF16(
          IDS_STATUSBAR_NETWORK_DEVICE_ENABLE,
          l10n_util::GetStringUTF16(IDS_STATUSBAR_NETWORK_DEVICE_CELLULAR)));
  localized_strings->SetString("disableCellular",
      l10n_util::GetStringFUTF16(
          IDS_STATUSBAR_NETWORK_DEVICE_DISABLE,
          l10n_util::GetStringUTF16(IDS_STATUSBAR_NETWORK_DEVICE_CELLULAR)));
  localized_strings->SetString("enableDataRoaming",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_ENABLE_DATA_ROAMING));
  localized_strings->SetString("generalNetworkingTitle",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CONTROL_TITLE));
  localized_strings->SetString("detailsInternetDismiss",
      l10n_util::GetStringUTF16(IDS_CLOSE));
  localized_strings->SetString("ownerOnly", l10n_util::GetStringUTF16(
      IDS_OPTIONS_ACCOUNTS_OWNER_ONLY));
  localized_strings->SetString("ownerUserId", UTF8ToUTF16(
      chromeos::UserCrosSettingsProvider::cached_owner()));

  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  FillNetworkInfo(localized_strings, cros);

  localized_strings->SetBoolean("networkUseSettingsUI", use_settings_ui_);
}

void InternetOptionsHandler::Initialize() {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  cros->RequestNetworkScan();
}

void InternetOptionsHandler::RegisterMessages() {
  // Setup handlers specific to this panel.
  DCHECK(web_ui_);
  web_ui_->RegisterMessageCallback("buttonClickCallback",
      NewCallback(this, &InternetOptionsHandler::ButtonClickCallback));
  web_ui_->RegisterMessageCallback("refreshCellularPlan",
      NewCallback(this, &InternetOptionsHandler::RefreshCellularPlanCallback));
  web_ui_->RegisterMessageCallback("loginToNetwork",
      NewCallback(this, &InternetOptionsHandler::LoginCallback));
  web_ui_->RegisterMessageCallback("loginToCertNetwork",
      NewCallback(this, &InternetOptionsHandler::LoginCertCallback));
  web_ui_->RegisterMessageCallback("loginToOtherNetwork",
      NewCallback(this, &InternetOptionsHandler::LoginToOtherCallback));
  web_ui_->RegisterMessageCallback("setDetails",
      NewCallback(this, &InternetOptionsHandler::SetDetailsCallback));
  web_ui_->RegisterMessageCallback("enableWifi",
      NewCallback(this, &InternetOptionsHandler::EnableWifiCallback));
  web_ui_->RegisterMessageCallback("disableWifi",
      NewCallback(this, &InternetOptionsHandler::DisableWifiCallback));
  web_ui_->RegisterMessageCallback("enableCellular",
      NewCallback(this, &InternetOptionsHandler::EnableCellularCallback));
  web_ui_->RegisterMessageCallback("disableCellular",
      NewCallback(this, &InternetOptionsHandler::DisableCellularCallback));
  web_ui_->RegisterMessageCallback("buyDataPlan",
      NewCallback(this, &InternetOptionsHandler::BuyDataPlanCallback));
  web_ui_->RegisterMessageCallback("showMorePlanInfo",
      NewCallback(this, &InternetOptionsHandler::BuyDataPlanCallback));
  web_ui_->RegisterMessageCallback("setApn",
        NewCallback(this, &InternetOptionsHandler::SetApnCallback));
  web_ui_->RegisterMessageCallback("setSimCardLock",
        NewCallback(this, &InternetOptionsHandler::SetSimCardLockCallback));
  web_ui_->RegisterMessageCallback("changePin",
        NewCallback(this, &InternetOptionsHandler::ChangePinCallback));
}

void InternetOptionsHandler::EnableWifiCallback(const ListValue* args) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  cros->EnableWifiNetworkDevice(true);
}

void InternetOptionsHandler::DisableWifiCallback(const ListValue* args) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  cros->EnableWifiNetworkDevice(false);
}

void InternetOptionsHandler::EnableCellularCallback(const ListValue* args) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  const chromeos::NetworkDevice* cellular = cros->FindCellularDevice();
  if (!cellular) {
    LOG(ERROR) << "Didn't find cellular device, it should have been available.";
    cros->EnableCellularNetworkDevice(true);
  } else if (cellular->sim_lock_state() == chromeos::SIM_UNLOCKED ||
             cellular->sim_lock_state() == chromeos::SIM_UNKNOWN) {
      cros->EnableCellularNetworkDevice(true);
  } else {
    chromeos::SimDialogDelegate::ShowDialog(GetNativeWindow(),
        chromeos::SimDialogDelegate::SIM_DIALOG_UNLOCK);
  }
}

void InternetOptionsHandler::DisableCellularCallback(const ListValue* args) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  cros->EnableCellularNetworkDevice(false);
}

void InternetOptionsHandler::BuyDataPlanCallback(const ListValue* args) {
  if (!web_ui_)
    return;
  Browser* browser = BrowserList::FindBrowserWithFeature(
      web_ui_->GetProfile(), Browser::FEATURE_TABSTRIP);
  if (browser)
    browser->OpenMobilePlanTabAndActivate();
}

void InternetOptionsHandler::SetApnCallback(const ListValue* args) {
  std::string service_path;
  std::string apn;
  std::string username;
  std::string password;
  if (args->GetSize() != 4 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &apn) ||
      !args->GetString(2, &username) ||
      !args->GetString(3, &password)) {
    NOTREACHED();
    return;
  }

  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::CellularNetwork* network =
        cros->FindCellularNetworkByPath(service_path);
  if (network) {
    network->SetApn(chromeos::CellularNetwork::Apn(
        apn, network->apn().network_id, username, password));
  }
}

void InternetOptionsHandler::SetSimCardLockCallback(const ListValue* args) {
  bool require_pin_new_value;
  if (!args->GetBoolean(0, &require_pin_new_value)) {
    NOTREACHED();
    return;
  }
  // 1. Bring up SIM unlock dialog, pass new RequirePin setting in URL.
  // 2. Dialog will ask for current PIN in any case.
  // 3. If card is locked it will first call PIN unlock operation
  // 4. Then it will call Set RequirePin, passing the same PIN.
  // 5. We'll get notified by REQUIRE_PIN_SETTING_CHANGE_ENDED notification.
  chromeos::SimDialogDelegate::SimDialogMode mode;
  if (require_pin_new_value)
    mode = chromeos::SimDialogDelegate::SIM_DIALOG_SET_LOCK_ON;
  else
    mode = chromeos::SimDialogDelegate::SIM_DIALOG_SET_LOCK_OFF;
  chromeos::SimDialogDelegate::ShowDialog(GetNativeWindow(), mode);
}

void InternetOptionsHandler::ChangePinCallback(const ListValue* args) {
  chromeos::SimDialogDelegate::ShowDialog(GetNativeWindow(),
      chromeos::SimDialogDelegate::SIM_DIALOG_CHANGE_PIN);
}

void InternetOptionsHandler::RefreshNetworkData(
    chromeos::NetworkLibrary* cros) {
  DictionaryValue dictionary;
  FillNetworkInfo(&dictionary, cros);
  web_ui_->CallJavascriptFunction(
      "options.InternetOptions.refreshNetworkData", dictionary);
}

void InternetOptionsHandler::OnNetworkManagerChanged(
    chromeos::NetworkLibrary* cros) {
  if (!web_ui_)
    return;
  MonitorNetworks(cros);
  RefreshNetworkData(cros);
}

void InternetOptionsHandler::OnNetworkChanged(
    chromeos::NetworkLibrary* cros,
    const chromeos::Network* network) {
  if (web_ui_)
    RefreshNetworkData(cros);
}

// Monitor wireless networks for changes. It is only necessary
// to set up individual observers for the cellular networks
// (if any) and for the connected Wi-Fi network (if any). The
// only change we are interested in for Wi-Fi networks is signal
// strength. For non-connected Wi-Fi networks, all information is
// reported via scan results, which trigger network manager
// updates. Only the connected Wi-Fi network has changes reported
// via service property updates.
void InternetOptionsHandler::MonitorNetworks(chromeos::NetworkLibrary* cros) {
  cros->RemoveObserverForAllNetworks(this);
  const chromeos::WifiNetwork* wifi_network = cros->wifi_network();
  if (wifi_network != NULL)
    cros->AddNetworkObserver(wifi_network->service_path(), this);
  // Always monitor the cellular networks, if any, so that changes
  // in network technology, roaming status, and signal strength
  // will be shown.
  const chromeos::CellularNetworkVector& cell_networks =
      cros->cellular_networks();
  for (size_t i = 0; i < cell_networks.size(); ++i) {
    chromeos::CellularNetwork* cell_network = cell_networks[i];
    cros->AddNetworkObserver(cell_network->service_path(), this);
  }
}

void InternetOptionsHandler::OnCellularDataPlanChanged(
    chromeos::NetworkLibrary* cros) {
  if (!web_ui_)
    return;
  const chromeos::CellularNetwork* cellular = cros->cellular_network();
  if (!cellular)
    return;
  const chromeos::CellularDataPlanVector* plans =
      cros->GetDataPlans(cellular->service_path());
  DictionaryValue connection_plans;
  ListValue* plan_list = new ListValue();
  if (plans) {
    for (chromeos::CellularDataPlanVector::const_iterator iter = plans->begin();
         iter != plans->end(); ++iter) {
      plan_list->Append(CellularDataPlanToDictionary(*iter));
    }
  }
  connection_plans.SetString("servicePath", cellular->service_path());
  connection_plans.SetBoolean("needsPlan", cellular->needs_new_plan());
  connection_plans.SetBoolean("activated",
      cellular->activation_state() == chromeos::ACTIVATION_STATE_ACTIVATED);
  connection_plans.Set("plans", plan_list);
  SetActivationButtonVisibility(cellular, &connection_plans);
  web_ui_->CallJavascriptFunction(
      "options.InternetOptions.updateCellularPlans", connection_plans);
}


void InternetOptionsHandler::Observe(NotificationType type,
                                     const NotificationSource& source,
                                     const NotificationDetails& details) {
  chromeos::CrosOptionsPageUIHandler::Observe(type, source, details);
  if (type == NotificationType::REQUIRE_PIN_SETTING_CHANGE_ENDED) {
    bool require_pin = *Details<bool>(details).ptr();
    DictionaryValue dictionary;
    dictionary.SetBoolean("requirePin", require_pin);
    web_ui_->CallJavascriptFunction(
        "options.InternetOptions.updateSecurityTab", dictionary);
  } else if (type == NotificationType::ENTER_PIN_ENDED) {
    // We make an assumption (which is valid for now) that the SIM
    // unlock dialog is put up only when the user is trying to enable
    // mobile data.
    bool cancelled = *Details<bool>(details).ptr();
    if (cancelled) {
      chromeos::NetworkLibrary* cros =
          chromeos::CrosLibrary::Get()->GetNetworkLibrary();
      DictionaryValue dictionary;
      FillNetworkInfo(&dictionary, cros);
      web_ui_->CallJavascriptFunction(
          "options.InternetOptions.setupAttributes", dictionary);
    }
    // The case in which the correct PIN was entered and the SIM is
    // now unlocked is handled in NetworkMenuButton.
  }
}

DictionaryValue* InternetOptionsHandler::CellularDataPlanToDictionary(
    const chromeos::CellularDataPlan* plan) {
  DictionaryValue* plan_dict = new DictionaryValue();
  plan_dict->SetInteger("planType", plan->plan_type);
  plan_dict->SetString("name", plan->plan_name);
  plan_dict->SetString("planSummary", plan->GetPlanDesciption());
  plan_dict->SetString("dataRemaining", plan->GetDataRemainingDesciption());
  plan_dict->SetString("planExpires", plan->GetPlanExpiration());
  plan_dict->SetString("warning", plan->GetRemainingWarning());
  return plan_dict;
}

void InternetOptionsHandler::SetDetailsCallback(const ListValue* args) {
  std::string service_path;
  std::string auto_connect_str;

  if (args->GetSize() < 2 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &auto_connect_str)) {
    NOTREACHED();
    return;
  }

  if (!chromeos::UserManager::Get()->current_user_is_owner()) {
    LOG(WARNING) << "Non-owner tried to change a network.";
    return;
  }

  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::WifiNetwork* network = cros->FindWifiNetworkByPath(service_path);
  if (!network)
    return;

  bool auto_connect = auto_connect_str == "true";
  if (auto_connect != network->auto_connect())
    network->SetAutoConnect(auto_connect);
}

void InternetOptionsHandler::PopulateDictionaryDetails(
    const chromeos::Network* net, chromeos::NetworkLibrary* cros) {
  DCHECK(net);
  DictionaryValue dictionary;
  std::string hardware_address;
  chromeos::NetworkIPConfigVector ipconfigs = cros->GetIPConfigs(
      net->device_path(), &hardware_address,
      chromeos::NetworkLibrary::FORMAT_COLON_SEPARATED_HEX);
  if (!hardware_address.empty())
    dictionary.SetString("hardwareAddress", hardware_address);
  scoped_ptr<ListValue> ipconfig_list(new ListValue());
  for (chromeos::NetworkIPConfigVector::const_iterator it = ipconfigs.begin();
       it != ipconfigs.end(); ++it) {
    scoped_ptr<DictionaryValue> ipconfig_dict(new DictionaryValue());
    const chromeos::NetworkIPConfig& ipconfig = *it;
    ipconfig_dict->SetString("address", ipconfig.address);
    ipconfig_dict->SetString("subnetAddress", ipconfig.netmask);
    ipconfig_dict->SetString("gateway", ipconfig.gateway);
    ipconfig_dict->SetString("dns", ipconfig.name_servers);
    ipconfig_list->Append(ipconfig_dict.release());
  }
  dictionary.Set("ipconfigs", ipconfig_list.release());

  chromeos::ConnectionType type = net->type();
  dictionary.SetInteger("type", type);
  dictionary.SetString("servicePath", net->service_path());
  dictionary.SetBoolean("connecting", net->connecting());
  dictionary.SetBoolean("connected", net->connected());
  dictionary.SetString("connectionState", net->GetStateString());

  if (type == chromeos::TYPE_WIFI) {
    const chromeos::WifiNetwork* wifi =
        cros->FindWifiNetworkByPath(net->service_path());
    if (!wifi) {
      LOG(WARNING) << "Cannot find network " << net->service_path();
    } else {
      PopulateWifiDetails(wifi, &dictionary);
    }
  } else if (type == chromeos::TYPE_CELLULAR) {
    const chromeos::CellularNetwork* cellular =
        cros->FindCellularNetworkByPath(net->service_path());
    if (!cellular) {
      LOG(WARNING) << "Cannot find network " << net->service_path();
    } else {
      PopulateCellularDetails(cros, cellular, &dictionary);
    }
  } else if (type == chromeos::TYPE_VPN) {
    const chromeos::VirtualNetwork* vpn =
        cros->FindVirtualNetworkByPath(net->service_path());
    if (!vpn) {
      LOG(WARNING) << "Cannot find network " << net->service_path();
    } else {
      PopulateVPNDetails(vpn, &dictionary);
    }
  }

  web_ui_->CallJavascriptFunction(
      "options.InternetOptions.showDetailedInfo", dictionary);
}

void InternetOptionsHandler::PopulateWifiDetails(
    const chromeos::WifiNetwork* wifi,
    DictionaryValue* dictionary) {
  dictionary->SetString("ssid", wifi->name());
  dictionary->SetBoolean("autoConnect", wifi->auto_connect());
  dictionary->SetBoolean("encrypted", wifi->encrypted());
}

void InternetOptionsHandler::PopulateCellularDetails(
    chromeos::NetworkLibrary* cros,
    const chromeos::CellularNetwork* cellular,
    DictionaryValue* dictionary) {
  // Cellular network / connection settings.
  dictionary->SetString("serviceName", cellular->name());
  dictionary->SetString("networkTechnology",
                        cellular->GetNetworkTechnologyString());
  dictionary->SetString("operatorName", cellular->operator_name());
  dictionary->SetString("operatorCode", cellular->operator_code());
  dictionary->SetString("activationState",
                        cellular->GetActivationStateString());
  dictionary->SetString("roamingState",
                        cellular->GetRoamingStateString());
  dictionary->SetString("restrictedPool",
                        cellular->restricted_pool() ?
                        l10n_util::GetStringUTF8(
                            IDS_CONFIRM_MESSAGEBOX_YES_BUTTON_LABEL) :
                        l10n_util::GetStringUTF8(
                            IDS_CONFIRM_MESSAGEBOX_NO_BUTTON_LABEL));
  dictionary->SetString("errorState", cellular->GetErrorString());
  dictionary->SetString("supportUrl", cellular->payment_url());
  dictionary->SetBoolean("needsPlan", cellular->needs_new_plan());
  dictionary->SetBoolean("gsm", cellular->is_gsm());
  const chromeos::CellularNetwork::Apn& apn = cellular->apn();
  dictionary->SetString("apn", apn.apn);
  dictionary->SetString("apn_network_id", apn.network_id);
  dictionary->SetString("apn_username", apn.username);
  dictionary->SetString("apn_password", apn.password);
  const chromeos::CellularNetwork::Apn& last_good_apn =
      cellular->last_good_apn();
  dictionary->SetString("last_good_apn", last_good_apn.apn);
  dictionary->SetString("last_good_apn_network_id", last_good_apn.network_id);
  dictionary->SetString("last_good_apn_username", last_good_apn.username);
  dictionary->SetString("last_good_apn_password", last_good_apn.password);

  // Device settings.
  const chromeos::NetworkDevice* device =
      cros->FindNetworkDeviceByPath(cellular->device_path());
  if (device) {
    dictionary->SetString("manufacturer", device->manufacturer());
    dictionary->SetString("modelId", device->model_id());
    dictionary->SetString("firmwareRevision", device->firmware_revision());
    dictionary->SetString("hardwareRevision", device->hardware_revision());
    dictionary->SetString("prlVersion",
                          StringPrintf("%u", device->prl_version()));
    dictionary->SetString("meid", device->meid());
    dictionary->SetString("imei", device->imei());
    dictionary->SetString("mdn", device->mdn());
    dictionary->SetString("imsi", device->imsi());
    dictionary->SetString("esn", device->esn());
    dictionary->SetString("min", device->min());
    dictionary->SetBoolean("simCardLockEnabled",
        device->sim_pin_required() == chromeos::SIM_PIN_REQUIRED);

    chromeos::ServicesCustomizationDocument* customization =
        chromeos::ServicesCustomizationDocument::GetInstance();
    if (customization->IsReady()) {
      std::string carrier_id = cros->GetCellularHomeCarrierId();
      const chromeos::ServicesCustomizationDocument::CarrierDeal* deal =
          customization->GetCarrierDeal(carrier_id, false);
      if (deal && !deal->top_up_url.empty())
        dictionary->SetString("carrierUrl", deal->top_up_url);
    }
  }

  SetActivationButtonVisibility(cellular, dictionary);
}

void InternetOptionsHandler::PopulateVPNDetails(
    const chromeos::VirtualNetwork* vpn,
    DictionaryValue* dictionary) {
  // TODO(altimofeev): implement this.
}

void InternetOptionsHandler::SetActivationButtonVisibility(
    const chromeos::CellularNetwork* cellular,
    DictionaryValue* dictionary) {
  if (cellular->needs_new_plan()) {
    dictionary->SetBoolean("showBuyButton", true);
  } else if (cellular->activation_state() !=
                 chromeos::ACTIVATION_STATE_ACTIVATING &&
             cellular->activation_state() !=
                 chromeos::ACTIVATION_STATE_ACTIVATED) {
    dictionary->SetBoolean("showActivateButton", true);
  }
}

void InternetOptionsHandler::LoginCallback(const ListValue* args) {
  std::string service_path;
  std::string password;

  if (args->GetSize() != 2 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &password)) {
    NOTREACHED();
    return;
  }

  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  cros->ConnectToWifiNetwork(service_path);
}

void InternetOptionsHandler::LoginCertCallback(const ListValue* args) {
  std::string service_path;
  std::string identity;
  std::string certpath;
  if (args->GetSize() < 3 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &certpath) ||
      !args->GetString(2, &identity)) {
    return;
  }
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::WifiNetwork* network = cros->FindWifiNetworkByPath(service_path);
  if (network) {
    std::string passphrase;
    if (args->GetSize() == 4 && args->GetString(3, &passphrase))
      network->SetPassphrase(passphrase);
    cros->ConnectToWifiNetwork(network);
  }
}

void InternetOptionsHandler::LoginToOtherCallback(const ListValue* args) {
  std::string security;
  std::string ssid;
  std::string password;

  if (args->GetSize() != 3 ||
      !args->GetString(0, &security) ||
      !args->GetString(1, &ssid) ||
      !args->GetString(2, &password)) {
    NOTREACHED();
    return;
  }

  chromeos::ConnectionSecurity sec = chromeos::SECURITY_UNKNOWN;
  if (security == "none") {
    sec = chromeos::SECURITY_NONE;
  } else if (security == "wep") {
    sec = chromeos::SECURITY_WEP;
  } else if (security == "wpa") {
    sec = chromeos::SECURITY_WPA;
  } else if (security == "rsn") {
    sec = chromeos::SECURITY_RSN;
  }

  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();

  cros->ConnectToWifiNetwork(sec, ssid, password, std::string(), std::string());
}

void InternetOptionsHandler::CreateModalPopup(views::WindowDelegate* view) {
  DCHECK(!use_settings_ui_);
  views::Window* window = browser::CreateViewsWindow(GetNativeWindow(),
                                                     gfx::Rect(),
                                                     view);
  window->SetIsAlwaysOnTop(true);
  window->Show();
}

gfx::NativeWindow InternetOptionsHandler::GetNativeWindow() const {
  // TODO(beng): This is an improper direct dependency on Browser. Route this
  // through some sort of delegate.
  Browser* browser = BrowserList::FindBrowserWithProfile(web_ui_->GetProfile());
  return browser->window()->GetNativeHandle();
}

void InternetOptionsHandler::ButtonClickCallback(const ListValue* args) {
  std::string str_type;
  std::string service_path;
  std::string command;
  if (args->GetSize() != 3 ||
      !args->GetString(0, &str_type) ||
      !args->GetString(1, &service_path) ||
      !args->GetString(2, &command)) {
    NOTREACHED();
    return;
  }

  int type = atoi(str_type.c_str());
  if (type == chromeos::TYPE_ETHERNET) {
    chromeos::NetworkLibrary* cros =
        chromeos::CrosLibrary::Get()->GetNetworkLibrary();
    const chromeos::EthernetNetwork* ether = cros->ethernet_network();
    PopulateDictionaryDetails(ether, cros);
  } else if (type == chromeos::TYPE_WIFI) {
    HandleWifiButtonClick(service_path, command);
  } else if (type == chromeos::TYPE_CELLULAR) {
    HandleCellularButtonClick(service_path, command);
  } else if (type == chromeos::TYPE_VPN) {
    HandleVPNButtonClick(service_path, command);
  } else {
    NOTREACHED();
  }
}

void InternetOptionsHandler::HandleWifiButtonClick(
    const std::string& service_path,
    const std::string& command) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::WifiNetwork* wifi = NULL;
  if (command == "forget") {
    if (!chromeos::UserManager::Get()->current_user_is_owner()) {
      LOG(WARNING) << "Non-owner tried to forget a network.";
      return;
    }
    cros->ForgetWifiNetwork(service_path);
  } else if (!use_settings_ui_ && service_path == kOtherNetworksFakePath) {
    // Other wifi networks.
    CreateModalPopup(new chromeos::NetworkConfigView(chromeos::TYPE_WIFI));
  } else if ((wifi = cros->FindWifiNetworkByPath(service_path))) {
    if (command == "connect") {
      // Connect to wifi here. Open password page if appropriate.
      if (wifi->IsPassphraseRequired()) {
        if (use_settings_ui_) {
          if (wifi->encryption() == chromeos::SECURITY_8021X) {
            PopulateDictionaryDetails(wifi, cros);
          } else {
            DictionaryValue dictionary;
            dictionary.SetString("servicePath", wifi->service_path());
            web_ui_->CallJavascriptFunction(
                "options.InternetOptions.showPasswordEntry", dictionary);
          }
        } else {
          CreateModalPopup(new chromeos::NetworkConfigView(wifi));
        }
      } else {
        cros->ConnectToWifiNetwork(wifi);
      }
    } else if (command == "disconnect") {
      cros->DisconnectFromNetwork(wifi);
    } else if (command == "options") {
      PopulateDictionaryDetails(wifi, cros);
    }
  }
}

void InternetOptionsHandler::HandleCellularButtonClick(
    const std::string& service_path,
    const std::string& command) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::CellularNetwork* cellular = NULL;
  if (service_path == kOtherNetworksFakePath) {
    chromeos::ChooseMobileNetworkDialog::ShowDialog(GetNativeWindow());
  } else if ((cellular = cros->FindCellularNetworkByPath(service_path))) {
    if (command == "connect") {
      cros->ConnectToCellularNetwork(cellular);
    } else if (command == "disconnect") {
      cros->DisconnectFromNetwork(cellular);
    } else if (command == "activate") {
      Browser* browser = BrowserList::GetLastActive();
      if (browser)
        browser->OpenMobilePlanTabAndActivate();
    } else if (command == "options") {
      PopulateDictionaryDetails(cellular, cros);
    }
  }
}

void InternetOptionsHandler::HandleVPNButtonClick(
    const std::string& service_path,
    const std::string& command) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::VirtualNetwork* network = NULL;
  // TODO(altimofeev): verify if service_path in condition is correct.
  if (!use_settings_ui_ && service_path == kOtherNetworksFakePath) {
    // Other VPN networks.
    CreateModalPopup(new chromeos::NetworkConfigView(chromeos::TYPE_VPN));
  } else if ((network = cros->FindVirtualNetworkByPath(service_path))) {
    if (command == "connect") {
      // Connect to VPN here. Open password page if appropriate.
      if (network->NeedMoreInfoToConnect()) {
        if (use_settings_ui_) {
          // TODO(altimofeev): implement this.
        } else {
          CreateModalPopup(new chromeos::NetworkConfigView(network));
        }
      } else {
        cros->ConnectToVirtualNetwork(network);
      }
    } else if (command == "disconnect") {
      cros->DisconnectFromNetwork(network);
    } else if (command == "options") {
      PopulateDictionaryDetails(network, cros);
    }
  }
}
void InternetOptionsHandler::RefreshCellularPlanCallback(
    const ListValue* args) {
  std::string service_path;
  if (args->GetSize() != 1 ||
      !args->GetString(0, &service_path)) {
    NOTREACHED();
    return;
  }
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  const chromeos::CellularNetwork* cellular =
      cros->FindCellularNetworkByPath(service_path);
  if (cellular)
    cellular->RefreshDataPlansIfNeeded();
}

ListValue* InternetOptionsHandler::GetNetwork(
    const std::string& service_path,
    const SkBitmap& icon,
    const std::string& name,
    bool connecting,
    bool connected,
    bool connectable,
    chromeos::ConnectionType connection_type,
    bool remembered,
    chromeos::ActivationState activation_state,
    bool needs_new_plan) {
  ListValue* network = new ListValue();

  // 802.1X networks can be connected but not have saved credentials, and
  // hence be "not configured".  Give preference to the "connected" and
  // "connecting" states.  http://crosbug.com/14459
  int connection_state = IDS_STATUSBAR_NETWORK_DEVICE_DISCONNECTED;
  if (connected)
    connection_state = IDS_STATUSBAR_NETWORK_DEVICE_CONNECTED;
  else if (connecting)
    connection_state = IDS_STATUSBAR_NETWORK_DEVICE_CONNECTING;
  else if (!connectable)
    connection_state = IDS_STATUSBAR_NETWORK_DEVICE_NOT_CONFIGURED;
  std::string status = l10n_util::GetStringUTF8(connection_state);
  if (connection_type == chromeos::TYPE_CELLULAR) {
    if (needs_new_plan) {
      status = l10n_util::GetStringUTF8(IDS_OPTIONS_SETTINGS_NO_PLAN_LABEL);
    } else if (activation_state != chromeos::ACTIVATION_STATE_ACTIVATED) {
      status.append(" / ");
      status.append(
          chromeos::CellularNetwork::ActivationStateToString(activation_state));
    }
  }

  // To keep the consistency with JS implementation, do not change the order
  // locally.
  // TODO(kochi): Use dictionaly for future maintainability.
  // 0) service path
  network->Append(Value::CreateStringValue(service_path));
  // 1) name
  network->Append(Value::CreateStringValue(name));
  // 2) status
  network->Append(Value::CreateStringValue(status));
  // 3) type
  network->Append(Value::CreateIntegerValue(static_cast<int>(connection_type)));
  // 4) connected
  network->Append(Value::CreateBooleanValue(connected));
  // 5) connecting
  network->Append(Value::CreateBooleanValue(connecting));
  // 6) icon data url
  network->Append(Value::CreateStringValue(icon.isNull() ? "" :
      web_ui_util::GetImageDataUrl(icon)));
  // 7) remembered
  network->Append(Value::CreateBooleanValue(remembered));
  // 8) activation state
  network->Append(Value::CreateIntegerValue(
                    static_cast<int>(activation_state)));
  // 9) needs new plan
  network->Append(Value::CreateBooleanValue(needs_new_plan));
  // 10) connectable
  network->Append(Value::CreateBooleanValue(connectable));
  return network;
}

ListValue* InternetOptionsHandler::GetWiredList() {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  ListValue* list = new ListValue();

  // If ethernet is not enabled, then don't add anything.
  if (cros->ethernet_enabled()) {
    const chromeos::EthernetNetwork* ethernet_network =
        cros->ethernet_network();
    const SkBitmap* icon = rb.GetBitmapNamed(IDR_STATUSBAR_WIRED_BLACK);
    const SkBitmap* bottom_right_badge = !ethernet_network ||
        (!ethernet_network->connecting() && !ethernet_network->connected()) ?
            rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_DISCONNECTED) : NULL;
    const SkBitmap* bottom_left_badge =
        chromeos::NetworkMenu::BadgeForPrivateNetworkStatus(ethernet_network);
    if (ethernet_network) {
      list->Append(GetNetwork(
          ethernet_network->service_path(),
          chromeos::NetworkMenu::IconForDisplay(icon, bottom_right_badge, NULL,
                                                bottom_left_badge),
          l10n_util::GetStringUTF8(IDS_STATUSBAR_NETWORK_DEVICE_ETHERNET),
          ethernet_network->connecting(),
          ethernet_network->connected(),
          ethernet_network->connectable(),
          chromeos::TYPE_ETHERNET,
          false,
          chromeos::ACTIVATION_STATE_UNKNOWN,
          false));
    }
  }
  return list;
}

ListValue* InternetOptionsHandler::GetWirelessList() {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  ListValue* list = new ListValue();

  const chromeos::Network* active_network = cros->active_network();
  bool has_vpn = active_network && cros->virtual_network();
  bool vpn_on_wireless = has_vpn &&
      active_network->type() == chromeos::TYPE_WIFI;
  const chromeos::WifiNetworkVector& wifi_networks = cros->wifi_networks();
  for (chromeos::WifiNetworkVector::const_iterator it =
      wifi_networks.begin(); it != wifi_networks.end(); ++it) {
    const SkBitmap* icon =
        chromeos::NetworkMenu::IconForNetworkStrength(*it, true);
    const SkBitmap* bottom_right_badge = (*it)->encrypted() ?
        rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_SECURE) : NULL;
    const SkBitmap* bottom_left_badge =
        vpn_on_wireless && active_network == (*it) ?
        chromeos::NetworkMenu::BadgeForPrivateNetworkStatus(NULL) : NULL;
    list->Append(GetNetwork(
        (*it)->service_path(),
        chromeos::NetworkMenu::IconForDisplay(icon, bottom_right_badge, NULL,
                                              bottom_left_badge),
        (*it)->name(),
        (*it)->connecting(),
        (*it)->connected(),
        (*it)->connectable(),
        chromeos::TYPE_WIFI,
        false,
        chromeos::ACTIVATION_STATE_UNKNOWN,
        false));
  }

  // Add "Other WiFi network..." if wifi is enabled.
  if (cros->wifi_enabled()) {
    list->Append(GetNetwork(
        kOtherNetworksFakePath,
        *rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0_BLACK),
        l10n_util::GetStringUTF8(IDS_OPTIONS_SETTINGS_OTHER_WIFI_NETWORKS),
        false,
        false,
        true,
        chromeos::TYPE_WIFI,
        false,
        chromeos::ACTIVATION_STATE_UNKNOWN,
        false));
  }

  bool vpn_on_cellular = has_vpn &&
                         active_network->type() == chromeos::TYPE_CELLULAR;
  const chromeos::CellularNetworkVector cellular_networks =
      cros->cellular_networks();
  for (chromeos::CellularNetworkVector::const_iterator it =
      cellular_networks.begin(); it != cellular_networks.end(); ++it) {
    const SkBitmap* icon =
        chromeos::NetworkMenu::IconForNetworkStrength(*it, true);
    const SkBitmap* bottom_right_badge =
        chromeos::NetworkMenu::BadgeForNetworkTechnology(*it);
    const SkBitmap* roaming_badge =
        chromeos::NetworkMenu::BadgeForRoamingStatus(*it);
    const SkBitmap* bottom_left_badge =
        vpn_on_cellular && active_network == (*it) ?
        chromeos::NetworkMenu::BadgeForPrivateNetworkStatus(NULL) : NULL;
    list->Append(GetNetwork(
        (*it)->service_path(),
        chromeos::NetworkMenu::IconForDisplay(icon, bottom_right_badge,
                                              roaming_badge, bottom_left_badge),
        (*it)->name(),
        (*it)->connecting(),
        (*it)->connected(),
        (*it)->connectable(),
        chromeos::TYPE_CELLULAR,
        false,
        (*it)->activation_state(),
        (*it)->SupportsDataPlan() && (*it)->restricted_pool()));
  }

  const chromeos::NetworkDevice* cellular_device = cros->FindCellularDevice();
  if (cellular_device && cellular_device->support_network_scan() &&
      cros->cellular_enabled()) {
    list->Append(GetNetwork(
        kOtherNetworksFakePath,
        *rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0_BLACK),
        l10n_util::GetStringUTF8(IDS_OPTIONS_SETTINGS_OTHER_CELLULAR_NETWORKS),
        false,
        false,
        true,
        chromeos::TYPE_CELLULAR,
        false,
        chromeos::ACTIVATION_STATE_ACTIVATED,
        false));
  }

  return list;
}

ListValue* InternetOptionsHandler::GetRememberedList() {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  ListValue* list = new ListValue();

  const chromeos::WifiNetworkVector& remembered_wifi_networks =
      cros->remembered_wifi_networks();
  const chromeos::Network* active_network = cros->active_network();
  bool vpn_on_wireless = active_network && cros->virtual_network() &&
                         active_network->type() == chromeos::TYPE_WIFI;

  for (chromeos::WifiNetworkVector::const_iterator rit =
           remembered_wifi_networks.begin();
       rit != remembered_wifi_networks.end(); ++rit) {
    chromeos::WifiNetwork* remembered = *rit;
    chromeos::Network* network = cros->FindNetworkFromRemembered(remembered);

    const SkBitmap* icon = network ?
        chromeos::NetworkMenu::IconForNetworkStrength(
            static_cast<chromeos::WifiNetwork*>(network), true) :
        rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0_BLACK);
    // Place the secure badge on the icon if the remembered network is
    // encrypted (the matching detected network, if any, will have the same
    // encrypted property by definition).
    const SkBitmap* bottom_right_badge = remembered->encrypted() ?
        rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_SECURE) : NULL;
    const SkBitmap* bottom_left_badge =
        vpn_on_wireless && active_network == network ?
        chromeos::NetworkMenu::BadgeForPrivateNetworkStatus(NULL) : NULL;
    list->Append(GetNetwork(
        remembered->service_path(),
        chromeos::NetworkMenu::IconForDisplay(icon, bottom_right_badge, NULL,
                                              bottom_left_badge),
        remembered->name(),
        network ? network->connecting() : false,
        network ? network->connected() : false,
        true,
        chromeos::TYPE_WIFI,
        true,
        chromeos::ACTIVATION_STATE_UNKNOWN,
        false));
  }
  return list;
}

void InternetOptionsHandler::FillNetworkInfo(
    DictionaryValue* dictionary, chromeos::NetworkLibrary* cros) {
  dictionary->SetBoolean("accessLocked", cros->IsLocked());
  dictionary->Set("wiredList", GetWiredList());
  dictionary->Set("wirelessList", GetWirelessList());
  dictionary->Set("rememberedList", GetRememberedList());
  dictionary->SetBoolean("wifiAvailable", cros->wifi_available());
  dictionary->SetBoolean("wifiEnabled", cros->wifi_enabled());
  dictionary->SetBoolean("cellularAvailable", cros->cellular_available());
  dictionary->SetBoolean("cellularEnabled", cros->cellular_enabled());
}
