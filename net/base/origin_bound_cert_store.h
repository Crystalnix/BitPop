// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_ORIGIN_BOUND_CERT_STORE_H_
#define NET_BASE_ORIGIN_BOUND_CERT_STORE_H_
#pragma once

#include <string>
#include <vector>

#include "base/time.h"
#include "net/base/net_export.h"
#include "net/base/ssl_client_cert_type.h"

namespace net {

// An interface for storing and retrieving origin bound certs. Origin bound
// certificates are specified in
// http://balfanz.github.com/tls-obc-spec/draft-balfanz-tls-obc-00.html.

// Owned only by a single OriginBoundCertService object, which is responsible
// for deleting it.

class NET_EXPORT OriginBoundCertStore {
 public:
  // The OriginBoundCert class contains a private key in addition to the origin
  // cert, and cert type.
  class NET_EXPORT OriginBoundCert {
   public:
    OriginBoundCert();
    OriginBoundCert(const std::string& origin,
                    SSLClientCertType type,
                    base::Time creation_time,
                    base::Time expiration_time,
                    const std::string& private_key,
                    const std::string& cert);
    ~OriginBoundCert();

    // Origin, for instance "https://www.verisign.com:443"
    const std::string& origin() const { return origin_; }
    // TLS ClientCertificateType.
    SSLClientCertType type() const { return type_; }
    // The time the certificate was created, also the start of the certificate
    // validity period.
    base::Time creation_time() const { return creation_time_; }
    // The time after which this certificate is no longer valid.
    base::Time expiration_time() const { return expiration_time_; }
    // The encoding of the private key depends on the type.
    // rsa_sign: DER-encoded PrivateKeyInfo struct.
    // ecdsa_sign: DER-encoded EncryptedPrivateKeyInfo struct.
    const std::string& private_key() const { return private_key_; }
    // DER-encoded certificate.
    const std::string& cert() const { return cert_; }

   private:
    std::string origin_;
    SSLClientCertType type_;
    base::Time creation_time_;
    base::Time expiration_time_;
    std::string private_key_;
    std::string cert_;
  };

  virtual ~OriginBoundCertStore() {}

  // TODO(rkn): File I/O may be required, so this should have an asynchronous
  // interface.
  // Returns true on success. |private_key_result| stores a DER-encoded
  // PrivateKeyInfo struct, |cert_result| stores a DER-encoded certificate,
  // |type| is the ClientCertificateType of the returned certificate,
  // |creation_time| stores the start of the validity period of the certificate
  // and |expiration_time| is the expiration time of the certificate.
  // Returns false if no origin bound cert exists for the specified origin.
  virtual bool GetOriginBoundCert(
      const std::string& origin,
      SSLClientCertType* type,
      base::Time* creation_time,
      base::Time* expiration_time,
      std::string* private_key_result,
      std::string* cert_result) = 0;

  // Adds an origin bound cert and the corresponding private key to the store.
  virtual void SetOriginBoundCert(
      const std::string& origin,
      SSLClientCertType type,
      base::Time creation_time,
      base::Time expiration_time,
      const std::string& private_key,
      const std::string& cert) = 0;

  // Removes an origin bound cert and the corresponding private key from the
  // store.
  virtual void DeleteOriginBoundCert(const std::string& origin) = 0;

  // Deletes all of the origin bound certs that have a creation_date greater
  // than or equal to |delete_begin| and less than |delete_end|.  If a
  // base::Time value is_null, that side of the comparison is unbounded.
  virtual void DeleteAllCreatedBetween(base::Time delete_begin,
                                       base::Time delete_end) = 0;

  // Removes all origin bound certs and the corresponding private keys from
  // the store.
  virtual void DeleteAll() = 0;

  // Returns all origin bound certs and the corresponding private keys.
  virtual void GetAllOriginBoundCerts(
      std::vector<OriginBoundCert>* origin_bound_certs) = 0;

  // Returns the number of certs in the store.
  // Public only for unit testing.
  virtual int GetCertCount() = 0;
};

}  // namespace net

#endif  // NET_BASE_ORIGIN_BOUND_CERT_STORE_H_
