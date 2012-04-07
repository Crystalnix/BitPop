// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RESOURCE_QUEUE_H_
#define CONTENT_BROWSER_RENDERER_HOST_RESOURCE_QUEUE_H_
#pragma once

#include <map>
#include <set>

#include "base/basictypes.h"
#include "content/common/content_export.h"

namespace content {
struct GlobalRequestID;
}

namespace net {
class URLRequest;
}  // namespace net

class ResourceDispatcherHostRequestInfo;
class ResourceQueue;

// Makes decisions about delaying or not each net::URLRequest in the queue.
// All methods are called on the IO thread.
class CONTENT_EXPORT ResourceQueueDelegate {
 public:
  // Gives the delegate a pointer to the queue object.
  virtual void Initialize(ResourceQueue* resource_queue) = 0;

  // Should return true if it wants the |request| to not be started at this
  // point. Use ResourceQueue::StartDelayedRequests to restart requests.
  virtual bool ShouldDelayRequest(
      net::URLRequest* request,
      const ResourceDispatcherHostRequestInfo& request_info,
      const content::GlobalRequestID& request_id) = 0;

  // Called just before ResourceQueue shutdown. After that, the delegate
  // should not use the ResourceQueue.
  virtual void WillShutdownResourceQueue() = 0;

 protected:
  virtual ~ResourceQueueDelegate();
};

// Makes it easy to delay starting URL requests until specified conditions are
// met.
class CONTENT_EXPORT ResourceQueue {
 public:
  typedef std::set<ResourceQueueDelegate*> DelegateSet;

  // UI THREAD ONLY ------------------------------------------------------------

  // Construct the queue. You must initialize it using Initialize.
  ResourceQueue();
  ~ResourceQueue();

  // Initialize the queue with set of delegates it should ask for each incoming
  // request.
  void Initialize(const DelegateSet& delegates);

  // IO THREAD ONLY ------------------------------------------------------------

  // Must be called before destroying the queue. No other methods can be called
  // after that.
  void Shutdown();

  // Takes care to start the |request| after all delegates allow that. If no
  // delegate demands delaying the request it will be started immediately.
  void AddRequest(net::URLRequest* request,
                  const ResourceDispatcherHostRequestInfo& request_info);

  // Tells the queue that the net::URLRequest object associated with
  // |request_id| is no longer valid.
  void RemoveRequest(const content::GlobalRequestID& request_id);

  // A delegate should call StartDelayedRequests when it wants to allow all
  // its delayed requests to start. If it was the last delegate that required
  // a request to be delayed, that request will be started.
  void StartDelayedRequests(ResourceQueueDelegate* delegate);

 private:
  typedef std::map<content::GlobalRequestID, net::URLRequest*> RequestMap;
  typedef std::map<content::GlobalRequestID, DelegateSet>
      InterestedDelegatesMap;

  // The registered delegates. Will not change after the queue has been
  // initialized.
  DelegateSet delegates_;

  // Stores net::URLRequest objects associated with each GlobalRequestID. This
  // helps decoupling the queue from ResourceDispatcherHost.
  RequestMap requests_;

  // Maps a GlobalRequestID to the set of delegates that want to prevent the
  // associated request from starting yet.
  InterestedDelegatesMap interested_delegates_;

  // True when we are shutting down.
  bool shutdown_;

  DISALLOW_COPY_AND_ASSIGN(ResourceQueue);
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_RESOURCE_QUEUE_H_
