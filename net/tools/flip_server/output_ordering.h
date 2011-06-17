// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_FLIP_SERVER_OUTPUT_ORDERING_H_
#define NET_TOOLS_FLIP_SERVER_OUTPUT_ORDERING_H_

#include <list>
#include <map>
#include <string>

#include "base/basictypes.h"
#include "net/tools/flip_server/constants.h"
#include "net/tools/flip_server/epoll_server.h"
#include "net/tools/flip_server/mem_cache.h"

namespace net {

class SMConnectionInterface;

class OutputOrdering {
 public:
  typedef std::list<MemCacheIter> PriorityRing;
  typedef std::map<uint32, PriorityRing> PriorityMap;

  struct PriorityMapPointer {
    PriorityMapPointer(): ring(NULL), alarm_enabled(false) {}
    PriorityRing* ring;
    PriorityRing::iterator it;
    bool alarm_enabled;
    EpollServer::AlarmRegToken alarm_token;
  };

  typedef std::map<uint32, PriorityMapPointer> StreamIdToPriorityMap;

  StreamIdToPriorityMap stream_ids_;
  PriorityMap priority_map_;
  PriorityRing first_data_senders_;
  uint32 first_data_senders_threshold_;  // when you've passed this, you're no
                                         // longer a first_data_sender...
  SMConnectionInterface* connection_;
  EpollServer* epoll_server_;

  explicit OutputOrdering(SMConnectionInterface* connection);
  ~OutputOrdering();
  void Reset();
  bool ExistsInPriorityMaps(uint32 stream_id);

  struct BeginOutputtingAlarm : public EpollAlarmCallbackInterface {
   public:
    BeginOutputtingAlarm(OutputOrdering* oo,
                         OutputOrdering::PriorityMapPointer* pmp,
                         const MemCacheIter& mci);
    virtual ~BeginOutputtingAlarm();

    // EpollAlarmCallbackInterface:
    virtual int64 OnAlarm();
    virtual void OnRegistration(const EpollServer::AlarmRegToken& tok,
                                EpollServer* eps);
    virtual void OnUnregistration();
    virtual void OnShutdown(EpollServer* eps);

   private:
    OutputOrdering* output_ordering_;
    OutputOrdering::PriorityMapPointer* pmp_;
    MemCacheIter mci_;
    EpollServer* epoll_server_;
  };

  void MoveToActive(PriorityMapPointer* pmp, MemCacheIter mci);
  void AddToOutputOrder(const MemCacheIter& mci);
  void SpliceToPriorityRing(PriorityRing::iterator pri);
  MemCacheIter* GetIter();
  void RemoveStreamId(uint32 stream_id);

  static double server_think_time_in_s() { return server_think_time_in_s_; }
  static void set_server_think_time_in_s(double value) {
    server_think_time_in_s_ = value;
  }

 private:
  static double server_think_time_in_s_;
};

}  // namespace net

#endif  // NET_TOOLS_FLIP_SERVER_OUTPUT_ORDERING_H_

