// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cert.h>
#include <pk11pub.h>

#include <algorithm>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/lazy_instance.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "crypto/nss_util.h"
#include "crypto/nss_util_internal.h"
#include "crypto/scoped_nss_types.h"
#include "net/base/cert_database.h"
#include "net/base/cert_status_flags.h"
#include "net/base/cert_verify_result.h"
#include "net/base/crypto_module.h"
#include "net/base/net_errors.h"
#include "net/base/x509_certificate.h"
#include "net/third_party/mozilla_security_manager/nsNSSCertTrust.h"
#include "net/third_party/mozilla_security_manager/nsNSSCertificateDB.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace psm = mozilla_security_manager;

namespace net {

// TODO(mattm): when https://bugzilla.mozilla.org/show_bug.cgi?id=588269 is
// fixed, switch back to using a separate userdb for each test.
// (When doing so, remember to add some standalone tests of DeleteCert since it
// won't be tested by TearDown anymore.)
class CertDatabaseNSSTest : public testing::Test {
 public:
  static void SetUpTestCase() {
    ASSERT_TRUE(crypto::OpenTestNSSDB());
    // There is no matching TearDownTestCase call to close the test NSS DB
    // because that would leave NSS in a potentially broken state for further
    // tests, due to https://bugzilla.mozilla.org/show_bug.cgi?id=588269
  }

  virtual void SetUp() {
    slot_ = cert_db_.GetPublicModule();

    // Test db should be empty at start of test.
    EXPECT_EQ(0U, ListCertsInSlot(slot_->os_module_handle()).size());
  }

  virtual void TearDown() {
    // Don't try to cleanup if the setup failed.
    ASSERT_TRUE(slot_->os_module_handle());

    EXPECT_TRUE(CleanupSlotContents(slot_->os_module_handle()));

    // Run the message loop to process any observer callbacks (e.g. for the
    // ClientSocketFactory singleton) so that the scoped ref ptrs created in
    // CertDatabase::NotifyObservers* get released.
    MessageLoop::current()->RunAllPending();

    EXPECT_EQ(0U, ListCertsInSlot(slot_->os_module_handle()).size());
  }

 protected:
  static std::string ReadTestFile(const std::string& name) {
    std::string result;
    FilePath cert_path = GetTestCertsDirectory().AppendASCII(name);
    EXPECT_TRUE(file_util::ReadFileToString(cert_path, &result));
    return result;
  }

  static bool ReadCertIntoList(const std::string& name,
                               CertificateList* certs) {
    std::string cert_data = ReadTestFile(name);
    if (cert_data.empty())
      return false;

    X509Certificate* cert = X509Certificate::CreateFromBytes(
        cert_data.data(), cert_data.size());
    if (!cert)
      return false;

    certs->push_back(cert);
    return true;
  }

  static CertificateList ListCertsInSlot(PK11SlotInfo* slot) {
    CertificateList result;
    CERTCertList* cert_list = PK11_ListCertsInSlot(slot);
    for (CERTCertListNode* node = CERT_LIST_HEAD(cert_list);
         !CERT_LIST_END(node, cert_list);
         node = CERT_LIST_NEXT(node)) {
      result.push_back(X509Certificate::CreateFromHandle(
          node->cert, X509Certificate::OSCertHandles()));
    }
    CERT_DestroyCertList(cert_list);

    // Sort the result so that test comparisons can be deterministic.
    std::sort(result.begin(), result.end(), X509Certificate::LessThan());
    return result;
  }

  scoped_refptr<CryptoModule> slot_;
  CertDatabase cert_db_;

 private:
  // Returns a FilePath object representing the src/net/data/ssl/certificates
  // directory in the source tree.
  static FilePath GetTestCertsDirectory() {
    FilePath certs_dir;
    PathService::Get(base::DIR_SOURCE_ROOT, &certs_dir);
    certs_dir = certs_dir.AppendASCII("net");
    certs_dir = certs_dir.AppendASCII("data");
    certs_dir = certs_dir.AppendASCII("ssl");
    certs_dir = certs_dir.AppendASCII("certificates");
    return certs_dir;
  }

