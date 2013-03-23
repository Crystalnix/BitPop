// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_BOOKMARKS_BOOKMARK_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_BOOKMARKS_BOOKMARK_API_H_

#include <list>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/bookmarks/bookmark_model_observer.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/base/dialogs/select_file_dialog.h"

class FilePath;

namespace base {
class ListValue;
}

namespace extensions {

// Observes BookmarkModel and then routes the notifications as events to
// the extension system.
class BookmarkEventRouter : public BookmarkModelObserver {
 public:
  explicit BookmarkEventRouter(BookmarkModel* model);
  virtual ~BookmarkEventRouter();

  // BookmarkModelObserver:
  virtual void Loaded(BookmarkModel* model, bool ids_reassigned) OVERRIDE;
  virtual void BookmarkModelBeingDeleted(BookmarkModel* model) OVERRIDE;
  virtual void BookmarkNodeMoved(BookmarkModel* model,
                                 const BookmarkNode* old_parent,
                                 int old_index,
                                 const BookmarkNode* new_parent,
                                 int new_index) OVERRIDE;
  virtual void BookmarkNodeAdded(BookmarkModel* model,
                                 const BookmarkNode* parent,
                                 int index) OVERRIDE;
  virtual void BookmarkNodeRemoved(BookmarkModel* model,
                                   const BookmarkNode* parent,
                                   int old_index,
                                   const BookmarkNode* node) OVERRIDE;
  virtual void BookmarkNodeChanged(BookmarkModel* model,
                                   const BookmarkNode* node) OVERRIDE;
  virtual void BookmarkNodeFaviconChanged(BookmarkModel* model,
                                          const BookmarkNode* node) OVERRIDE;
  virtual void BookmarkNodeChildrenReordered(BookmarkModel* model,
                                             const BookmarkNode* node) OVERRIDE;
  virtual void ExtensiveBookmarkChangesBeginning(BookmarkModel* model) OVERRIDE;
  virtual void ExtensiveBookmarkChangesEnded(BookmarkModel* model) OVERRIDE;

 private:
  // Helper to actually dispatch an event to extension listeners.
  void DispatchEvent(Profile* profile,
                     const char* event_name,
                     scoped_ptr<base::ListValue> event_args);

  BookmarkModel* model_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkEventRouter);
};

class BookmarkAPI : public ProfileKeyedService,
                    public EventRouter::Observer {
 public:
  explicit BookmarkAPI(Profile* profile);
  virtual ~BookmarkAPI();

  // ProfileKeyedService implementation.
  virtual void Shutdown() OVERRIDE;

  // EventRouter::Observer implementation.
  virtual void OnListenerAdded(const EventListenerInfo& details)
      OVERRIDE;

 private:
  Profile* profile_;

  // Created lazily upon OnListenerAdded.
  scoped_ptr<BookmarkEventRouter> bookmark_event_router_;
};

class BookmarksFunction : public AsyncExtensionFunction,
                          public content::NotificationObserver {
 public:
  // AsyncExtensionFunction:
  virtual void Run() OVERRIDE;

 protected:
  virtual ~BookmarksFunction() {}

  // Helper to get the bookmark id as int64 from the given string id.
  // Sets error_ to an error string if the given id string can't be parsed
  // as an int64. In case of error, doesn't change id and returns false.
  bool GetBookmarkIdAsInt64(const std::string& id_string, int64* id);

  // Helper that checks if bookmark editing is enabled. If it's not, this sets
  // error_ to the appropriate error string.
  bool EditBookmarksEnabled();

 private:
  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  content::NotificationRegistrar registrar_;
};

class GetBookmarksFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.get")

 protected:
  virtual ~GetBookmarksFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class GetBookmarkChildrenFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.getChildren")

 protected:
  virtual ~GetBookmarkChildrenFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class GetBookmarkRecentFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.getRecent")

 protected:
  virtual ~GetBookmarkRecentFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class GetBookmarkTreeFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.getTree")

 protected:
  virtual ~GetBookmarkTreeFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class GetBookmarkSubTreeFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.getSubTree")

 protected:
  virtual ~GetBookmarkSubTreeFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class SearchBookmarksFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.search")

 protected:
  virtual ~SearchBookmarksFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class RemoveBookmarkFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.remove")

  // Returns true on successful parse and sets invalid_id to true if conversion
  // from id string to int64 failed.
  static bool ExtractIds(const base::ListValue* args, std::list<int64>* ids,
                         bool* invalid_id);
  // ExtensionFunction:
  virtual void GetQuotaLimitHeuristics(
      QuotaLimitHeuristics* heuristics) const OVERRIDE;

 protected:
  virtual ~RemoveBookmarkFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class RemoveTreeBookmarkFunction : public RemoveBookmarkFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.removeTree")

 protected:
  virtual ~RemoveTreeBookmarkFunction() {}
};

class CreateBookmarkFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.create")

  // ExtensionFunction:
  virtual void GetQuotaLimitHeuristics(
      QuotaLimitHeuristics* heuristics) const OVERRIDE;

 protected:
  virtual ~CreateBookmarkFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class MoveBookmarkFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.move")

  static bool ExtractIds(const base::ListValue* args, std::list<int64>* ids,
                         bool* invalid_id);

  // ExtensionFunction:
  virtual void GetQuotaLimitHeuristics(
      QuotaLimitHeuristics* heuristics) const OVERRIDE;

 protected:
  virtual ~MoveBookmarkFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class UpdateBookmarkFunction : public BookmarksFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.update")

  static bool ExtractIds(const base::ListValue* args, std::list<int64>* ids,
                         bool* invalid_id);

  // ExtensionFunction:
  virtual void GetQuotaLimitHeuristics(
      QuotaLimitHeuristics* heuristics) const OVERRIDE;

 protected:
  virtual ~UpdateBookmarkFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class BookmarksIOFunction : public BookmarksFunction,
                            public ui::SelectFileDialog::Listener {
 public:
  BookmarksIOFunction();

  virtual void FileSelected(const FilePath& path, int index, void* params) = 0;

  // ui::SelectFileDialog::Listener:
  virtual void MultiFilesSelected(const std::vector<FilePath>& files,
                                  void* params) OVERRIDE;
  virtual void FileSelectionCanceled(void* params) OVERRIDE;

  void SelectFile(ui::SelectFileDialog::Type type);

 protected:
  virtual ~BookmarksIOFunction();

 private:
  void ShowSelectFileDialog(
      ui::SelectFileDialog::Type type,
      const FilePath& default_path);

 protected:
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
};

class ImportBookmarksFunction : public BookmarksIOFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.import");

  // BookmarkManagerIOFunction:
  virtual void FileSelected(const FilePath& path, int index, void* params)
      OVERRIDE;

 private:
  virtual ~ImportBookmarksFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class ExportBookmarksFunction : public BookmarksIOFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.export");

  // BookmarkManagerIOFunction:
  virtual void FileSelected(const FilePath& path, int index, void* params)
      OVERRIDE;

 private:
  virtual ~ExportBookmarksFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_BOOKMARKS_BOOKMARK_API_H_
