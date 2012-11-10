// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_browser_private_api.h"

#include <utility>

#include "base/base64.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/singleton.h"
#include "base/string_split.h"
#include "base/stringprintf.h"
#include "base/time.h"
#include "base/values.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/disks/disk_mount_manager.h"
#include "chrome/browser/chromeos/extensions/file_handler_util.h"
#include "chrome/browser/chromeos/extensions/file_manager_util.h"
#include "chrome/browser/chromeos/gdata/drive_webapps_registry.h"
#include "chrome/browser/chromeos/gdata/gdata.pb.h"
#include "chrome/browser/chromeos/gdata/gdata_documents_service.h"
#include "chrome/browser/chromeos/gdata/gdata_operation_registry.h"
#include "chrome/browser/chromeos/gdata/gdata_system_service.h"
#include "chrome/browser/chromeos/gdata/gdata_util.h"
#include "chrome/browser/chromeos/gdata/gdata_wapi_parser.h"
#include "chrome/browser/chromeos/system/statistics_provider.h"
#include "chrome/browser/extensions/extension_function_dispatcher.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/process_map.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/select_file_dialog_extension.h"
#include "chrome/browser/ui/webui/extensions/extension_icon_source.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/file_browser_handler.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "grit/platform_locale_settings.h"
#include "net/base/escape.h"
#include "ui/base/dialogs/selected_file_info.h"
#include "ui/base/l10n/l10n_util.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_file_util.h"
#include "webkit/fileapi/file_system_mount_point_provider.h"
#include "webkit/fileapi/file_system_operation_context.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/file_system_util.h"

using chromeos::disks::DiskMountManager;
using content::BrowserContext;
using content::BrowserThread;
using content::ChildProcessSecurityPolicy;
using content::SiteInstance;
using content::WebContents;
using extensions::Extension;
using file_handler_util::FileTaskExecutor;
using gdata::GDataOperationRegistry;
using gdata::InstalledApp;

namespace {

// Default icon path for drive docs.
const char kDefaultDriveIcon[] = "images/filetype_generic.png";
const int kPreferredIconSize = 16;

// Error messages.
const char kFileError[] = "File error %d";
const char kInvalidFileUrl[] = "Invalid file URL";
const char kVolumeDevicePathNotFound[] = "Device path not found";

// Typedef for holding a map from app_id to DriveWebAppInfo so
// we can look up information on the apps.
typedef std::map<std::string, gdata::DriveWebAppInfo*> WebAppInfoMap;

// Unescape rules used for parsing query parameters.
const net::UnescapeRule::Type kUnescapeRuleForQueryParameters =
    net::UnescapeRule::SPACES |
    net::UnescapeRule::URL_SPECIAL_CHARS |
    net::UnescapeRule::REPLACE_PLUS_WITH_SPACE;

ListValue* URLPatternSetToStringList(const URLPatternSet& patterns) {
  ListValue* list = new ListValue();
  for (URLPatternSet::const_iterator it = patterns.begin();
       it != patterns.end(); ++it) {
    list->Append(new StringValue(it->GetAsString()));
  }

  return list;
}

const DiskMountManager::Disk* GetVolumeAsDisk(const std::string& mount_path) {
  DiskMountManager* disk_mount_manager = DiskMountManager::GetInstance();

  DiskMountManager::MountPointMap::const_iterator mount_point_it =
      disk_mount_manager->mount_points().find(mount_path);
  if (mount_point_it == disk_mount_manager->mount_points().end())
    return NULL;

  const DiskMountManager::Disk* disk = disk_mount_manager->FindDiskBySourcePath(
      mount_point_it->second.source_path);

  return (disk && disk->is_hidden()) ? NULL : disk;
}

base::DictionaryValue* CreateValueFromDisk(
    Profile* profile,
    const DiskMountManager::Disk* volume) {
  base::DictionaryValue* volume_info = new base::DictionaryValue();

  std::string mount_path;
  if (!volume->mount_path().empty()) {
    FilePath relative_mount_path;
    file_manager_util::ConvertFileToRelativeFileSystemPath(profile,
        FilePath(volume->mount_path()), &relative_mount_path);
    mount_path = relative_mount_path.value();
  }

  volume_info->SetString("devicePath", volume->device_path());
  volume_info->SetString("mountPath", mount_path);
  volume_info->SetString("systemPath", volume->system_path());
  volume_info->SetString("filePath", volume->file_path());
  volume_info->SetString("deviceLabel", volume->device_label());
  volume_info->SetString("driveLabel", volume->drive_label());
  volume_info->SetString("deviceType",
      DiskMountManager::DeviceTypeToString(volume->device_type()));
  volume_info->SetInteger("totalSize", volume->total_size_in_bytes());
  volume_info->SetBoolean("isParent", volume->is_parent());
  volume_info->SetBoolean("isReadOnly", volume->is_read_only());
  volume_info->SetBoolean("hasMedia", volume->has_media());
  volume_info->SetBoolean("isOnBootDevice", volume->on_boot_device());

  return volume_info;
}

base::DictionaryValue* CreateValueFromMountPoint(Profile* profile,
    const DiskMountManager::MountPointInfo& mount_point_info,
    const GURL& extension_source_url) {

  base::DictionaryValue *mount_info = new base::DictionaryValue();

  mount_info->SetString("mountType",
                        DiskMountManager::MountTypeToString(
                            mount_point_info.mount_type));
  mount_info->SetString("sourcePath", mount_point_info.source_path);

  FilePath relative_mount_path;
  // Convert mount point path to relative path with the external file system
  // exposed within File API.
  if (file_manager_util::ConvertFileToRelativeFileSystemPath(profile,
          FilePath(mount_point_info.mount_path),
          &relative_mount_path)) {
    mount_info->SetString("mountPath", relative_mount_path.value());
  }

  mount_info->SetString("mountCondition",
      DiskMountManager::MountConditionToString(
          mount_point_info.mount_condition));

  return mount_info;
}

// Gives the extension renderer |host| file |permissions| for the given |path|.
void GrantFilePermissionsToHost(content::RenderViewHost* host,
                                const FilePath& path,
                                int permissions) {
  ChildProcessSecurityPolicy::GetInstance()->GrantPermissionsForFile(
      host->GetProcess()->GetID(), path, permissions);
}

void AddGDataMountPoint(
    Profile* profile,
    const std::string& extension_id,
    content::RenderViewHost* render_view_host) {
  fileapi::ExternalFileSystemMountPointProvider* provider =
      BrowserContext::GetFileSystemContext(profile)->external_provider();
  if (!provider)
    return;

  const FilePath mount_point = gdata::util::GetGDataMountPointPath();
  if (!render_view_host || !render_view_host->GetProcess())
    return;

  // Grant R/W permissions to gdata 'folder'. File API layer still
  // expects this to be satisfied.
  GrantFilePermissionsToHost(render_view_host,
                             mount_point,
                             file_handler_util::GetReadWritePermissions());

  // Grant R/W permission for tmp and pinned cache folder.
  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile);
  // |system_service| is NULL if incognito window / guest login.
  if (!system_service || !system_service->file_system())
    return;
  gdata::GDataCache* cache = system_service->cache();

  // We check permissions for raw cache file paths only for read-only
  // operations (when fileEntry.file() is called), so read only permissions
  // should be sufficient for all cache paths. For the rest of supported
  // operations the file access check is done for drive/ paths.
  GrantFilePermissionsToHost(render_view_host,
                             cache->GetCacheDirectoryPath(
                                 gdata::GDataCache::CACHE_TYPE_TMP),
                             file_handler_util::GetReadOnlyPermissions());
  GrantFilePermissionsToHost(
      render_view_host,
      cache->GetCacheDirectoryPath(
          gdata::GDataCache::CACHE_TYPE_PERSISTENT),
      file_handler_util::GetReadOnlyPermissions());

  FilePath mount_point_virtual;
  if (provider->GetVirtualPath(mount_point, &mount_point_virtual))
    provider->GrantFileAccessToExtension(extension_id, mount_point_virtual);
}

// Given a file url, find the virtual FilePath associated with it.
FilePath GetVirtualPathFromURL(const GURL& file_url) {
  FilePath virtual_path;
  fileapi::FileSystemType type = fileapi::kFileSystemTypeUnknown;
  GURL file_origin_url;
  if (!CrackFileSystemURL(file_url, &file_origin_url, &type, &virtual_path) ||
      type != fileapi::kFileSystemTypeExternal) {
    NOTREACHED();
    return FilePath();
  }
  return virtual_path;
}

// Look up apps in the registry, and collect applications that match the file
// paths given. Returns the intersection of all available application ids in
// |available_apps| and a map of application ID to the Drive web application
// info collected in |app_info| so details can be collected later. The caller
// takes ownership of the pointers in |app_info|.
void IntersectAvailableDriveTasks(
    gdata::DriveWebAppsRegistry* registry,
    const std::vector<FilePath>& file_paths,
    WebAppInfoMap* app_info,
    std::set<std::string>* available_apps) {
  for (std::vector<FilePath>::const_iterator iter = file_paths.begin();
      iter != file_paths.end(); ++iter) {
    if (iter->empty())
      continue;
    ScopedVector<gdata::DriveWebAppInfo> info;
    registry->GetWebAppsForFile(*iter, std::string(""), &info);
    std::vector<gdata::DriveWebAppInfo*> info_ptrs;
    info.release(&info_ptrs);  // so they don't go away prematurely.
    std::set<std::string> apps_for_this_file;
    for (std::vector<gdata::DriveWebAppInfo*>::iterator
        apps = info_ptrs.begin(); apps != info_ptrs.end(); ++apps) {
      std::pair<WebAppInfoMap::iterator, bool> insert_result =
          app_info->insert(std::make_pair((*apps)->app_id, *apps));
     apps_for_this_file.insert((*apps)->app_id);
     // If we failed to insert an app_id because there was a duplicate, then we
     // must delete it (since we own it).
     if (!insert_result.second)
       delete *apps;
    }
    if (iter == file_paths.begin()) {
      *available_apps = apps_for_this_file;
    } else {
      std::set<std::string> intersection;
      std::set_intersection(available_apps->begin(),
                            available_apps->end(),
                            apps_for_this_file.begin(),
                            apps_for_this_file.end(),
                            std::inserter(intersection,
                                          intersection.begin()));
      *available_apps = intersection;
    }
  }
}

// Finds an icon in the list of icons. If unable to find an icon of the exact
// size requested, returns one with the next larger size. If all icons are
// smaller than the preferred size, we'll return the largest one available.
// Icons must be sorted by the icon size, smallest to largest. If there are no
// icons in the list, returns an empty URL.
GURL FindPreferredIcon(const InstalledApp::IconList& icons,
                       int preferred_size) {
  GURL result;
  if (icons.empty())
    return result;
  result = icons.rbegin()->second;
  for (InstalledApp::IconList::const_reverse_iterator iter = icons.rbegin();
       iter != icons.rend() && iter->first >= preferred_size; ++iter) {
        result = iter->second;
  }
  return result;
}

