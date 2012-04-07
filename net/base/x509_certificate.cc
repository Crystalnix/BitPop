// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/x509_certificate.h"

#include <stdlib.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/metrics/histogram.h"
#include "base/pickle.h"
#include "base/sha1.h"
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/synchronization/lock.h"
#include "base/time.h"
#include "googleurl/src/url_canon_ip.h"
#include "net/base/cert_status_flags.h"
#include "net/base/cert_verify_result.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"
#include "net/base/pem_tokenizer.h"

namespace net {

namespace {

// Indicates the order to use when trying to decode binary data, which is
// based on (speculation) as to what will be most common -> least common
const X509Certificate::Format kFormatDecodePriority[] = {
  X509Certificate::FORMAT_SINGLE_CERTIFICATE,
  X509Certificate::FORMAT_PKCS7
};

// The PEM block header used for DER certificates
const char kCertificateHeader[] = "CERTIFICATE";
// The PEM block header used for PKCS#7 data
const char kPKCS7Header[] = "PKCS7";

#if !defined(USE_NSS)
// A thread-safe cache for OS certificate handles.
//
// Within each of the supported underlying crypto libraries, a certificate
// handle is represented as a ref-counted object that contains the parsed
// data for the certificate. In addition, the underlying OS handle may also
// contain a copy of the original ASN.1 DER used to constructed the handle.
//
// In order to reduce the memory usage when multiple SSL connections exist,
// with each connection storing the server's identity certificate plus any
// intermediates supplied, the certificate handles are cached. Any two
// X509Certificates that were created from the same ASN.1 DER data,
// regardless of where that data came from, will share the same underlying
// OS certificate handle.
class X509CertificateCache {
 public:
  // Performs a compare-and-swap like operation. If an OS certificate handle
  // for the same certificate data as |*cert_handle| already exists in the
  // cache, the original |*cert_handle| will be freed and |cert_handle|
  // will be updated to point to a duplicated reference to the existing cached
  // certificate, with the caller taking ownership of this duplicated handle.
  // If an equivalent OS certificate handle is not found, a duplicated
  // reference to |*cert_handle| will be added to the cache. In either case,
  // upon return, the caller fully owns |*cert_handle| and is responsible for
  // calling FreeOSCertHandle(), after first calling Remove().
  void InsertOrUpdate(X509Certificate::OSCertHandle* cert_handle);

  // Decrements the cache reference count for |cert_handle|, a handle that was
  // previously obtained by calling InsertOrUpdate(). If this is the last
  // cached reference held, this will remove the handle from the cache. The
  // caller retains ownership of |cert_handle| and remains responsible for
  // calling FreeOSCertHandle() to release the underlying OS certificate
  void Remove(X509Certificate::OSCertHandle cert_handle);

 private:
  // A single entry in the cache. Certificates will be keyed by their SHA1
  // fingerprints, but will not be considered equivalent unless the entire
  // certificate data matches.
  struct Entry {
    Entry() : cert_handle(NULL), ref_count(0) {}

    X509Certificate::OSCertHandle cert_handle;

    // Increased by each call to InsertOrUpdate(), and balanced by each call
    // to Remove(). When it equals 0, all references created by
    // InsertOrUpdate() have been released, so the cache entry will be removed
    // the cached OS certificate handle will be freed.
    int ref_count;
  };
  typedef std::map<SHA1Fingerprint, Entry, SHA1FingerprintLessThan> CertMap;

  // Obtain an instance of X509CertificateCache via a LazyInstance.
  X509CertificateCache() {}
  ~X509CertificateCache() {}
  friend struct base::DefaultLazyInstanceTraits<X509CertificateCache>;

  // You must acquire this lock before using any private data of this object
  // You must not block while holding this lock.
  base::Lock lock_;

  // The certificate cache.  You must acquire |lock_| before using |cache_|.
  CertMap cache_;

