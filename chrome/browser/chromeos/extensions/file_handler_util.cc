// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_handler_util.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "base/i18n/case_conversion.h"
#include "base/json/json_writer.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/gdata/gdata_util.h"
#include "chrome/browser/chromeos/extensions/file_manager_util.h"
#include "chrome/browser/chromeos/gdata/drive_task_executor.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/lazy_background_task_queue.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/common/extensions/file_browser_handler.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "net/base/escape.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_mount_point_provider.h"
#include "webkit/fileapi/file_system_util.h"

using content::BrowserContext;
using content::BrowserThread;
using content::ChildProcessSecurityPolicy;
using content::SiteInstance;
using content::WebContents;
using extensions::Extension;

namespace file_handler_util {

// The prefix used to differentiate drive extensions from Chrome extensions.
const char FileTaskExecutor::kDriveTaskExtensionPrefix[] = "drive-app:";
const size_t FileTaskExecutor::kDriveTaskExtensionPrefixLength =
    arraysize(FileTaskExecutor::kDriveTaskExtensionPrefix) - 1;

namespace {

const int kReadWriteFilePermissions = base::PLATFORM_FILE_OPEN |
                                      base::PLATFORM_FILE_CREATE |
                                      base::PLATFORM_FILE_OPEN_ALWAYS |
                                      base::PLATFORM_FILE_CREATE_ALWAYS |
                                      base::PLATFORM_FILE_OPEN_TRUNCATED |
                                      base::PLATFORM_FILE_READ |
                                      base::PLATFORM_FILE_WRITE |
                                      base::PLATFORM_FILE_EXCLUSIVE_READ |
                                      base::PLATFORM_FILE_EXCLUSIVE_WRITE |
                                      base::PLATFORM_FILE_ASYNC |
                                      base::PLATFORM_FILE_WRITE_ATTRIBUTES;

const int kReadOnlyFilePermissions = base::PLATFORM_FILE_OPEN |
                                     base::PLATFORM_FILE_READ |
                                     base::PLATFORM_FILE_EXCLUSIVE_READ |
                                     base::PLATFORM_FILE_ASYNC;

// Returns process id of the process the extension is running in.
int ExtractProcessFromExtensionId(const std::string& extension_id,
                                  Profile* profile) {
  GURL extension_url =
      Extension::GetBaseURLFromExtensionId(extension_id);
  ExtensionProcessManager* manager = profile->GetExtensionProcessManager();

  SiteInstance* site_instance = manager->GetSiteInstanceForURL(extension_url);
  if (!site_instance || !site_instance->HasProcess())
    return -1;
  content::RenderProcessHost* process = site_instance->GetProcess();

  return process->GetID();
}

URLPatternSet GetAllMatchingPatterns(const FileBrowserHandler* handler,
                                     const std::vector<GURL>& files_list) {
  URLPatternSet matching_patterns;
  const URLPatternSet& patterns = handler->file_url_patterns();
  for (URLPatternSet::const_iterator pattern_it = patterns.begin();
       pattern_it != patterns.end(); ++pattern_it) {
    for (std::vector<GURL>::const_iterator file_it = files_list.begin();
         file_it != files_list.end(); ++file_it) {
      if (pattern_it->MatchesURL(*file_it)) {
        matching_patterns.AddPattern(*pattern_it);
        break;
      }
    }
  }

  return matching_patterns;
}

typedef std::set<const FileBrowserHandler*> ActionSet;

const FileBrowserHandler* FindFileBrowserHandler(const Extension* extension,
                                                 const std::string& action_id) {
  for (Extension::FileBrowserHandlerList::const_iterator action_iter =
           extension->file_browser_handlers()->begin();
       action_iter != extension->file_browser_handlers()->end();
       ++action_iter) {
    if (action_iter->get()->id() == action_id)
      return action_iter->get();
  }
  return NULL;
}

unsigned int GetAccessPermissionsForHandler(const Extension* extension,
                                            const std::string& action_id) {
  const FileBrowserHandler* action =
      FindFileBrowserHandler(extension, action_id);
  if (!action)
    return 0;
  unsigned int result = 0;
  if (action->CanRead())
    result |= kReadOnlyFilePermissions;
  if (action->CanWrite())
    result |= kReadWriteFilePermissions;
  // TODO(tbarzic): We don't handle Create yet.
  return result;
}


std::string EscapedUtf8ToLower(const std::string& str) {
  string16 utf16 = UTF8ToUTF16(
      net::UnescapeURLComponent(str, net::UnescapeRule::NORMAL));
  return net::EscapeUrlEncodedData(
      UTF16ToUTF8(base::i18n::ToLower(utf16)),
      false /* do not replace space with plus */);
}

bool GetFileBrowserHandlers(Profile* profile,
                            const GURL& selected_file_url,
                            ActionSet* results) {
  ExtensionService* service = profile->GetExtensionService();
  if (!service)
    return false;  // In unit-tests, we may not have an ExtensionService.

  // We need case-insensitive matching, and pattern in the handler is already
  // in lower case.
  const GURL lowercase_url(EscapedUtf8ToLower(selected_file_url.spec()));

  for (ExtensionSet::const_iterator iter = service->extensions()->begin();
       iter != service->extensions()->end();
       ++iter) {
    const Extension* extension = *iter;
    if (profile->IsOffTheRecord() &&
        !service->IsIncognitoEnabled(extension->id()))
      continue;
    if (!extension->file_browser_handlers())
      continue;

    for (Extension::FileBrowserHandlerList::const_iterator action_iter =
             extension->file_browser_handlers()->begin();
         action_iter != extension->file_browser_handlers()->end();
         ++action_iter) {
      const FileBrowserHandler* action = action_iter->get();
      if (!action->MatchesURL(lowercase_url))
        continue;

      results->insert(action_iter->get());
    }
  }
  return true;
}

bool SortByLastUsedTimestampDesc(const LastUsedHandler& a,
                                 const LastUsedHandler& b) {
  return a.timestamp > b.timestamp;
}

// TODO(zelidrag): Wire this with ICU to make this sort I18N happy.
bool SortByTaskName(const LastUsedHandler& a, const LastUsedHandler& b) {
  return base::strcasecmp(a.handler->title().c_str(),
                          b.handler->title().c_str()) > 0;
}

void SortLastUsedHandlerList(LastUsedHandlerList *list) {
  // Sort by the last used descending.
  std::sort(list->begin(), list->end(), SortByLastUsedTimestampDesc);
  if (list->size() > 1) {
    // Sort the rest by name.
    std::sort(list->begin() + 1, list->end(), SortByTaskName);
  }
}

}  // namespace

// Update file handler usage stats.
void UpdateFileHandlerUsageStats(Profile* profile, const std::string& task_id) {
  if (!profile || !profile->GetPrefs())
    return;
  DictionaryPrefUpdate prefs_usage_update(profile->GetPrefs(),
      prefs::kLastUsedFileBrowserHandlers);
  prefs_usage_update->SetWithoutPathExpansion(task_id,
      new base::FundamentalValue(
          static_cast<int>(base::Time::Now().ToInternalValue()/
                           base::Time::kMicrosecondsPerSecond)));
}

int GetReadWritePermissions() {
  return kReadWriteFilePermissions;
}

int GetReadOnlyPermissions() {
  return kReadOnlyFilePermissions;
}

std::string MakeTaskID(const std::string& extension_id,
                       const std::string& action_id) {
  return base::StringPrintf("%s|%s", extension_id.c_str(), action_id.c_str());
}

std::string MakeDriveTaskID(const std::string& app_id,
                            const std::string& action_id) {
  return MakeTaskID(FileTaskExecutor::kDriveTaskExtensionPrefix + app_id,
                    action_id);
}


// Breaks down task_id that is used between getFileTasks() and executeTask() on
// its building blocks. task_id field the following structure:
//     <extension-id>|<task-action-id>
// Currently, the only supported task-type is of 'context'.
bool CrackTaskID(const std::string& task_id,
                 std::string* extension_id,
                 std::string* action_id) {
  std::vector<std::string> result;
  int count = Tokenize(task_id, std::string("|"), &result);
  if (count != 2)
    return false;
  *extension_id = result[0];
  *action_id = result[1];
  return true;
}

// Find a specific handler in the handler list.
LastUsedHandlerList::iterator FindHandler(
    LastUsedHandlerList* list,
    const std::string& extension_id,
    const std::string& id) {
  LastUsedHandlerList::iterator iter = list->begin();
  while (iter != list->end() &&
         !(iter->handler->extension_id() == extension_id &&
           iter->handler->id() == id)) {
    iter++;
  }
  return iter;
}

// Given the list of selected files, returns array of context menu tasks
// that are shared
bool FindCommonTasks(Profile* profile,
                     const std::vector<GURL>& files_list,
                     LastUsedHandlerList* named_action_list) {
  named_action_list->clear();
  ActionSet common_tasks;
  for (std::vector<GURL>::const_iterator it = files_list.begin();
       it != files_list.end(); ++it) {
    ActionSet file_actions;
    if (!GetFileBrowserHandlers(profile, *it, &file_actions))
      return false;
    // If there is nothing to do for one file, the intersection of tasks for all
    // files will be empty at the end.
    if (!file_actions.size())
      return true;

    // For the very first file, just copy elements.
    if (it == files_list.begin()) {
      common_tasks = file_actions;
    } else {
      if (common_tasks.size()) {
        // For all additional files, find intersection between the accumulated
        // and file specific set.
        ActionSet intersection;
        std::set_intersection(common_tasks.begin(), common_tasks.end(),
                              file_actions.begin(), file_actions.end(),
                              std::inserter(intersection,
                                            intersection.begin()));
        common_tasks = intersection;
      }
    }
  }

  const DictionaryValue* prefs_tasks =
      profile->GetPrefs()->GetDictionary(prefs::kLastUsedFileBrowserHandlers);
  for (ActionSet::const_iterator iter = common_tasks.begin();
       iter != common_tasks.end(); ++iter) {
    // Get timestamp of when this task was used last time.
    int last_used_timestamp = 0;

    if ((*iter)->extension_id() == kFileBrowserDomain) {
      // Give a little bump to the action from File Browser extension
      // to make sure it is the default on a fresh profile.
      last_used_timestamp = 1;
    }
    prefs_tasks->GetInteger(MakeTaskID((*iter)->extension_id(), (*iter)->id()),
                            &last_used_timestamp);
    URLPatternSet matching_patterns = GetAllMatchingPatterns(*iter, files_list);
    named_action_list->push_back(LastUsedHandler(last_used_timestamp, *iter,
                                                 matching_patterns));
  }

  LastUsedHandlerList::iterator watch_iter = FindHandler(
      named_action_list, kFileBrowserDomain, kFileBrowserWatchTaskId);
  LastUsedHandlerList::iterator gallery_iter = FindHandler(
      named_action_list, kFileBrowserDomain, kFileBrowserGalleryTaskId);
  if (watch_iter != named_action_list->end() &&
      gallery_iter != named_action_list->end()) {
    // Both "watch" and "gallery" actions are applicable which means that
    // the selection is all videos. Showing them both is confusing. We only keep
    // the one that makes more sense ("watch" for single selection, "gallery"
    // for multiple selection).

    if (files_list.size() == 1)
      named_action_list->erase(gallery_iter);
    else
      named_action_list->erase(watch_iter);
  }

  SortLastUsedHandlerList(named_action_list);
  return true;
}

bool GetDefaultTask(
    Profile* profile, const GURL& url, const FileBrowserHandler** handler) {
  std::vector<GURL> file_urls;
  file_urls.push_back(url);

  LastUsedHandlerList common_tasks;
  if (!FindCommonTasks(profile, file_urls, &common_tasks))
    return false;

  if (common_tasks.size() == 0)
    return false;

  *handler = common_tasks[0].handler;
  return true;
}

class ExtensionTaskExecutor : public FileTaskExecutor {
 public:
  // FileTaskExecutor overrides.
  virtual bool ExecuteAndNotify(const std::vector<GURL>& file_urls,
                       const FileTaskFinishedCallback& done) OVERRIDE;

