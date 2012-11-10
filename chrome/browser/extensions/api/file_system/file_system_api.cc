// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/file_system/file_system_api.h"

#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/shell_window_registry.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/browser/ui/extensions/shell_window.h"
#include "chrome/common/extensions/api/file_system.h"
#include "chrome/common/extensions/permissions/api_permission.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/file_system_util.h"
#include "webkit/fileapi/isolated_context.h"

using fileapi::IsolatedContext;

const char kInvalidParameters[] = "Invalid parameters";
const char kSecurityError[] = "Security error";
const char kInvalidCallingPage[] = "Invalid calling page";
const char kUserCancelled[] = "User cancelled";
const char kWritableFileError[] = "Invalid file for writing";
const char kRequiresFileSystemWriteError[] =
    "Operation requires fileSystemWrite permission";
const char kUnknownChooseFileType[] = "Unknown type";

const char kOpenFileOption[] = "openFile";
const char kOpenWritableFileOption[] ="openWritableFile";
const char kSaveFileOption[] = "saveFile";

namespace file_system = extensions::api::file_system;
namespace ChooseFile = file_system::ChooseFile;

namespace {

struct RewritePair {
  int path_key;
  const char* output;
};

const RewritePair g_rewrite_pairs[] = {
#if defined(OS_WIN)
  {base::DIR_PROFILE, "~"},
#elif defined(OS_POSIX)
  {base::DIR_HOME, "~"},
#endif
};

FilePath PrettifyPath(const FilePath& file_path) {
#if defined(OS_WIN) || defined(OS_POSIX)
  for (size_t i = 0; i < arraysize(g_rewrite_pairs); ++i) {
    FilePath candidate_path;
    if (!PathService::Get(g_rewrite_pairs[i].path_key, &candidate_path))
      continue;  // We don't DCHECK this value, as Get will return false even
                 // if the path_key gives a blank string as a result.

    FilePath output = FilePath::FromUTF8Unsafe(g_rewrite_pairs[i].output);
    if (candidate_path.AppendRelativePath(file_path, &output)) {
      // The output path must not be absolute, as it might collide with the
      // real filesystem.
      DCHECK(!output.IsAbsolute());
      return output;
    }
  }
#endif

  return file_path;
}

bool g_skip_picker_for_test = false;
FilePath* g_path_to_be_picked_for_test;

bool GetFilePathOfFileEntry(const std::string& filesystem_name,
                            const std::string& filesystem_path,
                            const content::RenderViewHost* render_view_host,
                            FilePath* file_path,
                            std::string* error) {
  std::string filesystem_id;
  if (!fileapi::CrackIsolatedFileSystemName(filesystem_name, &filesystem_id)) {
    *error = kInvalidParameters;
    return false;
  }

  // Only return the display path if the process has read access to the
  // filesystem.
  content::ChildProcessSecurityPolicy* policy =
      content::ChildProcessSecurityPolicy::GetInstance();
  if (!policy->CanReadFileSystem(render_view_host->GetProcess()->GetID(),
                                 filesystem_id)) {
    *error = kSecurityError;
    return false;
  }

  IsolatedContext* context = IsolatedContext::GetInstance();
  FilePath relative_path = FilePath::FromUTF8Unsafe(filesystem_path);
  FilePath virtual_path = context->CreateVirtualRootPath(filesystem_id)
      .Append(relative_path);
  if (!context->CrackIsolatedPath(virtual_path,
                                  &filesystem_id,
                                  NULL,
                                  file_path)) {
    *error = kInvalidParameters;
    return false;
  }

  return true;
}

bool DoCheckWritableFile(const FilePath& path) {
  // Don't allow links.
  if (file_util::PathExists(path) && file_util::IsLink(path))
    return false;

  // Create the file if it doesn't already exist.
  base::PlatformFileError error = base::PLATFORM_FILE_OK;
  int creation_flags = base::PLATFORM_FILE_CREATE |
                       base::PLATFORM_FILE_READ |
                       base::PLATFORM_FILE_WRITE;
  base::CreatePlatformFile(path, creation_flags, NULL, &error);
  return error == base::PLATFORM_FILE_OK ||
         error == base::PLATFORM_FILE_ERROR_EXISTS;
}

}  // namespace

namespace extensions {

bool FileSystemGetDisplayPathFunction::RunImpl() {
  std::string filesystem_name;
  std::string filesystem_path;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &filesystem_name));
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(1, &filesystem_path));

  FilePath file_path;
  if (!GetFilePathOfFileEntry(filesystem_name, filesystem_path,
                              render_view_host_, &file_path, &error_))
    return false;

  file_path = PrettifyPath(file_path);
  SetResult(base::Value::CreateStringValue(file_path.value()));
  return true;
}

