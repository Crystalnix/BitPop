// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/about_flags.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>

#include "base/command_line.h"
#include "base/memory/singleton.h"
#include "base/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace about_flags {

// Macros to simplify specifying the type.
#define SINGLE_VALUE_TYPE_AND_VALUE(command_line_switch, switch_value) \
    Experiment::SINGLE_VALUE, command_line_switch, switch_value, NULL, 0
#define SINGLE_VALUE_TYPE(command_line_switch) \
    SINGLE_VALUE_TYPE_AND_VALUE(command_line_switch, "")
#define MULTI_VALUE_TYPE(choices) \
    Experiment::MULTI_VALUE, "", "", choices, arraysize(choices)

namespace {

const unsigned kOsAll = kOsMac | kOsWin | kOsLinux | kOsCrOS;

// Names for former Chrome OS Labs experiments, shared with prefs migration
// code.
const char kMediaPlayerExperimentName[] = "media-player";
const char kAdvancedFileSystemExperimentName[] = "advanced-file-system";
const char kVerticalTabsExperimentName[] = "vertical-tabs";

// RECORDING USER METRICS FOR FLAGS:
// -----------------------------------------------------------------------------
// The first line of the experiment is the internal name. If you'd like to
// gather statistics about the usage of your flag, you should append a marker
// comment to the end of the feature name, like so:
//   "my-special-feature",  // FLAGS:RECORD_UMA
//
// After doing that, run //chrome/tools/extract_actions.py (see instructions at
// the top of that file for details) to update the chromeactions.txt file, which
// will enable UMA to record your feature flag.
//
// After your feature has shipped under a flag, you can locate the metrics
// under the action name AboutFlags_internal-action-name. Actions are recorded
// once per startup, so you should divide this number by AboutFlags_StartupTick
// to get a sense of usage. Note that this will not be the same as number of
// users with a given feature enabled because users can quit and relaunch
// the application multiple times over a given time interval.
// TODO(rsesek): See if there's a way to count per-user, rather than
// per-startup.

// To add a new experiment add to the end of kExperiments. There are two
// distinct types of experiments:
// . SINGLE_VALUE: experiment is either on or off. Use the SINGLE_VALUE_TYPE
//   macro for this type supplying the command line to the macro.
// . MULTI_VALUE: a list of choices, the first of which should correspond to a
//   deactivated state for this lab (i.e. no command line option).  To specify
//   this type of experiment use the macro MULTI_VALUE_TYPE supplying it the
//   array of choices.
// See the documentation of Experiment for details on the fields.
//
// When adding a new choice, add it to the end of the list.
const Experiment kExperiments[] = {
  {
    "expose-for-tabs",  // FLAGS:RECORD_UMA
    IDS_FLAGS_TABPOSE_NAME,
    IDS_FLAGS_TABPOSE_DESCRIPTION,
    kOsMac,
#if defined(OS_MACOSX)
    // The switch exists only on OS X.
    SINGLE_VALUE_TYPE(switches::kEnableExposeForTabs)
#else
    SINGLE_VALUE_TYPE("")
#endif
  },
  {
    "vertical-tabs",  // FLAGS:RECORD_UMA
    IDS_FLAGS_SIDE_TABS_NAME,
    IDS_FLAGS_SIDE_TABS_DESCRIPTION,
    kOsWin | kOsCrOS,
    SINGLE_VALUE_TYPE(switches::kEnableVerticalTabs)
  },
  {
    "remoting",  // FLAGS:RECORD_UMA
    IDS_FLAGS_REMOTING_NAME,
    IDS_FLAGS_REMOTING_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kEnableRemoting)
  },
  {
    "conflicting-modules-check",  // FLAGS:RECORD_UMA
    IDS_FLAGS_CONFLICTS_CHECK_NAME,
    IDS_FLAGS_CONFLICTS_CHECK_DESCRIPTION,
    kOsWin,
    SINGLE_VALUE_TYPE(switches::kConflictingModulesCheck)
  },
  {
    "cloud-print-proxy",  // FLAGS:RECORD_UMA
    IDS_FLAGS_CLOUD_PRINT_PROXY_NAME,
    IDS_FLAGS_CLOUD_PRINT_PROXY_DESCRIPTION,
#if defined(GOOGLE_CHROME_BUILD)
    // For a Chrome build, we know we have a PDF plug-in on Windows, so it's
    // fully enabled. Linux still need some final polish.
    kOsLinux,
#else
    // Otherwise, where we know Windows could be working if a viable PDF
    // plug-in could be supplied, we'll keep the lab enabled. Mac always has
    // PDF rasterization available, so no flag needed there.
    kOsWin | kOsLinux,
#endif
    SINGLE_VALUE_TYPE(switches::kEnableCloudPrintProxy)
  },
  {
    "crxless-web-apps",
    IDS_FLAGS_CRXLESS_WEB_APPS_NAME,
    IDS_FLAGS_CRXLESS_WEB_APPS_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kEnableCrxlessWebApps)
  },
  {
    "composited-layer-borders",
    IDS_FLAGS_COMPOSITED_LAYER_BORDERS,
    IDS_FLAGS_COMPOSITED_LAYER_BORDERS_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kShowCompositedLayerBorders)
  },
  {
    "show-fps-counter",
    IDS_FLAGS_SHOW_FPS_COUNTER,
    IDS_FLAGS_SHOW_FPS_COUNTER_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kShowFPSCounter)
  },
  {
    "gpu-canvas-2d",  // FLAGS:RECORD_UMA
    IDS_FLAGS_ACCELERATED_CANVAS_2D_NAME,
    IDS_FLAGS_ACCELERATED_CANVAS_2D_DESCRIPTION,
    kOsWin | kOsLinux | kOsCrOS,
    SINGLE_VALUE_TYPE(switches::kEnableAccelerated2dCanvas)
  },
  {
    "print-preview",  // FLAGS:RECORD_UMA
    IDS_FLAGS_PRINT_PREVIEW_NAME,
    IDS_FLAGS_PRINT_PREVIEW_DESCRIPTION,
    kOsMac | kOsWin | kOsLinux, // This switch is not available in CrOS.
    SINGLE_VALUE_TYPE(switches::kEnablePrintPreview)
  },
  {
    "enable-nacl",  // FLAGS:RECORD_UMA
    IDS_FLAGS_ENABLE_NACL_NAME,
    IDS_FLAGS_ENABLE_NACL_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kEnableNaCl)
  },
  {
    "dns-server",  // FLAGS:RECORD_UMA
    IDS_FLAGS_DNS_SERVER_NAME,
    IDS_FLAGS_DNS_SERVER_DESCRIPTION,
    kOsLinux,
    SINGLE_VALUE_TYPE(switches::kDnsServer)
  },
  {
    "extension-apis",  // FLAGS:RECORD_UMA
    IDS_FLAGS_EXPERIMENTAL_EXTENSION_APIS_NAME,
    IDS_FLAGS_EXPERIMENTAL_EXTENSION_APIS_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kEnableExperimentalExtensionApis)
  },
  {
    "click-to-play",  // FLAGS:RECORD_UMA
    IDS_FLAGS_CLICK_TO_PLAY_NAME,
    IDS_FLAGS_CLICK_TO_PLAY_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kEnableClickToPlay)
  },
  {
    "disable-hyperlink-auditing",
    IDS_FLAGS_DISABLE_HYPERLINK_AUDITING_NAME,
    IDS_FLAGS_DISABLE_HYPERLINK_AUDITING_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kNoPings)
  },
  {
    "experimental-location-features",  // FLAGS:RECORD_UMA
    IDS_FLAGS_EXPERIMENTAL_LOCATION_FEATURES_NAME,
    IDS_FLAGS_EXPERIMENTAL_LOCATION_FEATURES_DESCRIPTION,
    kOsMac | kOsWin | kOsLinux,  // Currently does nothing on CrOS.
    SINGLE_VALUE_TYPE(switches::kExperimentalLocationFeatures)
  },
  {
    "block-reading-third-party-cookies",
    IDS_FLAGS_BLOCK_ALL_THIRD_PARTY_COOKIES_NAME,
    IDS_FLAGS_BLOCK_ALL_THIRD_PARTY_COOKIES_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kBlockReadingThirdPartyCookies)
  },
  {
    "disable-interactive-form-validation",
    IDS_FLAGS_DISABLE_INTERACTIVE_FORM_VALIDATION_NAME,
    IDS_FLAGS_DISABLE_INTERACTIVE_FORM_VALIDATION_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kDisableInteractiveFormValidation)
  },
  {
    "webaudio",
    IDS_FLAGS_WEBAUDIO_NAME,
    IDS_FLAGS_WEBAUDIO_DESCRIPTION,
    kOsMac,  // TODO(crogers): add windows and linux when FFT is ready.
    SINGLE_VALUE_TYPE(switches::kEnableWebAudio)
  },
  {
    "p2papi",
    IDS_FLAGS_P2P_API_NAME,
    IDS_FLAGS_P2P_API_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kEnableP2PApi)
  },
  {
    "focus-existing-tab-on-open",  // FLAGS:RECORD_UMA
    IDS_FLAGS_FOCUS_EXISTING_TAB_ON_OPEN_NAME,
    IDS_FLAGS_FOCUS_EXISTING_TAB_ON_OPEN_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kFocusExistingTabOnOpen)
  },
  {
    "new-tab-page-4",
    IDS_FLAGS_NEW_TAB_PAGE_4_NAME,
    IDS_FLAGS_NEW_TAB_PAGE_4_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kNewTabPage4)
  },
  {
    "tab-groups-context-menu",
    IDS_FLAGS_TAB_GROUPS_CONTEXT_MENU_NAME,
    IDS_FLAGS_TAB_GROUPS_CONTEXT_MENU_DESCRIPTION,
    kOsWin,
    SINGLE_VALUE_TYPE(switches::kEnableTabGroupsContextMenu)
  },
  {
    "ppapi-flash-in-process",
    IDS_FLAGS_PPAPI_FLASH_IN_PROCESS_NAME,
    IDS_FLAGS_PPAPI_FLASH_IN_PROCESS_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kPpapiFlashInProcess)
  },