 private:
  // FileTaskExecutor is the only class allowed to create one.
  friend class FileTaskExecutor;

  ExtensionTaskExecutor(Profile* profile,
                        const GURL source_url,
                        const std::string& extension_id,
                        const std::string& action_id);
  virtual ~ExtensionTaskExecutor();

  struct FileDefinition {
    FileDefinition();
    ~FileDefinition();

    GURL target_file_url;
    FilePath virtual_path;
    FilePath absolute_path;
    bool is_directory;
  };

  typedef std::vector<FileDefinition> FileDefinitionList;
  class ExecuteTasksFileSystemCallbackDispatcher;
  void RequestFileEntryOnFileThread(
      scoped_refptr<fileapi::FileSystemContext> file_system_context,
      const GURL& handler_base_url,
      const scoped_refptr<const extensions::Extension>& handler,
      int handler_pid,
      const std::vector<GURL>& file_urls);

  void ExecuteDoneOnUIThread(bool success);
  void ExecuteFileActionsOnUIThread(const std::string& file_system_name,
                                    const GURL& file_system_root,
                                    const FileDefinitionList& file_list,
                                    int handler_pid);
  void SetupPermissionsAndDispatchEvent(const std::string& file_system_name,
                                        const GURL& file_system_root,
                                        const FileDefinitionList& file_list,
                                        int handler_pid_in,
                                        extensions::ExtensionHost* host);