bool FileSystemEntryFunction::HasFileSystemWritePermission() {
  const extensions::Extension* extension = GetExtension();
  if (!extension)
    return false;

  return extension->HasAPIPermission(APIPermission::kFileSystemWrite);
}

void FileSystemEntryFunction::CheckWritableFile(const FilePath& path) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  if (DoCheckWritableFile(path)) {
    content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
        base::Bind(&FileSystemEntryFunction::RegisterFileSystemAndSendResponse,
            this, path, WRITABLE));
    return;
  }

  content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
      base::Bind(&FileSystemEntryFunction::HandleWritableFileError, this));
}

void FileSystemEntryFunction::RegisterFileSystemAndSendResponse(
    const FilePath& path, EntryType entry_type) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  fileapi::IsolatedContext* isolated_context =
      fileapi::IsolatedContext::GetInstance();
  DCHECK(isolated_context);

  std::string registered_name;
  std::string filesystem_id = isolated_context->RegisterFileSystemForPath(
      fileapi::kFileSystemTypeIsolated, path, &registered_name);

  content::ChildProcessSecurityPolicy* policy =
      content::ChildProcessSecurityPolicy::GetInstance();
  int renderer_id = render_view_host_->GetProcess()->GetID();
  if (entry_type == WRITABLE)
    policy->GrantReadWriteFileSystem(renderer_id, filesystem_id);
  else
    policy->GrantReadFileSystem(renderer_id, filesystem_id);

  // We only need file level access for reading FileEntries. Saving FileEntries
  // just needs the file system to have read/write access, which is granted
  // above if required.
  if (!policy->CanReadFile(renderer_id, path))
    policy->GrantReadFile(renderer_id, path);

  DictionaryValue* dict = new DictionaryValue();
  SetResult(dict);
  dict->SetString("fileSystemId", filesystem_id);
  dict->SetString("baseName", registered_name);
  SendResponse(true);
}

void FileSystemEntryFunction::HandleWritableFileError() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  error_ = kWritableFileError;
  SendResponse(false);
}

bool FileSystemGetWritableFileEntryFunction::RunImpl() {
  std::string filesystem_name;
  std::string filesystem_path;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &filesystem_name));
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(1, &filesystem_path));

  if (!HasFileSystemWritePermission()) {
    error_ = kRequiresFileSystemWriteError;
    return false;
  }

  FilePath path;
  if (!GetFilePathOfFileEntry(filesystem_name, filesystem_path,
                              render_view_host_, &path, &error_))
    return false;

  content::BrowserThread::PostTask(content::BrowserThread::FILE, FROM_HERE,
      base::Bind(&FileSystemGetWritableFileEntryFunction::CheckWritableFile,
          this, path));
  return true;
}

bool FileSystemIsWritableFileEntryFunction::RunImpl() {
  std::string filesystem_name;
  std::string filesystem_path;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &filesystem_name));
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(1, &filesystem_path));

  std::string filesystem_id;
  if (!fileapi::CrackIsolatedFileSystemName(filesystem_name, &filesystem_id)) {
    error_ = kInvalidParameters;
    return false;
  }

  content::ChildProcessSecurityPolicy* policy =
      content::ChildProcessSecurityPolicy::GetInstance();
  int renderer_id = render_view_host_->GetProcess()->GetID();
  bool is_writable = policy->CanReadWriteFileSystem(renderer_id,
                                                    filesystem_id);

  SetResult(base::Value::CreateBooleanValue(is_writable));
  return true;
}

// Handles showing a dialog to the user to ask for the filename for a file to
// save or open.
class FileSystemChooseFileFunction::FilePicker
    : public ui::SelectFileDialog::Listener {
 public:
  FilePicker(FileSystemChooseFileFunction* function,
             content::WebContents* web_contents,
             const FilePath& suggested_name,
             ui::SelectFileDialog::Type picker_type,
             EntryType entry_type)
      : suggested_name_(suggested_name),
        entry_type_(entry_type),
        function_(function) {
    select_file_dialog_ = ui::SelectFileDialog::Create(
        this, new ChromeSelectFilePolicy(web_contents));
    ui::SelectFileDialog::FileTypeInfo file_type_info;
    FilePath::StringType extension = suggested_name.Extension();
    if (!extension.empty()) {
      extension.erase(extension.begin());  // drop the .
      file_type_info.extensions.resize(1);
      file_type_info.extensions[0].push_back(extension);
    }
    file_type_info.include_all_files = true;
    gfx::NativeWindow owning_window = web_contents ?
        platform_util::GetTopLevel(web_contents->GetNativeView()) : NULL;

    if (g_skip_picker_for_test) {
      if (g_path_to_be_picked_for_test) {
        content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
            base::Bind(
                &FileSystemChooseFileFunction::FilePicker::FileSelected,
                base::Unretained(this), *g_path_to_be_picked_for_test, 1,
                static_cast<void*>(NULL)));
      } else {
        content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
            base::Bind(
                &FileSystemChooseFileFunction::FilePicker::
                    FileSelectionCanceled,
                base::Unretained(this), static_cast<void*>(NULL)));
      }
      return;
    }

    select_file_dialog_->SelectFile(picker_type,
                                    string16(),
                                    suggested_name,
                                    &file_type_info, 0, FILE_PATH_LITERAL(""),
                                    owning_window, NULL);
  }

  virtual ~FilePicker() {}

 private:
  // SelectFileDialog::Listener implementation.
  virtual void FileSelected(const FilePath& path,
                            int index,
                            void* params) OVERRIDE {
    function_->FileSelected(path, entry_type_);
    delete this;
  }

  virtual void FileSelectionCanceled(void* params) OVERRIDE {
    function_->FileSelectionCanceled();
    delete this;
  }

  FilePath suggested_name_;

  EntryType entry_type_;

  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  scoped_refptr<FileSystemChooseFileFunction> function_;

  DISALLOW_COPY_AND_ASSIGN(FilePicker);
};