#if defined(TOOLKIT_GTK)
  {
    "global-gnome-menu",
    IDS_FLAGS_LINUX_GLOBAL_MENUBAR_NAME,
    IDS_FLAGS_LINUX_GLOBAL_MENUBAR_DESCRIPTION,
    kOsLinux,
    SINGLE_VALUE_TYPE(switches::kGlobalGnomeMenu)
  },
#endif
  {
    "enable-experimental-eap",
    IDS_FLAGS_ENABLE_EXPERIMENTAL_EAP_NAME,
    IDS_FLAGS_ENABLE_EXPERIMENTAL_EAP_DESCRIPTION,
    kOsCrOS,
#if defined(OS_CHROMEOS)
    // The switch exists only on Chrome OS.
    SINGLE_VALUE_TYPE(switches::kEnableExperimentalEap)
#else
    SINGLE_VALUE_TYPE("")
#endif
  },
  {
    "enable-vpn",
    IDS_FLAGS_ENABLE_VPN_NAME,
    IDS_FLAGS_ENABLE_VPN_DESCRIPTION,
    kOsCrOS,
#if defined(OS_CHROMEOS)
    // The switch exists only on Chrome OS.
    SINGLE_VALUE_TYPE(switches::kEnableVPN)
#else
    SINGLE_VALUE_TYPE("")
#endif
  },
  {
    "multi-profiles",
    IDS_FLAGS_MULTI_PROFILES_NAME,
    IDS_FLAGS_MULTI_PROFILES_DESCRIPTION,
    kOsAll,
    SINGLE_VALUE_TYPE(switches::kMultiProfiles)
  },
};

