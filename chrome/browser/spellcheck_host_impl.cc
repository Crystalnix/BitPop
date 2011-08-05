// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/spellcheck_host_impl.h"

#include <set>

#include "base/file_util.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/string_split.h"
#include "base/threading/thread_restrictions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/spellcheck_host_observer.h"
#include "chrome/browser/spellchecker_platform_engine.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/spellcheck_common.h"
#include "chrome/common/spellcheck_messages.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/common/notification_service.h"
#include "googleurl/src/gurl.h"
#include "net/url_request/url_request_context_getter.h"
#include "third_party/hunspell/google/bdict.h"
#include "ui/base/l10n/l10n_util.h"
#if defined(OS_MACOSX)
#include "base/metrics/histogram.h"
#endif

namespace {

FilePath GetFirstChoiceFilePath(const std::string& language) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  FilePath dict_dir;
  PathService::Get(chrome::DIR_APP_DICTIONARIES, &dict_dir);
  return SpellCheckCommon::GetVersionedFileName(language, dict_dir);
}

#if defined(OS_MACOSX)
// Collect metrics on how often Hunspell is used on OS X vs the native
// spellchecker.
void RecordSpellCheckStats(bool native_spellchecker_used,
                           const std::string& language) {
  static std::set<std::string> languages_seen;

  // Only count a language code once for each session..
  if (languages_seen.find(language) != languages_seen.end()) {
    return;
  }
  languages_seen.insert(language);

  enum {
    SPELLCHECK_OSX_NATIVE_SPELLCHECKER_USED = 0,
    SPELLCHECK_HUNSPELL_USED = 1
  };

  bool engine_used = native_spellchecker_used ?
                         SPELLCHECK_OSX_NATIVE_SPELLCHECKER_USED :
                         SPELLCHECK_HUNSPELL_USED;

  UMA_HISTOGRAM_COUNTS("SpellCheck.OSXEngineUsed", engine_used);
}
#endif

#if defined(OS_WIN)
FilePath GetFallbackFilePath(const FilePath& first_choice) {
  FilePath dict_dir;
  PathService::Get(chrome::DIR_USER_DATA, &dict_dir);
  return dict_dir.Append(first_choice.BaseName());
}
#endif

}  // namespace

// Constructed on UI thread.
SpellCheckHostImpl::SpellCheckHostImpl(
    SpellCheckHostObserver* observer,
    const std::string& language,
    net::URLRequestContextGetter* request_context_getter)
    : observer_(observer),
      language_(language),
      file_(base::kInvalidPlatformFileValue),
      tried_to_download_(false),
      use_platform_spellchecker_(false),
      request_context_getter_(request_context_getter),
      misspelled_word_count_(0),
      spellchecked_word_count_(0),
      replaced_word_count_(0) {
  DCHECK(observer_);
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  FilePath personal_file_directory;
  PathService::Get(chrome::DIR_USER_DATA, &personal_file_directory);
  custom_dictionary_file_ =
      personal_file_directory.Append(chrome::kCustomDictionaryFileName);

  registrar_.Add(this, NotificationType::RENDERER_PROCESS_CREATED,
                 NotificationService::AllSources());
}

SpellCheckHostImpl::~SpellCheckHostImpl() {
  if (file_ != base::kInvalidPlatformFileValue)
    base::ClosePlatformFile(file_);
}

void SpellCheckHostImpl::Initialize() {
  if (SpellCheckerPlatform::SpellCheckerAvailable() &&
      SpellCheckerPlatform::PlatformSupportsLanguage(language_)) {
#if defined(OS_MACOSX)
    RecordSpellCheckStats(true, language_);
#endif
    use_platform_spellchecker_ = true;
    SpellCheckerPlatform::SetLanguage(language_);
    MessageLoop::current()->PostTask(FROM_HERE,
        NewRunnableMethod(this,
            &SpellCheckHostImpl::InformObserverOfInitialization));
    return;
  }

#if defined(OS_MACOSX)
  RecordSpellCheckStats(false, language_);
#endif

  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(this,
                        &SpellCheckHostImpl::InitializeDictionaryLocation));
}

void SpellCheckHostImpl::UnsetObserver() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  observer_ = NULL;
  request_context_getter_ = NULL;
  fetcher_.reset();
  registrar_.RemoveAll();
}