bool FileSystemChooseFileFunction::ShowPicker(
    const FilePath& suggested_name,
    ui::SelectFileDialog::Type picker_type,
    EntryType entry_type) {
  ShellWindowRegistry* registry = ShellWindowRegistry::Get(profile());
  DCHECK(registry);
  ShellWindow* shell_window = registry->GetShellWindowForRenderViewHost(
      render_view_host());
  if (!shell_window) {
    error_ = kInvalidCallingPage;
    return false;
  }

  // The file picker will hold a reference to this function instance, preventing
  // its destruction (and subsequent sending of the function response) until the
  // user has selected a file or cancelled the picker. At that point, the picker
  // will delete itself, which will also free the function instance.
  new FilePicker(this, shell_window->web_contents(), suggested_name,
      picker_type, entry_type);
  return true;
}

// static
void FileSystemChooseFileFunction::SkipPickerAndAlwaysSelectPathForTest(
    FilePath* path) {
  g_skip_picker_for_test = true;
  g_path_to_be_picked_for_test = path;
}

// static
void FileSystemChooseFileFunction::SkipPickerAndAlwaysCancelForTest() {
  g_skip_picker_for_test = true;
  g_path_to_be_picked_for_test = NULL;
}

// static
void FileSystemChooseFileFunction::StopSkippingPickerForTest() {
  g_skip_picker_for_test = false;
}

void FileSystemChooseFileFunction::FileSelected(const FilePath& path,
                                                EntryType entry_type) {
  if (entry_type == WRITABLE) {
    content::BrowserThread::PostTask(content::BrowserThread::FILE, FROM_HERE,
        base::Bind(&FileSystemChooseFileFunction::CheckWritableFile,
            this, path));
    return;
  }

  // Don't need to check the file, it's for reading.
  RegisterFileSystemAndSendResponse(path, READ_ONLY);
}

void FileSystemChooseFileFunction::FileSelectionCanceled() {
  error_ = kUserCancelled;
  SendResponse(false);
}

bool FileSystemChooseFileFunction::RunImpl() {
  scoped_ptr<ChooseFile::Params> params(ChooseFile::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  FilePath suggested_name;
  EntryType entry_type = READ_ONLY;
  ui::SelectFileDialog::Type picker_type =
      ui::SelectFileDialog::SELECT_OPEN_FILE;

  file_system::ChooseFileOptions* options = params->options.get();
  if (options) {
    if (options->type.get()) {
      if (*options->type == kOpenWritableFileOption) {
        entry_type = WRITABLE;
      } else if (*options->type == kSaveFileOption) {
        entry_type = WRITABLE;
        picker_type = ui::SelectFileDialog::SELECT_SAVEAS_FILE;
      } else if (*options->type != kOpenFileOption) {
        error_ = kUnknownChooseFileType;
        return false;
      }
    }

    if (options->suggested_name.get()) {
      suggested_name = FilePath::FromUTF8Unsafe(
          *options->suggested_name.get());

      // Don't allow any path components; shorten to the base name. This should
      // result in a relative path, but in some cases may not. Clear the
      // suggestion for safety if this is the case.
      suggested_name = suggested_name.BaseName();
      if (suggested_name.IsAbsolute()) {
        suggested_name = FilePath();
      }
    }
  }

  if (entry_type == WRITABLE && !HasFileSystemWritePermission()) {
    error_ = kRequiresFileSystemWriteError;
    return false;
  }

  return ShowPicker(suggested_name, picker_type, entry_type);
}

}  // namespace extensions
