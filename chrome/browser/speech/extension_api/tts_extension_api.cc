// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/speech/extension_api/tts_extension_api.h"

#include <string>

#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/speech/extension_api/tts_extension_api_constants.h"
#include "chrome/browser/speech/extension_api/tts_extension_api_controller.h"
#include "ui/base/l10n/l10n_util.h"

namespace constants = tts_extension_api_constants;

bool ExtensionTtsSpeakFunction::RunImpl() {
  std::string text;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &text));
  if (text.size() > 32768) {
    error_ = constants::kErrorUtteranceTooLong;
    return false;
  }

  scoped_ptr<DictionaryValue> options(new DictionaryValue());
  if (args_->GetSize() >= 2) {
    DictionaryValue* temp_options = NULL;
    if (args_->GetDictionary(1, &temp_options))
      options.reset(temp_options->DeepCopy());
  }

  std::string voice_name;
  if (options->HasKey(constants::kVoiceNameKey)) {
    EXTENSION_FUNCTION_VALIDATE(
        options->GetString(constants::kVoiceNameKey, &voice_name));
  }

  std::string lang;
  if (options->HasKey(constants::kLangKey))
    EXTENSION_FUNCTION_VALIDATE(options->GetString(constants::kLangKey, &lang));
  if (!lang.empty() && !l10n_util::IsValidLocaleSyntax(lang)) {
    error_ = constants::kErrorInvalidLang;
    return false;
  }

  std::string gender;
  if (options->HasKey(constants::kGenderKey))
    EXTENSION_FUNCTION_VALIDATE(
        options->GetString(constants::kGenderKey, &gender));
  if (!gender.empty() &&
      gender != constants::kGenderFemale &&
      gender != constants::kGenderMale) {
    error_ = constants::kErrorInvalidGender;
    return false;
  }

  double rate = 1.0;
  if (options->HasKey(constants::kRateKey)) {
    EXTENSION_FUNCTION_VALIDATE(
        options->GetDouble(constants::kRateKey, &rate));
    if (rate < 0.1 || rate > 10.0) {
      error_ = constants::kErrorInvalidRate;
      return false;
    }
  }

  double pitch = 1.0;
  if (options->HasKey(constants::kPitchKey)) {
    EXTENSION_FUNCTION_VALIDATE(
        options->GetDouble(constants::kPitchKey, &pitch));
    if (pitch < 0.0 || pitch > 2.0) {
      error_ = constants::kErrorInvalidPitch;
      return false;
    }
  }

  double volume = 1.0;
  if (options->HasKey(constants::kVolumeKey)) {
    EXTENSION_FUNCTION_VALIDATE(
        options->GetDouble(constants::kVolumeKey, &volume));
    if (volume < 0.0 || volume > 1.0) {
      error_ = constants::kErrorInvalidVolume;
      return false;
    }
  }

  bool can_enqueue = false;
  if (options->HasKey(constants::kEnqueueKey)) {
    EXTENSION_FUNCTION_VALIDATE(
        options->GetBoolean(constants::kEnqueueKey, &can_enqueue));
  }

  std::set<std::string> required_event_types;
  if (options->HasKey(constants::kRequiredEventTypesKey)) {
    ListValue* list;
    EXTENSION_FUNCTION_VALIDATE(
        options->GetList(constants::kRequiredEventTypesKey, &list));
    for (size_t i = 0; i < list->GetSize(); i++) {
      std::string event_type;
      if (!list->GetString(i, &event_type))
        required_event_types.insert(event_type);
    }
  }

  std::set<std::string> desired_event_types;
  if (options->HasKey(constants::kDesiredEventTypesKey)) {
    ListValue* list;
    EXTENSION_FUNCTION_VALIDATE(
        options->GetList(constants::kDesiredEventTypesKey, &list));
    for (size_t i = 0; i < list->GetSize(); i++) {
      std::string event_type;
      if (!list->GetString(i, &event_type))
        desired_event_types.insert(event_type);
    }
  }

  std::string voice_extension_id;
  if (options->HasKey(constants::kExtensionIdKey)) {
    EXTENSION_FUNCTION_VALIDATE(
        options->GetString(constants::kExtensionIdKey, &voice_extension_id));
  }

  int src_id = -1;
  if (options->HasKey(constants::kSrcIdKey)) {
    EXTENSION_FUNCTION_VALIDATE(
        options->GetInteger(constants::kSrcIdKey, &src_id));
  }

  // If we got this far, the arguments were all in the valid format, so
  // send the success response to the callback now - this ensures that
  // the callback response always arrives before events, which makes
  // the behavior more predictable and easier to write unit tests for too.
  SendResponse(true);

  UtteranceContinuousParameters continuous_params;
  continuous_params.rate = rate;
  continuous_params.pitch = pitch;
  continuous_params.volume = volume;

  Utterance* utterance = new Utterance(profile());
  utterance->set_text(text);
  utterance->set_voice_name(voice_name);
  utterance->set_src_extension_id(extension_id());
  utterance->set_src_id(src_id);
  utterance->set_src_url(source_url());
  utterance->set_lang(lang);
  utterance->set_gender(gender);
  utterance->set_continuous_parameters(continuous_params);
  utterance->set_can_enqueue(can_enqueue);
  utterance->set_required_event_types(required_event_types);
  utterance->set_desired_event_types(desired_event_types);
  utterance->set_extension_id(voice_extension_id);
  utterance->set_options(options.get());

  ExtensionTtsController* controller = ExtensionTtsController::GetInstance();
  controller->SpeakOrEnqueue(utterance);
  return true;
}

bool ExtensionTtsStopSpeakingFunction::RunImpl() {
  ExtensionTtsController::GetInstance()->Stop();
  return true;
}

bool ExtensionTtsIsSpeakingFunction::RunImpl() {
  SetResult(Value::CreateBooleanValue(
      ExtensionTtsController::GetInstance()->IsSpeaking()));
  return true;
}

bool ExtensionTtsGetVoicesFunction::RunImpl() {
  SetResult(ExtensionTtsController::GetInstance()->GetVoices(profile()));
  return true;
}