  // Populates |handler_host_permissions| with file path-permissions pairs that
  // will be given to the handler extension host process.
  void InitHandlerHostFileAccessPermissions(
      const FileDefinitionList& file_list,
      const extensions::Extension* handler_extension,
      const std::string& action_id,
      const base::Closure& callback);

  // Invoked upon completion of InitHandlerHostFileAccessPermissions initiated
  // by ExecuteFileActionsOnUIThread.
  void OnInitAccessForExecuteFileActionsOnUIThread(
      const std::string& file_system_name,
      const GURL& file_system_root,
      const FileDefinitionList& file_list,
      int handler_pid);

  // Registers file permissions from |handler_host_permissions_| with
  // ChildProcessSecurityPolicy for process with id |handler_pid|.
  void SetupHandlerHostFileAccessPermissions(int handler_pid);

  // Helper function to get the extension pointer.
  const extensions::Extension* GetExtension();

  const GURL source_url_;
  const std::string extension_id_;
  const std::string action_id_;
  FileTaskFinishedCallback done_;

  // (File path, permission for file path) pairs for the handler.
  std::vector<std::pair<FilePath, int> > handler_host_permissions_;
};

// static
FileTaskExecutor* FileTaskExecutor::Create(Profile* profile,
                                           const GURL source_url,
                                           const std::string& extension_id,
                                           const std::string& action_id) {
  // Check out the extension ID and see if this is a drive task,
  // and instantiate drive-specific executor if so.
  if (StartsWithASCII(extension_id,
                      FileTaskExecutor::kDriveTaskExtensionPrefix,
                      false)) {
    return new gdata::DriveTaskExecutor(profile,
                                        extension_id, // really app_id
                                        action_id);
  } else {
    return new ExtensionTaskExecutor(profile,
                                     source_url,
                                     extension_id,
                                     action_id);
  }
}

FileTaskExecutor::FileTaskExecutor(Profile* profile) : profile_(profile) {
}

FileTaskExecutor::~FileTaskExecutor() {
}

bool FileTaskExecutor::Execute(const std::vector<GURL>& file_urls) {
  return ExecuteAndNotify(file_urls, FileTaskFinishedCallback());
}

Browser* FileTaskExecutor::GetBrowser() const {
  return browser::FindOrCreateTabbedBrowser(
      profile_ ? profile_ : ProfileManager::GetDefaultProfileOrOffTheRecord());
}

ExtensionTaskExecutor::FileDefinition::FileDefinition() : is_directory(false) {
}

ExtensionTaskExecutor::FileDefinition::~FileDefinition() {
}

class ExtensionTaskExecutor::ExecuteTasksFileSystemCallbackDispatcher {
 public:
  static fileapi::FileSystemContext::OpenFileSystemCallback CreateCallback(
      ExtensionTaskExecutor* executor,
      scoped_refptr<fileapi::FileSystemContext> file_system_context,
      const GURL& source_url,
      scoped_refptr<const Extension> handler_extension,
      int handler_pid,
      const std::string& action_id,
      const std::vector<GURL>& file_urls) {
    return base::Bind(
        &ExecuteTasksFileSystemCallbackDispatcher::DidOpenFileSystem,
        base::Owned(new ExecuteTasksFileSystemCallbackDispatcher(
            executor, file_system_context, source_url, handler_extension,
            handler_pid, action_id, file_urls)));
  }