  static bool CleanupSlotContents(PK11SlotInfo* slot) {
    CertDatabase cert_db;
    bool ok = true;
    CertificateList certs = ListCertsInSlot(slot);
    for (size_t i = 0; i < certs.size(); ++i) {
      if (!cert_db.DeleteCertAndKey(certs[i]))
        ok = false;
    }
    return ok;
  }
};

TEST_F(CertDatabaseNSSTest, ListCerts) {
  // This test isn't terribly useful, though it will at least let valgrind test
  // for leaks.
  CertificateList certs;
  cert_db_.ListCerts(&certs);
  // The test DB is empty, but let's assume there will always be something in
  // the other slots.
  EXPECT_LT(0U, certs.size());
}

TEST_F(CertDatabaseNSSTest, ImportFromPKCS12WrongPassword) {
  std::string pkcs12_data = ReadTestFile("client.p12");

  EXPECT_EQ(ERR_PKCS12_IMPORT_BAD_PASSWORD,
            cert_db_.ImportFromPKCS12(slot_,
                                      pkcs12_data,
                                      string16(),
                                      true,  // is_extractable
                                      NULL));

  // Test db should still be empty.
  EXPECT_EQ(0U, ListCertsInSlot(slot_->os_module_handle()).size());
}

TEST_F(CertDatabaseNSSTest, ImportFromPKCS12AsExtractableAndExportAgain) {
  std::string pkcs12_data = ReadTestFile("client.p12");

  EXPECT_EQ(OK, cert_db_.ImportFromPKCS12(slot_,
                                          pkcs12_data,
                                          ASCIIToUTF16("12345"),
                                          true,  // is_extractable
                                          NULL));

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());
  scoped_refptr<X509Certificate> cert(cert_list[0]);

  EXPECT_EQ("testusercert",
            cert->subject().common_name);

  // TODO(mattm): move export test to separate test case?
  std::string exported_data;
  EXPECT_EQ(1, cert_db_.ExportToPKCS12(cert_list, ASCIIToUTF16("exportpw"),
                                       &exported_data));
  ASSERT_LT(0U, exported_data.size());
  // TODO(mattm): further verification of exported data?
}

TEST_F(CertDatabaseNSSTest, ImportFromPKCS12Twice) {
  std::string pkcs12_data = ReadTestFile("client.p12");

  EXPECT_EQ(OK, cert_db_.ImportFromPKCS12(slot_,
                                          pkcs12_data,
                                          ASCIIToUTF16("12345"),
                                          true,  // is_extractable
                                          NULL));
  EXPECT_EQ(1U, ListCertsInSlot(slot_->os_module_handle()).size());

  // NSS has a SEC_ERROR_PKCS12_DUPLICATE_DATA error, but it doesn't look like
  // it's ever used.  This test verifies that.
  EXPECT_EQ(OK, cert_db_.ImportFromPKCS12(slot_,
                                          pkcs12_data,
                                          ASCIIToUTF16("12345"),
                                          true,  // is_extractable
                                          NULL));
  EXPECT_EQ(1U, ListCertsInSlot(slot_->os_module_handle()).size());
}

TEST_F(CertDatabaseNSSTest, ImportFromPKCS12AsUnextractableAndExportAgain) {
  std::string pkcs12_data = ReadTestFile("client.p12");

  EXPECT_EQ(OK, cert_db_.ImportFromPKCS12(slot_,
                                          pkcs12_data,
                                          ASCIIToUTF16("12345"),
                                          false,  // is_extractable
                                          NULL));

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());
  scoped_refptr<X509Certificate> cert(cert_list[0]);

  EXPECT_EQ("testusercert",
            cert->subject().common_name);

  std::string exported_data;
  EXPECT_EQ(0, cert_db_.ExportToPKCS12(cert_list, ASCIIToUTF16("exportpw"),
                                       &exported_data));
}

