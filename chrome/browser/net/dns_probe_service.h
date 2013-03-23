// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NET_DNS_PROBE_SERVICE_H_
#define CHROME_BROWSER_NET_DNS_PROBE_SERVICE_H_

#include <vector>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/time.h"
#include "chrome/browser/net/dns_probe_job.h"
#include "net/base/network_change_notifier.h"

namespace net {
struct DnsConfig;
}

namespace chrome_browser_net {

class DnsProbeService : public net::NetworkChangeNotifier::IPAddressObserver {
 public:
  enum Result {
    PROBE_UNKNOWN,
    PROBE_NO_INTERNET,
    PROBE_BAD_CONFIG,
    PROBE_NXDOMAIN,
    MAX_RESULT
  };
  typedef base::Callback<void(Result result)> CallbackType;

  DnsProbeService();
  virtual ~DnsProbeService();

  void ProbeDns(const CallbackType& callback);

  // NetworkChangeNotifier::IPAddressObserver implementation:
  virtual void OnIPAddressChanged() OVERRIDE;

 protected:
  // This can be called by tests to pretend the cached reuslt has expired.
  void ExpireResults();

 private:
  enum State {
    STATE_NO_RESULTS,
    STATE_PROBE_RUNNING,
    STATE_RESULTS_CACHED,
  };

  void StartProbes();
  void OnProbesComplete();
  void CallCallbacks();

  void OnProbeJobComplete(DnsProbeJob* job, DnsProbeJob::Result result);
  Result EvaluateResults() const;
  void HistogramProbes() const;

  // These are expected to be overridden by tests to return mock jobs.
  virtual scoped_ptr<DnsProbeJob> CreateSystemProbeJob(
      const DnsProbeJob::CallbackType& job_callback);
  virtual scoped_ptr<DnsProbeJob> CreatePublicProbeJob(
      const DnsProbeJob::CallbackType& job_callback);

  scoped_ptr<DnsProbeJob> CreateProbeJob(
      const net::DnsConfig& dns_config,
      const DnsProbeJob::CallbackType& job_callback);
  void GetSystemDnsConfig(net::DnsConfig* config);
  void GetPublicDnsConfig(net::DnsConfig* config);
  bool ResultsExpired();

  scoped_ptr<DnsProbeJob> system_job_;
  scoped_ptr<DnsProbeJob> public_job_;
  DnsProbeJob::Result system_result_;
  DnsProbeJob::Result public_result_;
  std::vector<CallbackType> callbacks_;
  State state_;
  Result result_;
  base::Time probe_start_time_;

  DISALLOW_COPY_AND_ASSIGN(DnsProbeService);
};

}  // namespace chrome_browser_net

#endif  // CHROME_BROWSER_NET_DNS_PROBE_SERVICE_H_