  void DidOpenFileSystem(base::PlatformFileError result,
                         const std::string& file_system_name,
                         const GURL& file_system_root) {
    if (result != base::PLATFORM_FILE_OK) {
      DidFail(result);
      return;
    }
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
    ExtensionTaskExecutor::FileDefinitionList file_list;
    for (std::vector<GURL>::iterator iter = origin_file_urls_.begin();
         iter != origin_file_urls_.end();
         ++iter) {
      // Set up file permission access.
      ExtensionTaskExecutor::FileDefinition file;
      if (!SetupFileAccessPermissions(*iter, &file)) {
        continue;
      }
      file_list.push_back(file);
    }
    if (file_list.empty()) {
      BrowserThread::PostTask(
          BrowserThread::UI, FROM_HERE,
          base::Bind(
              &ExtensionTaskExecutor::ExecuteDoneOnUIThread,
              executor_,
              false));
      return;
    }

    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(
            &ExtensionTaskExecutor::ExecuteFileActionsOnUIThread,
            executor_,
            file_system_name,
            file_system_root,
            file_list,
            handler_pid_));
  }

  void DidFail(base::PlatformFileError error_code) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(
            &ExtensionTaskExecutor::ExecuteDoneOnUIThread,
            executor_,
            false));
  }

 private:
  ExecuteTasksFileSystemCallbackDispatcher(
      ExtensionTaskExecutor* executor,
      scoped_refptr<fileapi::FileSystemContext> file_system_context,
      const GURL& source_url,
      const scoped_refptr<const Extension>& handler_extension,
      int handler_pid,
      const std::string& action_id,
      const std::vector<GURL>& file_urls)
      : executor_(executor),
        file_system_context_(file_system_context),
        source_url_(source_url),
        handler_extension_(handler_extension),
        handler_pid_(handler_pid),
        action_id_(action_id),
        origin_file_urls_(file_urls) {
    DCHECK(executor_);
  }

  // Checks legitimacy of file url and grants file RO access permissions from
  // handler (target) extension and its renderer process.
  bool SetupFileAccessPermissions(const GURL& origin_file_url,
                                  FileDefinition* file) {
    if (!handler_extension_.get())
      return false;

    if (handler_pid_ == 0)
      return false;

    GURL file_origin_url;
    FilePath virtual_path;
    fileapi::FileSystemType type;
    if (!CrackFileSystemURL(origin_file_url, &file_origin_url, &type,
                            &virtual_path)) {
      return false;
    }

    if (type != fileapi::kFileSystemTypeExternal)
      return false;

    fileapi::ExternalFileSystemMountPointProvider* external_provider =
        file_system_context_->external_provider();
    if (!external_provider)
      return false;

    if (!external_provider->IsAccessAllowed(file_origin_url,
                                            type,
                                            virtual_path)) {
      return false;
    }

    // Make sure this url really being used by the right caller extension.
    if (source_url_.GetOrigin() != file_origin_url) {
      DidFail(base::PLATFORM_FILE_ERROR_SECURITY);
      return false;
    }

    FilePath root_path =
        external_provider->GetFileSystemRootPathOnFileThread(
          file_origin_url,
          fileapi::kFileSystemTypeExternal,
          virtual_path,
          false);     // create
    FilePath final_file_path = root_path.Append(virtual_path);

    // Check if this file system entry exists first.
    base::PlatformFileInfo file_info;

    bool is_gdata_file = gdata::util::IsUnderGDataMountPoint(final_file_path);

    // If the file is under gdata mount point, there is no actual file to be
    // found on the final_file_path.
    if (!is_gdata_file) {
      if (!file_util::PathExists(final_file_path) ||
          file_util::IsLink(final_file_path) ||
          !file_util::GetFileInfo(final_file_path, &file_info)) {
        return false;
      }
    }

    // Grant access to this particular file to target extension. This will
    // ensure that the target extension can access only this FS entry and
    // prevent from traversing FS hierarchy upward.
    external_provider->GrantFileAccessToExtension(handler_extension_->id(),
                                                  virtual_path);

    // Output values.
    GURL target_origin_url(Extension::GetBaseURLFromExtensionId(
        handler_extension_->id()));
    GURL base_url = fileapi::GetFileSystemRootURI(target_origin_url,
        fileapi::kFileSystemTypeExternal);
    file->target_file_url = GURL(base_url.spec() + virtual_path.value());
    file->virtual_path = virtual_path;
    file->is_directory = file_info.is_directory;
    file->absolute_path = final_file_path;
    return true;
  }

  ExtensionTaskExecutor* executor_;
  scoped_refptr<fileapi::FileSystemContext> file_system_context_;
  // Extension source URL.
  GURL source_url_;
  scoped_refptr<const Extension> handler_extension_;
  int handler_pid_;
  std::string action_id_;
  std::vector<GURL> origin_file_urls_;
  DISALLOW_COPY_AND_ASSIGN(ExecuteTasksFileSystemCallbackDispatcher);
};