void SpellCheckHostImpl::InitForRenderer(RenderProcessHost* process) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  PrefService* prefs = process->profile()->GetPrefs();
  IPC::PlatformFileForTransit file;

  if (GetDictionaryFile() != base::kInvalidPlatformFileValue) {
#if defined(OS_POSIX)
    file = base::FileDescriptor(GetDictionaryFile(), false);
#elif defined(OS_WIN)
    ::DuplicateHandle(::GetCurrentProcess(),
                      GetDictionaryFile(),
                      process->GetHandle(),
                      &file,
                      0,
                      false,
                      DUPLICATE_SAME_ACCESS);
#endif
  }

  process->Send(new SpellCheckMsg_Init(
      file,
      GetCustomWords(),
      GetLanguage(),
      prefs->GetBoolean(prefs::kEnableAutoSpellCorrect)));
}

void SpellCheckHostImpl::AddWord(const std::string& word) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  custom_words_.push_back(word);
  SpellCheckHost::RecordCustomWordCountStats(custom_words_.size());
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(this,
          &SpellCheckHostImpl::WriteWordToCustomDictionary, word));

  for (RenderProcessHost::iterator i(RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    i.GetCurrentValue()->Send(new SpellCheckMsg_WordAdded(GetLastAddedFile()));
  }
}

void SpellCheckHostImpl::InitializeDictionaryLocation() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  // Initialize the BDICT path. This initialization should be in the FILE thread
  // because it checks if there is a "Dictionaries" directory and create it.
  if (bdict_file_path_.empty())
    bdict_file_path_ = GetFirstChoiceFilePath(language_);

#if defined(OS_WIN)
  // Check if the dictionary exists in the fallback location. If so, use it
  // rather than downloading anew.
  FilePath fallback = GetFallbackFilePath(bdict_file_path_);
  if (!file_util::PathExists(bdict_file_path_) &&
      file_util::PathExists(fallback)) {
    bdict_file_path_ = fallback;
  }
#endif

  InitializeInternal();
}

void SpellCheckHostImpl::InitializeInternal() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  if (!observer_)
    return;

  file_ = base::CreatePlatformFile(
      bdict_file_path_,
      base::PLATFORM_FILE_READ | base::PLATFORM_FILE_OPEN,
      NULL, NULL);

  // File didn't exist. Download it.
  if (file_ == base::kInvalidPlatformFileValue && !tried_to_download_ &&
      request_context_getter_) {
    // We download from the ui thread because we need to know that
    // |request_context_getter_| is still valid before initiating the download.
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this, &SpellCheckHostImpl::DownloadDictionary));
    return;
  }

  request_context_getter_ = NULL;

  if (file_ != base::kInvalidPlatformFileValue) {
    // Load custom dictionary.
    std::string contents;
    file_util::ReadFileToString(custom_dictionary_file_, &contents);
    std::vector<std::string> list_of_words;
    base::SplitString(contents, '\n', &list_of_words);
    for (size_t i = 0; i < list_of_words.size(); ++i)
      custom_words_.push_back(list_of_words[i]);
  }

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      NewRunnableMethod(this,
          &SpellCheckHostImpl::InformObserverOfInitialization));
  SpellCheckHost::RecordCustomWordCountStats(custom_words_.size());
}

void SpellCheckHostImpl::InitializeOnFileThread() {
  DCHECK(!BrowserThread::CurrentlyOn(BrowserThread::FILE));

  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(this, &SpellCheckHostImpl::Initialize));
}

void SpellCheckHostImpl::InformObserverOfInitialization() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (observer_)
    observer_->SpellCheckHostInitialized();

  for (RenderProcessHost::iterator i(RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    RenderProcessHost* process = i.GetCurrentValue();
    if (process)
      InitForRenderer(process);
  }
}

void SpellCheckHostImpl::DownloadDictionary() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!request_context_getter_) {
    InitializeOnFileThread();
    return;
  }

  // Determine URL of file to download.
  static const char kDownloadServerUrl[] =
      "http://cache.pack.google.com/edgedl/chrome/dict/";
  std::string bdict_file = bdict_file_path_.BaseName().MaybeAsASCII();
  if (bdict_file.empty()) {
    NOTREACHED();
    return;
  }
  GURL url = GURL(std::string(kDownloadServerUrl) +
                  StringToLowerASCII(bdict_file));
  fetcher_.reset(new URLFetcher(url, URLFetcher::GET, this));
  fetcher_->set_request_context(request_context_getter_);
  tried_to_download_ = true;
  fetcher_->Start();
  request_context_getter_ = NULL;
}