  DISALLOW_COPY_AND_ASSIGN(X509CertificateCache);
};

base::LazyInstance<X509CertificateCache>::Leaky
    g_x509_certificate_cache = LAZY_INSTANCE_INITIALIZER;

void X509CertificateCache::InsertOrUpdate(
    X509Certificate::OSCertHandle* cert_handle) {
  DCHECK(cert_handle);
  SHA1Fingerprint fingerprint =
      X509Certificate::CalculateFingerprint(*cert_handle);

  X509Certificate::OSCertHandle old_handle = NULL;
  {
    base::AutoLock lock(lock_);
    CertMap::iterator pos = cache_.find(fingerprint);
    if (pos == cache_.end()) {
      // A cached entry was not found, so initialize a new entry. The entry
      // assumes ownership of the current |*cert_handle|.
      Entry cache_entry;
      cache_entry.cert_handle = *cert_handle;
      cache_entry.ref_count = 0;
      CertMap::value_type cache_value(fingerprint, cache_entry);
      pos = cache_.insert(cache_value).first;
    } else {
      bool is_same_cert =
          X509Certificate::IsSameOSCert(*cert_handle, pos->second.cert_handle);
      if (!is_same_cert) {
        // Two certificates don't match, due to a SHA1 hash collision. Given
        // the low probability, the simplest solution is to not cache the
        // certificate, which should not affect performance too negatively.
        return;
      }
      // A cached entry was found and will be used instead of the caller's
      // handle. Ensure the caller's original handle will be freed, since
      // ownership is assumed.
      old_handle = *cert_handle;
    }
    // Whether an existing cached handle or a new handle, increment the
    // cache's reference count and return a handle that the caller can own.
    ++pos->second.ref_count;
    *cert_handle = X509Certificate::DupOSCertHandle(pos->second.cert_handle);
  }
  // If the caller's handle was replaced with a cached handle, free the
  // original handle now. This is done outside of the lock because
  // |old_handle| may be the only handle for this particular certificate, so
  // freeing it may be complex or resource-intensive and does not need to
  // be guarded by the lock.
  if (old_handle) {
    X509Certificate::FreeOSCertHandle(old_handle);
    DHISTOGRAM_COUNTS("X509CertificateReuseCount", 1);
  }
}

void X509CertificateCache::Remove(X509Certificate::OSCertHandle cert_handle) {
  SHA1Fingerprint fingerprint =
      X509Certificate::CalculateFingerprint(cert_handle);
  base::AutoLock lock(lock_);

  CertMap::iterator pos = cache_.find(fingerprint);
  if (pos == cache_.end())
    return;  // A hash collision where the winning cert was already freed.

  bool is_same_cert = X509Certificate::IsSameOSCert(cert_handle,
                                                    pos->second.cert_handle);
  if (!is_same_cert)
    return;  // A hash collision where the winning cert is still around.

  if (--pos->second.ref_count == 0) {
    // The last reference to |cert_handle| has been removed, so release the
    // Entry's OS handle and remove the Entry. The caller still holds a
    // reference to |cert_handle| and is responsible for freeing it.
    X509Certificate::FreeOSCertHandle(pos->second.cert_handle);
    cache_.erase(pos);
  }
}
#endif  // !defined(USE_NSS)

// See X509CertificateCache::InsertOrUpdate. NSS has a built-in cache, so there
// is no point in wrapping another cache around it.
void InsertOrUpdateCache(X509Certificate::OSCertHandle* cert_handle) {
#if !defined(USE_NSS)
  g_x509_certificate_cache.Pointer()->InsertOrUpdate(cert_handle);
#endif
}

// See X509CertificateCache::Remove.
void RemoveFromCache(X509Certificate::OSCertHandle cert_handle) {
#if !defined(USE_NSS)
  g_x509_certificate_cache.Pointer()->Remove(cert_handle);
#endif
}

// CompareSHA1Hashes is a helper function for using bsearch() with an array of
// SHA1 hashes.
int CompareSHA1Hashes(const void* a, const void* b) {
  return memcmp(a, b, base::kSHA1Length);
}

// Utility to split |src| on the first occurrence of |c|, if any. |right| will
// either be empty if |c| was not found, or will contain the remainder of the
// string including the split character itself.
void SplitOnChar(const base::StringPiece& src,
                 char c,
                 base::StringPiece* left,
                 base::StringPiece* right) {
  size_t pos = src.find(c);
  if (pos == base::StringPiece::npos) {
    *left = src;
    right->clear();
  } else {
    *left = src.substr(0, pos);
    *right = src.substr(pos);
  }
}

#if defined(OS_WIN)
X509Certificate::OSCertHandle CreateOSCert(base::StringPiece der_cert) {
  X509Certificate::OSCertHandle cert_handle = NULL;
  BOOL ok = CertAddEncodedCertificateToStore(
      X509Certificate::cert_store(), X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
      reinterpret_cast<const BYTE*>(der_cert.data()), der_cert.size(),
      CERT_STORE_ADD_USE_EXISTING, &cert_handle);
  return ok ? cert_handle : NULL;
}
#else
X509Certificate::OSCertHandle CreateOSCert(base::StringPiece der_cert) {
  return X509Certificate::CreateOSCertHandleFromBytes(
      const_cast<char*>(der_cert.data()), der_cert.size());
}
#endif

// Returns true if |type| is |kPublicKeyTypeRSA| or |kPublicKeyTypeDSA|, and
// if |size_bits| is < 1024. Note that this means there may be false
// negatives: keys for other algorithms and which are weak will pass this
// test.
bool IsWeakKey(X509Certificate::PublicKeyType type, size_t size_bits) {
  switch (type) {
    case X509Certificate::kPublicKeyTypeRSA:
    case X509Certificate::kPublicKeyTypeDSA:
      return size_bits < 1024;
    default:
      return false;
  }
}

}  // namespace

bool X509Certificate::LessThan::operator()(X509Certificate* lhs,
                                           X509Certificate* rhs) const {
  if (lhs == rhs)
    return false;

  int rv = memcmp(lhs->fingerprint_.data, rhs->fingerprint_.data,
                  sizeof(lhs->fingerprint_.data));
  if (rv != 0)
    return rv < 0;

  rv = memcmp(lhs->ca_fingerprint_.data, rhs->ca_fingerprint_.data,
              sizeof(lhs->ca_fingerprint_.data));
  return rv < 0;
}

X509Certificate::X509Certificate(const std::string& subject,
                                 const std::string& issuer,
                                 base::Time start_date,
                                 base::Time expiration_date)
    : subject_(subject),
      issuer_(issuer),
      valid_start_(start_date),
      valid_expiry_(expiration_date),
      cert_handle_(NULL) {
  memset(fingerprint_.data, 0, sizeof(fingerprint_.data));
  memset(ca_fingerprint_.data, 0, sizeof(ca_fingerprint_.data));
}

// static
X509Certificate* X509Certificate::CreateFromHandle(
    OSCertHandle cert_handle,
    const OSCertHandles& intermediates) {
  DCHECK(cert_handle);
  return new X509Certificate(cert_handle, intermediates);
}

// static
X509Certificate* X509Certificate::CreateFromDERCertChain(
    const std::vector<base::StringPiece>& der_certs) {
  if (der_certs.empty())
    return NULL;

  X509Certificate::OSCertHandles intermediate_ca_certs;
  for (size_t i = 1; i < der_certs.size(); i++) {
    OSCertHandle handle = CreateOSCert(der_certs[i]);
    if (!handle)
      break;
    intermediate_ca_certs.push_back(handle);
  }

  OSCertHandle handle = NULL;
  // Return NULL if we failed to parse any of the certs.
  if (der_certs.size() - 1 == intermediate_ca_certs.size())
    handle = CreateOSCert(der_certs[0]);

  X509Certificate* cert = NULL;
  if (handle) {
    cert = CreateFromHandle(handle, intermediate_ca_certs);
    FreeOSCertHandle(handle);
  }

  for (size_t i = 0; i < intermediate_ca_certs.size(); i++)
    FreeOSCertHandle(intermediate_ca_certs[i]);

  return cert;
}

// static
X509Certificate* X509Certificate::CreateFromBytes(const char* data,
                                                  int length) {
  OSCertHandle cert_handle = CreateOSCertHandleFromBytes(data, length);
  if (!cert_handle)
    return NULL;

  X509Certificate* cert = CreateFromHandle(cert_handle, OSCertHandles());
  FreeOSCertHandle(cert_handle);
  return cert;
}

// static
X509Certificate* X509Certificate::CreateFromPickle(const Pickle& pickle,
                                                   void** pickle_iter,
                                                   PickleType type) {
  OSCertHandle cert_handle = ReadOSCertHandleFromPickle(pickle, pickle_iter);
  if (!cert_handle)
    return NULL;

  OSCertHandles intermediates;
  size_t num_intermediates = 0;
  if (type == PICKLETYPE_CERTIFICATE_CHAIN) {
    if (!pickle.ReadSize(pickle_iter, &num_intermediates)) {
      FreeOSCertHandle(cert_handle);
      return NULL;
    }

    for (size_t i = 0; i < num_intermediates; ++i) {
      OSCertHandle intermediate = ReadOSCertHandleFromPickle(pickle,
                                                             pickle_iter);
      if (!intermediate)
        break;
      intermediates.push_back(intermediate);
    }
  }

  X509Certificate* cert = NULL;
  if (intermediates.size() == num_intermediates)
    cert = CreateFromHandle(cert_handle, intermediates);
  FreeOSCertHandle(cert_handle);
  for (size_t i = 0; i < intermediates.size(); ++i)
    FreeOSCertHandle(intermediates[i]);

  return cert;
}

// static
CertificateList X509Certificate::CreateCertificateListFromBytes(
    const char* data, int length, int format) {
  OSCertHandles certificates;

  // Check to see if it is in a PEM-encoded form. This check is performed
  // first, as both OS X and NSS will both try to convert if they detect
  // PEM encoding, except they don't do it consistently between the two.
  base::StringPiece data_string(data, length);
  std::vector<std::string> pem_headers;

  // To maintain compatibility with NSS/Firefox, CERTIFICATE is a universally
  // valid PEM block header for any format.
  pem_headers.push_back(kCertificateHeader);
  if (format & FORMAT_PKCS7)
    pem_headers.push_back(kPKCS7Header);

  PEMTokenizer pem_tok(data_string, pem_headers);
  while (pem_tok.GetNext()) {
    std::string decoded(pem_tok.data());

    OSCertHandle handle = NULL;
    if (format & FORMAT_PEM_CERT_SEQUENCE)
      handle = CreateOSCertHandleFromBytes(decoded.c_str(), decoded.size());
    if (handle != NULL) {
      // Parsed a DER encoded certificate. All PEM blocks that follow must
      // also be DER encoded certificates wrapped inside of PEM blocks.
      format = FORMAT_PEM_CERT_SEQUENCE;
      certificates.push_back(handle);
      continue;
    }

    // If the first block failed to parse as a DER certificate, and
    // formats other than PEM are acceptable, check to see if the decoded
    // data is one of the accepted formats.
    if (format & ~FORMAT_PEM_CERT_SEQUENCE) {
      for (size_t i = 0; certificates.empty() &&
           i < arraysize(kFormatDecodePriority); ++i) {
        if (format & kFormatDecodePriority[i]) {
          certificates = CreateOSCertHandlesFromBytes(decoded.c_str(),
              decoded.size(), kFormatDecodePriority[i]);
        }
      }
    }

    // Stop parsing after the first block for any format but a sequence of
    // PEM-encoded DER certificates. The case of FORMAT_PEM_CERT_SEQUENCE
    // is handled above, and continues processing until a certificate fails
    // to parse.
    break;
  }

  // Try each of the formats, in order of parse preference, to see if |data|
  // contains the binary representation of a Format, if it failed to parse
  // as a PEM certificate/chain.
  for (size_t i = 0; certificates.empty() &&
       i < arraysize(kFormatDecodePriority); ++i) {
    if (format & kFormatDecodePriority[i])
      certificates = CreateOSCertHandlesFromBytes(data, length,
                                                  kFormatDecodePriority[i]);
  }

  CertificateList results;
  // No certificates parsed.
  if (certificates.empty())
    return results;

  for (OSCertHandles::iterator it = certificates.begin();
       it != certificates.end(); ++it) {
    X509Certificate* result = CreateFromHandle(*it, OSCertHandles());
    results.push_back(scoped_refptr<X509Certificate>(result));
    FreeOSCertHandle(*it);
  }

  return results;
}

void X509Certificate::Persist(Pickle* pickle) {
  DCHECK(cert_handle_);
  if (!WriteOSCertHandleToPickle(cert_handle_, pickle)) {
    NOTREACHED();
    return;
  }

  if (!pickle->WriteSize(intermediate_ca_certs_.size())) {
    NOTREACHED();
    return;
  }

  for (size_t i = 0; i < intermediate_ca_certs_.size(); ++i) {
    if (!WriteOSCertHandleToPickle(intermediate_ca_certs_[i], pickle)) {
      NOTREACHED();
      return;
    }
  }
}

void X509Certificate::GetDNSNames(std::vector<std::string>* dns_names) const {
  GetSubjectAltName(dns_names, NULL);
  if (dns_names->empty())
    dns_names->push_back(subject_.common_name);
}

bool X509Certificate::HasExpired() const {
  return base::Time::Now() > valid_expiry();
}

bool X509Certificate::Equals(const X509Certificate* other) const {
  return IsSameOSCert(cert_handle_, other->cert_handle_);
}

// static
bool X509Certificate::VerifyHostname(
    const std::string& hostname,
    const std::string& cert_common_name,
    const std::vector<std::string>& cert_san_dns_names,
    const std::vector<std::string>& cert_san_ip_addrs) {
  DCHECK(!hostname.empty());
  // Perform name verification following http://tools.ietf.org/html/rfc6125.
  // The terminology used in this method is as per that RFC:-
  // Reference identifier == the host the local user/agent is intending to
  //                         access, i.e. the thing displayed in the URL bar.
  // Presented identifier(s) == name(s) the server knows itself as, in its cert.

  // CanonicalizeHost requires surrounding brackets to parse an IPv6 address.
  const std::string host_or_ip = hostname.find(':') != std::string::npos ?
      "[" + hostname + "]" : hostname;
  url_canon::CanonHostInfo host_info;
  std::string reference_name = CanonicalizeHost(host_or_ip, &host_info);
  // CanonicalizeHost does not normalize absolute vs relative DNS names. If
  // the input name was absolute (included trailing .), normalize it as if it
  // was relative.
  if (!reference_name.empty() && *reference_name.rbegin() == '.')
    reference_name.resize(reference_name.size() - 1);
  if (reference_name.empty())
    return false;

  // Allow fallback to Common name matching?
  const bool common_name_fallback = cert_san_dns_names.empty() &&
                                    cert_san_ip_addrs.empty();

  // Fully handle all cases where |hostname| contains an IP address.
  if (host_info.IsIPAddress()) {
    if (common_name_fallback &&
        host_info.family == url_canon::CanonHostInfo::IPV4) {
      // Fallback to Common name matching. As this is deprecated and only
      // supported for compatibility refuse it for IPv6 addresses.
      return reference_name == cert_common_name;
    }
    base::StringPiece ip_addr_string(
        reinterpret_cast<const char*>(host_info.address),
        host_info.AddressLength());
    return std::find(cert_san_ip_addrs.begin(), cert_san_ip_addrs.end(),
                     ip_addr_string) != cert_san_ip_addrs.end();
  }

  // |reference_domain| is the remainder of |host| after the leading host
  // component is stripped off, but includes the leading dot e.g.
  // "www.f.com" -> ".f.com".
  // If there is no meaningful domain part to |host| (e.g. it contains no dots)
  // then |reference_domain| will be empty.
  base::StringPiece reference_host, reference_domain;
  SplitOnChar(reference_name, '.', &reference_host, &reference_domain);
  bool allow_wildcards = false;
  if (!reference_domain.empty()) {
    DCHECK(reference_domain.starts_with("."));
    // We required at least 3 components (i.e. 2 dots) as a basic protection
    // against too-broad wild-carding.
    // Also we don't attempt wildcard matching on a purely numerical hostname.
    allow_wildcards = reference_domain.rfind('.') != 0 &&
        reference_name.find_first_not_of("0123456789.") != std::string::npos;
  }

  // Now step through the DNS names doing wild card comparison (if necessary)
  // on each against the reference name. If subjectAltName is empty, then
  // fallback to use the common name instead.
  std::vector<std::string> common_name_as_vector;
  const std::vector<std::string>* presented_names = &cert_san_dns_names;
  if (common_name_fallback) {
    // Note: there's a small possibility cert_common_name is an international
    // domain name in non-standard encoding (e.g. UTF8String or BMPString
    // instead of A-label). As common name fallback is deprecated we're not
    // doing anything specific to deal with this.
    common_name_as_vector.push_back(cert_common_name);
    presented_names = &common_name_as_vector;
  }
  for (std::vector<std::string>::const_iterator it =
           presented_names->begin();
       it != presented_names->end(); ++it) {
    // Catch badly corrupt cert names up front.
    if (it->empty() || it->find('\0') != std::string::npos) {
      DVLOG(1) << "Bad name in cert: " << *it;
      continue;
    }
    std::string presented_name(StringToLowerASCII(*it));

    // Remove trailing dot, if any.
    if (*presented_name.rbegin() == '.')
      presented_name.resize(presented_name.length() - 1);

    // The hostname must be at least as long as the cert name it is matching,
    // as we require the wildcard (if present) to match at least one character.
    if (presented_name.length() > reference_name.length())
      continue;

    base::StringPiece presented_host, presented_domain;
    SplitOnChar(presented_name, '.', &presented_host, &presented_domain);

    if (presented_domain != reference_domain)
      continue;

    base::StringPiece pattern_begin, pattern_end;
    SplitOnChar(presented_host, '*', &pattern_begin, &pattern_end);

    if (pattern_end.empty()) {  // No '*' in the presented_host
      if (presented_host == reference_host)
        return true;
      continue;
    }
    pattern_end.remove_prefix(1);  // move past the *

    if (!allow_wildcards)
      continue;

    // * must not match a substring of an IDN A label; just a whole fragment.
    if (reference_host.starts_with("xn--") &&
        !(pattern_begin.empty() && pattern_end.empty()))
      continue;

    if (reference_host.starts_with(pattern_begin) &&
        reference_host.ends_with(pattern_end))
      return true;
  }
  return false;
}

int X509Certificate::Verify(const std::string& hostname,
                            int flags,
                            CRLSet* crl_set,
                            CertVerifyResult* verify_result) const {
  verify_result->Reset();
  verify_result->verified_cert = const_cast<X509Certificate*>(this);

  if (IsBlacklisted()) {
    verify_result->cert_status |= CERT_STATUS_REVOKED;
    return ERR_CERT_REVOKED;
  }

  int rv = VerifyInternal(hostname, flags, crl_set, verify_result);

  // This check is done after VerifyInternal so that VerifyInternal can fill in
  // the list of public key hashes.
  if (IsPublicKeyBlacklisted(verify_result->public_key_hashes)) {
    verify_result->cert_status |= CERT_STATUS_REVOKED;
    rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  // Check for weak keys in the entire verified chain.
  size_t size_bits = 0;
  PublicKeyType type = kPublicKeyTypeUnknown;
  bool weak_key = false;

  GetPublicKeyInfo(verify_result->verified_cert->os_cert_handle(), &size_bits,
                   &type);
  if (IsWeakKey(type, size_bits)) {
    weak_key = true;
  } else {
    const OSCertHandles& intermediates =
        verify_result->verified_cert->GetIntermediateCertificates();
    for (OSCertHandles::const_iterator i = intermediates.begin();
         i != intermediates.end(); ++i) {
      GetPublicKeyInfo(*i, &size_bits, &type);
      if (IsWeakKey(type, size_bits))
        weak_key = true;
    }
  }

  if (weak_key) {
    verify_result->cert_status |= CERT_STATUS_WEAK_KEY;
    // Avoid replacing a more serious error, such as an OS/library failure,
    // by ensuring that if verification failed, it failed with a certificate
    // error.
    if (rv == OK || IsCertificateError(rv))
      rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  // Treat certificates signed using broken signature algorithms as invalid.
  if (verify_result->has_md2 || verify_result->has_md4) {
    verify_result->cert_status |= CERT_STATUS_INVALID;
    rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  // Flag certificates using weak signature algorithms.
  if (verify_result->has_md5) {
    verify_result->cert_status |= CERT_STATUS_WEAK_SIGNATURE_ALGORITHM;
    // Avoid replacing a more serious error, such as an OS/library failure,
    // by ensuring that if verification failed, it failed with a certificate
    // error.
    if (rv == OK || IsCertificateError(rv))
      rv = MapCertStatusToNetError(verify_result->cert_status);
  }

  return rv;
}

#if !defined(USE_NSS)
bool X509Certificate::VerifyNameMatch(const std::string& hostname) const {
  std::vector<std::string> dns_names, ip_addrs;
  GetSubjectAltName(&dns_names, &ip_addrs);
  return VerifyHostname(hostname, subject_.common_name, dns_names, ip_addrs);
}
#endif

// static
bool X509Certificate::GetPEMEncoded(OSCertHandle cert_handle,
                                    std::string* pem_encoded) {
  std::string der_encoded;
  if (!GetDEREncoded(cert_handle, &der_encoded) || der_encoded.empty())
    return false;
  std::string b64_encoded;
  if (!base::Base64Encode(der_encoded, &b64_encoded) || b64_encoded.empty())
    return false;
  *pem_encoded = "-----BEGIN CERTIFICATE-----\n";

  // Divide the Base-64 encoded data into 64-character chunks, as per
  // 4.3.2.4 of RFC 1421.
  static const size_t kChunkSize = 64;
  size_t chunks = (b64_encoded.size() + (kChunkSize - 1)) / kChunkSize;
  for (size_t i = 0, chunk_offset = 0; i < chunks;
       ++i, chunk_offset += kChunkSize) {
    pem_encoded->append(b64_encoded, chunk_offset, kChunkSize);
    pem_encoded->append("\n");
  }
  pem_encoded->append("-----END CERTIFICATE-----\n");
  return true;
}

bool X509Certificate::GetPEMEncodedChain(
    std::vector<std::string>* pem_encoded) const {
  std::vector<std::string> encoded_chain;
  std::string pem_data;
  if (!GetPEMEncoded(os_cert_handle(), &pem_data))
    return false;
  encoded_chain.push_back(pem_data);
  for (size_t i = 0; i < intermediate_ca_certs_.size(); ++i) {
    if (!GetPEMEncoded(intermediate_ca_certs_[i], &pem_data))
      return false;
    encoded_chain.push_back(pem_data);
  }
  pem_encoded->swap(encoded_chain);
  return true;
}

X509Certificate::X509Certificate(OSCertHandle cert_handle,
                                 const OSCertHandles& intermediates)
    : cert_handle_(DupOSCertHandle(cert_handle)) {
  InsertOrUpdateCache(&cert_handle_);
  for (size_t i = 0; i < intermediates.size(); ++i) {
    // Duplicate the incoming certificate, as the caller retains ownership
    // of |intermediates|.
    OSCertHandle intermediate = DupOSCertHandle(intermediates[i]);
    // Update the cache, which will assume ownership of the duplicated
    // handle and return a suitable equivalent, potentially from the cache.
    InsertOrUpdateCache(&intermediate);
    intermediate_ca_certs_.push_back(intermediate);
  }
  // Platform-specific initialization.
  Initialize();
}

X509Certificate::~X509Certificate() {
  if (cert_handle_) {
    RemoveFromCache(cert_handle_);
    FreeOSCertHandle(cert_handle_);
  }
  for (size_t i = 0; i < intermediate_ca_certs_.size(); ++i) {
    RemoveFromCache(intermediate_ca_certs_[i]);
    FreeOSCertHandle(intermediate_ca_certs_[i]);
  }
}

bool X509Certificate::IsBlacklisted() const {
  static const unsigned kComodoSerialBytes = 16;
  static const uint8 kComodoSerials[][kComodoSerialBytes] = {
    // Not a real certificate. For testing only.
    {0x07,0x7a,0x59,0xbc,0xd5,0x34,0x59,0x60,0x1c,0xa6,0x90,0x72,0x67,0xa6,0xdd,0x1c},

    // The next nine certificates all expire on Fri Mar 14 23:59:59 2014.
    // Some serial numbers actually have a leading 0x00 byte required to
    // encode a positive integer in DER if the most significant bit is 0.
    // We omit the leading 0x00 bytes to make all serial numbers 16 bytes.

    // Subject: CN=mail.google.com
    // subjectAltName dNSName: mail.google.com, www.mail.google.com
    {0x04,0x7e,0xcb,0xe9,0xfc,0xa5,0x5f,0x7b,0xd0,0x9e,0xae,0x36,0xe1,0x0c,0xae,0x1e},
    // Subject: CN=global trustee
    // subjectAltName dNSName: global trustee
    // Note: not a CA certificate.
    {0xd8,0xf3,0x5f,0x4e,0xb7,0x87,0x2b,0x2d,0xab,0x06,0x92,0xe3,0x15,0x38,0x2f,0xb0},
    // Subject: CN=login.live.com
    // subjectAltName dNSName: login.live.com, www.login.live.com
    {0xb0,0xb7,0x13,0x3e,0xd0,0x96,0xf9,0xb5,0x6f,0xae,0x91,0xc8,0x74,0xbd,0x3a,0xc0},
    // Subject: CN=addons.mozilla.org
    // subjectAltName dNSName: addons.mozilla.org, www.addons.mozilla.org
    {0x92,0x39,0xd5,0x34,0x8f,0x40,0xd1,0x69,0x5a,0x74,0x54,0x70,0xe1,0xf2,0x3f,0x43},
    // Subject: CN=login.skype.com
    // subjectAltName dNSName: login.skype.com, www.login.skype.com
    {0xe9,0x02,0x8b,0x95,0x78,0xe4,0x15,0xdc,0x1a,0x71,0x0a,0x2b,0x88,0x15,0x44,0x47},
    // Subject: CN=login.yahoo.com
    // subjectAltName dNSName: login.yahoo.com, www.login.yahoo.com
    {0xd7,0x55,0x8f,0xda,0xf5,0xf1,0x10,0x5b,0xb2,0x13,0x28,0x2b,0x70,0x77,0x29,0xa3},
    // Subject: CN=www.google.com
    // subjectAltName dNSName: www.google.com, google.com
    {0xf5,0xc8,0x6a,0xf3,0x61,0x62,0xf1,0x3a,0x64,0xf5,0x4f,0x6d,0xc9,0x58,0x7c,0x06},
    // Subject: CN=login.yahoo.com
    // subjectAltName dNSName: login.yahoo.com
    {0x39,0x2a,0x43,0x4f,0x0e,0x07,0xdf,0x1f,0x8a,0xa3,0x05,0xde,0x34,0xe0,0xc2,0x29},
    // Subject: CN=login.yahoo.com
    // subjectAltName dNSName: login.yahoo.com
    {0x3e,0x75,0xce,0xd4,0x6b,0x69,0x30,0x21,0x21,0x88,0x30,0xae,0x86,0xa8,0x2a,0x71},
  };

  if (!serial_number_.empty() && (serial_number_[0] & 0x80) != 0) {
    // This is a negative serial number, which isn't technically allowed but
    // which probably happens. In order to avoid confusing a negative serial
    // number with a positive one once the leading zeros have been removed, we
    // disregard it.
    return false;
  }

  base::StringPiece serial(serial_number_);
  // Remove leading zeros.
  while (serial.size() > 1 && serial[0] == 0)
    serial.remove_prefix(1);

  if (serial.size() == kComodoSerialBytes) {
    for (unsigned i = 0; i < arraysize(kComodoSerials); i++) {
      if (memcmp(kComodoSerials[i], serial.data(), kComodoSerialBytes) == 0) {
        UMA_HISTOGRAM_ENUMERATION("Net.SSLCertBlacklisted", i,
                                  arraysize(kComodoSerials) + 1);
        return true;
      }
    }
  }

  return false;
}

// static
bool X509Certificate::IsPublicKeyBlacklisted(
    const std::vector<SHA1Fingerprint>& public_key_hashes) {
  static const unsigned kNumHashes = 8;
  static const uint8 kHashes[kNumHashes][base::kSHA1Length] = {
    // Subject: CN=DigiNotar Root CA
    // Issuer: CN=Entrust.net x2 and self-signed
    {0x41, 0x0f, 0x36, 0x36, 0x32, 0x58, 0xf3, 0x0b, 0x34, 0x7d,
     0x12, 0xce, 0x48, 0x63, 0xe4, 0x33, 0x43, 0x78, 0x06, 0xa8},
    // Subject: CN=DigiNotar Cyber CA
    // Issuer: CN=GTE CyberTrust Global Root
    {0xc4, 0xf9, 0x66, 0x37, 0x16, 0xcd, 0x5e, 0x71, 0xd6, 0x95,
     0x0b, 0x5f, 0x33, 0xce, 0x04, 0x1c, 0x95, 0xb4, 0x35, 0xd1},
    // Subject: CN=DigiNotar Services 1024 CA
    // Issuer: CN=Entrust.net
    {0xe2, 0x3b, 0x8d, 0x10, 0x5f, 0x87, 0x71, 0x0a, 0x68, 0xd9,
     0x24, 0x80, 0x50, 0xeb, 0xef, 0xc6, 0x27, 0xbe, 0x4c, 0xa6},
    // Subject: CN=DigiNotar PKIoverheid CA Organisatie - G2
    // Issuer: CN=Staat der Nederlanden Organisatie CA - G2
    {0x7b, 0x2e, 0x16, 0xbc, 0x39, 0xbc, 0xd7, 0x2b, 0x45, 0x6e,
     0x9f, 0x05, 0x5d, 0x1d, 0xe6, 0x15, 0xb7, 0x49, 0x45, 0xdb},
    // Subject: CN=DigiNotar PKIoverheid CA Overheid en Bedrijven
    // Issuer: CN=Staat der Nederlanden Overheid CA
    {0xe8, 0xf9, 0x12, 0x00, 0xc6, 0x5c, 0xee, 0x16, 0xe0, 0x39,
     0xb9, 0xf8, 0x83, 0x84, 0x16, 0x61, 0x63, 0x5f, 0x81, 0xc5},
    // Subject: O=Digicert Sdn. Bhd.
    // Issuer: CN=GTE CyberTrust Global Root
    // Expires: Jul 17 15:16:54 2012 GMT
    {0x01, 0x29, 0xbc, 0xd5, 0xb4, 0x48, 0xae, 0x8d, 0x24, 0x96,
     0xd1, 0xc3, 0xe1, 0x97, 0x23, 0x91, 0x90, 0x88, 0xe1, 0x52},
    // Subject: O=Digicert Sdn. Bhd.
    // Issuer: CN=Entrust.net Certification Authority (2048)
    // Expires: Jul 16 17:53:37 2015 GMT
    {0xd3, 0x3c, 0x5b, 0x41, 0xe4, 0x5c, 0xc4, 0xb3, 0xbe, 0x9a,
     0xd6, 0x95, 0x2c, 0x4e, 0xcc, 0x25, 0x28, 0x03, 0x29, 0x81},
    // Issuer: CN=Trustwave Organization Issuing CA, Level 2
    // Coveres two certificates, the latter of which expires Apr 15 21:09:30
    // 2021 GMT.
    {0xe1, 0x2d, 0x89, 0xf5, 0x6d, 0x22, 0x76, 0xf8, 0x30, 0xe6,
     0xce, 0xaf, 0xa6, 0x6c, 0x72, 0x5c, 0x0b, 0x41, 0xa9, 0x32},
  };

  for (unsigned i = 0; i < kNumHashes; i++) {
    for (std::vector<SHA1Fingerprint>::const_iterator
         j = public_key_hashes.begin(); j != public_key_hashes.end(); ++j) {
      if (memcmp(j->data, kHashes[i], base::kSHA1Length) == 0)
        return true;
    }
  }

  return false;
}

// static
bool X509Certificate::IsSHA1HashInSortedArray(const SHA1Fingerprint& hash,
                                              const uint8* array,
                                              size_t array_byte_len) {
  DCHECK_EQ(0u, array_byte_len % base::kSHA1Length);
  const size_t arraylen = array_byte_len / base::kSHA1Length;
  return NULL != bsearch(hash.data, array, arraylen, base::kSHA1Length,
                         CompareSHA1Hashes);
}

}  // namespace net
