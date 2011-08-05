// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_context_menu.h"

#include "base/i18n/rtl.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/profiles/profile.h"
#include "content/common/notification_service.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "views/controls/menu/menu_item_view.h"

////////////////////////////////////////////////////////////////////////////////
// BookmarkContextMenu, public:

BookmarkContextMenu::BookmarkContextMenu(
    gfx::NativeWindow parent_window,
    Profile* profile,
    PageNavigator* page_navigator,
    const BookmarkNode* parent,
    const std::vector<const BookmarkNode*>& selection,
    bool close_on_remove)
    : ALLOW_THIS_IN_INITIALIZER_LIST(
          controller_(new BookmarkContextMenuControllerViews(parent_window,
              this, profile, page_navigator, parent, selection))),
      parent_window_(parent_window),
      ALLOW_THIS_IN_INITIALIZER_LIST(menu_(new views::MenuItemView(this))),
      parent_node_(parent),
      observer_(NULL),
      close_on_remove_(close_on_remove) {
  controller_->BuildMenu();
}

BookmarkContextMenu::~BookmarkContextMenu() {
}

void BookmarkContextMenu::RunMenuAt(const gfx::Point& point) {
  NotificationService::current()->Notify(
      NotificationType::BOOKMARK_CONTEXT_MENU_SHOWN,
      Source<BookmarkContextMenu>(this),
      NotificationService::NoDetails());
  // width/height don't matter here.
  views::MenuItemView::AnchorPosition anchor = base::i18n::IsRTL() ?
      views::MenuItemView::TOPRIGHT : views::MenuItemView::TOPLEFT;
  menu_->RunMenuAt(parent_window_, NULL, gfx::Rect(point.x(), point.y(), 0, 0),
                   anchor, true);
}

////////////////////////////////////////////////////////////////////////////////
// BookmarkContextMenu, views::MenuDelegate implementation:

void BookmarkContextMenu::ExecuteCommand(int command_id) {
  controller_->ExecuteCommand(command_id);
}

bool BookmarkContextMenu::IsItemChecked(int command_id) const {
  return controller_->IsItemChecked(command_id);
}

bool BookmarkContextMenu::IsCommandEnabled(int command_id) const {
  return controller_->IsCommandEnabled(command_id);
}

bool BookmarkContextMenu::ShouldCloseAllMenusOnExecute(int id) {
  return (id != IDC_BOOKMARK_BAR_REMOVE) || close_on_remove_;
}

////////////////////////////////////////////////////////////////////////////////
// BookmarkContextMenu, BookmarkContextMenuControllerViewsDelegate
// implementation:

void BookmarkContextMenu::CloseMenu() {
  menu_->Cancel();
}

void BookmarkContextMenu::AddItemWithStringId(int command_id, int string_id) {
  menu_->AppendMenuItemWithLabel(
      command_id, UTF16ToWide(l10n_util::GetStringUTF16(string_id)));
}

void BookmarkContextMenu::AddSeparator() {
  menu_->AppendSeparator();
}

void BookmarkContextMenu::AddCheckboxItem(int command_id, int string_id) {
  menu_->AppendMenuItem(command_id,
                        UTF16ToWide(l10n_util::GetStringUTF16(string_id)),
                        views::MenuItemView::CHECKBOX);
}

void BookmarkContextMenu::WillRemoveBookmarks(
    const std::vector<const BookmarkNode*>& bookmarks) {
  if (observer_)
    observer_->WillRemoveBookmarks(bookmarks);
}

void BookmarkContextMenu::DidRemoveBookmarks() {
  if (observer_)
    observer_->DidRemoveBookmarks();
}