const Experiment* experiments = kExperiments;
size_t num_experiments = arraysize(kExperiments);

// Stores and encapsulates the little state that about:flags has.
class FlagsState {
 public:
  FlagsState() : needs_restart_(false) {}
  void ConvertFlagsToSwitches(PrefService* prefs, CommandLine* command_line);
  bool IsRestartNeededToCommitChanges();
  void SetExperimentEnabled(
      PrefService* prefs, const std::string& internal_name, bool enable);
  void RemoveFlagsSwitches(
      std::map<std::string, CommandLine::StringType>* switch_list);
  void reset();

  // Returns the singleton instance of this class
  static FlagsState* GetInstance() {
    return Singleton<FlagsState>::get();
  }

 private:
  bool needs_restart_;
  std::map<std::string, std::string> flags_switches_;

  DISALLOW_COPY_AND_ASSIGN(FlagsState);
};

// Extracts the list of enabled lab experiments from preferences and stores them
// in a set.
void GetEnabledFlags(const PrefService* prefs, std::set<std::string>* result) {
  const ListValue* enabled_experiments = prefs->GetList(
      prefs::kEnabledLabsExperiments);
  if (!enabled_experiments)
    return;

  for (ListValue::const_iterator it = enabled_experiments->begin();
       it != enabled_experiments->end();
       ++it) {
    std::string experiment_name;
    if (!(*it)->GetAsString(&experiment_name)) {
      LOG(WARNING) << "Invalid entry in " << prefs::kEnabledLabsExperiments;
      continue;
    }
    result->insert(experiment_name);
  }
}

