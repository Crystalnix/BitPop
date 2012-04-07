// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SELECT_FILE_DIALOG_H_
#define CHROME_BROWSER_UI_SELECT_FILE_DIALOG_H_
#pragma once

#include <string>
#include <vector>

#include "base/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/string16.h"
#include "ui/gfx/native_widget_types.h"

namespace content {
class WebContents;
}

// This function is declared extern such that it is accessible for unit tests
// in /chrome/browser/ui/views/select_file_dialog_win_unittest.cc
extern std::wstring AppendExtensionIfNeeded(const std::wstring& filename,
                                            const std::wstring& filter_selected,
                                            const std::wstring& suggested_ext);

// A base class for shell dialogs.
class BaseShellDialog {
 public:
  // Returns true if a shell dialog box is currently being shown modally
  // to the specified owner.
  virtual bool IsRunning(gfx::NativeWindow owning_window) const = 0;

  // Notifies the dialog box that the listener has been destroyed and it should
  // no longer be sent notifications.
  virtual void ListenerDestroyed() = 0;

 protected:
  virtual ~BaseShellDialog() {}
};

// Shows a dialog box for selecting a file or a folder.
class SelectFileDialog
    : public base::RefCountedThreadSafe<SelectFileDialog>,
      public BaseShellDialog {
 public:
  enum Type {
    SELECT_NONE,
    SELECT_FOLDER,
    SELECT_SAVEAS_FILE,
    SELECT_OPEN_FILE,
    SELECT_OPEN_MULTI_FILE
  };

  // An interface implemented by a Listener object wishing to know about the
  // the result of the Select File/Folder action. These callbacks must be
  // re-entrant.
  class Listener {
   public:
    // Notifies the Listener that a file/folder selection has been made. The
    // file/folder path is in |path|. |params| is contextual passed to
    // SelectFile. |index| specifies the index of the filter passed to the
    // the initial call to SelectFile.
    virtual void FileSelected(const FilePath& path,
                              int index, void* params) = 0;

    // Notifies the Listener that many files have been selected. The
    // files are in |files|. |params| is contextual passed to SelectFile.
    virtual void MultiFilesSelected(
      const std::vector<FilePath>& files, void* params) {}

    // Notifies the Listener that the file/folder selection was aborted (via
    // the  user canceling or closing the selection dialog box, for example).
    // |params| is contextual passed to SelectFile.
    virtual void FileSelectionCanceled(void* params) {}

   protected:
    virtual ~Listener() {}
  };

  // Creates a dialog box helper. This object is ref-counted, but the returned
  // object will have no reference (refcount is 0).
  static SelectFileDialog* Create(Listener* listener);

  // Holds information about allowed extensions on a file save dialog.
  // |extensions| is a list of allowed extensions. For example, it might be
  //   { { "htm", "html" }, { "txt" } }. Only pass more than one extension
  //   in the inner vector if the extensions are equivalent. Do NOT include
  //   leading periods.
  // |extension_description_overrides| overrides the system descriptions of the
  //   specified extensions. Entries correspond to |extensions|; if left blank
  //   the system descriptions will be used.
  // |include_all_files| specifies whether there will be a filter added for all
  //   files (i.e. *.*).
  struct FileTypeInfo {
    FileTypeInfo();
    ~FileTypeInfo();

    std::vector<std::vector<FilePath::StringType> > extensions;
    std::vector<string16> extension_description_overrides;
    bool include_all_files;
  };

  // Selects a File.
  // Before doing anything this function checks if FileBrowsing is forbidden
  // by Policy. If so, it tries to show an InfoBar and behaves as though no File
  // was selected (the user clicked `Cancel` immediately).
  // Otherwise it will start displaying the dialog box. This will also
  // block the calling window until the dialog box is complete. The listener
  // associated with this object will be notified when the selection is
  // complete.
  // |type| is the type of file dialog to be shown, see Type enumeration above.
  // |title| is the title to be displayed in the dialog. If this string is
  //   empty, the default title is used.
  // |default_path| is the default path and suggested file name to be shown in
  //   the dialog. This only works for SELECT_SAVEAS_FILE and SELECT_OPEN_FILE.
  //   Can be an empty string to indicate the platform default.
  // |file_types| holds the infomation about the file types allowed. Pass NULL
  //   to get no special behavior
  // |file_type_index| is the 1-based index into the file type list in
  //   |file_types|. Specify 0 if you don't need to specify extension behavior.
  // |default_extension| is the default extension to add to the file if the
  //   user doesn't type one. This should NOT include the '.'. On Windows, if
  //   you specify this you must also specify |file_types|.
  // |source_contents| is the TabContents the call is originating from, i.e.
  //   where the InfoBar should be shown in case file-selection dialogs are
  //   forbidden by policy, or NULL if no InfoBar should be shown.
  // |owning_window| is the window the dialog is modal to, or NULL for a
  //   modeless dialog.
  // |params| is data from the calling context which will be passed through to
  //   the listener. Can be NULL.
  // NOTE: only one instance of any shell dialog can be shown per owning_window
  //       at a time (for obvious reasons).
  void SelectFile(Type type,
                  const string16& title,
                  const FilePath& default_path,
                  const FileTypeInfo* file_types,
                  int file_type_index,
                  const FilePath::StringType& default_extension,
                  content::WebContents* source_contents,
                  gfx::NativeWindow owning_window,
                  void* params);
  bool HasMultipleFileTypeChoices();

 protected:
  friend class base::RefCountedThreadSafe<SelectFileDialog>;
  explicit SelectFileDialog(Listener* listener);
  virtual ~SelectFileDialog();

  // Displays the actual file-selection dialog.
  // This is overridden in the platform-specific descendants of FileSelectDialog
  // and gets called from SelectFile after testing the
  // AllowFileSelectionDialogs-Policy.
  virtual void SelectFileImpl(Type type,
                              const string16& title,
                              const FilePath& default_path,
                              const FileTypeInfo* file_types,
                              int file_type_index,
                              const FilePath::StringType& default_extension,
                              gfx::NativeWindow owning_window,
                              void* params) = 0;

  // The listener to be notified of selection completion.
  Listener* listener_;

 private:
  // Tests if the file selection dialog can be displayed by
  // testing if the AllowFileSelectionDialogs-Policy is
  // either unset or set to true.
  bool CanOpenSelectFileDialog();

  // Informs the |listener_| that the file selection dialog was canceled. Moved
  // to a function for being able to post it to the message loop.
  void CancelFileSelection(void* params);

  // Returns true if the dialog has multiple file type choices.
  virtual bool HasMultipleFileTypeChoicesImpl() = 0;
};

#endif  // CHROME_BROWSER_UI_SELECT_FILE_DIALOG_H_