// Importing a PKCS#12 file with a certificate but no corresponding
// private key should not mark an existing private key as unextractable.
TEST_F(CertDatabaseNSSTest, ImportFromPKCS12OnlyMarkIncludedKey) {
  std::string pkcs12_data = ReadTestFile("client.p12");
  EXPECT_EQ(OK, cert_db_.ImportFromPKCS12(slot_,
                                          pkcs12_data,
                                          ASCIIToUTF16("12345"),
                                          true,  // is_extractable
                                          NULL));

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());

  // Now import a PKCS#12 file with just a certificate but no private key.
  pkcs12_data = ReadTestFile("client-nokey.p12");
  EXPECT_EQ(OK, cert_db_.ImportFromPKCS12(slot_,
                                          pkcs12_data,
                                          ASCIIToUTF16("12345"),
                                          false,  // is_extractable
                                          NULL));

  cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());

  // Make sure the imported private key is still extractable.
  std::string exported_data;
  EXPECT_EQ(1, cert_db_.ExportToPKCS12(cert_list, ASCIIToUTF16("exportpw"),
                                       &exported_data));
  ASSERT_LT(0U, exported_data.size());
}

TEST_F(CertDatabaseNSSTest, ImportFromPKCS12InvalidFile) {
  std::string pkcs12_data = "Foobarbaz";

  EXPECT_EQ(ERR_PKCS12_IMPORT_INVALID_FILE,
            cert_db_.ImportFromPKCS12(slot_,
                                      pkcs12_data,
                                      string16(),
                                      true,  // is_extractable
                                      NULL));

  // Test db should still be empty.
  EXPECT_EQ(0U, ListCertsInSlot(slot_->os_module_handle()).size());
}

TEST_F(CertDatabaseNSSTest, ImportCACert_SSLTrust) {
  std::string cert_data = ReadTestFile("root_ca_cert.crt");

  CertificateList certs =
      X509Certificate::CreateCertificateListFromBytes(
          cert_data.data(), cert_data.size(), X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, certs.size());
  EXPECT_FALSE(certs[0]->os_cert_handle()->isperm);

  // Import it.
  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportCACerts(certs, CertDatabase::TRUSTED_SSL,
                                     &failed));

  EXPECT_EQ(0U, failed.size());

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());
  scoped_refptr<X509Certificate> cert(cert_list[0]);
  EXPECT_EQ("Test CA", cert->subject().common_name);

  EXPECT_EQ(CertDatabase::TRUSTED_SSL,
            cert_db_.GetCertTrust(cert.get(), CA_CERT));

  psm::nsNSSCertTrust trust(cert->os_cert_handle()->trust);
  EXPECT_TRUE(trust.HasTrustedCA(PR_TRUE, PR_FALSE, PR_FALSE));
  EXPECT_FALSE(trust.HasTrustedCA(PR_FALSE, PR_TRUE, PR_FALSE));
  EXPECT_FALSE(trust.HasTrustedCA(PR_FALSE, PR_FALSE, PR_TRUE));
  EXPECT_FALSE(trust.HasTrustedCA(PR_TRUE, PR_TRUE, PR_TRUE));
  EXPECT_TRUE(trust.HasCA(PR_TRUE, PR_TRUE, PR_TRUE));
}

TEST_F(CertDatabaseNSSTest, ImportCACert_EmailTrust) {
  std::string cert_data = ReadTestFile("root_ca_cert.crt");

  CertificateList certs =
      X509Certificate::CreateCertificateListFromBytes(
          cert_data.data(), cert_data.size(), X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, certs.size());
  EXPECT_FALSE(certs[0]->os_cert_handle()->isperm);

  // Import it.
  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportCACerts(certs, CertDatabase::TRUSTED_EMAIL,
                                     &failed));

  EXPECT_EQ(0U, failed.size());

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());
  scoped_refptr<X509Certificate> cert(cert_list[0]);
  EXPECT_EQ("Test CA", cert->subject().common_name);

  EXPECT_EQ(CertDatabase::TRUSTED_EMAIL,
            cert_db_.GetCertTrust(cert.get(), CA_CERT));

  psm::nsNSSCertTrust trust(cert->os_cert_handle()->trust);
  EXPECT_FALSE(trust.HasTrustedCA(PR_TRUE, PR_FALSE, PR_FALSE));
  EXPECT_TRUE(trust.HasTrustedCA(PR_FALSE, PR_TRUE, PR_FALSE));
  EXPECT_FALSE(trust.HasTrustedCA(PR_FALSE, PR_FALSE, PR_TRUE));
  EXPECT_TRUE(trust.HasCA(PR_TRUE, PR_TRUE, PR_TRUE));
}