ExtensionTaskExecutor::ExtensionTaskExecutor(
    Profile* profile,
    const GURL source_url,
    const std::string& extension_id,
    const std::string& action_id)
  : FileTaskExecutor(profile),
    source_url_(source_url),
    extension_id_(extension_id),
    action_id_(action_id) {
}

ExtensionTaskExecutor::~ExtensionTaskExecutor() {}

bool ExtensionTaskExecutor::ExecuteAndNotify(
    const std::vector<GURL>& file_urls,
    const FileTaskFinishedCallback& done) {
  ExtensionService* service = profile()->GetExtensionService();
  if (!service)
    return false;

  scoped_refptr<const Extension> handler =
      service->GetExtensionById(extension_id_, false);

  if (!handler.get())
    return false;

  int handler_pid = ExtractProcessFromExtensionId(handler->id(), profile());
  if (handler_pid <= 0) {
    if (!handler->has_lazy_background_page())
      return false;
  }

  done_ = done;

  // Get local file system instance on file thread.
  scoped_refptr<fileapi::FileSystemContext> file_system_context =
      BrowserContext::GetFileSystemContext(profile());
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(
          &ExtensionTaskExecutor::RequestFileEntryOnFileThread,
          this,
          file_system_context,
          Extension::GetBaseURLFromExtensionId(handler->id()),
          handler,
          handler_pid,
          file_urls));
  return true;
}

