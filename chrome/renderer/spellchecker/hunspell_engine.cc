// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hunspell_engine.h"

#include "base/metrics/histogram.h"
#include "base/time.h"
#include "chrome/common/spellcheck_common.h"
#include "chrome/common/spellcheck_messages.h"
#include "content/public/renderer/render_thread.h"
#include "third_party/hunspell/src/hunspell/hunspell.hxx"

using base::TimeTicks;
using content::RenderThread;

#if !defined(OS_MACOSX)
SpellingEngine* CreateNativeSpellingEngine() {
  return new HunspellEngine();
}
#endif

HunspellEngine::HunspellEngine()
    : file_(base::kInvalidPlatformFileValue),
      initialized_(false),
      dictionary_requested_(false) {
  // Wait till we check the first word before doing any initializing.
}

HunspellEngine::~HunspellEngine() {
}

void HunspellEngine::Init(base::PlatformFile file,
                          const std::vector<std::string>& custom_words) {
  initialized_ = true;
  hunspell_.reset();
  bdict_file_.reset();
  file_ = file;

  custom_words_.insert(custom_words_.end(),
                       custom_words.begin(), custom_words.end());

  // Delay the actual initialization of hunspell until it is needed.
}

void HunspellEngine::InitializeHunspell() {
  if (hunspell_.get())
    return;

  bdict_file_.reset(new file_util::MemoryMappedFile);

  if (bdict_file_->Initialize(file_)) {
    TimeTicks debug_start_time = base::Histogram::DebugNow();

    hunspell_.reset(
        new Hunspell(bdict_file_->data(), bdict_file_->length()));

    // Add custom words to Hunspell.
    chrome::spellcheck_common::WordList::iterator it;
    for (it = custom_words_.begin(); it != custom_words_.end(); ++it)
      AddWordToHunspell(*it);

    DHISTOGRAM_TIMES("Spellcheck.InitTime",
                     base::Histogram::DebugNow() - debug_start_time);
  } else {
    NOTREACHED() << "Could not mmap spellchecker dictionary.";
  }
}

void HunspellEngine::AddWordToHunspell(const std::string& word) {
  if (!word.empty() && word.length() < MAXWORDLEN)
    hunspell_->add(word.c_str());
}

void HunspellEngine::RemoveWordFromHunspell(const std::string& word) {
  if (!word.empty() && word.length() < MAXWORDLEN)
    hunspell_->remove(word.c_str());
}

bool HunspellEngine::CheckSpelling(const string16& word_to_check, int tag) {
  bool word_correct = false;
  std::string word_to_check_utf8(UTF16ToUTF8(word_to_check));
  // Hunspell shouldn't let us exceed its max, but check just in case
  if (word_to_check_utf8.length() < MAXWORDLEN) {
    if (hunspell_.get()) {
      // |hunspell_->spell| returns 0 if the word is spelled correctly and
      // non-zero otherwsie.
      word_correct = (hunspell_->spell(word_to_check_utf8.c_str()) != 0);
    } else {
      // If |hunspell_| is NULL here, an error has occurred, but it's better
      // to check rather than crash.
      word_correct = true;
    }
  }

  return word_correct;
}

void HunspellEngine::FillSuggestionList(
    const string16& wrong_word,
    std::vector<string16>* optional_suggestions) {
  // If |hunspell_| is NULL here, an error has occurred, but it's better
  // to check rather than crash.
  // TODO(groby): Technically, it's not. We should track down the issue.
  if (!hunspell_.get())
    return;

  char** suggestions;
  int number_of_suggestions =
      hunspell_->suggest(&suggestions, UTF16ToUTF8(wrong_word).c_str());

  // Populate the vector of WideStrings.
  for (int i = 0; i < number_of_suggestions; ++i) {
    if (i < chrome::spellcheck_common::kMaxSuggestions)
      optional_suggestions->push_back(UTF8ToUTF16(suggestions[i]));
    free(suggestions[i]);
  }
  if (suggestions != NULL)
    free(suggestions);
}

void HunspellEngine::OnWordAdded(const std::string& word) {
  if (!hunspell_.get()) {
    // Save it for later---add it when hunspell is initialized.
    custom_words_.push_back(word);
  } else {
    AddWordToHunspell(word);
  }
}

void HunspellEngine::OnWordRemoved(const std::string& word) {
  if (!hunspell_.get()) {
    chrome::spellcheck_common::WordList::iterator it = std::find(
        custom_words_.begin(), custom_words_.end(), word);
    if (it != custom_words_.end())
      custom_words_.erase(it);
  } else {
    RemoveWordFromHunspell(word);
  }
}

bool HunspellEngine::InitializeIfNeeded() {
  if (!initialized_ && !dictionary_requested_) {
    // RenderThread will not exist in test.
    if (RenderThread::Get())
      RenderThread::Get()->Send(new SpellCheckHostMsg_RequestDictionary);
    dictionary_requested_ = true;
    return true;
  }

  // Don't initialize if hunspell is disabled.
  if (file_ != base::kInvalidPlatformFileValue)
    InitializeHunspell();

  return !initialized_;
}

bool HunspellEngine::IsEnabled() {
  return file_ != base::kInvalidPlatformFileValue;
}
