// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/cert_database.h"

#include <cert.h>
#include <certdb.h>
#include <keyhi.h>
#include <pk11pub.h>
#include <secmod.h>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "crypto/nss_util.h"
#include "crypto/nss_util_internal.h"
#include "net/base/crypto_module.h"
#include "net/base/net_errors.h"
#include "net/base/x509_certificate.h"
#include "net/third_party/mozilla_security_manager/nsNSSCertificateDB.h"
#include "net/third_party/mozilla_security_manager/nsNSSCertTrust.h"
#include "net/third_party/mozilla_security_manager/nsPKCS12Blob.h"

// In NSS 3.13, CERTDB_VALID_PEER was renamed CERTDB_TERMINAL_RECORD. So we use
// the new name of the macro.
#if !defined(CERTDB_TERMINAL_RECORD)
#define CERTDB_TERMINAL_RECORD CERTDB_VALID_PEER
#endif

// PSM = Mozilla's Personal Security Manager.
namespace psm = mozilla_security_manager;

namespace net {

CertDatabase::CertDatabase() {
  crypto::EnsureNSSInit();
  psm::EnsurePKCS12Init();
}

int CertDatabase::CheckUserCert(X509Certificate* cert_obj) {
  if (!cert_obj)
    return ERR_CERT_INVALID;
  if (cert_obj->HasExpired())
    return ERR_CERT_DATE_INVALID;

  // Check if the private key corresponding to the certificate exist
  // We shouldn't accept any random client certificate sent by a CA.

  // Note: The NSS source documentation wrongly suggests that this
  // also imports the certificate if the private key exists. This
  // doesn't seem to be the case.

  CERTCertificate* cert = cert_obj->os_cert_handle();
  PK11SlotInfo* slot = PK11_KeyForCertExists(cert, NULL, NULL);
  if (!slot) {
    LOG(ERROR) << "No corresponding private key in store";
    return ERR_NO_PRIVATE_KEY_FOR_CERT;
  }
  PK11_FreeSlot(slot);

  return OK;
}

int CertDatabase::AddUserCert(X509Certificate* cert_obj) {
  CERTCertificate* cert = cert_obj->os_cert_handle();
  PK11SlotInfo* slot = NULL;

  {
    crypto::AutoNSSWriteLock lock;
    slot = PK11_ImportCertForKey(
        cert,
        cert_obj->GetDefaultNickname(net::USER_CERT).c_str(),
        NULL);
  }

  if (!slot) {
    LOG(ERROR) << "Couldn't import user certificate.";
    return ERR_ADD_USER_CERT_FAILED;
  }
  PK11_FreeSlot(slot);
  CertDatabase::NotifyObserversOfUserCertAdded(cert_obj);
  return OK;
}

void CertDatabase::ListCerts(CertificateList* certs) {
  certs->clear();

  CERTCertList* cert_list = PK11_ListCerts(PK11CertListUnique, NULL);
  CERTCertListNode* node;
  for (node = CERT_LIST_HEAD(cert_list);
       !CERT_LIST_END(node, cert_list);
       node = CERT_LIST_NEXT(node)) {
    certs->push_back(X509Certificate::CreateFromHandle(
        node->cert, X509Certificate::OSCertHandles()));
  }
  CERT_DestroyCertList(cert_list);
}

CryptoModule* CertDatabase::GetPublicModule() const {
  CryptoModule* module =
      CryptoModule::CreateFromHandle(crypto::GetPublicNSSKeySlot());
  // The module is already referenced when returned from
  // GetPublicNSSKeySlot, so we need to deref it once.
  PK11_FreeSlot(module->os_module_handle());

  return module;
}

CryptoModule* CertDatabase::GetPrivateModule() const {
  CryptoModule* module =
      CryptoModule::CreateFromHandle(crypto::GetPrivateNSSKeySlot());
  // The module is already referenced when returned from
  // GetPrivateNSSKeySlot, so we need to deref it once.
  PK11_FreeSlot(module->os_module_handle());

  return module;
}

void CertDatabase::ListModules(CryptoModuleList* modules, bool need_rw) const {
  modules->clear();

  PK11SlotList* slot_list = NULL;
  // The wincx arg is unused since we don't call PK11_SetIsLoggedInFunc.
  slot_list = PK11_GetAllTokens(CKM_INVALID_MECHANISM,
                                need_rw ? PR_TRUE : PR_FALSE,  // needRW
                                PR_TRUE,  // loadCerts (unused)
                                NULL);  // wincx
  if (!slot_list) {
    LOG(ERROR) << "PK11_GetAllTokens failed: " << PORT_GetError();
    return;
  }

  PK11SlotListElement* slot_element = PK11_GetFirstSafe(slot_list);
  while (slot_element) {
    modules->push_back(CryptoModule::CreateFromHandle(slot_element->slot));
    slot_element = PK11_GetNextSafe(slot_list, slot_element,
                                    PR_FALSE);  // restart
  }

  PK11_FreeSlotList(slot_list);
}

int CertDatabase::ImportFromPKCS12(
    CryptoModule* module,
    const std::string& data,
    const string16& password,
    bool is_extractable,
    net::CertificateList* imported_certs) {
  int result = psm::nsPKCS12Blob_Import(module->os_module_handle(),
                                        data.data(), data.size(),
                                        password,
                                        is_extractable,
                                        imported_certs);
  if (result == net::OK)
    CertDatabase::NotifyObserversOfUserCertAdded(NULL);

  return result;
}

int CertDatabase::ExportToPKCS12(
    const CertificateList& certs,
    const string16& password,
    std::string* output) const {
  return psm::nsPKCS12Blob_Export(output, certs, password);
}

X509Certificate* CertDatabase::FindRootInList(
    const CertificateList& certificates) const {
  DCHECK_GT(certificates.size(), 0U);

  if (certificates.size() == 1)
    return certificates[0].get();

  X509Certificate* cert0 = certificates[0];
  X509Certificate* cert1 = certificates[1];
  X509Certificate* certn_2 = certificates[certificates.size() - 2];
  X509Certificate* certn_1 = certificates[certificates.size() - 1];

  if (CERT_CompareName(&cert1->os_cert_handle()->issuer,
                       &cert0->os_cert_handle()->subject) == SECEqual)
    return cert0;
  if (CERT_CompareName(&certn_2->os_cert_handle()->issuer,
                       &certn_1->os_cert_handle()->subject) == SECEqual)
    return certn_1;

  VLOG(1) << "certificate list is not a hierarchy";
  return cert0;
}

bool CertDatabase::ImportCACerts(const CertificateList& certificates,
                                 TrustBits trust_bits,
                                 ImportCertFailureList* not_imported) {
  X509Certificate* root = FindRootInList(certificates);
  bool success = psm::ImportCACerts(certificates, root, trust_bits,
                                    not_imported);
  if (success)
    CertDatabase::NotifyObserversOfCertTrustChanged(NULL);

  return success;
}

bool CertDatabase::ImportServerCert(const CertificateList& certificates,
                                    ImportCertFailureList* not_imported) {
  return psm::ImportServerCert(certificates, not_imported);
}

CertDatabase::TrustBits CertDatabase::GetCertTrust(const X509Certificate* cert,
                                                   CertType type) const {
  CERTCertTrust nsstrust;
  SECStatus srv = CERT_GetCertTrust(cert->os_cert_handle(), &nsstrust);
  if (srv != SECSuccess) {
    LOG(ERROR) << "CERT_GetCertTrust failed with error " << PORT_GetError();
    return UNTRUSTED;
  }
  psm::nsNSSCertTrust trust(&nsstrust);
  switch (type) {
    case CA_CERT:
      return trust.HasTrustedCA(PR_TRUE, PR_FALSE, PR_FALSE) * TRUSTED_SSL +
          trust.HasTrustedCA(PR_FALSE, PR_TRUE, PR_FALSE) * TRUSTED_EMAIL +
          trust.HasTrustedCA(PR_FALSE, PR_FALSE, PR_TRUE) * TRUSTED_OBJ_SIGN;
    case SERVER_CERT:
      return trust.HasTrustedPeer(PR_TRUE, PR_FALSE, PR_FALSE) * TRUSTED_SSL +
          trust.HasTrustedPeer(PR_FALSE, PR_TRUE, PR_FALSE) * TRUSTED_EMAIL +
          trust.HasTrustedPeer(PR_FALSE, PR_FALSE, PR_TRUE) * TRUSTED_OBJ_SIGN;
    default:
      return UNTRUSTED;
  }
}

bool CertDatabase::IsUntrusted(const X509Certificate* cert) const {
  CERTCertTrust nsstrust;
  SECStatus rv = CERT_GetCertTrust(cert->os_cert_handle(), &nsstrust);
  if (rv != SECSuccess) {
    LOG(ERROR) << "CERT_GetCertTrust failed with error " << PORT_GetError();
    return false;
  }

  // The CERTCertTrust structure contains three trust records:
  // sslFlags, emailFlags, and objectSigningFlags.  The three
  // trust records are independent of each other.
  //
  // If the CERTDB_TERMINAL_RECORD bit in a trust record is set,
  // then that trust record is a terminal record.  A terminal
  // record is used for explicit trust and distrust of an
  // end-entity or intermediate CA cert.
  //
  // In a terminal record, if neither CERTDB_TRUSTED_CA nor
  // CERTDB_TRUSTED is set, then the terminal record means
  // explicit distrust.  On the other hand, if the terminal
  // record has either CERTDB_TRUSTED_CA or CERTDB_TRUSTED bit
  // set, then the terminal record means explicit trust.
  //
  // For a root CA, the trust record does not have
  // the CERTDB_TERMINAL_RECORD bit set.

  static const unsigned int kTrusted = CERTDB_TRUSTED_CA | CERTDB_TRUSTED;
  if ((nsstrust.sslFlags & CERTDB_TERMINAL_RECORD) != 0 &&
      (nsstrust.sslFlags & kTrusted) == 0) {
    return true;
  }
  if ((nsstrust.emailFlags & CERTDB_TERMINAL_RECORD) != 0 &&
      (nsstrust.emailFlags & kTrusted) == 0) {
    return true;
  }
  if ((nsstrust.objectSigningFlags & CERTDB_TERMINAL_RECORD) != 0 &&
      (nsstrust.objectSigningFlags & kTrusted) == 0) {
    return true;
  }

  // Self-signed certificates that don't have any trust bits set are untrusted.
  // Other certificates that don't have any trust bits set may still be trusted
  // if they chain up to a trust anchor.
  if (CERT_CompareName(&cert->os_cert_handle()->issuer,
                       &cert->os_cert_handle()->subject) == SECEqual) {
    return (nsstrust.sslFlags & kTrusted) == 0 &&
           (nsstrust.emailFlags & kTrusted) == 0 &&
           (nsstrust.objectSigningFlags & kTrusted) == 0;
  }

  return false;
}

bool CertDatabase::SetCertTrust(const X509Certificate* cert,
                                CertType type,
                                TrustBits trust_bits) {
  bool success = psm::SetCertTrust(cert, type, trust_bits);
  if (success)
    CertDatabase::NotifyObserversOfCertTrustChanged(cert);

  return success;
}

bool CertDatabase::DeleteCertAndKey(const X509Certificate* cert) {
  // For some reason, PK11_DeleteTokenCertAndKey only calls
  // SEC_DeletePermCertificate if the private key is found.  So, we check
  // whether a private key exists before deciding which function to call to
  // delete the cert.
  SECKEYPrivateKey *privKey = PK11_FindKeyByAnyCert(cert->os_cert_handle(),
                                                    NULL);
  if (privKey) {
    SECKEY_DestroyPrivateKey(privKey);
    if (PK11_DeleteTokenCertAndKey(cert->os_cert_handle(), NULL)) {
      LOG(ERROR) << "PK11_DeleteTokenCertAndKey failed: " << PORT_GetError();
      return false;
    }
  } else {
    if (SEC_DeletePermCertificate(cert->os_cert_handle())) {
      LOG(ERROR) << "SEC_DeletePermCertificate failed: " << PORT_GetError();
      return false;
    }
  }

  CertDatabase::NotifyObserversOfUserCertRemoved(cert);

  return true;
}

bool CertDatabase::IsReadOnly(const X509Certificate* cert) const {
  PK11SlotInfo* slot = cert->os_cert_handle()->slot;
  return slot && PK11_IsReadOnly(slot);
}

}  // namespace net