void ExtensionTaskExecutor::RequestFileEntryOnFileThread(
    scoped_refptr<fileapi::FileSystemContext> file_system_context,
    const GURL& handler_base_url,
    const scoped_refptr<const Extension>& handler,
    int handler_pid,
    const std::vector<GURL>& file_urls) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  GURL origin_url = handler_base_url.GetOrigin();
  file_system_context->OpenFileSystem(
      origin_url, fileapi::kFileSystemTypeExternal, false, // create
      ExecuteTasksFileSystemCallbackDispatcher::CreateCallback(
          this,
          file_system_context,
          source_url_,
          handler,
          handler_pid,
          action_id_,
          file_urls));
}

void ExtensionTaskExecutor::ExecuteDoneOnUIThread(bool success) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!done_.is_null())
    done_.Run(success);
  done_.Reset();
}

const Extension* ExtensionTaskExecutor::GetExtension() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  ExtensionService* service = profile()->GetExtensionService();
  return service ? service->GetExtensionById(extension_id_, false) :
                   NULL;
}

void ExtensionTaskExecutor::ExecuteFileActionsOnUIThread(
    const std::string& file_system_name,
    const GURL& file_system_root,
    const FileDefinitionList& file_list,
    int handler_pid) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  const Extension* extension = GetExtension();
  if (!extension) {
    ExecuteDoneOnUIThread(false);
    return;
  }

  InitHandlerHostFileAccessPermissions(
      file_list,
      extension,
      action_id_,
      base::Bind(
          &ExtensionTaskExecutor::OnInitAccessForExecuteFileActionsOnUIThread,
          this,
          file_system_name,
          file_system_root,
          file_list,
          handler_pid));
}

void ExtensionTaskExecutor::OnInitAccessForExecuteFileActionsOnUIThread(
    const std::string& file_system_name,
    const GURL& file_system_root,
    const FileDefinitionList& file_list,
    int handler_pid) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  const Extension* extension = GetExtension();
  if (!extension) {
    ExecuteDoneOnUIThread(false);
    return;
  }

  if (handler_pid > 0) {
    SetupPermissionsAndDispatchEvent(file_system_name, file_system_root,
        file_list, handler_pid, NULL);
  } else {
    // We have to wake the handler background page before we proceed.
    extensions::LazyBackgroundTaskQueue* queue =
        extensions::ExtensionSystem::Get(profile())->
        lazy_background_task_queue();
    if (!queue->ShouldEnqueueTask(profile(), extension)) {
      ExecuteDoneOnUIThread(false);
      return;
    }
    queue->AddPendingTask(
        profile(), extension_id_,
        base::Bind(&ExtensionTaskExecutor::SetupPermissionsAndDispatchEvent,
                   this, file_system_name, file_system_root, file_list,
                   handler_pid));
  }
}