// Takes a map of app_id to application information in |app_info|, and the set
// of |available_apps| and adds Drive tasks to the |result_list| for each of the
// |available_apps|.
void CreateDriveTasks(
    gdata::DriveWebAppsRegistry* registry,
    const WebAppInfoMap& app_info,
    const std::set<std::string>& available_apps,
    ListValue* result_list) {
  // OK, now we traverse the intersection of available applications for this
  // list of files, adding a task for each one that is found.
  for (std::set<std::string>::iterator app_iter = available_apps.begin();
       app_iter != available_apps.end(); ++app_iter) {
    WebAppInfoMap::const_iterator info_iter = app_info.find(*app_iter);
    DCHECK(info_iter != app_info.end());
    gdata::DriveWebAppInfo* info = info_iter->second;
    DictionaryValue* task = new DictionaryValue;
    // TODO(gspencer): For now, the action id is always "open-with", but we
    // could add any actions that the drive app supports.
    std::string task_id =
        file_handler_util::MakeDriveTaskID(*app_iter, "open-with");
    task->SetString("taskId", task_id);
    task->SetString("title", info->app_name);

    // Create the list of extensions as patterns registered for this
    // application. (Extensions here refers to filename suffixes (extensions),
    // not Chrome or Drive extensions.)
    ListValue* pattern_list = new ListValue;
    std::set<std::string> extensions =
        registry->GetExtensionsForWebStoreApp(*app_iter);
    for (std::set<std::string>::iterator ext_iter = extensions.begin();
         ext_iter != extensions.end(); ++ext_iter) {
      pattern_list->Append(new StringValue("filesystem:*." + *ext_iter));
    }
    task->Set("patterns", pattern_list);
    GURL best_icon = FindPreferredIcon(info->app_icons,
                                       kPreferredIconSize);
    if (!best_icon.is_empty()) {
      task->SetString("iconUrl", best_icon.spec());
    }
    task->SetBoolean("driveApp", true);
    result_list->Append(task);
  }
}

}  // namespace

class RequestLocalFileSystemFunction::LocalFileSystemCallbackDispatcher {
 public:
  static fileapi::FileSystemContext::OpenFileSystemCallback CreateCallback(
      RequestLocalFileSystemFunction* function,
      scoped_refptr<fileapi::FileSystemContext> file_system_context,
      int child_id,
      scoped_refptr<const Extension> extension) {
    return base::Bind(
        &LocalFileSystemCallbackDispatcher::DidOpenFileSystem,
        base::Owned(new LocalFileSystemCallbackDispatcher(
            function, file_system_context, child_id, extension)));
  }

  void DidOpenFileSystem(base::PlatformFileError result,
                         const std::string& name,
                         const GURL& root_path) {
    if (result != base::PLATFORM_FILE_OK) {
      DidFail(result);
      return;
    }
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
    // Set up file permission access.
    if (!SetupFileSystemAccessPermissions()) {
      DidFail(base::PLATFORM_FILE_ERROR_SECURITY);
      return;
    }

    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(
            &RequestLocalFileSystemFunction::RespondSuccessOnUIThread,
            function_,
            name,
            root_path));
  }

  void DidFail(base::PlatformFileError error_code) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(
            &RequestLocalFileSystemFunction::RespondFailedOnUIThread,
            function_,
            error_code));
  }

 private:
  LocalFileSystemCallbackDispatcher(
      RequestLocalFileSystemFunction* function,
      scoped_refptr<fileapi::FileSystemContext> file_system_context,
      int child_id,
      scoped_refptr<const Extension> extension)
      : function_(function),
        file_system_context_(file_system_context),
        child_id_(child_id),
        extension_(extension)  {
    DCHECK(function_);
  }

  // Grants file system access permissions to file browser component.
  bool SetupFileSystemAccessPermissions() {
    if (!extension_.get())
      return false;

    // Make sure that only component extension can access the entire
    // local file system.
    if (extension_->location() != Extension::COMPONENT) {
      NOTREACHED() << "Private method access by non-component extension "
                   << extension_->id();
      return false;
    }

    fileapi::ExternalFileSystemMountPointProvider* provider =
        file_system_context_->external_provider();
    if (!provider)
      return false;

    // Grant full access to File API from this component extension.
    provider->GrantFullAccessToExtension(extension_->id());

    // Grant R/W file permissions to the renderer hosting component
    // extension for all paths exposed by our local file system provider.
    std::vector<FilePath> root_dirs = provider->GetRootDirectories();
    for (std::vector<FilePath>::iterator iter = root_dirs.begin();
         iter != root_dirs.end();
         ++iter) {
      ChildProcessSecurityPolicy::GetInstance()->GrantPermissionsForFile(
          child_id_, *iter, file_handler_util::GetReadWritePermissions());
    }
    return true;
  }

  RequestLocalFileSystemFunction* function_;
  scoped_refptr<fileapi::FileSystemContext> file_system_context_;
  // Renderer process id.
  int child_id_;
  // Extension source URL.
  scoped_refptr<const Extension> extension_;
  DISALLOW_COPY_AND_ASSIGN(LocalFileSystemCallbackDispatcher);
};

void RequestLocalFileSystemFunction::RequestOnFileThread(
    scoped_refptr<fileapi::FileSystemContext> file_system_context,
    const GURL& source_url,
    int child_id) {
  GURL origin_url = source_url.GetOrigin();
  file_system_context->OpenFileSystem(
      origin_url, fileapi::kFileSystemTypeExternal, false,  // create
      LocalFileSystemCallbackDispatcher::CreateCallback(
          this,
          file_system_context,
          child_id,
          GetExtension()));
}

bool RequestLocalFileSystemFunction::RunImpl() {
  if (!dispatcher() || !render_view_host() || !render_view_host()->GetProcess())
    return false;

  scoped_refptr<fileapi::FileSystemContext> file_system_context =
      BrowserContext::GetFileSystemContext(profile_);
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(
          &RequestLocalFileSystemFunction::RequestOnFileThread,
          this,
          file_system_context,
          source_url_,
          render_view_host()->GetProcess()->GetID()));
  // Will finish asynchronously.
  return true;
}

void RequestLocalFileSystemFunction::RespondSuccessOnUIThread(
    const std::string& name, const GURL& root_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Add gdata mount point immediately when we kick of first instance of file
  // manager. The actual mount event will be sent to UI only when we perform
  // proper authentication.
  if (gdata::util::IsGDataAvailable(profile_))
    AddGDataMountPoint(profile_, extension_id(), render_view_host());
  DictionaryValue* dict = new DictionaryValue();
  SetResult(dict);
  dict->SetString("name", name);
  dict->SetString("path", root_path.spec());
  dict->SetInteger("error", gdata::GDATA_FILE_OK);
  SendResponse(true);
}

void RequestLocalFileSystemFunction::RespondFailedOnUIThread(
    base::PlatformFileError error_code) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  error_ = base::StringPrintf(kFileError, static_cast<int>(error_code));
  SendResponse(false);
}

bool FileWatchBrowserFunctionBase::GetLocalFilePath(
    scoped_refptr<fileapi::FileSystemContext> file_system_context,
    const GURL& file_url, FilePath* local_path, FilePath* virtual_path) {
  GURL file_origin_url;
  fileapi::FileSystemType type;
  if (!CrackFileSystemURL(file_url, &file_origin_url, &type,
                          virtual_path)) {
    return false;
  }
  if (type != fileapi::kFileSystemTypeExternal)
    return false;

  FilePath root_path = file_system_context->
      external_provider()->GetFileSystemRootPathOnFileThread(
          file_origin_url,
          fileapi::kFileSystemTypeExternal,
          *virtual_path,
          false);
  if (root_path == FilePath())
    return false;

  *local_path = root_path.Append(*virtual_path);
  return true;
}

void FileWatchBrowserFunctionBase::RespondOnUIThread(bool success) {
  SetResult(Value::CreateBooleanValue(success));
  SendResponse(success);
}

bool FileWatchBrowserFunctionBase::RunImpl() {
  if (!render_view_host() || !render_view_host()->GetProcess())
    return false;

  // First param is url of a file to watch.
  std::string url;
  if (!args_->GetString(0, &url) || url.empty())
    return false;

  GURL file_watch_url(url);
  scoped_refptr<fileapi::FileSystemContext> file_system_context =
      BrowserContext::GetFileSystemContext(profile_);
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(
          &FileWatchBrowserFunctionBase::RunFileWatchOperationOnFileThread,
          this,
          file_system_context,
          FileBrowserEventRouterFactory::GetForProfile(profile_),
          file_watch_url,
          extension_id()));

  return true;
}

void FileWatchBrowserFunctionBase::RunFileWatchOperationOnFileThread(
    scoped_refptr<fileapi::FileSystemContext> file_system_context,
    scoped_refptr<FileBrowserEventRouter> event_router,
    const GURL& file_url, const std::string& extension_id) {
  FilePath local_path;
  FilePath virtual_path;
  if (!GetLocalFilePath(
          file_system_context, file_url, &local_path, &virtual_path) ||
      local_path == FilePath()) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(
            &FileWatchBrowserFunctionBase::RespondOnUIThread,
            this,
            false));
  }
  if (!PerformFileWatchOperation(event_router,
                                 local_path,
                                 virtual_path,
                                 extension_id)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(
            &FileWatchBrowserFunctionBase::RespondOnUIThread,
            this,
            false));
  }
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(
          &FileWatchBrowserFunctionBase::RespondOnUIThread,
          this,
          true));
}

bool AddFileWatchBrowserFunction::PerformFileWatchOperation(
    scoped_refptr<FileBrowserEventRouter> event_router,
    const FilePath& local_path, const FilePath& virtual_path,
    const std::string& extension_id) {
  return event_router->AddFileWatch(local_path, virtual_path, extension_id);
}

bool RemoveFileWatchBrowserFunction::PerformFileWatchOperation(
    scoped_refptr<FileBrowserEventRouter> event_router,
    const FilePath& local_path, const FilePath& unused,
    const std::string& extension_id) {
  event_router->RemoveFileWatch(local_path, extension_id);
  return true;
}

// Find special tasks here for Drive (Blox) apps. Iterate through matching drive
// apps and add them, with generated task ids. Extension ids will be the app_ids
// from drive. We'll know that they are drive apps because the extension id will
// begin with kDriveTaskExtensionPrefix.
bool GetFileTasksFileBrowserFunction::FindDriveAppTasks(
    const std::vector<GURL>& file_urls,
    ListValue* result_list) {

  // Crack all the urls into file paths.
  std::vector<FilePath> file_paths;
  for (std::vector<GURL>::const_iterator iter = file_urls.begin();
       iter != file_urls.end(); ++iter) {
    FilePath raw_path;
    fileapi::FileSystemType type = fileapi::kFileSystemTypeUnknown;
    if (fileapi::CrackFileSystemURL(*iter, NULL, &type, &raw_path) &&
        type == fileapi::kFileSystemTypeExternal) {
      file_paths.push_back(raw_path);
    }
  }

  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  // |system_service| is NULL if incognito window / guest login. We return true
  // in this case because there might be other extension tasks, even if we don't
  // have any to add.
  if (!system_service || !system_service->webapps_registry())
    return true;


  gdata::DriveWebAppsRegistry* registry = system_service->webapps_registry();

  // Map of app_id to DriveWebAppInfo so we can look up the apps we've found
  // after taking the intersection of available apps.
  std::map<std::string, gdata::DriveWebAppInfo*> app_info;
  // Set of application IDs. This will end up with the intersection of the
  // application IDs that apply to the paths in |file_paths|.
  std::set<std::string> available_apps;

  IntersectAvailableDriveTasks(registry, file_paths,
                               &app_info, &available_apps);
  CreateDriveTasks(registry, app_info, available_apps, result_list);

  // We own the pointers in |app_info|, so we need to delete them.
  STLDeleteContainerPairSecondPointers(app_info.begin(), app_info.end());
  return true;
}


