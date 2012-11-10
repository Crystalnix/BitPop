// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_SPELLCHECKER_SPELLCHECK_H_
#define CHROME_RENDERER_SPELLCHECKER_SPELLCHECK_H_

#include <string>
#include <queue>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/platform_file.h"
#include "base/string16.h"
#include "base/time.h"
#include "chrome/renderer/spellchecker/spellcheck_worditerator.h"
#include "content/public/renderer/render_process_observer.h"
#include "ipc/ipc_platform_file.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebVector.h"
#include "unicode/uscript.h"

class Hunspell;
struct SpellCheckResult;

namespace file_util {
class MemoryMappedFile;
}

namespace WebKit {
class WebTextCheckingCompletion;
struct WebTextCheckingResult;
}

// TODO(morrita): Needs reorg with SpellCheckProvider.
// See http://crbug.com/73699.
class SpellCheck : public content::RenderProcessObserver,
                   public base::SupportsWeakPtr<SpellCheck> {
 public:
  SpellCheck();
  virtual ~SpellCheck();

  void Init(base::PlatformFile file,
            const std::vector<std::string>& custom_words,
            const std::string& language);

  // SpellCheck a word.
  // Returns true if spelled correctly, false otherwise.
  // If the spellchecker failed to initialize, always returns true.
  // The |tag| parameter should either be a unique identifier for the document
  // that the word came from (if the current platform requires it), or 0.
  // In addition, finds the suggested words for a given word
  // and puts them into |*optional_suggestions|.
  // If the word is spelled correctly, the vector is empty.
  // If optional_suggestions is NULL, suggested words will not be looked up.
  // Note that Doing suggest lookups can be slow.
  bool SpellCheckWord(const char16* in_word,
                      int in_word_len,
                      int tag,
                      int* misspelling_start,
                      int* misspelling_len,
                      std::vector<string16>* optional_suggestions);

  // SpellCheck a paragrpah.
  // Returns true if |text| is correctly spelled, false otherwise.
  // If the spellchecker failed to initialize, always returns true.
  bool SpellCheckParagraph(
      const string16& text,
      WebKit::WebVector<WebKit::WebTextCheckingResult>* results);

  // Find a possible correctly spelled word for a misspelled word. Computes an
  // empty string if input misspelled word is too long, there is ambiguity, or
  // the correct spelling cannot be determined.
  // NOTE: If using the platform spellchecker, this will send a *lot* of sync
  // IPCs. We should probably refactor this if we ever plan to take it out from
  // behind its command line flag.
  string16 GetAutoCorrectionWord(const string16& word, int tag);

  // Requests to spellcheck the specified text in the background. This function
  // posts a background task and calls SpellCheckParagraph() in the task.
  void RequestTextChecking(const string16& text,
                           int offset,
                           WebKit::WebTextCheckingCompletion* completion);

  // Creates a list of WebTextCheckingResult objects (used by WebKit) from a
  // list of SpellCheckResult objects (used by Chrome). This function also
  // checks misspelled words returned by the Spelling service and changes the
  // underline colors of contextually-misspelled words.
  void CreateTextCheckingResults(
      int line_offset,
      const string16& line_text,
      const std::vector<SpellCheckResult>& spellcheck_results,
      WebKit::WebVector<WebKit::WebTextCheckingResult>* textcheck_results);

  // Returns true if the spellchecker delegate checking to a system-provided
  // checker on the browser process.
  bool is_using_platform_spelling_engine() const {
    return is_using_platform_spelling_engine_;
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(SpellCheckTest, GetAutoCorrectionWord_EN_US);
  FRIEND_TEST_ALL_PREFIXES(SpellCheckTest,
      RequestSpellCheckMultipleTimesWithoutInitialization);

  class SpellCheckRequestParam;

  // RenderProcessObserver implementation:
  virtual bool OnControlMessageReceived(const IPC::Message& message) OVERRIDE;

  // Message handlers.
  void OnInit(IPC::PlatformFileForTransit bdict_file,
              const std::vector<std::string>& custom_words,
              const std::string& language,
              bool auto_spell_correct);
  void OnWordAdded(const std::string& word);
  void OnEnableAutoSpellCorrect(bool enable);

  // Initializes the Hunspell dictionary, or does nothing if |hunspell_| is
  // non-null. This blocks.
  void InitializeHunspell();

  // If there is no dictionary file, then this requests one from the browser
  // and does not block. In this case it returns true.
  // If there is a dictionary file, but Hunspell has not been loaded, then
  // this loads Hunspell.
  // If Hunspell is already loaded, this does nothing. In both the latter cases
  // it returns false, meaning that it is OK to continue spellchecking.
  bool InitializeIfNeeded();

  // When called, relays the request to check the spelling to the proper
  // backend, either hunspell or a platform-specific backend.
  bool CheckSpelling(const string16& word_to_check, int tag);

  // Posts delayed spellcheck task and clear it if any.
  void PostDelayedSpellCheckTask();

  // Performs spell checking from the request queue.
  void PerformSpellCheck();

  // When called, relays the request to fill the list with suggestions to
  // the proper backend, either hunspell or a platform-specific backend.
  void FillSuggestionList(const string16& wrong_word,
                          std::vector<string16>* optional_suggestions);

  // Returns whether or not the given word is a contraction of valid words
  // (e.g. "word:word").
  bool IsValidContraction(const string16& word, int tag);

  // Add the given custom word to |hunspell_|.
  void AddWordToHunspell(const std::string& word);

  // We memory-map the BDict file.
  scoped_ptr<file_util::MemoryMappedFile> bdict_file_;

  // The hunspell dictionary in use.
  scoped_ptr<Hunspell> hunspell_;

  base::PlatformFile file_;
  std::vector<std::string> custom_words_;

  // Represents character attributes used for filtering out characters which
  // are not supported by this SpellCheck object.
  SpellcheckCharAttribute character_attributes_;

  // Represents word iterators used in this spellchecker. The |text_iterator_|
  // splits text provided by WebKit into words, contractions, or concatenated
  // words. The |contraction_iterator_| splits a concatenated word extracted by
  // |text_iterator_| into word components so we can treat a concatenated word
  // consisting only of correct words as a correct word.
  SpellcheckWordIterator text_iterator_;
  SpellcheckWordIterator contraction_iterator_;

  // Remember state for auto spell correct.
  bool auto_spell_correct_turned_on_;

  // True if a platform-specific spellchecking engine is being used,
  // and False if hunspell is being used.
  bool is_using_platform_spelling_engine_;

  // This flags is true if we have been intialized.
  // The value indicates whether we should request a
  // dictionary from the browser when the render view asks us to check the
  // spelling of a word.
  bool initialized_;

  // This flags is true if we have requested dictionary.
  bool dictionary_requested_;

  // The parameters of a pending background-spellchecking request. When WebKit
  // sends a background-spellchecking request before initializing hunspell,
  // we save its parameters and start spellchecking after we finish initializing
  // hunspell. (When WebKit sends two or more requests, we cancel the previous
  // requests so we do not have to use vectors.)
  scoped_refptr<SpellCheckRequestParam> pending_request_param_;

  // The set of params already requested. When finishing the request, the params
  // should be removed from the set.
  std::queue<scoped_refptr<SpellCheckRequestParam> > requested_params_;

  DISALLOW_COPY_AND_ASSIGN(SpellCheck);
};

#endif  // CHROME_RENDERER_SPELLCHECKER_SPELLCHECK_H_
