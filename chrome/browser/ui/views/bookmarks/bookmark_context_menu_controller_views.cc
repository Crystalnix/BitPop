// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_context_menu_controller_views.h"

#include "base/compiler_specific.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_editor.h"
#include "chrome/browser/bookmarks/bookmark_folder_editor_controller.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/pref_names.h"
#include "content/browser/tab_contents/page_navigator.h"
#include "grit/generated_resources.h"

BookmarkContextMenuControllerViews::BookmarkContextMenuControllerViews(
    gfx::NativeWindow parent_window,
    BookmarkContextMenuControllerViewsDelegate* delegate,
    Profile* profile,
    PageNavigator* navigator,
    const BookmarkNode* parent,
    const std::vector<const BookmarkNode*>& selection)
    : parent_window_(parent_window),
      delegate_(delegate),
      profile_(profile),
      navigator_(navigator),
      parent_(parent),
      selection_(selection),
      model_(profile->GetBookmarkModel()) {
  DCHECK(profile_);
  DCHECK(model_->IsLoaded());
  model_->AddObserver(this);
}

BookmarkContextMenuControllerViews::~BookmarkContextMenuControllerViews() {
  if (model_)
    model_->RemoveObserver(this);
}

void BookmarkContextMenuControllerViews::BuildMenu() {
  if (selection_.size() == 1 && selection_[0]->is_url()) {
    delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_OPEN_ALL,
                                   IDS_BOOMARK_BAR_OPEN_IN_NEW_TAB);
    delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW,
                                   IDS_BOOMARK_BAR_OPEN_IN_NEW_WINDOW);
    delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO,
                                   IDS_BOOMARK_BAR_OPEN_INCOGNITO);
  } else {
    delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_OPEN_ALL,
                                   IDS_BOOMARK_BAR_OPEN_ALL);
    delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW,
                                   IDS_BOOMARK_BAR_OPEN_ALL_NEW_WINDOW);
    delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO,
                                   IDS_BOOMARK_BAR_OPEN_ALL_INCOGNITO);
  }

  delegate_->AddSeparator();
  if (selection_.size() == 1 && selection_[0]->is_folder()) {
    delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_RENAME_FOLDER,
                                   IDS_BOOKMARK_BAR_RENAME_FOLDER);
  } else {
    delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_EDIT,
                                   IDS_BOOKMARK_BAR_EDIT);
  }

  delegate_->AddSeparator();
  delegate_->AddItemWithStringId(IDC_CUT, IDS_CUT);
  delegate_->AddItemWithStringId(IDC_COPY, IDS_COPY);
  delegate_->AddItemWithStringId(IDC_PASTE, IDS_PASTE);

  delegate_->AddSeparator();
  delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_REMOVE,
                                 IDS_BOOKMARK_BAR_REMOVE);

  delegate_->AddSeparator();
  delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK,
                                 IDS_BOOMARK_BAR_ADD_NEW_BOOKMARK);
  delegate_->AddItemWithStringId(IDC_BOOKMARK_BAR_NEW_FOLDER,
                                 IDS_BOOMARK_BAR_NEW_FOLDER);

  delegate_->AddSeparator();
  delegate_->AddItemWithStringId(IDC_BOOKMARK_MANAGER, IDS_BOOKMARK_MANAGER);
  delegate_->AddCheckboxItem(IDC_BOOKMARK_BAR_ALWAYS_SHOW,
                             IDS_BOOMARK_BAR_ALWAYS_SHOW);
}