TEST_F(CertDatabaseNSSTest, ImportCACert_ObjSignTrust) {
  std::string cert_data = ReadTestFile("root_ca_cert.crt");

  CertificateList certs =
      X509Certificate::CreateCertificateListFromBytes(
          cert_data.data(), cert_data.size(), X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, certs.size());
  EXPECT_FALSE(certs[0]->os_cert_handle()->isperm);

  // Import it.
  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportCACerts(certs, CertDatabase::TRUSTED_OBJ_SIGN,
                                     &failed));

  EXPECT_EQ(0U, failed.size());

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());
  scoped_refptr<X509Certificate> cert(cert_list[0]);
  EXPECT_EQ("Test CA", cert->subject().common_name);

  EXPECT_EQ(CertDatabase::TRUSTED_OBJ_SIGN,
            cert_db_.GetCertTrust(cert.get(), CA_CERT));

  psm::nsNSSCertTrust trust(cert->os_cert_handle()->trust);
  EXPECT_FALSE(trust.HasTrustedCA(PR_TRUE, PR_FALSE, PR_FALSE));
  EXPECT_FALSE(trust.HasTrustedCA(PR_FALSE, PR_TRUE, PR_FALSE));
  EXPECT_TRUE(trust.HasTrustedCA(PR_FALSE, PR_FALSE, PR_TRUE));
  EXPECT_TRUE(trust.HasCA(PR_TRUE, PR_TRUE, PR_TRUE));
}

TEST_F(CertDatabaseNSSTest, ImportCA_NotCACert) {
  std::string cert_data = ReadTestFile("google.single.pem");

  CertificateList certs =
      X509Certificate::CreateCertificateListFromBytes(
          cert_data.data(), cert_data.size(), X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, certs.size());
  EXPECT_FALSE(certs[0]->os_cert_handle()->isperm);

  // Import it.
  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportCACerts(certs, CertDatabase::TRUSTED_SSL,
                                     &failed));
  ASSERT_EQ(1U, failed.size());
  // Note: this compares pointers directly.  It's okay in this case because
  // ImportCACerts returns the same pointers that were passed in.  In the
  // general case IsSameOSCert should be used.
  EXPECT_EQ(certs[0], failed[0].certificate);
  EXPECT_EQ(ERR_IMPORT_CA_CERT_NOT_CA, failed[0].net_error);

  EXPECT_EQ(0U, ListCertsInSlot(slot_->os_module_handle()).size());
}

TEST_F(CertDatabaseNSSTest, ImportCACertHierarchy) {
  CertificateList certs;
  ASSERT_TRUE(ReadCertIntoList("dod_root_ca_2_cert.der", &certs));
  ASSERT_TRUE(ReadCertIntoList("dod_ca_17_cert.der", &certs));
  ASSERT_TRUE(ReadCertIntoList("www_us_army_mil_cert.der", &certs));

  // Import it.
  CertDatabase::ImportCertFailureList failed;
  // Have to specify email trust for the cert verification of the child cert to
  // work (see
  // http://mxr.mozilla.org/mozilla/source/security/nss/lib/certhigh/certvfy.c#752
  // "XXX This choice of trustType seems arbitrary.")
  EXPECT_TRUE(cert_db_.ImportCACerts(
      certs, CertDatabase::TRUSTED_SSL | CertDatabase::TRUSTED_EMAIL,
      &failed));

  ASSERT_EQ(1U, failed.size());
  EXPECT_EQ("www.us.army.mil", failed[0].certificate->subject().common_name);
  EXPECT_EQ(ERR_IMPORT_CA_CERT_NOT_CA, failed[0].net_error);

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(2U, cert_list.size());
  EXPECT_EQ("DoD Root CA 2", cert_list[0]->subject().common_name);
  EXPECT_EQ("DOD CA-17", cert_list[1]->subject().common_name);
}