// Takes a set of enabled lab experiments
void SetEnabledFlags(
    PrefService* prefs, const std::set<std::string>& enabled_experiments) {
  ListPrefUpdate update(prefs, prefs::kEnabledLabsExperiments);
  ListValue* experiments_list = update.Get();

  experiments_list->Clear();
  for (std::set<std::string>::const_iterator it = enabled_experiments.begin();
       it != enabled_experiments.end();
       ++it) {
    experiments_list->Append(new StringValue(*it));
  }
}

// Returns the name used in prefs for the choice at the specified index.
std::string NameForChoice(const Experiment& e, int index) {
  DCHECK_EQ(Experiment::MULTI_VALUE, e.type);
  DCHECK_LT(index, e.num_choices);
  return std::string(e.internal_name) + about_flags::testing::kMultiSeparator +
      base::IntToString(index);
}

// Adds the internal names for the specified experiment to |names|.
void AddInternalName(const Experiment& e, std::set<std::string>* names) {
  if (e.type == Experiment::SINGLE_VALUE) {
    names->insert(e.internal_name);
  } else {
    DCHECK_EQ(Experiment::MULTI_VALUE, e.type);
    for (int i = 0; i < e.num_choices; ++i)
      names->insert(NameForChoice(e, i));
  }
}

// Confirms that an experiment is valid, used in a DCHECK in
// SanitizeList below.
bool ValidateExperiment(const Experiment& e) {
  switch (e.type) {
    case Experiment::SINGLE_VALUE:
      DCHECK_EQ(0, e.num_choices);
      DCHECK(!e.choices);
      break;
    case Experiment::MULTI_VALUE:
      DCHECK_GT(e.num_choices, 0);
      DCHECK(e.choices);
      DCHECK(e.choices[0].command_line_switch);
      DCHECK_EQ('\0', e.choices[0].command_line_switch[0]);
      break;
    default:
      NOTREACHED();
  }
  return true;
}

// Removes all experiments from prefs::kEnabledLabsExperiments that are
// unknown, to prevent this list to become very long as experiments are added
// and removed.
void SanitizeList(PrefService* prefs) {
  std::set<std::string> known_experiments;
  for (size_t i = 0; i < num_experiments; ++i) {
    DCHECK(ValidateExperiment(experiments[i]));
    AddInternalName(experiments[i], &known_experiments);
  }

  std::set<std::string> enabled_experiments;
  GetEnabledFlags(prefs, &enabled_experiments);

  std::set<std::string> new_enabled_experiments;
  std::set_intersection(
      known_experiments.begin(), known_experiments.end(),
      enabled_experiments.begin(), enabled_experiments.end(),
      std::inserter(new_enabled_experiments, new_enabled_experiments.begin()));

  SetEnabledFlags(prefs, new_enabled_experiments);
}

void GetSanitizedEnabledFlags(
    PrefService* prefs, std::set<std::string>* result) {
  SanitizeList(prefs);
  GetEnabledFlags(prefs, result);
}

// Variant of GetSanitizedEnabledFlags that also removes any flags that aren't
// enabled on the current platform.
void GetSanitizedEnabledFlagsForCurrentPlatform(
    PrefService* prefs, std::set<std::string>* result) {
  GetSanitizedEnabledFlags(prefs, result);

  // Filter out any experiments that aren't enabled on the current platform.  We
  // don't remove these from prefs else syncing to a platform with a different
  // set of experiments would be lossy.
  std::set<std::string> platform_experiments;
  int current_platform = GetCurrentPlatform();
  for (size_t i = 0; i < num_experiments; ++i) {
    if (experiments[i].supported_platforms & current_platform)
      AddInternalName(experiments[i], &platform_experiments);
  }

  std::set<std::string> new_enabled_experiments;
  std::set_intersection(
      platform_experiments.begin(), platform_experiments.end(),
      result->begin(), result->end(),
      std::inserter(new_enabled_experiments, new_enabled_experiments.begin()));

  result->swap(new_enabled_experiments);
}

