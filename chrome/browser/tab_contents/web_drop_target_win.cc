// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/tab_contents/web_drop_target_win.h"

#include <windows.h>
#include <shlobj.h>

#include "chrome/browser/bookmarks/bookmark_node_data.h"
#include "chrome/browser/tab_contents/web_drag_utils_win.h"
#include "chrome/browser/ui/bookmarks/bookmark_tab_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_util.h"
#include "ui/base/clipboard/clipboard_util_win.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/dragdrop/os_exchange_data_provider_win.h"
#include "ui/gfx/point.h"
#include "webkit/glue/webdropdata.h"
#include "webkit/glue/window_open_disposition.h"

using WebKit::WebDragOperationNone;
using WebKit::WebDragOperationCopy;
using WebKit::WebDragOperationLink;
using WebKit::WebDragOperationMove;
using WebKit::WebDragOperationGeneric;
using content::OpenURLParams;
using content::Referrer;
using content::WebContents;

namespace {

// A helper method for getting the preferred drop effect.
DWORD GetPreferredDropEffect(DWORD effect) {
  if (effect & DROPEFFECT_COPY)
    return DROPEFFECT_COPY;
  if (effect & DROPEFFECT_LINK)
    return DROPEFFECT_LINK;
  if (effect & DROPEFFECT_MOVE)
    return DROPEFFECT_MOVE;
  return DROPEFFECT_NONE;
}

}  // namespace

// InterstitialDropTarget is like a ui::DropTarget implementation that
// WebDropTarget passes through to if an interstitial is showing.  Rather than
// passing messages on to the renderer, we just check to see if there's a link
// in the drop data and handle links as navigations.
class InterstitialDropTarget {
 public:
  explicit InterstitialDropTarget(WebContents* web_contents)
      : web_contents_(web_contents) {}

  DWORD OnDragEnter(IDataObject* data_object, DWORD effect) {
    return ui::ClipboardUtil::HasUrl(data_object) ?
        GetPreferredDropEffect(effect) : DROPEFFECT_NONE;
  }

  DWORD OnDragOver(IDataObject* data_object, DWORD effect) {
    return ui::ClipboardUtil::HasUrl(data_object) ?
        GetPreferredDropEffect(effect) : DROPEFFECT_NONE;
  }

  void OnDragLeave(IDataObject* data_object) {
  }

  DWORD OnDrop(IDataObject* data_object, DWORD effect) {
    if (!ui::ClipboardUtil::HasUrl(data_object))
      return DROPEFFECT_NONE;

    std::wstring url;
    std::wstring title;
    ui::ClipboardUtil::GetUrl(data_object, &url, &title, true);
    OpenURLParams params(
        GURL(url), Referrer(), CURRENT_TAB,
        content::PAGE_TRANSITION_AUTO_BOOKMARK, false);
    web_contents_->OpenURL(params);
    return GetPreferredDropEffect(effect);
  }

 private:
  WebContents* web_contents_;

  DISALLOW_COPY_AND_ASSIGN(InterstitialDropTarget);
};

WebDropTarget::WebDropTarget(HWND source_hwnd, WebContents* web_contents)
    : ui::DropTarget(source_hwnd),
      web_contents_(web_contents),
      tab_(NULL),
      current_rvh_(NULL),
      drag_cursor_(WebDragOperationNone),
      interstitial_drop_target_(new InterstitialDropTarget(web_contents)) {
}

WebDropTarget::~WebDropTarget() {
}

DWORD WebDropTarget::OnDragEnter(IDataObject* data_object,
                                 DWORD key_state,
                                 POINT cursor_position,
                                 DWORD effects) {
  current_rvh_ = web_contents_->GetRenderViewHost();

  if (!tab_)
    tab_ = TabContentsWrapper::GetCurrentWrapperForContents(web_contents_);

  // Don't pass messages to the renderer if an interstitial page is showing
  // because we don't want the interstitial page to navigate.  Instead,
  // pass the messages on to a separate interstitial DropTarget handler.
  if (web_contents_->ShowingInterstitialPage())
    return interstitial_drop_target_->OnDragEnter(data_object, effects);

  // TODO(tc): PopulateWebDropData can be slow depending on what is in the
  // IDataObject.  Maybe we can do this in a background thread.
  WebDropData drop_data;
  WebDropData::PopulateWebDropData(data_object, &drop_data);

  if (drop_data.url.is_empty())
    ui::OSExchangeDataProviderWin::GetPlainTextURL(data_object, &drop_data.url);

  drag_cursor_ = WebDragOperationNone;

  POINT client_pt = cursor_position;
  ScreenToClient(GetHWND(), &client_pt);
  web_contents_->GetRenderViewHost()->DragTargetDragEnter(drop_data,
      gfx::Point(client_pt.x, client_pt.y),
      gfx::Point(cursor_position.x, cursor_position.y),
      web_drag_utils_win::WinDragOpMaskToWebDragOpMask(effects));

  // This is non-null if web_contents_ is showing an ExtensionWebUI with
  // support for (at the moment experimental) drag and drop extensions.
  if (tab_ && tab_->bookmark_tab_helper()->GetBookmarkDragDelegate()) {
    ui::OSExchangeData os_exchange_data(
        new ui::OSExchangeDataProviderWin(data_object));
    BookmarkNodeData bookmark_drag_data;
    if (bookmark_drag_data.Read(os_exchange_data))
      tab_->bookmark_tab_helper()->GetBookmarkDragDelegate()->OnDragEnter(
          bookmark_drag_data);
  }

  // We lie here and always return a DROPEFFECT because we don't want to
  // wait for the IPC call to return.
  return web_drag_utils_win::WebDragOpToWinDragOp(drag_cursor_);
}