bool GetFileTasksFileBrowserFunction::RunImpl() {
  ListValue* files_list = NULL;
  if (!args_->GetList(0, &files_list))
    return false;

  std::vector<GURL> file_urls;
  for (size_t i = 0; i < files_list->GetSize(); ++i) {
    std::string file_url;
    if (!files_list->GetString(i, &file_url))
      return false;
    file_urls.push_back(GURL(file_url));
  }

  ListValue* result_list = new ListValue();
  SetResult(result_list);

  file_handler_util::LastUsedHandlerList common_tasks;
  if (!file_handler_util::FindCommonTasks(profile_, file_urls, &common_tasks))
    return false;

  ExtensionService* service = profile_->GetExtensionService();
  for (file_handler_util::LastUsedHandlerList::const_iterator iter =
           common_tasks.begin();
       iter != common_tasks.end();
       ++iter) {
    const FileBrowserHandler* handler = iter->handler;
    const std::string extension_id = handler->extension_id();
    const Extension* extension = service->GetExtensionById(extension_id, false);
    CHECK(extension);
    DictionaryValue* task = new DictionaryValue;
    task->SetString("taskId",
        file_handler_util::MakeTaskID(extension_id, handler->id()));
    task->SetString("title", handler->title());
    task->Set("patterns", URLPatternSetToStringList(iter->patterns));
    // TODO(zelidrag): Figure out how to expose icon URL that task defined in
    // manifest instead of the default extension icon.
    GURL icon =
        ExtensionIconSource::GetIconURL(extension,
                                        ExtensionIconSet::EXTENSION_ICON_BITTY,
                                        ExtensionIconSet::MATCH_BIGGER,
                                        false, NULL);     // grayscale
    task->SetString("iconUrl", icon.spec());
    task->SetBoolean("driveApp", false);
    result_list->Append(task);
  }

  // Take the union of Drive and extension tasks: Because any extension tasks we
  // found must apply to all of the files (intersection), and because the same
  // is true of the drive apps, we simply take the union of two lists by adding
  // the drive tasks to the extension task list. We know there aren't duplicates
  // because they're entirely different kinds of tasks, but there could be both
  // kinds of tasks for a file type (an image file, for instance).
  if (!FindDriveAppTasks(file_urls, result_list))
    return false;

  // TODO(zelidrag, serya): Add intent content tasks to result_list once we
  // implement that API.
  SendResponse(true);
  return true;
}

ExecuteTasksFileBrowserFunction::ExecuteTasksFileBrowserFunction() {}

void ExecuteTasksFileBrowserFunction::OnTaskExecuted(bool success) {
  SendResponse(success);
}

ExecuteTasksFileBrowserFunction::~ExecuteTasksFileBrowserFunction() {}

bool ExecuteTasksFileBrowserFunction::RunImpl() {
  // First param is task id that was to the extension with getFileTasks call.
  std::string task_id;
  if (!args_->GetString(0, &task_id) || !task_id.size())
    return false;

  // TODO(kaznacheev): Crack the task_id here, store it in the Executor
  // and avoid passing it around.

  // The second param is the list of files that need to be executed with this
  // task.
  ListValue* files_list = NULL;
  if (!args_->GetList(1, &files_list))
    return false;

  std::string extension_id;
  std::string action_id;
  if (!file_handler_util::CrackTaskID(task_id, &extension_id, &action_id)) {
    LOG(WARNING) << "Invalid task " << task_id;
    return false;
  }

  if (!files_list->GetSize())
    return true;

  std::vector<GURL> file_urls;
  for (size_t i = 0; i < files_list->GetSize(); i++) {
    std::string origin_file_url;
    if (!files_list->GetString(i, &origin_file_url)) {
      error_ = kInvalidFileUrl;
      return false;
    }
    file_urls.push_back(GURL(origin_file_url));
  }

  scoped_refptr<FileTaskExecutor> executor(
      FileTaskExecutor::Create(profile(),
                               source_url(),
                               extension_id,
                               action_id));

  if (!executor->ExecuteAndNotify(
      file_urls,
      base::Bind(&ExecuteTasksFileBrowserFunction::OnTaskExecuted, this)))
    return false;

  SetResult(new base::FundamentalValue(true));
  return true;
}

SetDefaultTaskFileBrowserFunction::SetDefaultTaskFileBrowserFunction() {}

SetDefaultTaskFileBrowserFunction::~SetDefaultTaskFileBrowserFunction() {}

bool SetDefaultTaskFileBrowserFunction::RunImpl() {
  // First param is task id that was to the extension with setDefaultTask call.
  std::string task_id;
  if (!args_->GetString(0, &task_id) || !task_id.size())
    return false;

  BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(
            &file_handler_util::UpdateFileHandlerUsageStats,
            profile_, task_id));

  SetResult(new base::FundamentalValue(true));
  return true;
}

FileBrowserFunction::FileBrowserFunction() {
}

FileBrowserFunction::~FileBrowserFunction() {
}

int32 FileBrowserFunction::GetTabId() const {
  if (!dispatcher()) {
    LOG(WARNING) << "No dispatcher";
    return 0;
  }
  if (!dispatcher()->delegate()) {
    LOG(WARNING) << "No delegate";
    return 0;
  }
  WebContents* web_contents =
      dispatcher()->delegate()->GetAssociatedWebContents();
  if (!web_contents) {
    LOG(WARNING) << "No associated tab contents";
    return 0;
  }
  return ExtensionTabUtil::GetTabId(web_contents);
}

void FileBrowserFunction::GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
    const UrlList& file_urls,
    GetLocalPathsCallback callback) {
  scoped_refptr<fileapi::FileSystemContext> file_system_context =
      BrowserContext::GetFileSystemContext(profile_);
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(
          &FileBrowserFunction::GetLocalPathsOnFileThread,
          this,
          file_system_context, file_urls, callback));
}

// GetFileSystemRootPathOnFileThread can only be called from the file thread,
// so here we are. This function takes a vector of virtual paths, converts
// them to local paths and calls |callback| with the result vector, on the UI
// thread.
void FileBrowserFunction::GetLocalPathsOnFileThread(
    scoped_refptr<fileapi::FileSystemContext> file_system_context,
    const UrlList& file_urls,
    GetLocalPathsCallback callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  std::vector<ui::SelectedFileInfo> selected_files;

  // FilePath(virtual_path) doesn't work on win, so limit this to ChromeOS.
  fileapi::ExternalFileSystemMountPointProvider* provider =
      file_system_context->external_provider();
  if (!provider) {
    LOG(WARNING) << "External provider is not available";
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::Bind(callback, selected_files));
    return;
  }

  GURL origin_url = source_url().GetOrigin();
  size_t len = file_urls.size();
  selected_files.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    FilePath local_path;
    const GURL& file_url = file_urls[i];

    // If "localPath" parameter is set, use it as the real path.
    // TODO(satorux): Eventually, we should be able to get the real path
    // from GDataFileSystem instead of passing through with filesystem
    // URLs. crosbug.com/27510.
    //
    // TODO(satorux): GURL::query() is not yet supported for filesystem:
    // URLs. For now, use GURL::spec() to get the query portion. Should
    // get rid of the hack once query() is supported: crbug.com/114484.
    const std::string::size_type query_start = file_url.spec().find('?');
    if (query_start != std::string::npos) {
      const std::string query = file_url.spec().substr(query_start + 1);
      std::vector<std::pair<std::string, std::string> > parameters;
      if (base::SplitStringIntoKeyValuePairs(
              query, '=', '&', &parameters)) {
        for (size_t i = 0; i < parameters.size(); ++i) {
          if (parameters[i].first == "localPath") {
            const std::string unescaped_value =
                net::UnescapeURLComponent(parameters[i].second,
                                          kUnescapeRuleForQueryParameters);
            local_path = FilePath::FromUTF8Unsafe(unescaped_value);
            break;
          }
        }
      }
    }

    // Extract the path from |file_url|.
    GURL file_origin_url;
    FilePath virtual_path;
    fileapi::FileSystemType type;

    if (!CrackFileSystemURL(file_url, &file_origin_url, &type,
                            &virtual_path)) {
      continue;
    }
    if (type != fileapi::kFileSystemTypeExternal) {
      NOTREACHED();
      continue;
    }

    FilePath root = provider->GetFileSystemRootPathOnFileThread(
        origin_url,
        fileapi::kFileSystemTypeExternal,
        FilePath(virtual_path),
        false);
    FilePath file_path;
    if (!root.empty()) {
      file_path = root.Append(virtual_path);
    } else {
      LOG(WARNING) << "GetLocalPathsOnFileThread failed "
                   << file_url.spec();
    }

    if (!file_path.empty()) {
      DVLOG(1) << "Selected: file path: " << file_path.value()
               << " local path: " << local_path.value();
      selected_files.push_back(
          ui::SelectedFileInfo(file_path, local_path));
    }
  }

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          base::Bind(callback, selected_files));
}

bool SelectFileFunction::RunImpl() {
  if (args_->GetSize() != 2) {
    return false;
  }
  std::string file_url;
  args_->GetString(0, &file_url);
  UrlList file_paths;
  file_paths.push_back(GURL(file_url));

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_paths,
      base::Bind(&SelectFileFunction::GetLocalPathsResponseOnUIThread, this));
  return true;
}

void SelectFileFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (files.size() != 1) {
    SendResponse(false);
    return;
  }
  int index;
  args_->GetInteger(1, &index);
  int32 tab_id = GetTabId();
  SelectFileDialogExtension::OnFileSelected(tab_id, files[0], index);
  SendResponse(true);
}


ViewFilesFunction::ViewFilesFunction() {
}

ViewFilesFunction::~ViewFilesFunction() {
}

bool ViewFilesFunction::RunImpl() {
  if (args_->GetSize() < 1) {
    return false;
  }

  ListValue* path_list = NULL;
  args_->GetList(0, &path_list);
  DCHECK(path_list);

  std::string internal_task_id;
  args_->GetString(1, &internal_task_id);

  std::string virtual_path;
  size_t len = path_list->GetSize();
  UrlList file_urls;
  file_urls.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    path_list->GetString(i, &virtual_path);
    file_urls.push_back(GURL(virtual_path));
  }

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_urls,
      base::Bind(&ViewFilesFunction::GetLocalPathsResponseOnUIThread,
                 this,
                 internal_task_id));
  return true;
}