TEST_F(CertDatabaseNSSTest, ImportCACertHierarchyDupeRoot) {
  CertificateList certs;
  ASSERT_TRUE(ReadCertIntoList("dod_root_ca_2_cert.der", &certs));

  // First import just the root.
  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportCACerts(
      certs, CertDatabase::TRUSTED_SSL | CertDatabase::TRUSTED_EMAIL,
      &failed));

  EXPECT_EQ(0U, failed.size());
  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());
  EXPECT_EQ("DoD Root CA 2", cert_list[0]->subject().common_name);

  ASSERT_TRUE(ReadCertIntoList("dod_ca_17_cert.der", &certs));
  ASSERT_TRUE(ReadCertIntoList("www_us_army_mil_cert.der", &certs));

  // Now import with the other certs in the list too.  Even though the root is
  // already present, we should still import the rest.
  failed.clear();
  EXPECT_TRUE(cert_db_.ImportCACerts(
      certs, CertDatabase::TRUSTED_SSL | CertDatabase::TRUSTED_EMAIL,
      &failed));

  ASSERT_EQ(2U, failed.size());
  EXPECT_EQ("DoD Root CA 2", failed[0].certificate->subject().common_name);
  EXPECT_EQ(ERR_IMPORT_CERT_ALREADY_EXISTS, failed[0].net_error);
  EXPECT_EQ("www.us.army.mil", failed[1].certificate->subject().common_name);
  EXPECT_EQ(ERR_IMPORT_CA_CERT_NOT_CA, failed[1].net_error);

  cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(2U, cert_list.size());
  EXPECT_EQ("DoD Root CA 2", cert_list[0]->subject().common_name);
  EXPECT_EQ("DOD CA-17", cert_list[1]->subject().common_name);
}

TEST_F(CertDatabaseNSSTest, ImportCACertHierarchyUntrusted) {
  CertificateList certs;
  ASSERT_TRUE(ReadCertIntoList("dod_root_ca_2_cert.der", &certs));
  ASSERT_TRUE(ReadCertIntoList("dod_ca_17_cert.der", &certs));

  // Import it.
  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportCACerts(certs, CertDatabase::UNTRUSTED, &failed));

  ASSERT_EQ(1U, failed.size());
  EXPECT_EQ("DOD CA-17", failed[0].certificate->subject().common_name);
  // TODO(mattm): should check for net error equivalent of
  // SEC_ERROR_UNTRUSTED_ISSUER
  EXPECT_EQ(ERR_FAILED, failed[0].net_error);

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());
  EXPECT_EQ("DoD Root CA 2", cert_list[0]->subject().common_name);
}

TEST_F(CertDatabaseNSSTest, ImportCACertHierarchyTree) {
  CertificateList certs;
  ASSERT_TRUE(ReadCertIntoList("dod_root_ca_2_cert.der", &certs));
  // This certificate is expired. http://crbug.com/111029
  // ASSERT_TRUE(ReadCertIntoList("dod_ca_13_cert.der", &certs));
  ASSERT_TRUE(ReadCertIntoList("dod_ca_17_cert.der", &certs));

  // Import it.
  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportCACerts(
      certs, CertDatabase::TRUSTED_SSL | CertDatabase::TRUSTED_EMAIL,
      &failed));

  EXPECT_EQ(0U, failed.size());

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  // One of the certificates is expired. http://crbug.com/111029
  // ASSERT_EQ(3U, cert_list.size());
  // EXPECT_EQ("DOD CA-13", cert_list[0]->subject().common_name);
  // EXPECT_EQ("DoD Root CA 2", cert_list[1]->subject().common_name);
  // EXPECT_EQ("DOD CA-17", cert_list[2]->subject().common_name);
  ASSERT_EQ(2U, cert_list.size());
  EXPECT_EQ("DoD Root CA 2", cert_list[0]->subject().common_name);
  EXPECT_EQ("DOD CA-17", cert_list[1]->subject().common_name);
}

TEST_F(CertDatabaseNSSTest, ImportCACertNotHierarchy) {
  std::string cert_data = ReadTestFile("root_ca_cert.crt");
  CertificateList certs =
      X509Certificate::CreateCertificateListFromBytes(
          cert_data.data(), cert_data.size(), X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(1U, certs.size());
  ASSERT_TRUE(ReadCertIntoList("dod_ca_13_cert.der", &certs));
  ASSERT_TRUE(ReadCertIntoList("dod_ca_17_cert.der", &certs));

  // Import it.
  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportCACerts(
      certs, CertDatabase::TRUSTED_SSL | CertDatabase::TRUSTED_EMAIL |
      CertDatabase::TRUSTED_OBJ_SIGN, &failed));

  ASSERT_EQ(2U, failed.size());
  // TODO(mattm): should check for net error equivalent of
  // SEC_ERROR_UNKNOWN_ISSUER
  EXPECT_EQ("DOD CA-13", failed[0].certificate->subject().common_name);
  EXPECT_EQ(ERR_FAILED, failed[0].net_error);
  EXPECT_EQ("DOD CA-17", failed[1].certificate->subject().common_name);
  EXPECT_EQ(ERR_FAILED, failed[1].net_error);

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());
  EXPECT_EQ("Test CA", cert_list[0]->subject().common_name);
}

