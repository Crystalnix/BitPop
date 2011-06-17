// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A dialog box that tells the user that we can't write to the specified user
// data directory.  Provides the user a chance to pick a different directory.

#ifndef CHROME_BROWSER_UI_VIEWS_USER_DATA_DIR_DIALOG_H_
#define CHROME_BROWSER_UI_VIEWS_USER_DATA_DIR_DIALOG_H_
#pragma once

#include "base/basictypes.h"
#include "base/message_loop.h"
#include "chrome/browser/ui/shell_dialogs.h"
#include "views/window/dialog_delegate.h"

class FilePath;

namespace views {
class MessageBoxView;
class Window;
}

class UserDataDirDialog : public views::DialogDelegate,
                          public MessageLoopForUI::Dispatcher,
                          public SelectFileDialog::Listener {
 public:
  // Creates and runs a user data directory picker dialog.  The method blocks
  // while the dialog is showing.  If the user picks a directory, this method
  // returns the chosen directory. |user_data_dir| is the value of the
  // directory we were not able to use.
  static FilePath RunUserDataDirDialog(const FilePath& user_data_dir);
  virtual ~UserDataDirDialog();

  FilePath user_data_dir() const { return user_data_dir_; }

  // views::DialogDelegate Methods:
  virtual std::wstring GetDialogButtonLabel(
      MessageBoxFlags::DialogButton button) const;
  virtual std::wstring GetWindowTitle() const;
  virtual void DeleteDelegate();
  virtual bool Accept();
  virtual bool Cancel();

  // views::WindowDelegate Methods:
  virtual bool IsAlwaysOnTop() const { return false; }
  virtual bool IsModal() const { return false; }
  virtual views::View* GetContentsView();

  // MessageLoop::Dispatcher Method:
  virtual bool Dispatch(const MSG& msg);

  // SelectFileDialog::Listener Methods:
  virtual void FileSelected(const FilePath& path, int index, void* params);
  virtual void FileSelectionCanceled(void* params);

 private:
  explicit UserDataDirDialog(const FilePath& user_data_dir);

  // Empty until the user picks a directory.
  FilePath user_data_dir_;

  views::MessageBoxView* message_box_view_;
  scoped_refptr<SelectFileDialog> select_file_dialog_;

  // Used to keep track of whether or not to block the message loop (still
  // waiting for the user to dismiss the dialog).
  bool is_blocking_;

  DISALLOW_COPY_AND_ASSIGN(UserDataDirDialog);
};

#endif  // CHROME_BROWSER_UI_VIEWS_USER_DATA_DIR_DIALOG_H_