void ViewFilesFunction::GetLocalPathsResponseOnUIThread(
    const std::string& internal_task_id,
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  bool success = true;
  for (SelectedFileInfoList::const_iterator iter = files.begin();
       iter != files.end();
       ++iter) {
    bool handled = file_manager_util::ExecuteBuiltinHandler(
        GetCurrentBrowser(), iter->file_path, internal_task_id);
    if (!handled && files.size() == 1)
      success = false;
  }
  SetResult(Value::CreateBooleanValue(success));
  SendResponse(true);
}

SelectFilesFunction::SelectFilesFunction() {
}

SelectFilesFunction::~SelectFilesFunction() {
}

bool SelectFilesFunction::RunImpl() {
  if (args_->GetSize() != 1) {
    return false;
  }

  ListValue* path_list = NULL;
  args_->GetList(0, &path_list);
  DCHECK(path_list);

  std::string virtual_path;
  size_t len = path_list->GetSize();
  UrlList file_urls;
  file_urls.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    path_list->GetString(i, &virtual_path);
    file_urls.push_back(GURL(virtual_path));
  }

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_urls,
      base::Bind(&SelectFilesFunction::GetLocalPathsResponseOnUIThread, this));
  return true;
}

void SelectFilesFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  int32 tab_id = GetTabId();
  SelectFileDialogExtension::OnMultiFilesSelected(tab_id, files);
  SendResponse(true);
}

bool CancelFileDialogFunction::RunImpl() {
  int32 tab_id = GetTabId();
  SelectFileDialogExtension::OnFileSelectionCanceled(tab_id);
  SendResponse(true);
  return true;
}

AddMountFunction::AddMountFunction() {
}

AddMountFunction::~AddMountFunction() {
}

bool AddMountFunction::RunImpl() {
  // The third argument is simply ignored.
  if (args_->GetSize() != 2 && args_->GetSize() != 3) {
    error_ = "Invalid argument count";
    return false;
  }

  std::string file_url;
  if (!args_->GetString(0, &file_url)) {
    return false;
  }

  std::string mount_type_str;
  if (!args_->GetString(1, &mount_type_str)) {
    return false;
  }

  // Set default return source path to the empty string.
  SetResult(Value::CreateStringValue(""));

  chromeos::MountType mount_type =
      DiskMountManager::MountTypeFromString(mount_type_str);
  switch (mount_type) {
    case chromeos::MOUNT_TYPE_INVALID: {
      error_ = "Invalid mount type";
      SendResponse(false);
      break;
    }
    case chromeos::MOUNT_TYPE_GDATA: {
      const bool success = true;
      // Pass back the gdata mount point path as source path.
      const std::string& gdata_path =
          gdata::util::GetGDataMountPointPathAsString();
      SetResult(Value::CreateStringValue(gdata_path));
      FileBrowserEventRouterFactory::GetForProfile(profile_)->
          MountDrive(base::Bind(&AddMountFunction::SendResponse,
                                this,
                                success));
      break;
    }
    default: {
      UrlList file_paths;
      file_paths.push_back(GURL(file_url));

      GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
          file_paths,
          base::Bind(&AddMountFunction::GetLocalPathsResponseOnUIThread,
                     this,
                     mount_type_str));
      break;
    }
  }

  return true;
}

void AddMountFunction::GetLocalPathsResponseOnUIThread(
    const std::string& mount_type_str,
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!files.size()) {
    SendResponse(false);
    return;
  }

  const FilePath& source_path = files[0].local_path;
  const FilePath::StringType& display_name = files[0].display_name;
  // Check if the source path is under GData cache directory.
  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  gdata::GDataCache* cache = system_service ? system_service->cache() : NULL;
  if (cache && cache->IsUnderGDataCacheDirectory(source_path)) {
    cache->SetMountedStateOnUIThread(
        source_path,
        true,
        base::Bind(&AddMountFunction::OnMountedStateSet, this, mount_type_str,
                   display_name));
  } else {
    OnMountedStateSet(mount_type_str, display_name,
                      gdata::GDATA_FILE_OK, source_path);
  }
}

void AddMountFunction::OnMountedStateSet(const std::string& mount_type,
                                         const FilePath::StringType& file_name,
                                         gdata::GDataFileError error,
                                         const FilePath& file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DiskMountManager* disk_mount_manager = DiskMountManager::GetInstance();
  // Pass back the actual source path of the mount point.
  SetResult(Value::CreateStringValue(file_path.value()));
  SendResponse(true);
  // MountPath() takes a std::string.
  disk_mount_manager->MountPath(file_path.AsUTF8Unsafe(),
                                FilePath(file_name).Extension(), file_name,
                                DiskMountManager::MountTypeFromString(
                                    mount_type));
}

RemoveMountFunction::RemoveMountFunction() {
}

RemoveMountFunction::~RemoveMountFunction() {
}

bool RemoveMountFunction::RunImpl() {
  if (args_->GetSize() != 1) {
    return false;
  }

  std::string mount_path;
  if (!args_->GetString(0, &mount_path)) {
    return false;
  }

  UrlList file_paths;
  file_paths.push_back(GURL(mount_path));
  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_paths,
      base::Bind(&RemoveMountFunction::GetLocalPathsResponseOnUIThread, this));
  return true;
}

void RemoveMountFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (files.size() != 1) {
    SendResponse(false);
    return;
  }
  DiskMountManager::GetInstance()->UnmountPath(files[0].local_path.value());
  SendResponse(true);
}

GetMountPointsFunction::GetMountPointsFunction() {
}

GetMountPointsFunction::~GetMountPointsFunction() {
}

bool GetMountPointsFunction::RunImpl() {
  if (args_->GetSize())
    return false;

  base::ListValue *mounts = new base::ListValue();
  SetResult(mounts);

  DiskMountManager* disk_mount_manager = DiskMountManager::GetInstance();
  DiskMountManager::MountPointMap mount_points =
      disk_mount_manager->mount_points();

  for (DiskMountManager::MountPointMap::const_iterator it =
           mount_points.begin();
       it != mount_points.end();
       ++it) {
    mounts->Append(CreateValueFromMountPoint(profile_, it->second,
                   source_url_));
  }

  SendResponse(true);
  return true;
}

GetSizeStatsFunction::GetSizeStatsFunction() {
}

GetSizeStatsFunction::~GetSizeStatsFunction() {
}

bool GetSizeStatsFunction::RunImpl() {
  if (args_->GetSize() != 1) {
    return false;
  }

  std::string mount_url;
  if (!args_->GetString(0, &mount_url))
    return false;

  UrlList mount_paths;
  mount_paths.push_back(GURL(mount_url));

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      mount_paths,
      base::Bind(&GetSizeStatsFunction::GetLocalPathsResponseOnUIThread, this));
  return true;
}

void GetSizeStatsFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (files.size() != 1) {
    SendResponse(false);
    return;
  }

  if (files[0].file_path == gdata::util::GetGDataMountPointPath()) {
    gdata::GDataSystemService* system_service =
        gdata::GDataSystemServiceFactory::GetForProfile(profile_);

    gdata::GDataFileSystemInterface* file_system =
        system_service->file_system();

    file_system->GetAvailableSpace(
        base::Bind(&GetSizeStatsFunction::GetGDataAvailableSpaceCallback,
                   this));

  } else {
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        base::Bind(
            &GetSizeStatsFunction::CallGetSizeStatsOnFileThread,
            this,
            files[0].file_path.value()));
  }
}

void GetSizeStatsFunction::GetGDataAvailableSpaceCallback(
    gdata::GDataFileError error,
    int64 bytes_total,
    int64 bytes_used) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (error == gdata::GDATA_FILE_OK) {
    int64 bytes_remaining = bytes_total - bytes_used;
    GetSizeStatsCallbackOnUIThread(static_cast<size_t>(bytes_total/1024),
                                   static_cast<size_t>(bytes_remaining/1024));
  } else {
    error_ = base::StringPrintf(kFileError, static_cast<int>(error));
    SendResponse(false);
  }
}

void GetSizeStatsFunction::CallGetSizeStatsOnFileThread(
    const std::string& mount_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  size_t total_size_kb = 0;
  size_t remaining_size_kb = 0;
  DiskMountManager::GetInstance()->
      GetSizeStatsOnFileThread(mount_path, &total_size_kb, &remaining_size_kb);

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(
          &GetSizeStatsFunction::GetSizeStatsCallbackOnUIThread,
          this,
          total_size_kb, remaining_size_kb));
}

void GetSizeStatsFunction::GetSizeStatsCallbackOnUIThread(
    size_t total_size_kb,
    size_t remaining_size_kb) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  base::DictionaryValue* sizes = new base::DictionaryValue();
  SetResult(sizes);

  sizes->SetInteger("totalSizeKB", total_size_kb);
  sizes->SetInteger("remainingSizeKB", remaining_size_kb);

  SendResponse(true);
}

FormatDeviceFunction::FormatDeviceFunction() {
}

FormatDeviceFunction::~FormatDeviceFunction() {
}

bool FormatDeviceFunction::RunImpl() {
  if (args_->GetSize() != 1) {
    return false;
  }

  std::string volume_file_url;
  if (!args_->GetString(0, &volume_file_url)) {
    NOTREACHED();
    return false;
  }

  UrlList file_paths;
  file_paths.push_back(GURL(volume_file_url));

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_paths,
      base::Bind(&FormatDeviceFunction::GetLocalPathsResponseOnUIThread, this));
  return true;
}

void FormatDeviceFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (files.size() != 1) {
    SendResponse(false);
    return;
  }

  DiskMountManager::GetInstance()->FormatMountedDevice(
      files[0].file_path.value());
  SendResponse(true);
}

GetVolumeMetadataFunction::GetVolumeMetadataFunction() {
}

GetVolumeMetadataFunction::~GetVolumeMetadataFunction() {
}

bool GetVolumeMetadataFunction::RunImpl() {
  if (args_->GetSize() != 1) {
    error_ = "Invalid argument count";
    return false;
  }

  std::string volume_mount_url;
  if (!args_->GetString(0, &volume_mount_url)) {
    NOTREACHED();
  }

  UrlList file_paths;
  file_paths.push_back(GURL(volume_mount_url));

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_paths,
      base::Bind(&GetVolumeMetadataFunction::GetLocalPathsResponseOnUIThread,
                 this));

  return true;
}

void GetVolumeMetadataFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Should contain volume's mount path.
  if (files.size() != 1) {
    error_ = "Invalid mount path.";
    SendResponse(false);
    return;
  }

  results_.reset();

  const DiskMountManager::Disk* volume = GetVolumeAsDisk(
      files[0].file_path.value());
  if (volume) {
    DictionaryValue* volume_info =
        CreateValueFromDisk(profile_, volume);
    SetResult(volume_info);
  }

  SendResponse(true);
}

