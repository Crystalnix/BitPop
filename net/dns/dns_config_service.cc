// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_config_service.h"

#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/values.h"
#include "net/base/ip_endpoint.h"

namespace net {

// Default values are taken from glibc resolv.h.
DnsConfig::DnsConfig()
    : append_to_multi_label_name(true),
      ndots(1),
      timeout(base::TimeDelta::FromSeconds(5)),
      attempts(2),
      rotate(false),
      edns0(false) {}

DnsConfig::~DnsConfig() {}

bool DnsConfig::Equals(const DnsConfig& d) const {
  return EqualsIgnoreHosts(d) && (hosts == d.hosts);
}

bool DnsConfig::EqualsIgnoreHosts(const DnsConfig& d) const {
  return (nameservers == d.nameservers) &&
         (search == d.search) &&
         (append_to_multi_label_name == d.append_to_multi_label_name) &&
         (ndots == d.ndots) &&
         (timeout == d.timeout) &&
         (attempts == d.attempts) &&
         (rotate == d.rotate) &&
         (edns0 == d.edns0);
}

void DnsConfig::CopyIgnoreHosts(const DnsConfig& d) {
  nameservers = d.nameservers;
  search = d.search;
  append_to_multi_label_name = d.append_to_multi_label_name;
  ndots = d.ndots;
  timeout = d.timeout;
  attempts = d.attempts;
  rotate = d.rotate;
  edns0 = d.edns0;
}

base::Value* DnsConfig::ToValue() const {
  DictionaryValue* dict = new DictionaryValue();

  ListValue* list = new ListValue();
  for (size_t i = 0; i < nameservers.size(); ++i)
    list->Append(Value::CreateStringValue(nameservers[i].ToString()));
  dict->Set("nameservers", list);

  list = new ListValue();
  for (size_t i = 0; i < search.size(); ++i)
    list->Append(Value::CreateStringValue(search[i]));
  dict->Set("search", list);

  dict->SetBoolean("append_to_multi_label_name", append_to_multi_label_name);
  dict->SetInteger("ndots", ndots);
  dict->SetDouble("timeout", timeout.InSecondsF());
  dict->SetInteger("attempts", attempts);
  dict->SetBoolean("rotate", rotate);
  dict->SetBoolean("edns0", edns0);
  dict->SetInteger("num_hosts", hosts.size());

  return dict;
}


DnsConfigService::DnsConfigService()
    : have_config_(false),
      have_hosts_(false),
      need_update_(false),
      last_sent_empty_(true) {}

DnsConfigService::~DnsConfigService() {
  // Must always clean up.
  NetworkChangeNotifier::RemoveDNSObserver(this);
}

void DnsConfigService::Read(const CallbackType& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(!callback.is_null());
  DCHECK(callback_.is_null());
  callback_ = callback;
  OnDNSChanged(NetworkChangeNotifier::CHANGE_DNS_WATCH_STARTED);
}

void DnsConfigService::Watch(const CallbackType& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(!callback.is_null());
  DCHECK(callback_.is_null());
  NetworkChangeNotifier::AddDNSObserver(this);
  callback_ = callback;
  if (NetworkChangeNotifier::IsWatchingDNS())
    OnDNSChanged(NetworkChangeNotifier::CHANGE_DNS_WATCH_STARTED);
  // else: Wait until signal before reading.
}

void DnsConfigService::InvalidateConfig() {
  DCHECK(CalledOnValidThread());
  base::TimeTicks now = base::TimeTicks::Now();
  if (!last_invalidate_config_time_.is_null()) {
    UMA_HISTOGRAM_LONG_TIMES("AsyncDNS.ConfigNotifyInterval",
                             now - last_invalidate_config_time_);
  }
  last_invalidate_config_time_ = now;
  if (!have_config_)
    return;
  have_config_ = false;
  StartTimer();
}

void DnsConfigService::InvalidateHosts() {
  DCHECK(CalledOnValidThread());
  base::TimeTicks now = base::TimeTicks::Now();
  if (!last_invalidate_hosts_time_.is_null()) {
    UMA_HISTOGRAM_LONG_TIMES("AsyncDNS.HostsNotifyInterval",
                             now - last_invalidate_hosts_time_);
  }
  last_invalidate_hosts_time_ = now;
  if (!have_hosts_)
    return;
  have_hosts_ = false;
  StartTimer();
}

void DnsConfigService::OnConfigRead(const DnsConfig& config) {
  DCHECK(CalledOnValidThread());
  DCHECK(config.IsValid());

  bool changed = false;
  if (!config.EqualsIgnoreHosts(dns_config_)) {
    dns_config_.CopyIgnoreHosts(config);
    need_update_ = true;
    changed = true;
  }
  if (!changed && !last_sent_empty_time_.is_null()) {
    UMA_HISTOGRAM_LONG_TIMES("AsyncDNS.UnchangedConfigInterval",
                             base::TimeTicks::Now() - last_sent_empty_time_);
  }
  UMA_HISTOGRAM_BOOLEAN("AsyncDNS.ConfigChange", changed);

  have_config_ = true;
  if (have_hosts_)
    OnCompleteConfig();
}

void DnsConfigService::OnHostsRead(const DnsHosts& hosts) {
  DCHECK(CalledOnValidThread());

  bool changed = false;
  if (hosts != dns_config_.hosts) {
    dns_config_.hosts = hosts;
    need_update_ = true;
    changed = true;
  }
  if (!changed && !last_sent_empty_time_.is_null()) {
    UMA_HISTOGRAM_LONG_TIMES("AsyncDNS.UnchangedHostsInterval",
                             base::TimeTicks::Now() - last_sent_empty_time_);
  }
  UMA_HISTOGRAM_BOOLEAN("AsyncDNS.HostsChange", changed);

  have_hosts_ = true;
  if (have_config_)
    OnCompleteConfig();
}

void DnsConfigService::StartTimer() {
  DCHECK(CalledOnValidThread());
  if (last_sent_empty_) {
    DCHECK(!timer_.IsRunning());
    return;  // No need to withdraw again.
  }
  timer_.Stop();

  // Give it a short timeout to come up with a valid config. Otherwise withdraw
  // the config from the receiver. The goal is to avoid perceivable network
  // outage (when using the wrong config) but at the same time avoid
  // unnecessary Job aborts in HostResolverImpl. The signals come from multiple
  // sources so it might receive multiple events during a config change.

  // DHCP and user-induced changes are on the order of seconds, so 100ms should
  // not add perceivable delay. On the other hand, config readers should finish
  // within 100ms with the rare exception of I/O block or extra large HOSTS.
  const base::TimeDelta kTimeout = base::TimeDelta::FromMilliseconds(100);

  timer_.Start(FROM_HERE,
               kTimeout,
               this,
               &DnsConfigService::OnTimeout);
}

void DnsConfigService::OnTimeout() {
  DCHECK(CalledOnValidThread());
  DCHECK(!last_sent_empty_);
  // Indicate that even if there is no change in On*Read, we will need to
  // update the receiver when the config becomes complete.
  need_update_ = true;
  // Empty config is considered invalid.
  last_sent_empty_ = true;
  last_sent_empty_time_ = base::TimeTicks::Now();
  callback_.Run(DnsConfig());
}

void DnsConfigService::OnCompleteConfig() {
  timer_.Stop();
  if (!need_update_)
    return;
  need_update_ = false;
  last_sent_empty_ = false;
  callback_.Run(dns_config_);
}

}  // namespace net

