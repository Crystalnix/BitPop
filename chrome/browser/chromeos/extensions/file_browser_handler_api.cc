// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_browser_handler_api.h"

#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop_proxy.h"
#include "base/platform_file.h"
#include "base/values.h"
#include "chrome/browser/chromeos/extensions/file_handler_util.h"
#include "chrome/browser/chromeos/extensions/file_manager_util.h"
#include "chrome/browser/chromeos/gdata/gdata_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/common/extensions/api/file_browser_handler_internal.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "googleurl/src/gurl.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_mount_point_provider.h"
#include "ui/base/dialogs/select_file_dialog.h"

using content::BrowserContext;
using extensions::api::file_browser_handler_internal::FileEntryInfo;
using file_handler::FileSelector;

namespace SelectFile =
    extensions::api::file_browser_handler_internal::SelectFile;

namespace {

const char kNoUserGestureError[] =
    "This method can only be called in response to user gesture, such as a "
    "mouse click or key press.";

// File selector implementation. It is bound to SelectFileFunction instance.
// When |SelectFile| is invoked, it will show save as dialog and listen for user
// action. When user selects the file (or closes the dialog), the function's
// |OnFilePathSelected| method will be called with the result.
// When |SelectFile| is called, the class instance takes ownership of itself.
class FileSelectorImpl : public FileSelector,
                         public ui::SelectFileDialog::Listener {
 public:
  explicit FileSelectorImpl(FileHandlerSelectFileFunction* function);
  virtual ~FileSelectorImpl() OVERRIDE;

 protected:
  // file_handler::FileSelectr overrides.
  // Shows save as dialog with suggested name in window bound to |browser|.
  // After this method is called, the selector implementation should not be
  // deleted. It will delete itself after it receives response from
  // SelectFielDialog.
  virtual void SelectFile(const FilePath& suggested_name,
                          Browser* browser) OVERRIDE;
  virtual void set_function_for_test(
      FileHandlerSelectFileFunction* function) OVERRIDE;

  // ui::SelectFileDialog::Listener overrides.
  virtual void FileSelected(const FilePath& path,
                            int index,
                            void* params) OVERRIDE;
  virtual void MultiFilesSelected(const std::vector<FilePath>& files,
                                  void* params) OVERRIDE;
  virtual void FileSelectionCanceled(void* params) OVERRIDE;

 private:
  bool DoSelectFile(const FilePath& suggeste_name, Browser* browser);
  void SendResponse(bool success, const FilePath& selected_path);

  // Dialog that is shown by selector.
  scoped_refptr<ui::SelectFileDialog> dialog_;

  // Extension function that uses the selector.
  scoped_refptr<FileHandlerSelectFileFunction> function_;

  DISALLOW_COPY_AND_ASSIGN(FileSelectorImpl);
};

FileSelectorImpl::FileSelectorImpl(FileHandlerSelectFileFunction* function)
    : function_(function) {
}

FileSelectorImpl::~FileSelectorImpl() {
  if (dialog_.get())
    dialog_->ListenerDestroyed();
  // Send response if needed.
  if (function_)
    SendResponse(false, FilePath());
}

void FileSelectorImpl::SelectFile(const FilePath& suggested_name,
                                  Browser* browser) {
  if (!DoSelectFile(suggested_name, browser)) {
    // If the dialog wasn't launched, let's asynchronously report failure to the
    // function.
    base::MessageLoopProxy::current()->PostTask(FROM_HERE,
        base::Bind(&FileSelectorImpl::FileSelectionCanceled,
                   base::Unretained(this), reinterpret_cast<void*>(NULL)));
  }
}

// This should be used in testing only.
void FileSelectorImpl::set_function_for_test(
    FileHandlerSelectFileFunction* function) {
  NOTREACHED();
}

bool FileSelectorImpl::DoSelectFile(const FilePath& suggested_name,
                                    Browser* browser) {
  DCHECK(!dialog_.get());
  DCHECK(browser);

  if (!browser->window())
    return false;

  TabContents* tab_contents = chrome::GetActiveTabContents(browser);
  if (!tab_contents)
    return false;

  dialog_ = ui::SelectFileDialog::Create(
      this, new ChromeSelectFilePolicy(tab_contents->web_contents()));

  dialog_->SelectFile(ui::SelectFileDialog::SELECT_SAVEAS_FILE,
      string16() /* dialog title*/, suggested_name,
      NULL /* allowed file types */, 0 /* file type index */,
      std::string() /* default file extension */,
      browser->window()->GetNativeWindow(), NULL /* params */);

  return dialog_->IsRunning(browser->window()->GetNativeWindow());
}

void FileSelectorImpl::FileSelected(
    const FilePath& path, int index, void* params) {
  SendResponse(true, path);
  delete this;
}

void FileSelectorImpl::MultiFilesSelected(
    const std::vector<FilePath>& files,
    void* params) {
  // Only single file should be selected in save-as dialog.
  NOTREACHED();
}

void FileSelectorImpl::FileSelectionCanceled(
    void* params) {
  SendResponse(false, FilePath());
  delete this;
}

void FileSelectorImpl::SendResponse(bool success,
                                    const FilePath& selected_path) {
  // We don't want to send multiple responses.
  if (function_.get())
    function_->OnFilePathSelected(success, selected_path);
  function_ = NULL;
}

typedef base::Callback<void (bool success,
                             const std::string& file_system_name,
                             const GURL& file_system_root)>
    FileSystemOpenCallback;

void RunOpenFileSystemCallback(
    const FileSystemOpenCallback& callback,
    base::PlatformFileError error,
    const std::string& file_system_name,
    const GURL& file_system_root) {
  bool success = (error == base::PLATFORM_FILE_OK);
  callback.Run(success, file_system_name, file_system_root);
}

}  // namespace

