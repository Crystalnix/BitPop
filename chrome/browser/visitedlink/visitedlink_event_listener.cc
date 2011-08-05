// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/visitedlink/visitedlink_event_listener.h"

#include "base/shared_memory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/render_messages.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/browser/renderer_host/render_widget_host.h"
#include "content/common/notification_service.h"

using base::Time;
using base::TimeDelta;

namespace {

// The amount of time we wait to accumulate visited link additions.
static const int kCommitIntervalMs = 100;

// Size of the buffer after which individual link updates deemed not warranted
// and the overall update should be used instead.
static const unsigned kVisitedLinkBufferThreshold = 50;

}  // namespace

// This class manages buffering and sending visited link hashes (fingerprints)
// to renderer based on widget visibility.
// As opposed to the VisitedLinkEventListener, which coalesces to
// reduce the rate of messages being sent to render processes, this class
// ensures that the updates occur only when explicitly requested. This is
// used for BrowserRenderProcessHost to only send Add/Reset link events to the
// renderers when their tabs are visible and the corresponding RenderViews are
// created.
class VisitedLinkUpdater {
 public:
  explicit VisitedLinkUpdater(int render_process_id)
      : reset_needed_(false), render_process_id_(render_process_id) {
  }

  // Informs the renderer about a new visited link table.
  void SendVisitedLinkTable(base::SharedMemory* table_memory) {
    RenderProcessHost* process = RenderProcessHost::FromID(render_process_id_);
    if (!process)
      return;  // Happens in tests
    base::SharedMemoryHandle handle_for_process;
    table_memory->ShareToProcess(process->GetHandle(), &handle_for_process);
    if (base::SharedMemory::IsHandleValid(handle_for_process))
      process->Send(new ViewMsg_VisitedLink_NewTable(handle_for_process));
  }

  // Buffers |links| to update, but doesn't actually relay them.
  void AddLinks(const VisitedLinkCommon::Fingerprints& links) {
    if (reset_needed_)
      return;

    if (pending_.size() + links.size() > kVisitedLinkBufferThreshold) {
      // Once the threshold is reached, there's no need to store pending visited
      // link updates -- we opt for resetting the state for all links.
      AddReset();
      return;
    }

    pending_.insert(pending_.end(), links.begin(), links.end());
  }

  // Tells the updater that sending individual link updates is no longer
  // necessary and the visited state for all links should be reset.
  void AddReset() {
    reset_needed_ = true;
    pending_.clear();
  }

  // Sends visited link update messages: a list of links whose visited state
  // changed or reset of visited state for all links.
  void Update() {
    RenderProcessHost* process = RenderProcessHost::FromID(render_process_id_);
    if (!process)
      return;  // Happens in tests

    if (!process->VisibleWidgetCount())
      return;

    if (reset_needed_) {
      process->Send(new ViewMsg_VisitedLink_Reset());
      reset_needed_ = false;
      return;
    }

    if (pending_.empty())
      return;

    process->Send(new ViewMsg_VisitedLink_Add(pending_));

    pending_.clear();
  }

 private:
  bool reset_needed_;
  int render_process_id_;
  VisitedLinkCommon::Fingerprints pending_;
};

VisitedLinkEventListener::VisitedLinkEventListener() {
  registrar_.Add(this, NotificationType::RENDERER_PROCESS_CREATED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::RENDERER_PROCESS_TERMINATED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::RENDER_WIDGET_VISIBILITY_CHANGED,
                 NotificationService::AllSources());
}

VisitedLinkEventListener::~VisitedLinkEventListener() {
}

void VisitedLinkEventListener::NewTable(base::SharedMemory* table_memory) {
  if (!table_memory)
    return;

  // Send to all RenderProcessHosts.
  for (Updaters::iterator i = updaters_.begin(); i != updaters_.end(); ++i) {
    // Make sure to not send to incognito renderers.
    RenderProcessHost* process = RenderProcessHost::FromID(i->first);
    VisitedLinkMaster* master = process->profile()->GetVisitedLinkMaster();
    if (master && master->shared_memory() == table_memory)
      i->second->SendVisitedLinkTable(table_memory);
  }
}

void VisitedLinkEventListener::Add(VisitedLinkMaster::Fingerprint fingerprint) {
  pending_visited_links_.push_back(fingerprint);

  if (!coalesce_timer_.IsRunning()) {
    coalesce_timer_.Start(
        TimeDelta::FromMilliseconds(kCommitIntervalMs), this,
        &VisitedLinkEventListener::CommitVisitedLinks);
  }
}

void VisitedLinkEventListener::Reset() {
  pending_visited_links_.clear();
  coalesce_timer_.Stop();

  for (Updaters::iterator i = updaters_.begin(); i != updaters_.end(); ++i) {
    i->second->AddReset();
    i->second->Update();
  }
}

void VisitedLinkEventListener::CommitVisitedLinks() {
  // Send to all RenderProcessHosts.
  for (Updaters::iterator i = updaters_.begin(); i != updaters_.end(); ++i) {
    i->second->AddLinks(pending_visited_links_);
    i->second->Update();
  }

  pending_visited_links_.clear();
}

void VisitedLinkEventListener::Observe(NotificationType type,
                                       const NotificationSource& source,
                                       const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::RENDERER_PROCESS_CREATED: {
      RenderProcessHost* process = Source<RenderProcessHost>(source).ptr();
      updaters_[process->id()] =
          make_linked_ptr(new VisitedLinkUpdater(process->id()));

      // Initialize support for visited links. Send the renderer process its
      // initial set of visited links.
      VisitedLinkMaster* master = process->profile()->GetVisitedLinkMaster();
      if (!master)
        return;

      updaters_[process->id()]->SendVisitedLinkTable(master->shared_memory());
      break;
    }
    case NotificationType::RENDERER_PROCESS_TERMINATED: {
      RenderProcessHost* process = Source<RenderProcessHost>(source).ptr();
      if (updaters_.count(process->id())) {
        updaters_.erase(process->id());
      }
      break;
    }
    case NotificationType::RENDER_WIDGET_VISIBILITY_CHANGED: {
      RenderWidgetHost* widget = Source<RenderWidgetHost>(source).ptr();
      int child_id = widget->process()->id();
      if (updaters_.count(child_id))
        updaters_[child_id]->Update();
      break;
    }
    default:
      NOTREACHED();
      break;
  }
}
