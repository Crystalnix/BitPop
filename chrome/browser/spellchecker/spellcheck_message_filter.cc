// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/spellchecker/spellcheck_message_filter.h"

#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/browser/spellchecker/spellcheck_host.h"
#include "chrome/browser/spellchecker/spellcheck_host_metrics.h"
#include "chrome/common/spellcheck_messages.h"
#include "content/public/browser/render_process_host.h"

using content::BrowserThread;

SpellCheckMessageFilter::SpellCheckMessageFilter(int render_process_id)
    : render_process_id_(render_process_id) {
}

SpellCheckMessageFilter::~SpellCheckMessageFilter() {
}

void SpellCheckMessageFilter::OverrideThreadForMessage(
    const IPC::Message& message, BrowserThread::ID* thread) {
  if (message.type() == SpellCheckHostMsg_RequestDictionary::ID ||
      message.type() == SpellCheckHostMsg_NotifyChecked::ID)
    *thread = BrowserThread::UI;
}

bool SpellCheckMessageFilter::OnMessageReceived(const IPC::Message& message,
                                                bool* message_was_ok) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(SpellCheckMessageFilter, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(SpellCheckHostMsg_RequestDictionary,
                        OnSpellCheckerRequestDictionary)
    IPC_MESSAGE_HANDLER(SpellCheckHostMsg_NotifyChecked,
                        OnNotifyChecked)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void SpellCheckMessageFilter::OnSpellCheckerRequestDictionary() {
  content::RenderProcessHost* host =
      content::RenderProcessHost::FromID(render_process_id_);
  if (!host)
    return;  // Teardown.
  Profile* profile = Profile::FromBrowserContext(host->GetBrowserContext());
  // The renderer has requested that we initialize its spellchecker. This should
  // generally only be called once per session, as after the first call, all
  // future renderers will be passed the initialization information on startup
  // (or when the dictionary changes in some way).
  SpellCheckHost* spell_check_host =
      SpellCheckFactory::GetHostForProfile(profile);

  if (spell_check_host) {
    // The spellchecker initialization already started and finished; just send
    // it to the renderer.
    spell_check_host->InitForRenderer(host);
  } else {
    // We may have gotten multiple requests from different renderers. We don't
    // want to initialize multiple times in this case, so we set |force| to
    // false.
    SpellCheckFactory::ReinitializeSpellCheckHost(profile, false);
  }
}

void SpellCheckMessageFilter::OnNotifyChecked(const string16& word,
                                              bool misspelled) {
  content::RenderProcessHost* host =
      content::RenderProcessHost::FromID(render_process_id_);
  if (!host)
    return;  // Teardown.
  // Delegates to SpellCheckHost which tracks the stats of our spellchecker.
  Profile* profile = Profile::FromBrowserContext(host->GetBrowserContext());
  SpellCheckHost* spellcheck_host =
      SpellCheckFactory::GetHostForProfile(profile);
  if (spellcheck_host && spellcheck_host->GetMetrics())
    spellcheck_host->GetMetrics()->RecordCheckedWordStats(word, misspelled);
}