bool ToggleFullscreenFunction::RunImpl() {
  Browser* browser = GetCurrentBrowser();
  if (browser) {
    browser->ToggleFullscreenModeWithExtension(
        file_manager_util::GetFileBrowserExtensionUrl());
  }
  return true;
}

bool IsFullscreenFunction::RunImpl() {
  Browser* browser = GetCurrentBrowser();
  SetResult(Value::CreateBooleanValue(
      browser && browser->window() && browser->window()->IsFullscreen()));
  return true;
}

bool FileDialogStringsFunction::RunImpl() {
  DictionaryValue* dict = new DictionaryValue();
  SetResult(dict);

#define SET_STRING(ns, id) \
  dict->SetString(#id, l10n_util::GetStringUTF16(ns##_##id))

  SET_STRING(IDS, WEB_FONT_FAMILY);
  SET_STRING(IDS, WEB_FONT_SIZE);

  SET_STRING(IDS_FILE_BROWSER, ROOT_DIRECTORY_LABEL);
  SET_STRING(IDS_FILE_BROWSER, ARCHIVE_DIRECTORY_LABEL);
  SET_STRING(IDS_FILE_BROWSER, REMOVABLE_DIRECTORY_LABEL);
  SET_STRING(IDS_FILE_BROWSER, DOWNLOADS_DIRECTORY_LABEL);
  SET_STRING(IDS_FILE_BROWSER, GDATA_DIRECTORY_LABEL);
  SET_STRING(IDS_FILE_BROWSER, NAME_COLUMN_LABEL);
  SET_STRING(IDS_FILE_BROWSER, SIZE_COLUMN_LABEL);
  SET_STRING(IDS_FILE_BROWSER, SIZE_KB);
  SET_STRING(IDS_FILE_BROWSER, SIZE_MB);
  SET_STRING(IDS_FILE_BROWSER, SIZE_GB);
  SET_STRING(IDS_FILE_BROWSER, SIZE_TB);
  SET_STRING(IDS_FILE_BROWSER, SIZE_PB);
  SET_STRING(IDS_FILE_BROWSER, TYPE_COLUMN_LABEL);
  SET_STRING(IDS_FILE_BROWSER, DATE_COLUMN_LABEL);
  SET_STRING(IDS_FILE_BROWSER, PREVIEW_COLUMN_LABEL);
  SET_STRING(IDS_FILE_BROWSER, OFFLINE_COLUMN_LABEL);

  SET_STRING(IDS_FILE_BROWSER, DOWNLOADS_DIRECTORY_WARNING);

  SET_STRING(IDS_FILE_BROWSER, ERROR_CREATING_FOLDER);
  SET_STRING(IDS_FILE_BROWSER, ERROR_INVALID_CHARACTER);
  SET_STRING(IDS_FILE_BROWSER, ERROR_RESERVED_NAME);
  SET_STRING(IDS_FILE_BROWSER, ERROR_HIDDEN_NAME);
  SET_STRING(IDS_FILE_BROWSER, ERROR_WHITESPACE_NAME);
  SET_STRING(IDS_FILE_BROWSER, ERROR_NEW_FOLDER_EMPTY_NAME);
  SET_STRING(IDS_FILE_BROWSER, NEW_FOLDER_BUTTON_LABEL);
  SET_STRING(IDS_FILE_BROWSER, FILENAME_LABEL);
  SET_STRING(IDS_FILE_BROWSER, PREPARING_LABEL);

  SET_STRING(IDS_FILE_BROWSER, DIMENSIONS_LABEL);
  SET_STRING(IDS_FILE_BROWSER, DIMENSIONS_FORMAT);

  SET_STRING(IDS_FILE_BROWSER, IMAGE_DIMENSIONS);
  SET_STRING(IDS_FILE_BROWSER, VOLUME_LABEL);
  SET_STRING(IDS_FILE_BROWSER, READ_ONLY);

  SET_STRING(IDS_FILE_BROWSER, ARCHIVE_MOUNT_FAILED);
  SET_STRING(IDS_FILE_BROWSER, UNMOUNT_FAILED);
  SET_STRING(IDS_FILE_BROWSER, MOUNT_ARCHIVE);
  SET_STRING(IDS_FILE_BROWSER, FORMAT_DEVICE_BUTTON_LABEL);
  SET_STRING(IDS_FILE_BROWSER, UNMOUNT_DEVICE_BUTTON_LABEL);
  SET_STRING(IDS_FILE_BROWSER, IMPORT_PHOTOS_BUTTON_LABEL);

  SET_STRING(IDS_FILE_BROWSER, SEARCH_TEXT_LABEL);

  SET_STRING(IDS_FILE_BROWSER, ACTION_VIEW);
  SET_STRING(IDS_FILE_BROWSER, ACTION_OPEN);
  SET_STRING(IDS_FILE_BROWSER, ACTION_WATCH);
  SET_STRING(IDS_FILE_BROWSER, ACTION_LISTEN);
  SET_STRING(IDS_FILE_BROWSER, INSTALL_CRX);

  SET_STRING(IDS_FILE_BROWSER, GALLERY_EDIT);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_SHARE);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_ENTER_WHEN_DONE);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_AUTOFIX);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_FIXED);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_CROP);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_EXPOSURE);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_BRIGHTNESS);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_CONTRAST);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_ROTATE_LEFT);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_ROTATE_RIGHT);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_UNDO);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_REDO);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_FILE_EXISTS);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_SAVED);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_OVERWRITE_ORIGINAL);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_OVERWRITE_BUBBLE);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_UNSAVED_CHANGES);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_READONLY_WARNING);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_IMAGE_ERROR);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_VIDEO_ERROR);
  SET_STRING(IDS_FILE_BROWSER, AUDIO_ERROR);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_IMAGE_OFFLINE);
  SET_STRING(IDS_FILE_BROWSER, GALLERY_VIDEO_OFFLINE);
  SET_STRING(IDS_FILE_BROWSER, AUDIO_OFFLINE);
  // Reusing the string, but with alias starting with GALLERY.
  dict->SetString("GALLERY_FILE_HIDDEN_NAME",
      l10n_util::GetStringUTF16(IDS_FILE_BROWSER_ERROR_HIDDEN_NAME));

  SET_STRING(IDS_FILE_BROWSER, PHOTO_IMPORT_IMPORT_BUTTON);
  SET_STRING(IDS_FILE_BROWSER, PHOTO_IMPORT_CANCEL_BUTTON);
  SET_STRING(IDS_FILE_BROWSER, PHOTO_IMPORT_GDATA_ERROR);
  SET_STRING(IDS_FILE_BROWSER, PHOTO_IMPORT_SOURCE_ERROR);
  SET_STRING(IDS_FILE_BROWSER, PHOTO_IMPORT_UNKNOWN_DATE);
  SET_STRING(IDS_FILE_BROWSER, PHOTO_IMPORT_NEW_ALBUM_NAME);
  SET_STRING(IDS_FILE_BROWSER, PHOTO_IMPORT_SELECT_ALBUM_CAPTION);
  SET_STRING(IDS_FILE_BROWSER, PHOTO_IMPORT_SELECT_ALBUM_CAPTION_PLURAL);
  SET_STRING(IDS_FILE_BROWSER, PHOTO_IMPORT_IMPORTING_ERROR);

  SET_STRING(IDS_FILE_BROWSER, CONFIRM_OVERWRITE_FILE);
  SET_STRING(IDS_FILE_BROWSER, FILE_ALREADY_EXISTS);
  SET_STRING(IDS_FILE_BROWSER, DIRECTORY_ALREADY_EXISTS);
  SET_STRING(IDS_FILE_BROWSER, ERROR_RENAMING);
  SET_STRING(IDS_FILE_BROWSER, RENAME_PROMPT);
  SET_STRING(IDS_FILE_BROWSER, RENAME_BUTTON_LABEL);

  SET_STRING(IDS_FILE_BROWSER, ERROR_DELETING);
  SET_STRING(IDS_FILE_BROWSER, DELETE_BUTTON_LABEL);

  SET_STRING(IDS_FILE_BROWSER, PASTE_BUTTON_LABEL);

  SET_STRING(IDS_FILE_BROWSER, COPY_BUTTON_LABEL);
  SET_STRING(IDS_FILE_BROWSER, CUT_BUTTON_LABEL);

  SET_STRING(IDS_FILE_BROWSER, TRANSFER_ITEMS_REMAINING);
  SET_STRING(IDS_FILE_BROWSER, TRANSFER_CANCELLED);
  SET_STRING(IDS_FILE_BROWSER, TRANSFER_TARGET_EXISTS_ERROR);
  SET_STRING(IDS_FILE_BROWSER, TRANSFER_FILESYSTEM_ERROR);
  SET_STRING(IDS_FILE_BROWSER, TRANSFER_UNEXPECTED_ERROR);
  SET_STRING(IDS_FILE_BROWSER, COPY_FILE_NAME);
  SET_STRING(IDS_FILE_BROWSER, COPY_ITEMS_REMAINING);
  SET_STRING(IDS_FILE_BROWSER, COPY_CANCELLED);
  SET_STRING(IDS_FILE_BROWSER, COPY_TARGET_EXISTS_ERROR);
  SET_STRING(IDS_FILE_BROWSER, COPY_FILESYSTEM_ERROR);
  SET_STRING(IDS_FILE_BROWSER, COPY_UNEXPECTED_ERROR);
  SET_STRING(IDS_FILE_BROWSER, MOVE_FILE_NAME);
  SET_STRING(IDS_FILE_BROWSER, MOVE_ITEMS_REMAINING);
  SET_STRING(IDS_FILE_BROWSER, MOVE_CANCELLED);
  SET_STRING(IDS_FILE_BROWSER, MOVE_TARGET_EXISTS_ERROR);
  SET_STRING(IDS_FILE_BROWSER, MOVE_FILESYSTEM_ERROR);
  SET_STRING(IDS_FILE_BROWSER, MOVE_UNEXPECTED_ERROR);

  SET_STRING(IDS_FILE_BROWSER, CANCEL_LABEL);
  SET_STRING(IDS_FILE_BROWSER, OPEN_LABEL);
  SET_STRING(IDS_FILE_BROWSER, SAVE_LABEL);
  SET_STRING(IDS_FILE_BROWSER, OK_LABEL);

  SET_STRING(IDS_FILE_BROWSER, DEFAULT_NEW_FOLDER_NAME);
  SET_STRING(IDS_FILE_BROWSER, MORE_FILES);

  SET_STRING(IDS_FILE_BROWSER, CONFIRM_DELETE_ONE);
  SET_STRING(IDS_FILE_BROWSER, CONFIRM_DELETE_SOME);

  SET_STRING(IDS_FILE_BROWSER, UNKNOWN_FILESYSTEM_WARNING);
  SET_STRING(IDS_FILE_BROWSER, UNSUPPORTED_FILESYSTEM_WARNING);
  SET_STRING(IDS_FILE_BROWSER, FORMATTING_WARNING);

  SET_STRING(IDS_FILE_BROWSER, GDATA_MENU_HELP);
  SET_STRING(IDS_FILE_BROWSER, GDATA_SHOW_HOSTED_FILES_OPTION);
  SET_STRING(IDS_FILE_BROWSER, GDATA_MOBILE_CONNECTION_OPTION);
  SET_STRING(IDS_FILE_BROWSER, GDATA_CLEAR_LOCAL_CACHE);
  SET_STRING(IDS_FILE_BROWSER, GDATA_SPACE_AVAILABLE);
  SET_STRING(IDS_FILE_BROWSER, GDATA_SPACE_AVAILABLE_LONG);
  SET_STRING(IDS_FILE_BROWSER, GDATA_WAITING_FOR_SPACE_INFO);
  SET_STRING(IDS_FILE_BROWSER, GDATA_FAILED_SPACE_INFO);
  SET_STRING(IDS_FILE_BROWSER, GDATA_BUY_MORE_SPACE);
  SET_STRING(IDS_FILE_BROWSER, GDATA_BUY_MORE_SPACE_LINK);

  SET_STRING(IDS_FILE_BROWSER, SELECT_FOLDER_TITLE);
  SET_STRING(IDS_FILE_BROWSER, SELECT_OPEN_FILE_TITLE);
  SET_STRING(IDS_FILE_BROWSER, SELECT_OPEN_MULTI_FILE_TITLE);
  SET_STRING(IDS_FILE_BROWSER, SELECT_SAVEAS_FILE_TITLE);

  SET_STRING(IDS_FILE_BROWSER, COMPUTING_SELECTION);
  SET_STRING(IDS_FILE_BROWSER, ONE_FILE_SELECTED);
  SET_STRING(IDS_FILE_BROWSER, ONE_DIRECTORY_SELECTED);
  SET_STRING(IDS_FILE_BROWSER, MANY_FILES_SELECTED);
  SET_STRING(IDS_FILE_BROWSER, MANY_DIRECTORIES_SELECTED);
  SET_STRING(IDS_FILE_BROWSER, MANY_ENTRIES_SELECTED);

  SET_STRING(IDS_FILE_BROWSER, OFFLINE_HEADER);
  SET_STRING(IDS_FILE_BROWSER, OFFLINE_MESSAGE);
  SET_STRING(IDS_FILE_BROWSER, OFFLINE_MESSAGE_PLURAL);
  SET_STRING(IDS_FILE_BROWSER, HOSTED_OFFLINE_MESSAGE);
  SET_STRING(IDS_FILE_BROWSER, HOSTED_OFFLINE_MESSAGE_PLURAL);
  SET_STRING(IDS_FILE_BROWSER, CONFIRM_MOBILE_DATA_USE);
  SET_STRING(IDS_FILE_BROWSER, CONFIRM_MOBILE_DATA_USE_PLURAL);
  SET_STRING(IDS_FILE_BROWSER, GDATA_OUT_OF_SPACE_HEADER);
  SET_STRING(IDS_FILE_BROWSER, GDATA_OUT_OF_SPACE_MESSAGE);
  SET_STRING(IDS_FILE_BROWSER, GDATA_SERVER_OUT_OF_SPACE_HEADER);
  SET_STRING(IDS_FILE_BROWSER, GDATA_SERVER_OUT_OF_SPACE_MESSAGE);
  SET_STRING(IDS_FILE_BROWSER, GDATA_WELCOME_TITLE);
  SET_STRING(IDS_FILE_BROWSER, GDATA_WELCOME_TEXT_SHORT);
  SET_STRING(IDS_FILE_BROWSER, GDATA_WELCOME_TEXT_LONG);
  SET_STRING(IDS_FILE_BROWSER, GDATA_WELCOME_DISMISS);
  SET_STRING(IDS_FILE_BROWSER, GDATA_WELCOME_TITLE_ALTERNATIVE);
  SET_STRING(IDS_FILE_BROWSER, GDATA_WELCOME_GET_STARTED);
  SET_STRING(IDS_FILE_BROWSER, NO_ACTION_FOR_FILE);

  // MP3 metadata extractor plugin
  SET_STRING(IDS_FILE_BROWSER, ID3_ALBUM);  // TALB
  SET_STRING(IDS_FILE_BROWSER, ID3_BPM);  // TBPM
  SET_STRING(IDS_FILE_BROWSER, ID3_COMPOSER);  // TCOM
  SET_STRING(IDS_FILE_BROWSER, ID3_COPYRIGHT_MESSAGE);  // TCOP
  SET_STRING(IDS_FILE_BROWSER, ID3_DATE);  // TDAT
  SET_STRING(IDS_FILE_BROWSER, ID3_PLAYLIST_DELAY);  // TDLY
  SET_STRING(IDS_FILE_BROWSER, ID3_ENCODED_BY);  // TENC
  SET_STRING(IDS_FILE_BROWSER, ID3_LYRICIST);  // TEXT
  SET_STRING(IDS_FILE_BROWSER, ID3_FILE_TYPE);  // TFLT
  SET_STRING(IDS_FILE_BROWSER, ID3_TIME);  // TIME
  SET_STRING(IDS_FILE_BROWSER, ID3_TITLE);  // TIT2
  SET_STRING(IDS_FILE_BROWSER, ID3_LENGTH);  // TLEN
  SET_STRING(IDS_FILE_BROWSER, ID3_FILE_OWNER);  // TOWN
  SET_STRING(IDS_FILE_BROWSER, ID3_LEAD_PERFORMER);  // TPE1
  SET_STRING(IDS_FILE_BROWSER, ID3_BAND);  // TPE2
  SET_STRING(IDS_FILE_BROWSER, ID3_TRACK_NUMBER);  // TRCK
  SET_STRING(IDS_FILE_BROWSER, ID3_YEAR);  // TYER
  SET_STRING(IDS_FILE_BROWSER, ID3_COPYRIGHT);  // WCOP
  SET_STRING(IDS_FILE_BROWSER, ID3_OFFICIAL_AUDIO_FILE_WEBPAGE);  // WOAF
  SET_STRING(IDS_FILE_BROWSER, ID3_OFFICIAL_ARTIST);  // WOAR
  SET_STRING(IDS_FILE_BROWSER, ID3_OFFICIAL_AUDIO_SOURCE_WEBPAGE);  // WOAS
  SET_STRING(IDS_FILE_BROWSER, ID3_PUBLISHERS_OFFICIAL_WEBPAGE);  // WPUB
  SET_STRING(IDS_FILE_BROWSER, ID3_USER_DEFINED_URL_LINK_FRAME);  // WXXX

  // File types
  SET_STRING(IDS_FILE_BROWSER, FOLDER);
  SET_STRING(IDS_FILE_BROWSER, GENERIC_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, NO_EXTENSION_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, DEVICE);
  SET_STRING(IDS_FILE_BROWSER, IMAGE_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, VIDEO_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, AUDIO_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, HTML_DOCUMENT_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, ZIP_ARCHIVE_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, RAR_ARCHIVE_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, TAR_ARCHIVE_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, TAR_BZIP2_ARCHIVE_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, TAR_GZIP_ARCHIVE_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, PLAIN_TEXT_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, PDF_DOCUMENT_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, WORD_DOCUMENT_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, POWERPOINT_PRESENTATION_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, EXCEL_FILE_TYPE);

  SET_STRING(IDS_FILE_BROWSER, GDOC_DOCUMENT_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, GSHEET_DOCUMENT_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, GSLIDES_DOCUMENT_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, GDRAW_DOCUMENT_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, GTABLE_DOCUMENT_FILE_TYPE);
  SET_STRING(IDS_FILE_BROWSER, GLINK_DOCUMENT_FILE_TYPE);

  SET_STRING(IDS_FILE_BROWSER, GDATA_LOADING);
  SET_STRING(IDS_FILE_BROWSER, GDATA_LOADING_PROGRESS);
  SET_STRING(IDS_FILE_BROWSER, GDATA_CANNOT_REACH);
  SET_STRING(IDS_FILE_BROWSER, GDATA_LEARN_MORE);
  SET_STRING(IDS_FILE_BROWSER, GDATA_RETRY);

  SET_STRING(IDS_FILE_BROWSER, AUDIO_PLAYER_TITLE);
  SET_STRING(IDS_FILE_BROWSER, AUDIO_PLAYER_DEFAULT_ARTIST);

  SET_STRING(IDS_FILE_BROWSER, FILE_ERROR_GENERIC);
  SET_STRING(IDS_FILE_BROWSER, FILE_ERROR_NOT_FOUND);
  SET_STRING(IDS_FILE_BROWSER, FILE_ERROR_SECURITY);
  SET_STRING(IDS_FILE_BROWSER, FILE_ERROR_NOT_READABLE);
  SET_STRING(IDS_FILE_BROWSER, FILE_ERROR_NO_MODIFICATION_ALLOWED);
  SET_STRING(IDS_FILE_BROWSER, FILE_ERROR_INVALID_STATE);
  SET_STRING(IDS_FILE_BROWSER, FILE_ERROR_INVALID_MODIFICATION);
  SET_STRING(IDS_FILE_BROWSER, FILE_ERROR_PATH_EXISTS);
  SET_STRING(IDS_FILE_BROWSER, FILE_ERROR_QUOTA_EXCEEDED);

  SET_STRING(IDS_FILE_BROWSER, SEARCH_NO_MATCHING_FILES);
  SET_STRING(IDS_FILE_BROWSER, SEARCH_EXPAND);
  SET_STRING(IDS_FILE_BROWSER, SEARCH_SPINNER);

  SET_STRING(IDS_FILE_BROWSER, CHANGE_DEFAULT_MENU_ITEM);
  SET_STRING(IDS_FILE_BROWSER, CHANGE_DEFAULT_CAPTION);
  SET_STRING(IDS_FILE_BROWSER, DEFAULT_ACTION_LABEL);

  SET_STRING(IDS_FILE_BROWSER, DETAIL_VIEW_TOOLTIP);
  SET_STRING(IDS_FILE_BROWSER, THUMBNAIL_VIEW_TOOLTIP);

  SET_STRING(IDS_FILE_BROWSER, TIME_TODAY);
  SET_STRING(IDS_FILE_BROWSER, TIME_YESTERDAY);

  SET_STRING(IDS_FILE_BROWSER, ALL_FILES_FILTER);