DWORD WebDropTarget::OnDragOver(IDataObject* data_object,
                                DWORD key_state,
                                POINT cursor_position,
                                DWORD effects) {
  DCHECK(current_rvh_);
  if (current_rvh_ != web_contents_->GetRenderViewHost())
    OnDragEnter(data_object, key_state, cursor_position, effects);

  if (web_contents_->ShowingInterstitialPage())
    return interstitial_drop_target_->OnDragOver(data_object, effects);

  POINT client_pt = cursor_position;
  ScreenToClient(GetHWND(), &client_pt);
  web_contents_->GetRenderViewHost()->DragTargetDragOver(
      gfx::Point(client_pt.x, client_pt.y),
      gfx::Point(cursor_position.x, cursor_position.y),
      web_drag_utils_win::WinDragOpMaskToWebDragOpMask(effects));

  if (tab_ && tab_->bookmark_tab_helper()->GetBookmarkDragDelegate()) {
    ui::OSExchangeData os_exchange_data(
        new ui::OSExchangeDataProviderWin(data_object));
    BookmarkNodeData bookmark_drag_data;
    if (bookmark_drag_data.Read(os_exchange_data))
      tab_->bookmark_tab_helper()->GetBookmarkDragDelegate()->OnDragOver(
          bookmark_drag_data);
  }

  return web_drag_utils_win::WebDragOpToWinDragOp(drag_cursor_);
}

void WebDropTarget::OnDragLeave(IDataObject* data_object) {
  DCHECK(current_rvh_);
  if (current_rvh_ != web_contents_->GetRenderViewHost())
    return;

  if (web_contents_->ShowingInterstitialPage()) {
    interstitial_drop_target_->OnDragLeave(data_object);
  } else {
    web_contents_->GetRenderViewHost()->DragTargetDragLeave();
  }

  if (tab_ && tab_->bookmark_tab_helper()->GetBookmarkDragDelegate()) {
    ui::OSExchangeData os_exchange_data(
        new ui::OSExchangeDataProviderWin(data_object));
    BookmarkNodeData bookmark_drag_data;
    if (bookmark_drag_data.Read(os_exchange_data))
      tab_->bookmark_tab_helper()->GetBookmarkDragDelegate()->OnDragLeave(
          bookmark_drag_data);
  }
}

DWORD WebDropTarget::OnDrop(IDataObject* data_object,
                            DWORD key_state,
                            POINT cursor_position,
                            DWORD effect) {
  DCHECK(current_rvh_);
  if (current_rvh_ != web_contents_->GetRenderViewHost())
    OnDragEnter(data_object, key_state, cursor_position, effect);

  if (web_contents_->ShowingInterstitialPage())
    interstitial_drop_target_->OnDragOver(data_object, effect);

  if (web_contents_->ShowingInterstitialPage())
    return interstitial_drop_target_->OnDrop(data_object, effect);

  POINT client_pt = cursor_position;
  ScreenToClient(GetHWND(), &client_pt);
  web_contents_->GetRenderViewHost()->DragTargetDrop(
      gfx::Point(client_pt.x, client_pt.y),
      gfx::Point(cursor_position.x, cursor_position.y));

  if (tab_ && tab_->bookmark_tab_helper()->GetBookmarkDragDelegate()) {
    ui::OSExchangeData os_exchange_data(
        new ui::OSExchangeDataProviderWin(data_object));
    BookmarkNodeData bookmark_drag_data;
    if (bookmark_drag_data.Read(os_exchange_data))
      tab_->bookmark_tab_helper()->GetBookmarkDragDelegate()->OnDrop(
          bookmark_drag_data);
  }

  current_rvh_ = NULL;

  // Focus the target browser.
  Browser* browser = Browser::GetBrowserForController(
      &web_contents_->GetController(), NULL);
  if (browser)
    browser->window()->Show();

  // This isn't always correct, but at least it's a close approximation.
  // For now, we always map a move to a copy to prevent potential data loss.
  DWORD drop_effect = web_drag_utils_win::WebDragOpToWinDragOp(drag_cursor_);
  return drop_effect != DROPEFFECT_MOVE ? drop_effect : DROPEFFECT_COPY;
}