void SpellCheckHostImpl::WriteWordToCustomDictionary(const std::string& word) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  // Stored in UTF-8.
  std::string word_to_add(word + "\n");
  FILE* f = file_util::OpenFile(custom_dictionary_file_, "a+");
  if (f)
    fputs(word_to_add.c_str(), f);
  file_util::CloseFile(f);
}

void SpellCheckHostImpl::RecordCheckedWordStats(bool misspell) {
  spellchecked_word_count_++;
  if (misspell) {
    misspelled_word_count_++;
    // If an user misspelled, that user should be counted as a part of
    // the population.  So we ensure to instantiate the histogram
    // entries here at the first time.
    if (misspelled_word_count_ == 1)
      RecordReplacedWordStats(0);
  }

  int percentage = (100 * misspelled_word_count_) / spellchecked_word_count_;
  UMA_HISTOGRAM_PERCENTAGE("SpellCheck.MisspellRatio", percentage);
}

void SpellCheckHostImpl::RecordReplacedWordStats(int delta) {
  replaced_word_count_ += delta;
  if (!misspelled_word_count_) {
    // This is possible when an extension gives the misspelling,
    // which is not recorded as a part of this metrics.
    return;
  }

  int percentage = (100 * replaced_word_count_) / misspelled_word_count_;
  UMA_HISTOGRAM_PERCENTAGE("SpellCheck.ReplaceRatio", percentage);
}

void SpellCheckHostImpl::OnURLFetchComplete(const URLFetcher* source,
                                            const GURL& url,
                                            const net::URLRequestStatus& status,
                                            int response_code,
                                            const net::ResponseCookies& cookies,
                                            const std::string& data) {
  DCHECK(source);
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  fetcher_.reset();

  if ((response_code / 100) != 2) {
    // Initialize will not try to download the file a second time.
    LOG(ERROR) << "Failure to download dictionary.";
    InitializeOnFileThread();
    return;
  }

  // Basic sanity check on the dictionary.
  // There's the small chance that we might see a 200 status code for a body
  // that represents some form of failure.
  if (data.size() < 4 || data[0] != 'B' || data[1] != 'D' || data[2] != 'i' ||
      data[3] != 'c') {
    LOG(ERROR) << "Failure to download dictionary.";
    InitializeOnFileThread();
    return;
  }

  data_ = data;
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(this, &SpellCheckHostImpl::SaveDictionaryData));
}

void SpellCheckHostImpl::Observe(NotificationType type,
                                 const NotificationSource& source,
                                 const NotificationDetails& details) {
  DCHECK(type == NotificationType::RENDERER_PROCESS_CREATED);
  RenderProcessHost* process = Source<RenderProcessHost>(source).ptr();
  InitForRenderer(process);
}

void SpellCheckHostImpl::SaveDictionaryData() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  // To prevent corrupted dictionary data from causing a renderer crash, scan
  // the dictionary data and verify it is sane before save it to a file.
  if (!hunspell::BDict::Verify(data_.data(), data_.size())) {
    LOG(ERROR) << "Failure to verify the downloaded dictionary.";
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this,
                          &SpellCheckHostImpl::InformObserverOfInitialization));
    return;
  }

  size_t bytes_written =
      file_util::WriteFile(bdict_file_path_, data_.data(), data_.length());
  if (bytes_written != data_.length()) {
    bool success = false;
#if defined(OS_WIN)
    bdict_file_path_ = GetFallbackFilePath(bdict_file_path_);
    bytes_written =
        file_util::WriteFile(GetFallbackFilePath(bdict_file_path_),
                                                 data_.data(), data_.length());
    if (bytes_written == data_.length())
      success = true;
#endif
    data_.clear();

    if (!success) {
      LOG(ERROR) << "Failure to save dictionary.";
      file_util::Delete(bdict_file_path_, false);
      // To avoid trying to load a partially saved dictionary, shortcut the
      // Initialize() call.
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
          NewRunnableMethod(this,
              &SpellCheckHostImpl::InformObserverOfInitialization));
      return;
    }
  }

  data_.clear();
  Initialize();
}

const base::PlatformFile& SpellCheckHostImpl::GetDictionaryFile() const {
  return file_;
}

const std::vector<std::string>& SpellCheckHostImpl::GetCustomWords() const {
  return custom_words_;
}

const std::string& SpellCheckHostImpl::GetLastAddedFile() const {
  return custom_words_.back();
}

const std::string& SpellCheckHostImpl::GetLanguage() const {
  return language_;
}

bool SpellCheckHostImpl::IsUsingPlatformChecker() const {
  return use_platform_spellchecker_;
}