#undef SET_STRING

  dict->SetBoolean("PDF_VIEW_ENABLED",
      file_manager_util::ShouldBeOpenedWithPdfPlugin(profile(), ".pdf"));

  ChromeURLDataManager::DataSource::SetFontAndTextDirection(dict);

  dict->SetBoolean("ENABLE_GDATA", gdata::util::IsGDataAvailable(profile()));

#if defined(USE_ASH)
  dict->SetBoolean("ASH", true);
#else
  dict->SetBoolean("ASH", false);
#endif

  std::string board;
  const char kMachineInfoBoard[] = "CHROMEOS_RELEASE_BOARD";
  chromeos::system::StatisticsProvider* provider =
      chromeos::system::StatisticsProvider::GetInstance();
  if (!provider->GetMachineStatistic(kMachineInfoBoard, &board))
    board = "unknown";
  dict->SetString(kMachineInfoBoard, board);

  dict->SetString("BROWSER_VERSION_MODIFIER",
                  chrome::VersionInfo::GetVersionStringModifier());

  return true;
}

GetGDataFilePropertiesFunction::GetGDataFilePropertiesFunction() {
}

GetGDataFilePropertiesFunction::~GetGDataFilePropertiesFunction() {
}

void GetGDataFilePropertiesFunction::DoOperation(
    const FilePath& file_path,
    base::DictionaryValue* property_dict,
    scoped_ptr<gdata::GDataEntryProto> entry_proto) {
  DCHECK(property_dict);

  // Nothing to do here so simply call OnOperationComplete().
  OnOperationComplete(file_path,
                      property_dict,
                      gdata::GDATA_FILE_OK,
                      entry_proto.Pass());
}