void BookmarkContextMenuControllerViews::ExecuteCommand(int id) {
  BookmarkModel* model = RemoveModelObserver();

  switch (id) {
    case IDC_BOOKMARK_BAR_OPEN_ALL:
    case IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO:
    case IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW: {
      WindowOpenDisposition initial_disposition;
      if (id == IDC_BOOKMARK_BAR_OPEN_ALL) {
        initial_disposition = NEW_FOREGROUND_TAB;
        UserMetrics::RecordAction(
            UserMetricsAction("BookmarkBar_ContextMenu_OpenAll"),
            profile_);
      } else if (id == IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW) {
        initial_disposition = NEW_WINDOW;
        UserMetrics::RecordAction(
            UserMetricsAction("BookmarkBar_ContextMenu_OpenAllInNewWindow"),
            profile_);
      } else {
        initial_disposition = OFF_THE_RECORD;
        UserMetrics::RecordAction(
            UserMetricsAction("BookmarkBar_ContextMenu_OpenAllIncognito"),
            profile_);
      }
      bookmark_utils::OpenAll(parent_window_, profile_, navigator_, selection_,
                              initial_disposition);
      break;
    }

    case IDC_BOOKMARK_BAR_RENAME_FOLDER:
    case IDC_BOOKMARK_BAR_EDIT:
      UserMetrics::RecordAction(
          UserMetricsAction("BookmarkBar_ContextMenu_Edit"), profile_);

      if (selection_.size() != 1) {
        NOTREACHED();
        return;
      }

      if (selection_[0]->is_url()) {
        BookmarkEditor::Show(parent_window_, profile_, parent_,
                             BookmarkEditor::EditDetails(selection_[0]),
                             BookmarkEditor::SHOW_TREE);
      } else {
        BookmarkFolderEditorController::Show(profile_, parent_window_,
            selection_[0], -1,
            BookmarkFolderEditorController::EXISTING_BOOKMARK);
      }
      break;

    case IDC_BOOKMARK_BAR_REMOVE: {
      UserMetrics::RecordAction(
          UserMetricsAction("BookmarkBar_ContextMenu_Remove"), profile_);

      delegate_->WillRemoveBookmarks(selection_);
      for (size_t i = 0; i < selection_.size(); ++i) {
        model->Remove(selection_[i]->parent(),
                      selection_[i]->parent()->GetIndexOf(selection_[i]));
      }
      delegate_->DidRemoveBookmarks();
      selection_.clear();
      break;
    }

    case IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK: {
      UserMetrics::RecordAction(
          UserMetricsAction("BookmarkBar_ContextMenu_Add"), profile_);

      // TODO: this should honor the index from GetParentForNewNodes.
      BookmarkEditor::Show(
          parent_window_, profile_,
          bookmark_utils::GetParentForNewNodes(parent_, selection_, NULL),
          BookmarkEditor::EditDetails(), BookmarkEditor::SHOW_TREE);
      break;
    }

    case IDC_BOOKMARK_BAR_NEW_FOLDER: {
      UserMetrics::RecordAction(
          UserMetricsAction("BookmarkBar_ContextMenu_NewFolder"),
          profile_);
      int index;
      const BookmarkNode* parent =
          bookmark_utils::GetParentForNewNodes(parent_, selection_, &index);
      BookmarkFolderEditorController::Show(profile_, parent_window_, parent,
          index, BookmarkFolderEditorController::NEW_BOOKMARK);
      break;
    }

    case IDC_BOOKMARK_BAR_ALWAYS_SHOW:
      bookmark_utils::ToggleWhenVisible(profile_);
      break;

    case IDC_BOOKMARK_MANAGER:
      UserMetrics::RecordAction(UserMetricsAction("ShowBookmarkManager"),
                                profile_);
      {
        Browser* browser = BrowserList::GetLastActiveWithProfile(profile_);
        if (browser)
          browser->OpenBookmarkManager();
        else
          NOTREACHED();
      }
      break;

    case IDC_CUT:
      delegate_->WillRemoveBookmarks(selection_);
      bookmark_utils::CopyToClipboard(model, selection_, true);
      delegate_->DidRemoveBookmarks();
      break;

    case IDC_COPY:
      bookmark_utils::CopyToClipboard(model, selection_, false);
      break;

    case IDC_PASTE: {
      int index;
      const BookmarkNode* paste_target =
          bookmark_utils::GetParentForNewNodes(parent_, selection_, &index);
      if (!paste_target)
        return;

      bookmark_utils::PasteFromClipboard(model, paste_target, index);
      break;
    }

    default:
      NOTREACHED();
  }
}

bool BookmarkContextMenuControllerViews::IsItemChecked(int id) const {
  DCHECK(id == IDC_BOOKMARK_BAR_ALWAYS_SHOW);
  return profile_->GetPrefs()->GetBoolean(prefs::kShowBookmarkBar);
}

bool BookmarkContextMenuControllerViews::IsCommandEnabled(int id) const {
  bool is_root_node =
      (selection_.size() == 1 &&
       selection_[0]->parent() == model_->root_node());
  bool can_edit =
      profile_->GetPrefs()->GetBoolean(prefs::kEditBookmarksEnabled);
  switch (id) {
    case IDC_BOOKMARK_BAR_OPEN_INCOGNITO:
      return !profile_->IsOffTheRecord() &&
             profile_->GetPrefs()->GetBoolean(prefs::kIncognitoEnabled);

    case IDC_BOOKMARK_BAR_OPEN_ALL_INCOGNITO:
      return HasURLs() && !profile_->IsOffTheRecord() &&
             profile_->GetPrefs()->GetBoolean(prefs::kIncognitoEnabled);

    case IDC_BOOKMARK_BAR_OPEN_ALL:
    case IDC_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW:
      return HasURLs();

    case IDC_BOOKMARK_BAR_RENAME_FOLDER:
    case IDC_BOOKMARK_BAR_EDIT:
      return selection_.size() == 1 && !is_root_node && can_edit;

    case IDC_BOOKMARK_BAR_REMOVE:
      return !selection_.empty() && !is_root_node && can_edit;

    case IDC_BOOKMARK_BAR_NEW_FOLDER:
    case IDC_BOOKMARK_BAR_ADD_NEW_BOOKMARK:
      return can_edit && bookmark_utils::GetParentForNewNodes(
          parent_, selection_, NULL) != NULL;

    case IDC_BOOKMARK_BAR_ALWAYS_SHOW:
      return !profile_->GetPrefs()->IsManagedPreference(
          prefs::kEnableBookmarkBar);

    case IDC_COPY:
    case IDC_CUT:
      return !selection_.empty() && !is_root_node &&
             (id == IDC_COPY || can_edit);

    case IDC_PASTE:
      // Paste to selection from the Bookmark Bar, to parent_ everywhere else
      return can_edit &&
             ((!selection_.empty() &&
               bookmark_utils::CanPasteFromClipboard(selection_[0])) ||
              bookmark_utils::CanPasteFromClipboard(parent_));
  }
  return true;
}

void BookmarkContextMenuControllerViews::BookmarkModelChanged() {
  delegate_->CloseMenu();
}

BookmarkModel* BookmarkContextMenuControllerViews::RemoveModelObserver() {
  BookmarkModel* model = model_;
  model_->RemoveObserver(this);
  model_ = NULL;
  return model;
}

bool BookmarkContextMenuControllerViews::HasURLs() const {
  for (size_t i = 0; i < selection_.size(); ++i) {
    if (bookmark_utils::NodeHasURLs(selection_[i]))
      return true;
  }
  return false;
}