void ExtensionTaskExecutor::SetupPermissionsAndDispatchEvent(
    const std::string& file_system_name,
    const GURL& file_system_root,
    const FileDefinitionList& file_list,
    int handler_pid_in,
    extensions::ExtensionHost* host) {
  int handler_pid = host ? host->render_process_host()->GetID() :
                           handler_pid_in;

  if (handler_pid <= 0) {
    ExecuteDoneOnUIThread(false);
    return;
  }

  extensions::EventRouter* event_router = profile()->GetExtensionEventRouter();
  if (!event_router) {
    ExecuteDoneOnUIThread(false);
    return;
  }

  SetupHandlerHostFileAccessPermissions(handler_pid);

  scoped_ptr<ListValue> event_args(new ListValue());
  event_args->Append(Value::CreateStringValue(action_id_));
  DictionaryValue* details = new DictionaryValue();
  event_args->Append(details);
  // Get file definitions. These will be replaced with Entry instances by
  // chromeHidden.Event.dispatchJSON() method from even_binding.js.
  ListValue* files_urls = new ListValue();
  details->Set("entries", files_urls);
  for (FileDefinitionList::const_iterator iter = file_list.begin();
       iter != file_list.end();
       ++iter) {
    DictionaryValue* file_def = new DictionaryValue();
    files_urls->Append(file_def);
    file_def->SetString("fileSystemName", file_system_name);
    file_def->SetString("fileSystemRoot", file_system_root.spec());
    FilePath root(FILE_PATH_LITERAL("/"));
    FilePath full_path = root.Append(iter->virtual_path);
    file_def->SetString("fileFullPath", full_path.value());
    file_def->SetBoolean("fileIsDirectory", iter->is_directory);
  }

  // Get tab id.
  Browser* current_browser = GetBrowser();
  if (current_browser) {
    WebContents* contents = chrome::GetActiveWebContents(current_browser);
    if (contents)
      details->SetInteger("tab_id", ExtensionTabUtil::GetTabId(contents));
  }

  std::string json_args;
  base::JSONWriter::Write(event_args.get(), &json_args);
  event_router->DispatchEventToExtension(
      extension_id_, std::string("fileBrowserHandler.onExecute"),
      json_args, profile(), GURL());
  ExecuteDoneOnUIThread(true);
}

void ExtensionTaskExecutor::InitHandlerHostFileAccessPermissions(
    const FileDefinitionList& file_list,
    const Extension* handler_extension,
    const std::string& action_id,
    const base::Closure& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  scoped_ptr<std::vector<FilePath> > gdata_paths(new std::vector<FilePath>);
  for (FileDefinitionList::const_iterator iter = file_list.begin();
       iter != file_list.end();
       ++iter) {
    // Setup permission for file's absolute file.
    handler_host_permissions_.push_back(std::make_pair(
        iter->absolute_path,
        GetAccessPermissionsForHandler(handler_extension, action_id)));

    if (gdata::util::IsUnderGDataMountPoint(iter->absolute_path))
      gdata_paths->push_back(iter->virtual_path);
  }

  if (gdata_paths->empty()) {
    // Invoke callback if none of the files are on gdata mount point.
    callback.Run();
    return;
  }

  // For files on gdata mount point, we'll have to give handler host permissions
  // for their cache paths. This has to be called on UI thread.
  gdata::util::InsertGDataCachePathsPermissions(profile(),
                                                gdata_paths.Pass(),
                                                &handler_host_permissions_,
                                                callback);
}

void ExtensionTaskExecutor::SetupHandlerHostFileAccessPermissions(
    int handler_pid) {
  for (size_t i = 0; i < handler_host_permissions_.size(); i++) {
    content::ChildProcessSecurityPolicy::GetInstance()->GrantPermissionsForFile(
        handler_pid,
        handler_host_permissions_[i].first,
        handler_host_permissions_[i].second);
  }

  // We don't need this anymore.
  handler_host_permissions_.clear();
}

} // namespace file_handler_util