bool GetGDataFilePropertiesFunction::RunImpl() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (args_->GetSize() != 1)
    return false;

  PrepareResults();
  return true;
}

void GetGDataFilePropertiesFunction::PrepareResults() {
  args_->GetList(0, &path_list_);
  DCHECK(path_list_);

  file_properties_.reset(new base::ListValue);

  current_index_ = 0;
  GetNextFileProperties();
}

void GetGDataFilePropertiesFunction::GetNextFileProperties() {
  if (current_index_ >= path_list_->GetSize()) {
    // Exit of asynchronous look and return the result.
    SetResult(file_properties_.release());
    SendResponse(true);
    return;
  }

  std::string file_str;
  path_list_->GetString(current_index_, &file_str);
  GURL file_url = GURL(file_str);
  FilePath file_path = GetVirtualPathFromURL(file_url);

  base::DictionaryValue* property_dict = new base::DictionaryValue;
  property_dict->SetString("fileUrl", file_url.spec());
  file_properties_->Append(property_dict);

  // Start getting the file info.
  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  system_service->file_system()->GetEntryInfoByPath(
      file_path,
      base::Bind(&GetGDataFilePropertiesFunction::OnGetFileInfo,
                 this,
                 file_path,
                 property_dict));
}

void GetGDataFilePropertiesFunction::CompleteGetFileProperties() {
  current_index_++;

  // Could be called from callback. Let finish operation.
  MessageLoop::current()->PostTask(FROM_HERE,
      Bind(&GetGDataFilePropertiesFunction::GetNextFileProperties, this));
}

void GetGDataFilePropertiesFunction::OnGetFileInfo(
    const FilePath& file_path,
    base::DictionaryValue* property_dict,
    gdata::GDataFileError error,
    scoped_ptr<gdata::GDataEntryProto> entry_proto) {
  DCHECK(property_dict);

  if (entry_proto.get() && !entry_proto->has_file_specific_info())
    error = gdata::GDATA_FILE_ERROR_NOT_FOUND;

  if (error == gdata::GDATA_FILE_OK)
    DoOperation(file_path, property_dict, entry_proto.Pass());
  else
    OnOperationComplete(file_path, property_dict, error, entry_proto.Pass());
}

void GetGDataFilePropertiesFunction::OnOperationComplete(
    const FilePath& file_path,
    base::DictionaryValue* property_dict,
    gdata::GDataFileError error,
    scoped_ptr<gdata::GDataEntryProto> entry_proto) {
  if (entry_proto.get() && !entry_proto->has_file_specific_info())
    error = gdata::GDATA_FILE_ERROR_NOT_FOUND;

  if (error != gdata::GDATA_FILE_OK) {
    property_dict->SetInteger("errorCode", error);
    CompleteGetFileProperties();
    return;
  }
  DCHECK(entry_proto.get());

  const gdata::GDataFileSpecificInfo& file_specific_info =
      entry_proto->file_specific_info();
  property_dict->SetString("thumbnailUrl", file_specific_info.thumbnail_url());
  if (!file_specific_info.alternate_url().empty())
    property_dict->SetString("editUrl", file_specific_info.alternate_url());

  if (!entry_proto->content_url().empty()) {
    property_dict->SetString("contentUrl", entry_proto->content_url());
  }

  property_dict->SetBoolean("isHosted",
                            file_specific_info.is_hosted_document());

  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);

  // Get drive WebApps that can accept this file.
  ScopedVector<gdata::DriveWebAppInfo> web_apps;
  system_service->webapps_registry()->GetWebAppsForFile(
          file_path, file_specific_info.content_mime_type(), &web_apps);
  if (!web_apps.empty()) {
    ListValue* apps = new ListValue();
    property_dict->Set("driveApps", apps);
    for (ScopedVector<gdata::DriveWebAppInfo>::const_iterator it =
             web_apps.begin();
         it != web_apps.end(); ++it) {
      const gdata::DriveWebAppInfo* webapp_info = *it;
      DictionaryValue* app = new DictionaryValue();
      app->SetString("appId", webapp_info->app_id);
      app->SetString("appName", webapp_info->app_name);
      GURL app_icon = FindPreferredIcon(webapp_info->app_icons,
                                        kPreferredIconSize);
      if (!app_icon.is_empty())
        app->SetString("appIcon", app_icon.spec());
      GURL doc_icon = FindPreferredIcon(webapp_info->document_icons,
                                        kPreferredIconSize);
      if (!doc_icon.is_empty())
        app->SetString("docIcon", doc_icon.spec());
      app->SetString("objectType", webapp_info->object_type);
      app->SetBoolean("isPrimary", webapp_info->is_primary_selector);
      apps->Append(app);
    }
  }

  system_service->cache()->GetCacheEntryOnUIThread(
      entry_proto->resource_id(),
      file_specific_info.file_md5(),
      base::Bind(
          &GetGDataFilePropertiesFunction::CacheStateReceived,
          this, property_dict));
}

void GetGDataFilePropertiesFunction::CacheStateReceived(
    base::DictionaryValue* property_dict,
    bool /* success */,
    const gdata::GDataCacheEntry& cache_entry) {
  // In case of an error (i.e. success is false), cache_entry.is_*() all
  // returns false.
  property_dict->SetBoolean("isPinned", cache_entry.is_pinned());
  property_dict->SetBoolean("isPresent", cache_entry.is_present());
  property_dict->SetBoolean("isDirty", cache_entry.is_dirty());

  CompleteGetFileProperties();
}

PinGDataFileFunction::PinGDataFileFunction() {
}

PinGDataFileFunction::~PinGDataFileFunction() {
}

bool PinGDataFileFunction::RunImpl() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (args_->GetSize() != 2 || !args_->GetBoolean(1, &set_pin_))
    return false;

  PrepareResults();

  return true;
}

void PinGDataFileFunction::DoOperation(
    const FilePath& file_path,
    base::DictionaryValue* properties,
    scoped_ptr<gdata::GDataEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  // This is subtle but we should take references of resource_id and md5
  // before |file_info| is passed to |callback| by base::Passed(). Otherwise,
  // file_info->whatever() crashes.
  const std::string& resource_id = entry_proto->resource_id();
  const std::string& md5 = entry_proto->file_specific_info().file_md5();
  const gdata::CacheOperationCallback callback =
      base::Bind(&PinGDataFileFunction::OnPinStateSet,
                 this,
                 file_path,
                 properties,
                 base::Passed(&entry_proto));

  if (set_pin_)
    system_service->cache()->PinOnUIThread(resource_id, md5, callback);
  else
    system_service->cache()->UnpinOnUIThread(resource_id, md5, callback);
}

void PinGDataFileFunction::OnPinStateSet(
    const FilePath& path,
    base::DictionaryValue* properties,
    scoped_ptr<gdata::GDataEntryProto> entry_proto,
    gdata::GDataFileError error,
    const std::string& /* resource_id */,
    const std::string& /* md5 */) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  OnOperationComplete(path, properties, error, entry_proto.Pass());
}

GetFileLocationsFunction::GetFileLocationsFunction() {
}

GetFileLocationsFunction::~GetFileLocationsFunction() {
}

bool GetFileLocationsFunction::RunImpl() {
  ListValue* file_urls_as_strings = NULL;
  if (!args_->GetList(0, &file_urls_as_strings))
    return false;

  // Convert the list of strings to a list of GURLs.
  UrlList file_urls;
  for (size_t i = 0; i < file_urls_as_strings->GetSize(); ++i) {
    std::string file_url_as_string;
    if (!file_urls_as_strings->GetString(i, &file_url_as_string))
      return false;
    file_urls.push_back(GURL(file_url_as_string));
  }

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_urls,
      base::Bind(&GetFileLocationsFunction::GetLocalPathsResponseOnUIThread,
                 this));
  return true;
}

void GetFileLocationsFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  ListValue* locations = new ListValue;
  for (size_t i = 0; i < files.size(); ++i) {
    if (gdata::util::IsUnderGDataMountPoint(files[i].file_path)) {
      locations->Append(Value::CreateStringValue("drive"));
    } else {
      locations->Append(Value::CreateStringValue("local"));
    }
  }

  SetResult(locations);
  SendResponse(true);
}

GetGDataFilesFunction::GetGDataFilesFunction()
    : local_paths_(NULL) {
}

GetGDataFilesFunction::~GetGDataFilesFunction() {
}

bool GetGDataFilesFunction::RunImpl() {
  ListValue* file_urls_as_strings = NULL;
  if (!args_->GetList(0, &file_urls_as_strings))
    return false;

  // Convert the list of strings to a list of GURLs.
  UrlList file_urls;
  for (size_t i = 0; i < file_urls_as_strings->GetSize(); ++i) {
    std::string file_url_as_string;
    if (!file_urls_as_strings->GetString(i, &file_url_as_string))
      return false;
    file_urls.push_back(GURL(file_url_as_string));
  }

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_urls,
      base::Bind(&GetGDataFilesFunction::GetLocalPathsResponseOnUIThread,
                 this));
  return true;
}

void GetGDataFilesFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  for (size_t i = 0; i < files.size(); ++i) {
    DCHECK(gdata::util::IsUnderGDataMountPoint(files[i].file_path));
    FilePath gdata_path = gdata::util::ExtractGDataPath(files[i].file_path);
    remaining_gdata_paths_.push(gdata_path);
  }

  local_paths_ = new ListValue;
  GetFileOrSendResponse();
}

void GetGDataFilesFunction::GetFileOrSendResponse() {
  // Send the response if all files are obtained.
  if (remaining_gdata_paths_.empty()) {
    SetResult(local_paths_);
    SendResponse(true);
    return;
  }

  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  DCHECK(system_service);

  // Get the file on the top of the queue.
  FilePath gdata_path = remaining_gdata_paths_.front();
  system_service->file_system()->GetFileByPath(
      gdata_path,
      base::Bind(&GetGDataFilesFunction::OnFileReady, this),
      gdata::GetDownloadDataCallback());
}