FileHandlerSelectFileFunction::FileHandlerSelectFileFunction() {}

// static
void FileHandlerSelectFileFunction::set_file_selector_for_test(
    FileSelector* file_selector) {
  FileHandlerSelectFileFunction::file_selector_for_test_ = file_selector;
}

// static
void FileHandlerSelectFileFunction::set_gesture_check_disabled_for_test(
    bool disabled) {
  FileHandlerSelectFileFunction::gesture_check_disabled_for_test_ = disabled;
}

FileHandlerSelectFileFunction::~FileHandlerSelectFileFunction() {}

bool FileHandlerSelectFileFunction::RunImpl() {
  scoped_ptr<SelectFile::Params> params(SelectFile::Params::Create(*args_));

  FilePath suggested_name(params->selection_params.suggested_name);

  if (!user_gesture() && !gesture_check_disabled_for_test_) {
    error_ = kNoUserGestureError;
    return false;
  }

  // If |file_selector_| is set (e.g. in test), use it instesad of creating new
  // file selector.
  FileSelector* file_selector = GetFileSelector();
  file_selector->SelectFile(suggested_name.BaseName(), GetCurrentBrowser());
  return true;
}

void FileHandlerSelectFileFunction::OnFilePathSelected(
    bool success,
    const FilePath& full_path) {
  if (!success) {
    Respond(false, std::string(), GURL(), FilePath());
    return;
  }

  full_path_ = full_path;

  BrowserContext::GetFileSystemContext(profile_)->OpenFileSystem(
      source_url_.GetOrigin(), fileapi::kFileSystemTypeExternal, false,
      base::Bind(&RunOpenFileSystemCallback,
          base::Bind(&FileHandlerSelectFileFunction::OnFileSystemOpened,
                     this)));
};

void FileHandlerSelectFileFunction::OnFileSystemOpened(
    bool success,
    const std::string& file_system_name,
    const GURL& file_system_root) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  if (success) {
    GrantPermissions(base::Bind(
        &FileHandlerSelectFileFunction::Respond, this,
            success, file_system_name, file_system_root));
    return;
  }
  Respond(success, file_system_name, file_system_root, FilePath());
}

void FileHandlerSelectFileFunction::GrantPermissions(
    const GrantPermissionsCallback& callback) {
  fileapi::ExternalFileSystemMountPointProvider* external_provider =
      BrowserContext::GetFileSystemContext(profile_)->external_provider();
  DCHECK(external_provider);

  FilePath virtual_path;
  external_provider->GetVirtualPath(full_path_, &virtual_path);
  DCHECK(!virtual_path.empty());

  // Grant access to this particular file to target extension. This will
  // ensure that the target extension can access only this FS entry and
  // prevent from traversing FS hierarchy upward.
  external_provider->GrantFileAccessToExtension(extension_id(), virtual_path);

  // Give read write permissions for the file.
  permissions_to_grant_.push_back(std::make_pair(
      full_path_,
      file_handler_util::GetReadWritePermissions()));

  if (!gdata::util::IsUnderGDataMountPoint(full_path_)) {
    OnGotPermissionsToGrant(callback, virtual_path);
    return;
  }

  // For drive files, we also have to grant permissions for cache paths.
  scoped_ptr<std::vector<FilePath> > gdata_paths(new std::vector<FilePath>());
  gdata_paths->push_back(virtual_path);

  gdata::util::InsertGDataCachePathsPermissions(
      profile(),
      gdata_paths.Pass(),
      &permissions_to_grant_,
      base::Bind(&FileHandlerSelectFileFunction::OnGotPermissionsToGrant,
          this, callback, virtual_path));
}

void FileHandlerSelectFileFunction::OnGotPermissionsToGrant(
    const GrantPermissionsCallback& callback,
    const FilePath& virtual_path) {
  for (size_t i = 0; i < permissions_to_grant_.size(); i++) {
    content::ChildProcessSecurityPolicy::GetInstance()->GrantPermissionsForFile(
        render_view_host()->GetProcess()->GetID(),
        permissions_to_grant_[i].first,
        permissions_to_grant_[i].second);
  }
  callback.Run(virtual_path);
}

void FileHandlerSelectFileFunction::Respond(
    bool success,
    const std::string& file_system_name,
    const GURL& file_system_root,
    const FilePath& virtual_path) {
  scoped_ptr<SelectFile::Results::Result> result(
      new SelectFile::Results::Result());
  result->success = success;
  if (success) {
    result->entry.reset(new FileEntryInfo());
    result->entry->file_system_name = file_system_name;
    result->entry->file_system_root = file_system_root.spec();
    result->entry->file_full_path = "/" + virtual_path.value();
    result->entry->file_is_directory = false;
  }

  results_ = SelectFile::Results::Create(*result);
  SendResponse(true);
}

FileSelector* FileHandlerSelectFileFunction::GetFileSelector() {
  FileSelector* result = file_selector_for_test_;
  if (result) {
    result->set_function_for_test(this);
    return result;
  }
  return new FileSelectorImpl(this);
}

FileSelector* FileHandlerSelectFileFunction::file_selector_for_test_ = NULL;

bool FileHandlerSelectFileFunction::gesture_check_disabled_for_test_ = false;