// http://crbug.com/108009 - Disabled, as google.chain.pem is an expired
// certificate.
TEST_F(CertDatabaseNSSTest, DISABLED_ImportServerCert) {
  // Need to import intermediate cert for the verify of google cert, otherwise
  // it will try to fetch it automatically with cert_pi_useAIACertFetch, which
  // will cause OCSPCreateSession on the main thread, which is not allowed.
  std::string cert_data = ReadTestFile("google.chain.pem");
  CertificateList certs =
      X509Certificate::CreateCertificateListFromBytes(
          cert_data.data(), cert_data.size(), X509Certificate::FORMAT_AUTO);
  ASSERT_EQ(2U, certs.size());

  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportServerCert(certs, &failed));

  EXPECT_EQ(0U, failed.size());

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(2U, cert_list.size());
  scoped_refptr<X509Certificate> goog_cert(cert_list[0]);
  scoped_refptr<X509Certificate> thawte_cert(cert_list[1]);
  EXPECT_EQ("www.google.com", goog_cert->subject().common_name);
  EXPECT_EQ("Thawte SGC CA", thawte_cert->subject().common_name);

  EXPECT_EQ(CertDatabase::UNTRUSTED,
            cert_db_.GetCertTrust(goog_cert.get(), SERVER_CERT));
  psm::nsNSSCertTrust goog_trust(goog_cert->os_cert_handle()->trust);
  EXPECT_TRUE(goog_trust.HasPeer(PR_TRUE, PR_TRUE, PR_TRUE));

  int flags = 0;
  CertVerifyResult verify_result;
  int error = goog_cert->Verify("www.google.com", flags, NULL, &verify_result);
  EXPECT_EQ(OK, error);
  EXPECT_EQ(0U, verify_result.cert_status);
}

TEST_F(CertDatabaseNSSTest, ImportServerCert_SelfSigned) {
  CertificateList certs;
  ASSERT_TRUE(ReadCertIntoList("punycodetest.der", &certs));

  CertDatabase::ImportCertFailureList failed;
  EXPECT_TRUE(cert_db_.ImportServerCert(certs, &failed));

  EXPECT_EQ(0U, failed.size());

  CertificateList cert_list = ListCertsInSlot(slot_->os_module_handle());
  ASSERT_EQ(1U, cert_list.size());
  scoped_refptr<X509Certificate> puny_cert(cert_list[0]);

  EXPECT_EQ(CertDatabase::UNTRUSTED,
            cert_db_.GetCertTrust(puny_cert.get(), SERVER_CERT));
  psm::nsNSSCertTrust puny_trust(puny_cert->os_cert_handle()->trust);
  EXPECT_TRUE(puny_trust.HasPeer(PR_TRUE, PR_TRUE, PR_TRUE));

  int flags = 0;
  CertVerifyResult verify_result;
  int error = puny_cert->Verify("xn--wgv71a119e.com", flags, NULL,
                                &verify_result);
  EXPECT_EQ(ERR_CERT_AUTHORITY_INVALID, error);
  EXPECT_EQ(CERT_STATUS_AUTHORITY_INVALID, verify_result.cert_status);

  // TODO(mattm): this should be SERVER_CERT, not CA_CERT, but that does not
  // work due to NSS bug: https://bugzilla.mozilla.org/show_bug.cgi?id=531160
  EXPECT_TRUE(cert_db_.SetCertTrust(
      puny_cert.get(), CA_CERT,
      CertDatabase::TRUSTED_SSL | CertDatabase::TRUSTED_EMAIL));

  verify_result.Reset();
  error = puny_cert->Verify("xn--wgv71a119e.com", flags, NULL, &verify_result);
  EXPECT_EQ(OK, error);
  EXPECT_EQ(0U, verify_result.cert_status);
}

}  // namespace net
