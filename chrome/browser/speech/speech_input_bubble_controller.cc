// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/speech/speech_input_bubble_controller.h"

#include "base/bind.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/rect.h"

using content::BrowserThread;
using content::WebContents;

namespace speech_input {

SpeechInputBubbleController::SpeechInputBubbleController(Delegate* delegate)
    : delegate_(delegate),
      current_bubble_caller_id_(0),
      registrar_(new content::NotificationRegistrar) {
}

SpeechInputBubbleController::~SpeechInputBubbleController() {
  DCHECK(bubbles_.empty());
}

void SpeechInputBubbleController::CreateBubble(int caller_id,
                                               int render_process_id,
                                               int render_view_id,
                                               const gfx::Rect& element_rect) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&SpeechInputBubbleController::CreateBubble, this,
                          caller_id, render_process_id, render_view_id,
                          element_rect));
    return;
  }
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  WebContents* web_contents = tab_util::GetWebContentsByID(render_process_id,
                                                           render_view_id);

  DCHECK_EQ(0u, bubbles_.count(caller_id));
  SpeechInputBubble* bubble = SpeechInputBubble::Create(web_contents, this,
                                                        element_rect);
  if (!bubble) {
    // Could be null if tab or display rect were invalid.
    // Simulate the cancel button being clicked to inform the delegate.
    BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE,
          base::Bind(
              &SpeechInputBubbleController::InvokeDelegateButtonClicked,
              this, caller_id, SpeechInputBubble::BUTTON_CANCEL));
    return;
  }

  bubbles_[caller_id] = bubble;

  UpdateTabContentsSubscription(caller_id, BUBBLE_ADDED);
}

void SpeechInputBubbleController::CloseBubble(int caller_id) {
  ProcessRequestInUiThread(caller_id, REQUEST_CLOSE, string16(), 0, 0);
}

void SpeechInputBubbleController::SetBubbleWarmUpMode(int caller_id) {
  ProcessRequestInUiThread(caller_id, REQUEST_SET_WARM_UP_MODE,
                           string16(), 0, 0);
}

void SpeechInputBubbleController::SetBubbleRecordingMode(int caller_id) {
  ProcessRequestInUiThread(caller_id, REQUEST_SET_RECORDING_MODE,
                           string16(), 0, 0);
}

void SpeechInputBubbleController::SetBubbleRecognizingMode(int caller_id) {
  ProcessRequestInUiThread(caller_id, REQUEST_SET_RECOGNIZING_MODE,
                           string16(), 0, 0);
}

void SpeechInputBubbleController::SetBubbleInputVolume(int caller_id,
                                                       float volume,
                                                       float noise_volume) {
  ProcessRequestInUiThread(caller_id, REQUEST_SET_INPUT_VOLUME, string16(),
                           volume, noise_volume);
}

void SpeechInputBubbleController::SetBubbleMessage(int caller_id,
                                                   const string16& text) {
  ProcessRequestInUiThread(caller_id, REQUEST_SET_MESSAGE, text, 0, 0);
}

void SpeechInputBubbleController::UpdateTabContentsSubscription(
    int caller_id, ManageSubscriptionAction action) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // If there are any other bubbles existing for the same TabContents, we would
  // have subscribed to tab close notifications on their behalf and we need to
  // stay registered. So we don't change the subscription in such cases.
  WebContents* web_contents = bubbles_[caller_id]->web_contents();
  for (BubbleCallerIdMap::iterator iter = bubbles_.begin();
       iter != bubbles_.end(); ++iter) {
    if (iter->second->web_contents() == web_contents &&
        iter->first != caller_id) {
      // At least one other bubble exists for the same TabContents. So don't
      // make any change to the subscription.
      return;
    }
  }

  if (action == BUBBLE_ADDED) {
    registrar_->Add(this, content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                    content::Source<WebContents>(web_contents));
  } else {
    registrar_->Remove(this, content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                    content::Source<WebContents>(web_contents));
  }
}

void SpeechInputBubbleController::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == content::NOTIFICATION_WEB_CONTENTS_DESTROYED) {
    // Cancel all bubbles and active recognition sessions for this tab.
    WebContents* web_contents = content::Source<WebContents>(source).ptr();
    BubbleCallerIdMap::iterator iter = bubbles_.begin();
    while (iter != bubbles_.end()) {
      if (iter->second->web_contents() == web_contents) {
        BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
            base::Bind(
                &SpeechInputBubbleController::InvokeDelegateButtonClicked,
                this, iter->first, SpeechInputBubble::BUTTON_CANCEL));
        CloseBubble(iter->first);
        // We expect to have a very small number of items in this map so
        // redo-ing from start is ok.
        iter = bubbles_.begin();
      } else {
        ++iter;
      }
    }
  } else {
    NOTREACHED() << "Unknown notification";
  }
}

void SpeechInputBubbleController::ProcessRequestInUiThread(
    int caller_id, RequestType type, const string16& text, float volume,
    float noise_volume) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, base::Bind(
        &SpeechInputBubbleController::ProcessRequestInUiThread, this,
        caller_id, type, text, volume, noise_volume));
    return;
  }
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // The bubble may have been closed before we got a chance to process this
  // request. So check before proceeding.
  if (!bubbles_.count(caller_id))
    return;

  bool change_active_bubble = (type == REQUEST_SET_WARM_UP_MODE ||
                               type == REQUEST_SET_MESSAGE);
  if (change_active_bubble) {
    if (current_bubble_caller_id_ && current_bubble_caller_id_ != caller_id)
      bubbles_[current_bubble_caller_id_]->Hide();
    current_bubble_caller_id_ = caller_id;
  }

  SpeechInputBubble* bubble = bubbles_[caller_id];
  switch (type) {
    case REQUEST_SET_WARM_UP_MODE:
      bubble->SetWarmUpMode();
      break;
    case REQUEST_SET_RECORDING_MODE:
      bubble->SetRecordingMode();
      break;
    case REQUEST_SET_RECOGNIZING_MODE:
      bubble->SetRecognizingMode();
      break;
    case REQUEST_SET_MESSAGE:
      bubble->SetMessage(text);
      break;
    case REQUEST_SET_INPUT_VOLUME:
      bubble->SetInputVolume(volume, noise_volume);
      break;
    case REQUEST_CLOSE:
      if (current_bubble_caller_id_ == caller_id)
        current_bubble_caller_id_ = 0;
      UpdateTabContentsSubscription(caller_id, BUBBLE_REMOVED);
      delete bubble;
      bubbles_.erase(caller_id);
      break;
    default:
      NOTREACHED();
      break;
  }

  if (change_active_bubble)
    bubble->Show();
}

void SpeechInputBubbleController::InfoBubbleButtonClicked(
    SpeechInputBubble::Button button) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(current_bubble_caller_id_);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(
          &SpeechInputBubbleController::InvokeDelegateButtonClicked,
          this, current_bubble_caller_id_, button));
}

void SpeechInputBubbleController::InfoBubbleFocusChanged() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(current_bubble_caller_id_);

  int old_bubble_caller_id = current_bubble_caller_id_;
  current_bubble_caller_id_ = 0;

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(
          &SpeechInputBubbleController::InvokeDelegateFocusChanged,
          this, old_bubble_caller_id));
}

void SpeechInputBubbleController::InvokeDelegateButtonClicked(
    int caller_id, SpeechInputBubble::Button button) {
  delegate_->InfoBubbleButtonClicked(caller_id, button);
}

void SpeechInputBubbleController::InvokeDelegateFocusChanged(int caller_id) {
  delegate_->InfoBubbleFocusChanged(caller_id);
}

}  // namespace speech_input