// Returns the Value representing the choice data in the specified experiment.
Value* CreateChoiceData(const Experiment& experiment,
                        const std::set<std::string>& enabled_experiments) {
  DCHECK_EQ(Experiment::MULTI_VALUE, experiment.type);
  ListValue* result = new ListValue;
  for (int i = 0; i < experiment.num_choices; ++i) {
    const Experiment::Choice& choice = experiment.choices[i];
    DictionaryValue* value = new DictionaryValue;
    std::string name = NameForChoice(experiment, i);
    value->SetString("description",
                     l10n_util::GetStringUTF16(choice.description_id));
    value->SetString("internal_name", name);
    value->SetBoolean("selected", enabled_experiments.count(name) > 0);
    result->Append(value);
  }
  return result;
}

}  // namespace

void ConvertFlagsToSwitches(PrefService* prefs, CommandLine* command_line) {
  FlagsState::GetInstance()->ConvertFlagsToSwitches(prefs, command_line);
}

ListValue* GetFlagsExperimentsData(PrefService* prefs) {
  std::set<std::string> enabled_experiments;
  GetSanitizedEnabledFlags(prefs, &enabled_experiments);

  int current_platform = GetCurrentPlatform();

  ListValue* experiments_data = new ListValue();
  for (size_t i = 0; i < num_experiments; ++i) {
    const Experiment& experiment = experiments[i];
    if (!(experiment.supported_platforms & current_platform))
      continue;

    DictionaryValue* data = new DictionaryValue();
    data->SetString("internal_name", experiment.internal_name);
    data->SetString("name",
                    l10n_util::GetStringUTF16(experiment.visible_name_id));
    data->SetString("description",
                    l10n_util::GetStringUTF16(
                        experiment.visible_description_id));

    switch (experiment.type) {
      case Experiment::SINGLE_VALUE:
        data->SetBoolean(
            "enabled",
            enabled_experiments.count(experiment.internal_name) > 0);
        break;
      case Experiment::MULTI_VALUE:
        data->Set("choices", CreateChoiceData(experiment, enabled_experiments));
        break;
      default:
        NOTREACHED();
    }

    experiments_data->Append(data);
  }
  return experiments_data;
}

bool IsRestartNeededToCommitChanges() {
  return FlagsState::GetInstance()->IsRestartNeededToCommitChanges();
}

void SetExperimentEnabled(
    PrefService* prefs, const std::string& internal_name, bool enable) {
  FlagsState::GetInstance()->SetExperimentEnabled(prefs, internal_name, enable);
}

void RemoveFlagsSwitches(
    std::map<std::string, CommandLine::StringType>* switch_list) {
  FlagsState::GetInstance()->RemoveFlagsSwitches(switch_list);
}

int GetCurrentPlatform() {
#if defined(OS_MACOSX)
  return kOsMac;
#elif defined(OS_WIN)
  return kOsWin;
#elif defined(OS_CHROMEOS)  // Needs to be before the OS_LINUX check.
  return kOsCrOS;
#elif defined(OS_LINUX)
  return kOsLinux;
#else
#error Unknown platform
#endif
}

void RecordUMAStatistics(const PrefService* prefs) {
  std::set<std::string> flags;
  GetEnabledFlags(prefs, &flags);
  for (std::set<std::string>::iterator it = flags.begin(); it != flags.end();
       ++it) {
    std::string action("AboutFlags_");
    action += *it;
    UserMetrics::RecordComputedAction(action);
  }
  // Since flag metrics are recorded every startup, add a tick so that the
  // stats can be made meaningful.
  if (flags.size())
    UserMetrics::RecordAction(UserMetricsAction("AboutFlags_StartupTick"));
  UserMetrics::RecordAction(UserMetricsAction("StartupTick"));
}

//////////////////////////////////////////////////////////////////////////////
// FlagsState implementation.

