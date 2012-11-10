// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#include "content/browser/host_zoom_map_impl.h"

#include "base/string_piece.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/resource_context.h"
#include "content/public/common/page_zoom.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_util.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"

using WebKit::WebView;
using content::BrowserThread;
using content::RenderProcessHost;
using content::RenderViewHost;

static const char* kHostZoomMapKeyName = "content_host_zoom_map";

namespace content {

HostZoomMap* HostZoomMap::GetForBrowserContext(BrowserContext* context) {
  HostZoomMapImpl* rv = static_cast<HostZoomMapImpl*>(
      context->GetUserData(kHostZoomMapKeyName));
  if (!rv) {
    rv = new HostZoomMapImpl();
    context->SetUserData(kHostZoomMapKeyName, rv);
  }
  return rv;
}

}  // namespace content

HostZoomMapImpl::HostZoomMapImpl()
    : default_zoom_level_(0.0) {
  registrar_.Add(
      this, content::NOTIFICATION_RENDER_VIEW_HOST_WILL_CLOSE_RENDER_VIEW,
      content::NotificationService::AllSources());
}

void HostZoomMapImpl::CopyFrom(HostZoomMap* copy_interface) {
  // This can only be called on the UI thread to avoid deadlocks, otherwise
  //   UI: a.CopyFrom(b);
  //   IO: b.CopyFrom(a);
  // can deadlock.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  HostZoomMapImpl* copy = static_cast<HostZoomMapImpl*>(copy_interface);
  base::AutoLock auto_lock(lock_);
  base::AutoLock copy_auto_lock(copy->lock_);
  for (HostZoomLevels::const_iterator i(copy->host_zoom_levels_.begin());
       i != copy->host_zoom_levels_.end(); ++i) {
    host_zoom_levels_[i->first] = i->second;
  }
}

double HostZoomMapImpl::GetZoomLevel(const std::string& host) const {
  base::AutoLock auto_lock(lock_);
  HostZoomLevels::const_iterator i(host_zoom_levels_.find(host));
  return (i == host_zoom_levels_.end()) ? default_zoom_level_ : i->second;
}

void HostZoomMapImpl::SetZoomLevel(const std::string& host, double level) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  {
    base::AutoLock auto_lock(lock_);

    if (content::ZoomValuesEqual(level, default_zoom_level_))
      host_zoom_levels_.erase(host);
    else
      host_zoom_levels_[host] = level;
  }

  // Notify renderers from this browser context.
  for (RenderProcessHost::iterator i(RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    RenderProcessHost* render_process_host = i.GetCurrentValue();
    if (HostZoomMap::GetForBrowserContext(
            render_process_host->GetBrowserContext()) == this) {
      render_process_host->Send(
          new ViewMsg_SetZoomLevelForCurrentURL(host, level));
    }
  }

  content::NotificationService::current()->Notify(
      content::NOTIFICATION_ZOOM_LEVEL_CHANGED,
      content::Source<HostZoomMap>(this),
      content::Details<const std::string>(&host));
}

double HostZoomMapImpl::GetDefaultZoomLevel() const {
  return default_zoom_level_;
}

void HostZoomMapImpl::SetDefaultZoomLevel(double level) {
  default_zoom_level_ = level;
}

double HostZoomMapImpl::GetTemporaryZoomLevel(int render_process_id,
                                              int render_view_id) const {
  base::AutoLock auto_lock(lock_);
  for (size_t i = 0; i < temporary_zoom_levels_.size(); ++i) {
    if (temporary_zoom_levels_[i].render_process_id == render_process_id &&
        temporary_zoom_levels_[i].render_view_id == render_view_id) {
      return temporary_zoom_levels_[i].zoom_level;
    }
  }
  return 0;
}

void HostZoomMapImpl::SetTemporaryZoomLevel(int render_process_id,
                                            int render_view_id,
                                            double level) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  {
    base::AutoLock auto_lock(lock_);
    size_t i;
    for (i = 0; i < temporary_zoom_levels_.size(); ++i) {
      if (temporary_zoom_levels_[i].render_process_id == render_process_id &&
          temporary_zoom_levels_[i].render_view_id == render_view_id) {
        if (level) {
          temporary_zoom_levels_[i].zoom_level = level;
        } else {
          temporary_zoom_levels_.erase(temporary_zoom_levels_.begin() + i);
        }
        break;
      }
    }

    if (level && i == temporary_zoom_levels_.size()) {
      TemporaryZoomLevel temp;
      temp.render_process_id = render_process_id;
      temp.render_view_id = render_view_id;
      temp.zoom_level = level;
      temporary_zoom_levels_.push_back(temp);
    }
  }

  std::string host;
  content::NotificationService::current()->Notify(
      content::NOTIFICATION_ZOOM_LEVEL_CHANGED,
      content::Source<HostZoomMap>(this),
      content::Details<const std::string>(&host));
}

void HostZoomMapImpl::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_RENDER_VIEW_HOST_WILL_CLOSE_RENDER_VIEW: {
      base::AutoLock auto_lock(lock_);
      int render_view_id =
          content::Source<RenderViewHost>(source)->GetRoutingID();
      int render_process_id =
          content::Source<RenderViewHost>(source)->GetProcess()->GetID();

      for (size_t i = 0; i < temporary_zoom_levels_.size(); ++i) {
        if (temporary_zoom_levels_[i].render_process_id == render_process_id &&
            temporary_zoom_levels_[i].render_view_id == render_view_id) {
          temporary_zoom_levels_.erase(temporary_zoom_levels_.begin() + i);
          break;
        }
      }
      break;
    }
    default:
      NOTREACHED() << "Unexpected preference observed.";
  }
}

HostZoomMapImpl::~HostZoomMapImpl() {
}
