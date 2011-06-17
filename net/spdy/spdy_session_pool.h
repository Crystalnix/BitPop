// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_SPDY_SESSION_POOL_H_
#define NET_SPDY_SPDY_SESSION_POOL_H_
#pragma once

#include <map>
#include <list>
#include <string>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "net/base/cert_database.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/network_change_notifier.h"
#include "net/base/ssl_config_service.h"
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_server.h"
#include "net/spdy/spdy_settings_storage.h"

namespace net {

class AddressList;
class BoundNetLog;
class ClientSocketHandle;
class HostResolver;
class HttpNetworkSession;
class SpdySession;

// This is a very simple pool for open SpdySessions.
class SpdySessionPool
    : public NetworkChangeNotifier::IPAddressObserver,
      public SSLConfigService::Observer,
      public CertDatabase::Observer {
 public:
  explicit SpdySessionPool(HostResolver* host_resolver,
                           SSLConfigService* ssl_config_service);
  virtual ~SpdySessionPool();

  // Either returns an existing SpdySession or creates a new SpdySession for
  // use.
  scoped_refptr<SpdySession> Get(
      const HostPortProxyPair& host_port_proxy_pair,
      const BoundNetLog& net_log);

  // Set the maximum concurrent sessions per domain.
  static void set_max_sessions_per_domain(int max) {
    if (max >= 1)
      g_max_sessions_per_domain = max;
  }

  // Builds a SpdySession from an existing SSL socket.  Users should try
  // calling Get() first to use an existing SpdySession so we don't get
  // multiple SpdySessions per domain.  Note that ownership of |connection| is
  // transferred from the caller to the SpdySession.
  // |certificate_error_code| is used to indicate the certificate error
  // encountered when connecting the SSL socket.  OK means there was no error.
  // For testing, setting is_secure to false allows Spdy to connect with a
  // pre-existing TCP socket.
  // Returns OK on success, and the |spdy_session| will be provided.
  // Returns an error on failure, and |spdy_session| will be NULL.
  net::Error GetSpdySessionFromSocket(
      const HostPortProxyPair& host_port_proxy_pair,
      ClientSocketHandle* connection,
      const BoundNetLog& net_log,
      int certificate_error_code,
      scoped_refptr<SpdySession>* spdy_session,
      bool is_secure);

  // TODO(willchan): Consider renaming to HasReusableSession, since perhaps we
  // should be creating a new session.
  bool HasSession(const HostPortProxyPair& host_port_proxy_pair) const;

  // Close all SpdySessions, including any new ones created in the process of
  // closing the current ones.
  void CloseAllSessions();
  // Close only the currently existing SpdySessions. Let any new ones created
  // continue to live.
  void CloseCurrentSessions();

  // Removes a SpdySession from the SpdySessionPool. This should only be called
  // by SpdySession, because otherwise session->state_ is not set to CLOSED.
  void Remove(const scoped_refptr<SpdySession>& session);

  // Creates a Value summary of the state of the spdy session pool. The caller
  // responsible for deleting the returned value.
  Value* SpdySessionPoolInfoToValue() const;

  SpdySettingsStorage* mutable_spdy_settings() { return &spdy_settings_; }
  const SpdySettingsStorage& spdy_settings() const { return spdy_settings_; }

  // NetworkChangeNotifier::IPAddressObserver methods:

  // We flush all idle sessions and release references to the active ones so
  // they won't get re-used.  The active ones will either complete successfully
  // or error out due to the IP address change.
  virtual void OnIPAddressChanged();

  // SSLConfigService::Observer methods:

  // We perform the same flushing as described above when SSL settings change.
  virtual void OnSSLConfigChanged();

  // A debugging mode where we compress all accesses through a single domain.
  static void ForceSingleDomain() { g_force_single_domain = true; }

  // Controls whether the pool allows use of a common session for domains
  // which share IP address resolutions.
  static void enable_ip_pooling(bool value) { g_enable_ip_pooling = value; }

  // CertDatabase::Observer methods:
  virtual void OnUserCertAdded(const X509Certificate* cert);
  virtual void OnCertTrustChanged(const X509Certificate* cert);

 private:
  friend class SpdySessionPoolPeer;  // For testing.
  friend class SpdyNetworkTransactionTest;  // For testing.
  FRIEND_TEST_ALL_PREFIXES(SpdyNetworkTransactionTest, WindowUpdateOverflow);

  typedef std::list<scoped_refptr<SpdySession> > SpdySessionList;
  typedef std::map<HostPortProxyPair, SpdySessionList*> SpdySessionsMap;
  typedef std::map<IPEndPoint, HostPortProxyPair> SpdyAliasMap;

  scoped_refptr<SpdySession> GetExistingSession(
      SpdySessionList* list,
      const BoundNetLog& net_log) const;
  scoped_refptr<SpdySession> GetFromAlias(
      const HostPortProxyPair& host_port_proxy_pair,
      const BoundNetLog& net_log,
      bool record_histograms) const;

  // Helper functions for manipulating the lists.
  const HostPortProxyPair& NormalizeListPair(
      const HostPortProxyPair& host_port_proxy_pair) const;
  SpdySessionList* AddSessionList(
      const HostPortProxyPair& host_port_proxy_pair);
  SpdySessionList* GetSessionList(
      const HostPortProxyPair& host_port_proxy_pair) const;
  void RemoveSessionList(const HostPortProxyPair& host_port_proxy_pair);

  // Does a DNS cache lookup for |pair|, and returns the |addresses| found.
  // Returns true if addresses found, false otherwise.
  bool LookupAddresses(const HostPortProxyPair& pair,
                       AddressList* addresses) const;

  // Add a set of |addresses| as IP-equivalent addresses for |pair|.
  void AddAliases(const AddressList& addresses, const HostPortProxyPair& pair);

  // Remove all aliases for |pair| from the aliases table.
  void RemoveAliases(const HostPortProxyPair& pair);

  SpdySettingsStorage spdy_settings_;

  // This is our weak session pool - one session per domain.
  SpdySessionsMap sessions_;
  // A map of IPEndPoint aliases for sessions.
  SpdyAliasMap aliases_;

  static size_t g_max_sessions_per_domain;
  static bool g_force_single_domain;
  static bool g_enable_ip_pooling;

  const scoped_refptr<SSLConfigService> ssl_config_service_;
  HostResolver* resolver_;

  DISALLOW_COPY_AND_ASSIGN(SpdySessionPool);
};

}  // namespace net

#endif  // NET_SPDY_SPDY_SESSION_POOL_H_