namespace {

void FlagsState::ConvertFlagsToSwitches(
    PrefService* prefs, CommandLine* command_line) {
  if (command_line->HasSwitch(switches::kNoExperiments))
    return;

  std::set<std::string> enabled_experiments;

  GetSanitizedEnabledFlagsForCurrentPlatform(prefs, &enabled_experiments);

  typedef std::map<std::string, std::pair<std::string, std::string> >
      NameToSwitchAndValueMap;
  NameToSwitchAndValueMap name_to_switch_map;
  for (size_t i = 0; i < num_experiments; ++i) {
    const Experiment& e = experiments[i];
    if (e.type == Experiment::SINGLE_VALUE) {
      name_to_switch_map[e.internal_name] =
          std::pair<std::string, std::string>(e.command_line_switch,
                                              e.command_line_value);
    } else {
      for (int j = 0; j < e.num_choices; ++j)
        name_to_switch_map[NameForChoice(e, j)] =
            std::pair<std::string, std::string>(
                e.choices[j].command_line_switch,
                e.choices[j].command_line_value);
    }
  }

  command_line->AppendSwitch(switches::kFlagSwitchesBegin);
  flags_switches_.insert(
      std::pair<std::string, std::string>(switches::kFlagSwitchesBegin,
                                          std::string()));
  for (std::set<std::string>::iterator it = enabled_experiments.begin();
       it != enabled_experiments.end();
       ++it) {
    const std::string& experiment_name = *it;
    NameToSwitchAndValueMap::const_iterator name_to_switch_it =
        name_to_switch_map.find(experiment_name);
    if (name_to_switch_it == name_to_switch_map.end()) {
      NOTREACHED();
      continue;
    }

    const std::pair<std::string, std::string>&
        switch_and_value_pair = name_to_switch_it->second;

    command_line->AppendSwitchASCII(switch_and_value_pair.first,
                                    switch_and_value_pair.second);
    flags_switches_[switch_and_value_pair.first] = switch_and_value_pair.second;
  }
  command_line->AppendSwitch(switches::kFlagSwitchesEnd);
  flags_switches_.insert(
      std::pair<std::string, std::string>(switches::kFlagSwitchesEnd,
                                          std::string()));
}

bool FlagsState::IsRestartNeededToCommitChanges() {
  return needs_restart_;
}

void FlagsState::SetExperimentEnabled(
    PrefService* prefs, const std::string& internal_name, bool enable) {
  needs_restart_ = true;

  size_t at_index = internal_name.find(about_flags::testing::kMultiSeparator);
  if (at_index != std::string::npos) {
    DCHECK(enable);
    // We're being asked to enable a multi-choice experiment. Disable the
    // currently selected choice.
    DCHECK_NE(at_index, 0u);
    const std::string experiment_name = internal_name.substr(0, at_index);
    SetExperimentEnabled(prefs, experiment_name, false);

    // And enable the new choice, if it is not the default first choice.
    if (internal_name != experiment_name + "@0") {
      std::set<std::string> enabled_experiments;
      GetSanitizedEnabledFlags(prefs, &enabled_experiments);
      enabled_experiments.insert(internal_name);
      SetEnabledFlags(prefs, enabled_experiments);
    }
    return;
  }

  std::set<std::string> enabled_experiments;
  GetSanitizedEnabledFlags(prefs, &enabled_experiments);

  const Experiment* e = NULL;
  for (size_t i = 0; i < num_experiments; ++i) {
    if (experiments[i].internal_name == internal_name) {
      e = experiments + i;
      break;
    }
  }
  DCHECK(e);

  if (e->type == Experiment::SINGLE_VALUE) {
    if (enable)
      enabled_experiments.insert(internal_name);
    else
      enabled_experiments.erase(internal_name);
  } else {
    if (enable) {
      // Enable the first choice.
      enabled_experiments.insert(NameForChoice(*e, 0));
    } else {
      // Find the currently enabled choice and disable it.
      for (int i = 0; i < e->num_choices; ++i) {
        std::string choice_name = NameForChoice(*e, i);
        if (enabled_experiments.find(choice_name) !=
            enabled_experiments.end()) {
          enabled_experiments.erase(choice_name);
          // Continue on just in case there's a bug and more than one
          // experiment for this choice was enabled.
        }
      }
    }
  }

  SetEnabledFlags(prefs, enabled_experiments);
}

void FlagsState::RemoveFlagsSwitches(
    std::map<std::string, CommandLine::StringType>* switch_list) {
  for (std::map<std::string, std::string>::const_iterator
           it = flags_switches_.begin(); it != flags_switches_.end(); ++it) {
    switch_list->erase(it->first);
  }
}

void FlagsState::reset() {
  needs_restart_ = false;
  flags_switches_.clear();
}

} // namespace

namespace testing {

// WARNING: '@' is also used in the html file. If you update this constant you
// also need to update the html file.
const char kMultiSeparator[] = "@";

void ClearState() {
  FlagsState::GetInstance()->reset();
}

void SetExperiments(const Experiment* e, size_t count) {
  if (!e) {
    experiments = kExperiments;
    num_experiments = arraysize(kExperiments);
  } else {
    experiments = e;
    num_experiments = count;
  }
}

const Experiment* GetExperiments(size_t* count) {
  *count = num_experiments;
  return experiments;
}

}  // namespace testing

}  // namespace about_flags
