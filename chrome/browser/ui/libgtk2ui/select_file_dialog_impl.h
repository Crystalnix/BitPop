// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file implements common select dialog functionality between GTK and KDE.

#ifndef CHROME_BROWSER_UI_LIBGTK2UI_SELECT_FILE_DIALOG_IMPL_H_
#define CHROME_BROWSER_UI_LIBGTK2UI_SELECT_FILE_DIALOG_IMPL_H_

#include <set>

#include "base/compiler_specific.h"
#include "base/nix/xdg_util.h"
#include "ui/base/dialogs/select_file_dialog.h"
#include "ui/base/dialogs/select_file_policy.h"
#include "ui/aura/window.h"

namespace libgtk2ui {

// Shared implementation SelectFileDialog used by SelectFileDialogImplGTK
class SelectFileDialogImpl : public ui::SelectFileDialog {
 public:
  // Main factory method which returns correct type.
  static ui::SelectFileDialog* Create(Listener* listener,
                                      ui::SelectFilePolicy* policy);

  // Factory method for creating a GTK-styled SelectFileDialogImpl
  static SelectFileDialogImpl* NewSelectFileDialogImplGTK(
      Listener* listener,
      ui::SelectFilePolicy* policy);
  // Factory method for creating a KDE-styled SelectFileDialogImpl
  static SelectFileDialogImpl* NewSelectFileDialogImplKDE(
      Listener* listener,
      ui::SelectFilePolicy* policy,
      base::nix::DesktopEnvironment desktop);

  // Returns true if the SelectFileDialog class returned by
  // NewSelectFileDialogImplKDE will actually work.
  static bool CheckKDEDialogWorksOnUIThread();

  // BaseShellDialog implementation.
  virtual bool IsRunning(gfx::NativeWindow parent_window) const OVERRIDE;
  virtual void ListenerDestroyed() OVERRIDE;

 protected:
  explicit SelectFileDialogImpl(Listener* listener,
                                ui::SelectFilePolicy* policy);
  virtual ~SelectFileDialogImpl();

  // SelectFileDialog implementation.
  // |params| is user data we pass back via the Listener interface.
  virtual void SelectFileImpl(Type type,
                              const string16& title,
                              const FilePath& default_path,
                              const FileTypeInfo* file_types,
                              int file_type_index,
                              const FilePath::StringType& default_extension,
                              gfx::NativeWindow owning_window,
                              void* params) = 0;

  // Wrapper for file_util::DirectoryExists() that allow access on the UI
  // thread. Use this only in the file dialog functions, where it's ok
  // because the file dialog has to do many stats anyway. One more won't
  // hurt too badly and it's likely already cached.
  bool CallDirectoryExistsOnUIThread(const FilePath& path);

  // The file filters.
  FileTypeInfo file_types_;

  // The index of the default selected file filter.
  // Note: This starts from 1, not 0.
  size_t file_type_index_;

  // The set of all parent windows for which we are currently running dialogs.
  std::set<aura::Window*> parents_;

  // The type of dialog we are showing the user.
  Type type_;

  // These two variables track where the user last saved a file or opened a
  // file so that we can display future dialogs with the same starting path.
  static FilePath* last_saved_path_;
  static FilePath* last_opened_path_;

  DISALLOW_COPY_AND_ASSIGN(SelectFileDialogImpl);
};

}  // namespace libgtk2ui

#endif  // CHROME_BROWSER_UI_LIBGTK2UI_SELECT_FILE_DIALOG_IMPL_H_
