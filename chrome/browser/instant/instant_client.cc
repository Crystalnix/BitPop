// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/instant/instant_client.h"

#include "base/utf_string_conversions.h"
#include "chrome/common/render_messages.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/font.h"

InstantClient::Delegate::~Delegate() {
}

InstantClient::InstantClient(Delegate* delegate) : delegate_(delegate) {
}

InstantClient::~InstantClient() {
}

void InstantClient::SetContents(content::WebContents* contents) {
  Observe(contents);
}

void InstantClient::Update(const string16& text,
                           size_t selection_start,
                           size_t selection_end,
                           bool verbatim) {
  Send(new ChromeViewMsg_SearchBoxChange(routing_id(), text, verbatim,
                                         selection_start, selection_end));
}

void InstantClient::Submit(const string16& text) {
  Send(new ChromeViewMsg_SearchBoxSubmit(routing_id(), text));
}

void InstantClient::Cancel(const string16& text) {
  Send(new ChromeViewMsg_SearchBoxCancel(routing_id(), text));
}

void InstantClient::SetPopupBounds(const gfx::Rect& bounds) {
  Send(new ChromeViewMsg_SearchBoxPopupResize(routing_id(), bounds));
}

void InstantClient::SetMarginSize(const int start, const int end) {
  Send(new ChromeViewMsg_SearchBoxMarginChange(routing_id(), start, end));
}

void InstantClient::DetermineIfPageSupportsInstant() {
  Send(new ChromeViewMsg_DetermineIfPageSupportsInstant(routing_id()));
}

void InstantClient::SendAutocompleteResults(
      const std::vector<InstantAutocompleteResult>& results) {
  Send(new ChromeViewMsg_SearchBoxAutocompleteResults(routing_id(), results));
}

void InstantClient::UpOrDownKeyPressed(int count) {
  Send(new ChromeViewMsg_SearchBoxUpOrDownKeyPressed(routing_id(), count));
}

void InstantClient::SearchModeChanged(const chrome::search::Mode& mode) {
  Send(new ChromeViewMsg_SearchBoxModeChanged(routing_id(), mode));
}

void InstantClient::SendThemeBackgroundInfo(
      const ThemeBackgroundInfo& theme_info) {
  Send(new ChromeViewMsg_SearchBoxThemeChanged(routing_id(), theme_info));
}

void InstantClient::SendThemeAreaHeight(int height) {
  Send(new ChromeViewMsg_SearchBoxThemeAreaHeightChanged(routing_id(), height));
}

void InstantClient::SetDisplayInstantResults(bool display_instant_results) {
  Send(new ChromeViewMsg_SearchBoxSetDisplayInstantResults(routing_id(),
               display_instant_results));
}

void InstantClient::KeyCaptureChanged(bool is_key_capture_enabled) {
  Send(new ChromeViewMsg_SearchBoxKeyCaptureChanged(routing_id(),
               is_key_capture_enabled));
}

void InstantClient::DidFinishLoad(
    int64 /* frame_id */,
    const GURL& /* validated_url */,
    bool is_main_frame,
    content::RenderViewHost* /* render_view_host */) {
  if (is_main_frame)
    DetermineIfPageSupportsInstant();
}

bool InstantClient::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(InstantClient, message)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SetSuggestions, SetSuggestions)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_InstantSupportDetermined,
                        InstantSupportDetermined)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_ShowInstantPreview,
                        ShowInstantPreview)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_StartCapturingKeyStrokes,
                        StartCapturingKeyStrokes);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_StopCapturingKeyStrokes,
                        StopCapturingKeyStrokes);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SearchBoxNavigate,
                        SearchBoxNavigate);
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void InstantClient::RenderViewGone(base::TerminationStatus status) {
  delegate_->RenderViewGone();
}

void InstantClient::DidCommitProvisionalLoadForFrame(
    int64 frame_id,
    bool is_main_frame,
    const GURL& url,
    content::PageTransition transition_type,
    content::RenderViewHost* render_view_host) {
  if (!is_main_frame)
    return;
  delegate_->AboutToNavigateMainFrame(url);
}

void InstantClient::SetSuggestions(
    int page_id,
    const std::vector<InstantSuggestion>& suggestions) {
  if (web_contents()->IsActiveEntry(page_id))
    delegate_->SetSuggestions(suggestions);
}

void InstantClient::InstantSupportDetermined(int page_id, bool result) {
  if (result) {
    // Inform the renderer process of the Omnibox's font information.
    const gfx::Font& omnibox_font =
        ui::ResourceBundle::GetSharedInstance().GetFont(
            ui::ResourceBundle::MediumFont);
    string16 omnibox_font_name = UTF8ToUTF16(omnibox_font.GetFontName());
    size_t omnibox_font_size = omnibox_font.GetFontSize();

    Send(new ChromeViewMsg_SearchBoxFontInformation(
        routing_id(), omnibox_font_name, omnibox_font_size));
  }
  if (web_contents()->IsActiveEntry(page_id))
    delegate_->InstantSupportDetermined(result);
}

void InstantClient::ShowInstantPreview(int page_id,
                                       InstantShownReason reason,
                                       int height,
                                       InstantSizeUnits units) {
  if (web_contents()->IsActiveEntry(page_id))
    delegate_->ShowInstantPreview(reason, height, units);
}

void InstantClient::StartCapturingKeyStrokes(int page_id) {
  if (web_contents()->IsActiveEntry(page_id))
    delegate_->StartCapturingKeyStrokes();
}

void InstantClient::StopCapturingKeyStrokes(int page_id) {
  if (web_contents()->IsActiveEntry(page_id))
    delegate_->StopCapturingKeyStrokes();
}

void InstantClient::SearchBoxNavigate(int page_id,
                                      const GURL& url,
                                      content::PageTransition transition) {
  if (web_contents()->IsActiveEntry(page_id))
    delegate_->NavigateToURL(url, transition);
}