void GetGDataFilesFunction::OnFileReady(
    gdata::GDataFileError error,
    const FilePath& local_path,
    const std::string& unused_mime_type,
    gdata::GDataFileType file_type) {
  FilePath gdata_path = remaining_gdata_paths_.front();

  if (error == gdata::GDATA_FILE_OK) {
    local_paths_->Append(Value::CreateStringValue(local_path.value()));
    DVLOG(1) << "Got " << gdata_path.value() << " as " << local_path.value();

    // TODO(benchan): If the file is a hosted document, a temporary JSON file
    // is created to represent the document. The JSON file is not cached and
    // should be deleted after use. We need to somehow communicate with
    // file_manager.js to manage the lifetime of the temporary file.
    // See crosbug.com/28058.
  } else {
    local_paths_->Append(Value::CreateStringValue(""));
    DVLOG(1) << "Failed to get " << gdata_path.value()
             << " with error code: " << error;
  }

  remaining_gdata_paths_.pop();

  // Start getting the next file.
  GetFileOrSendResponse();
}

GetFileTransfersFunction::GetFileTransfersFunction() {}

GetFileTransfersFunction::~GetFileTransfersFunction() {}

ListValue* GetFileTransfersFunction::GetFileTransfersList() {
  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  if (!system_service)
    return NULL;

  std::vector<gdata::GDataOperationRegistry::ProgressStatus>
      list = system_service->docs_service()->operation_registry()->
      GetProgressStatusList();
  return file_manager_util::ProgressStatusVectorToListValue(
      profile_, source_url_.GetOrigin(), list);
}

bool GetFileTransfersFunction::RunImpl() {
  scoped_ptr<ListValue> progress_status_list(GetFileTransfersList());
  if (!progress_status_list.get()) {
    SendResponse(false);
    return false;
  }

  SetResult(progress_status_list.release());
  SendResponse(true);
  return true;
}


CancelFileTransfersFunction::CancelFileTransfersFunction() {}

CancelFileTransfersFunction::~CancelFileTransfersFunction() {}

bool CancelFileTransfersFunction::RunImpl() {
  ListValue* url_list = NULL;
  if (!args_->GetList(0, &url_list)) {
    SendResponse(false);
    return false;
  }

  std::string virtual_path;
  size_t len = url_list->GetSize();
  UrlList file_urls;
  file_urls.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    url_list->GetString(i, &virtual_path);
    file_urls.push_back(GURL(virtual_path));
  }

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_urls,
      base::Bind(&CancelFileTransfersFunction::GetLocalPathsResponseOnUIThread,
                 this));
  return true;
}

void CancelFileTransfersFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  if (!system_service) {
    SendResponse(false);
    return;
  }

  gdata::GDataOperationRegistry* operation_registry =
      system_service->docs_service()->operation_registry();

  scoped_ptr<ListValue> responses(new ListValue());
  for (size_t i = 0; i < files.size(); ++i) {
    DCHECK(gdata::util::IsUnderGDataMountPoint(files[i].file_path));
    FilePath file_path = gdata::util::ExtractGDataPath(files[i].file_path);
    scoped_ptr<DictionaryValue> result(new DictionaryValue());
    result->SetBoolean(
        "canceled",
        operation_registry->CancelForFilePath(file_path));
    GURL file_url;
    if (file_manager_util::ConvertFileToFileSystemUrl(profile_,
            gdata::util::GetSpecialRemoteRootPath().Append(file_path),
            source_url_.GetOrigin(),
            &file_url)) {
      result->SetString("fileUrl", file_url.spec());
    }

    responses->Append(result.release());
  }
  SetResult(responses.release());
  SendResponse(true);
}

TransferFileFunction::TransferFileFunction() {}

TransferFileFunction::~TransferFileFunction() {}

bool TransferFileFunction::RunImpl() {
  std::string local_file_url;
  std::string remote_file_url;
  if (!args_->GetString(0, &local_file_url) ||
      !args_->GetString(1, &remote_file_url)) {
    return false;
  }

  UrlList file_urls;
  file_urls.push_back(GURL(local_file_url));
  file_urls.push_back(GURL(remote_file_url));

  GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      file_urls,
      base::Bind(&TransferFileFunction::GetLocalPathsResponseOnUIThread,
                 this));
  return true;
}

void TransferFileFunction::GetLocalPathsResponseOnUIThread(
    const SelectedFileInfoList& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (files.size() != 2U) {
    SendResponse(false);
    return;
  }

  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  if (!system_service) {
    SendResponse(false);
    return;
  }

  FilePath source_file = files[0].file_path;
  FilePath destination_file = files[1].file_path;

  bool source_file_under_gdata =
      gdata::util::IsUnderGDataMountPoint(source_file);
  bool destination_file_under_gdata =
      gdata::util::IsUnderGDataMountPoint(destination_file);

  if (source_file_under_gdata && !destination_file_under_gdata) {
    // Transfer a file from gdata to local file system.
    source_file = gdata::util::ExtractGDataPath(source_file);
    system_service->file_system()->TransferFileFromRemoteToLocal(
        source_file,
        destination_file,
        base::Bind(&TransferFileFunction::OnTransferCompleted, this));
  } else if (!source_file_under_gdata && destination_file_under_gdata) {
    // Transfer a file from local to gdata file system
    destination_file = gdata::util::ExtractGDataPath(destination_file);
    system_service->file_system()->TransferFileFromLocalToRemote(
        source_file,
        destination_file,
        base::Bind(&TransferFileFunction::OnTransferCompleted, this));
  } else {
    // Local-to-local or gdata-to-gdata file transfers should be done via
    // FileEntry.copyTo in the File API and are thus not supported here.
    NOTREACHED();
    SendResponse(false);
  }
}

void TransferFileFunction::OnTransferCompleted(gdata::GDataFileError error) {
  if (error == gdata::GDATA_FILE_OK) {
    SendResponse(true);
  } else {
    error_ = base::StringPrintf("%d", static_cast<int>(
        fileapi::PlatformFileErrorToWebFileError(
            gdata::util::GDataFileErrorToPlatformError(error))));
    SendResponse(false);
  }
}

// Read GData-related preferences.
bool GetGDataPreferencesFunction::RunImpl() {
  scoped_ptr<DictionaryValue> value(new DictionaryValue());

  const PrefService* service = profile_->GetPrefs();

  bool driveEnabled = gdata::util::IsGDataAvailable(profile_);

  if (driveEnabled)
    AddGDataMountPoint(profile_, extension_id(), render_view_host());

  value->SetBoolean("driveEnabled", driveEnabled);

  value->SetBoolean("cellularDisabled",
                    service->GetBoolean(prefs::kDisableGDataOverCellular));

  value->SetBoolean("hostedFilesDisabled",
                    service->GetBoolean(prefs::kDisableGDataHostedFiles));

  SetResult(value.release());
  return true;
}

// Write GData-related preferences.
bool SetGDataPreferencesFunction::RunImpl() {
  base::DictionaryValue* value = NULL;

  if (!args_->GetDictionary(0, &value) || !value)
    return false;

  PrefService* service = profile_->GetPrefs();

  bool tmp;

  if (value->GetBoolean("cellularDisabled", &tmp)) {
    service->SetBoolean(prefs::kDisableGDataOverCellular, tmp);
  }

  if (value->GetBoolean("hostedFilesDisabled", &tmp)) {
    service->SetBoolean(prefs::kDisableGDataHostedFiles, tmp);
  }

  return true;
}

SearchDriveFunction::SearchDriveFunction() {}

SearchDriveFunction::~SearchDriveFunction() {}

bool SearchDriveFunction::RunImpl() {
  if (!args_->GetString(0, &query_))
    return false;

  if (!args_->GetString(1, &next_feed_))
    return false;

  BrowserContext::GetFileSystemContext(profile())->OpenFileSystem(
      source_url_.GetOrigin(), fileapi::kFileSystemTypeExternal, false,
      base::Bind(&SearchDriveFunction::OnFileSystemOpened, this));
  return true;
}

void SearchDriveFunction::OnFileSystemOpened(
    base::PlatformFileError result,
    const std::string& file_system_name,
    const GURL& file_system_url) {
  if (result != base::PLATFORM_FILE_OK) {
    SendResponse(false);
    return;
  }

  file_system_name_ = file_system_name;
  file_system_url_ = file_system_url;

  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  if (!system_service || !system_service->file_system()) {
    SendResponse(false);
    return;
  }

  system_service->file_system()->Search(
      query_, GURL(next_feed_),
      base::Bind(&SearchDriveFunction::OnSearch, this));
}

void SearchDriveFunction::OnSearch(
    gdata::GDataFileError error,
    const GURL& next_feed,
    scoped_ptr<std::vector<gdata::SearchResultInfo> > results) {
  if (error != gdata::GDATA_FILE_OK) {
    SendResponse(false);
    return;
  }

  DCHECK(results.get());

  base::ListValue* entries = new ListValue();
  // Convert gdata files to something File API stack can understand.
  for (size_t i = 0; i < results->size(); ++i) {
    DictionaryValue* entry = new DictionaryValue();
    entry->SetString("fileSystemName", file_system_name_);
    entry->SetString("fileSystemRoot", file_system_url_.spec());
    entry->SetString("fileFullPath", "/" + results->at(i).path.value());
    entry->SetBoolean("fileIsDirectory", results->at(i).is_directory);

    entries->Append(entry);
  }

  base::DictionaryValue* result = new DictionaryValue();
  result->Set("entries", entries);
  result->SetString("nextFeed", next_feed.spec());

  SetResult(result);
  SendResponse(true);
}

bool ClearDriveCacheFunction::RunImpl() {
  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  // |system_service| is NULL if incognito window / guest login.
  if (!system_service || !system_service->file_system())
    return false;

  // TODO(yoshiki): Receive a callback from JS-side and pass it to
  // ClearCacheAndRemountFileSystem(). http://crbug.com/140511
  system_service->ClearCacheAndRemountFileSystem(base::Callback<void(bool)>());

  SendResponse(true);
  return true;
}

bool GetNetworkConnectionStateFunction::RunImpl() {
  chromeos::NetworkLibrary* network_library =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  if (!network_library)
    return false;

  const chromeos::Network* active_network = network_library->active_network();

  scoped_ptr<DictionaryValue> value(new DictionaryValue());
  value->SetBoolean("online", active_network && active_network->online());

  std::string type_string;
  if (!active_network)
    type_string = "none";
  else if (active_network->type() == chromeos::TYPE_CELLULAR)
    type_string = "cellular";
  else
    type_string = "ethernet";  // Currently we do not care about other types.

  value->SetString("type", type_string);
  SetResult(value.release());

  return true;
}

bool RequestDirectoryRefreshFunction::RunImpl() {
  std::string file_url_as_string;
  if (!args_->GetString(0, &file_url_as_string))
    return false;

  gdata::GDataSystemService* system_service =
      gdata::GDataSystemServiceFactory::GetForProfile(profile_);
  if (!system_service || !system_service->file_system())
    return false;

  FilePath directory_path = GetVirtualPathFromURL(GURL(file_url_as_string));
  system_service->file_system()->RequestDirectoryRefresh(directory_path);

  return true;
}
