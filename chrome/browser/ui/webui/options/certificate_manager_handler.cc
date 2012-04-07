// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/certificate_manager_handler.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/file_util.h"  // for FileAccessProvider
#include "base/memory/scoped_vector.h"
#include "base/safe_strerror_posix.h"
#include "base/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/certificate_viewer.h"
#include "chrome/browser/ui/certificate_dialogs.h"
#include "chrome/browser/ui/crypto_module_password_dialog.h"
#include "content/public/browser/browser_thread.h"  // for FileAccessProvider
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/generated_resources.h"
#include "net/base/crypto_module.h"
#include "net/base/net_errors.h"
#include "net/base/x509_certificate.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/l10n_util_collator.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/cryptohome_library.h"
#endif

using content::BrowserThread;

namespace {

static const char kKeyId[] = "id";
static const char kSubNodesId[] = "subnodes";
static const char kNameId[] = "name";
static const char kReadOnlyId[] = "readonly";
static const char kUntrustedId[] = "untrusted";
static const char kSecurityDeviceId[] = "device";
static const char kErrorId[] = "error";

// Enumeration of different callers of SelectFile.  (Start counting at 1 so
// if SelectFile is accidentally called with params=NULL it won't match any.)
enum {
  EXPORT_PERSONAL_FILE_SELECTED = 1,
  IMPORT_PERSONAL_FILE_SELECTED,
  IMPORT_SERVER_FILE_SELECTED,
  IMPORT_CA_FILE_SELECTED,
};

// TODO(mattm): These are duplicated from cookies_view_handler.cc
// Encodes a pointer value into a hex string.
std::string PointerToHexString(const void* pointer) {
  return base::HexEncode(&pointer, sizeof(pointer));
}

// Decodes a pointer from a hex string.
void* HexStringToPointer(const std::string& str) {
  std::vector<uint8> buffer;
  if (!base::HexStringToBytes(str, &buffer) ||
      buffer.size() != sizeof(void*)) {
    return NULL;
  }

  return *reinterpret_cast<void**>(&buffer[0]);
}

std::string OrgNameToId(const std::string& org) {
  return "org-" + org;
}

std::string CertToId(const net::X509Certificate& cert) {
  return "cert-" + PointerToHexString(&cert);
}

net::X509Certificate* IdToCert(const std::string& id) {
  if (!StartsWithASCII(id, "cert-", true))
    return NULL;
  return reinterpret_cast<net::X509Certificate*>(
      HexStringToPointer(id.substr(5)));
}

net::X509Certificate* CallbackArgsToCert(const ListValue* args) {
  std::string node_id;
  if (!args->GetString(0, &node_id)){
    return NULL;
  }
  net::X509Certificate* cert = IdToCert(node_id);
  if (!cert) {
    NOTREACHED();
    return NULL;
  }
  return cert;
}

bool CallbackArgsToBool(const ListValue* args, int index, bool* result) {
  std::string string_value;
  if (!args->GetString(index, &string_value))
    return false;

  *result = string_value[0] == 't';
  return true;
}

struct DictionaryIdComparator {
  explicit DictionaryIdComparator(icu::Collator* collator)
      : collator_(collator) {
  }

  bool operator()(const Value* a,
                  const Value* b) const {
    DCHECK(a->GetType() == Value::TYPE_DICTIONARY);
    DCHECK(b->GetType() == Value::TYPE_DICTIONARY);
    const DictionaryValue* a_dict = reinterpret_cast<const DictionaryValue*>(a);
    const DictionaryValue* b_dict = reinterpret_cast<const DictionaryValue*>(b);
    string16 a_str;
    string16 b_str;
    a_dict->GetString(kNameId, &a_str);
    b_dict->GetString(kNameId, &b_str);
    if (collator_ == NULL)
      return a_str < b_str;
    return l10n_util::CompareString16WithCollator(
        collator_, a_str, b_str) == UCOL_LESS;
  }

  icu::Collator* collator_;
};

std::string NetErrorToString(int net_error) {
  switch (net_error) {
    // TODO(mattm): handle more cases.
    case net::ERR_IMPORT_CA_CERT_NOT_CA:
      return l10n_util::GetStringUTF8(IDS_CERT_MANAGER_ERROR_NOT_CA);
    default:
      return l10n_util::GetStringUTF8(IDS_CERT_MANAGER_UNKNOWN_ERROR);
  }
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
//  FileAccessProvider

// TODO(mattm): Move to some shared location?
class FileAccessProvider
    : public base::RefCountedThreadSafe<FileAccessProvider>,
      public CancelableRequestProvider {
 public:
  // Reports 0 on success or errno on failure, and the data of the file upon
  // success.
  // TODO(mattm): don't pass std::string by value.. could use RefCountedBytes
  // but it's a vector.  Maybe do the derive from CancelableRequest thing
  // described in cancelable_request.h?
  typedef base::Callback<void(int, std::string)> ReadCallback;

  // Reports 0 on success or errno on failure, and the number of bytes written,
  // on success.
  typedef base::Callback<void(int, int)> WriteCallback;

  Handle StartRead(const FilePath& path,
                   CancelableRequestConsumerBase* consumer,
                   const ReadCallback& callback);
  Handle StartWrite(const FilePath& path,
                    const std::string& data,
                    CancelableRequestConsumerBase* consumer,
                    const WriteCallback& callback);

 private:
  void DoRead(scoped_refptr<CancelableRequest<ReadCallback> > request,
              FilePath path);
  void DoWrite(scoped_refptr<CancelableRequest<WriteCallback> > request,
              FilePath path,
              std::string data);
};

CancelableRequestProvider::Handle FileAccessProvider::StartRead(
    const FilePath& path,
    CancelableRequestConsumerBase* consumer,
    const ReadCallback& callback) {
  scoped_refptr<CancelableRequest<ReadCallback> > request(
      new CancelableRequest<ReadCallback>(callback));
  AddRequest(request, consumer);

  // Send the parameters and the request to the file thread.
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&FileAccessProvider::DoRead, this, request, path));

  // The handle will have been set by AddRequest.
  return request->handle();
}

CancelableRequestProvider::Handle FileAccessProvider::StartWrite(
    const FilePath& path,
    const std::string& data,
    CancelableRequestConsumerBase* consumer,
    const WriteCallback& callback) {
  scoped_refptr<CancelableRequest<WriteCallback> > request(
      new CancelableRequest<WriteCallback>(callback));
  AddRequest(request, consumer);

  // Send the parameters and the request to the file thWrite.
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&FileAccessProvider::DoWrite, this, request, path, data));

  // The handle will have been set by AddRequest.
  return request->handle();
}

void FileAccessProvider::DoRead(
    scoped_refptr<CancelableRequest<ReadCallback> > request,
    FilePath path) {
  if (request->canceled())
    return;

  std::string data;
  VLOG(1) << "DoRead starting read";
  bool success = file_util::ReadFileToString(path, &data);
  int saved_errno = success ? 0 : errno;
  VLOG(1) << "DoRead done read: " << success << " " << data.size();
  request->ForwardResult(saved_errno, data);
}

void FileAccessProvider::DoWrite(
    scoped_refptr<CancelableRequest<WriteCallback> > request,
    FilePath path,
    std::string data) {
  VLOG(1) << "DoWrite starting write";
  int bytes_written = file_util::WriteFile(path, data.data(), data.size());
  int saved_errno = bytes_written >= 0 ? 0 : errno;
  VLOG(1) << "DoWrite done write " << bytes_written;

  if (request->canceled())
    return;

  request->ForwardResult(saved_errno, bytes_written);
}

///////////////////////////////////////////////////////////////////////////////
//  CertificateManagerHandler

CertificateManagerHandler::CertificateManagerHandler()
    : file_access_provider_(new FileAccessProvider) {
  certificate_manager_model_.reset(new CertificateManagerModel(this));
}

CertificateManagerHandler::~CertificateManagerHandler() {
}

void CertificateManagerHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  RegisterTitle(localized_strings, "certificateManagerPage",
                IDS_CERTIFICATE_MANAGER_TITLE);

  // Tabs.
  localized_strings->SetString("personalCertsTabTitle",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_PERSONAL_CERTS_TAB_LABEL));
  localized_strings->SetString("serverCertsTabTitle",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_SERVER_CERTS_TAB_LABEL));
  localized_strings->SetString("caCertsTabTitle",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_CERT_AUTHORITIES_TAB_LABEL));
  localized_strings->SetString("unknownCertsTabTitle",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_UNKNOWN_TAB_LABEL));

  // Tab descriptions.
  localized_strings->SetString("personalCertsTabDescription",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_USER_TREE_DESCRIPTION));
  localized_strings->SetString("serverCertsTabDescription",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_SERVER_TREE_DESCRIPTION));
  localized_strings->SetString("caCertsTabDescription",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_AUTHORITIES_TREE_DESCRIPTION));
  localized_strings->SetString("unknownCertsTabDescription",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_UNKNOWN_TREE_DESCRIPTION));

  // Tree columns.
  localized_strings->SetString("certNameColumn",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_NAME_COLUMN_LABEL));
  localized_strings->SetString("certDeviceColumn",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_DEVICE_COLUMN_LABEL));
  localized_strings->SetString("certSerialColumn",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_SERIAL_NUMBER_COLUMN_LABEL));
  localized_strings->SetString("certExpiresColumn",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EXPIRES_COLUMN_LABEL));

  // Buttons.
  localized_strings->SetString("view_certificate",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_VIEW_CERT_BUTTON));
  localized_strings->SetString("import_certificate",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_IMPORT_BUTTON));
  localized_strings->SetString("export_certificate",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EXPORT_BUTTON));
  localized_strings->SetString("export_all_certificates",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EXPORT_ALL_BUTTON));
  localized_strings->SetString("edit_certificate",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EDIT_BUTTON));
  localized_strings->SetString("delete_certificate",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_DELETE_BUTTON));

  // Certificate Delete overlay strings.
  localized_strings->SetString("personalCertsTabDeleteConfirm",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_DELETE_USER_FORMAT));
  localized_strings->SetString("personalCertsTabDeleteImpact",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_DELETE_USER_DESCRIPTION));
  localized_strings->SetString("serverCertsTabDeleteConfirm",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_DELETE_SERVER_FORMAT));
  localized_strings->SetString("serverCertsTabDeleteImpact",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_DELETE_SERVER_DESCRIPTION));
  localized_strings->SetString("caCertsTabDeleteConfirm",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_DELETE_CA_FORMAT));
  localized_strings->SetString("caCertsTabDeleteImpact",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_DELETE_CA_DESCRIPTION));
  localized_strings->SetString("unknownCertsTabDeleteConfirm",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_DELETE_UNKNOWN_FORMAT));
  localized_strings->SetString("unknownCertsTabDeleteImpact", "");

  // Certificate Restore overlay strings.
  localized_strings->SetString("certificateRestorePasswordDescription",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_RESTORE_PASSWORD_DESC));
  localized_strings->SetString("certificatePasswordLabel",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_PASSWORD_LABEL));

  // Personal Certificate Export overlay strings.
  localized_strings->SetString("certificateExportPasswordDescription",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EXPORT_PASSWORD_DESC));
  localized_strings->SetString("certificateExportPasswordHelp",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EXPORT_PASSWORD_HELP));
  localized_strings->SetString("certificateConfirmPasswordLabel",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_CONFIRM_PASSWORD_LABEL));

  // Edit CA Trust & Import CA overlay strings.
  localized_strings->SetString("certificateEditTrustLabel",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EDIT_TRUST_LABEL));
  localized_strings->SetString("certificateEditCaTrustDescriptionFormat",
      l10n_util::GetStringUTF16(
          IDS_CERT_MANAGER_EDIT_CA_TRUST_DESCRIPTION_FORMAT));
  localized_strings->SetString("certificateImportCaDescriptionFormat",
      l10n_util::GetStringUTF16(
          IDS_CERT_MANAGER_IMPORT_CA_DESCRIPTION_FORMAT));
  localized_strings->SetString("certificateCaTrustSSLLabel",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EDIT_CA_TRUST_SSL_LABEL));
  localized_strings->SetString("certificateCaTrustEmailLabel",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EDIT_CA_TRUST_EMAIL_LABEL));
  localized_strings->SetString("certificateCaTrustObjSignLabel",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_EDIT_CA_TRUST_OBJSIGN_LABEL));
  localized_strings->SetString("certificateImportErrorFormat",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_IMPORT_ERROR_FORMAT));

  // Badges next to certificates
  localized_strings->SetString("badgeCertUntrusted",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_UNTRUSTED));

#if defined(OS_CHROMEOS)
  localized_strings->SetString("importAndBindCertificate",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_IMPORT_AND_BIND_BUTTON));
  localized_strings->SetString("hardwareBackedKeyFormat",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_HARDWARE_BACKED_KEY_FORMAT));
  localized_strings->SetString("chromeOSDeviceName",
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_HARDWARE_BACKED));
#endif  // defined(OS_CHROMEOS)
}

void CertificateManagerHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "viewCertificate",
      base::Bind(&CertificateManagerHandler::View, base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "getCaCertificateTrust",
      base::Bind(&CertificateManagerHandler::GetCATrust,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "editCaCertificateTrust",
      base::Bind(&CertificateManagerHandler::EditCATrust,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "editServerCertificate",
      base::Bind(&CertificateManagerHandler::EditServer,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "cancelImportExportCertificate",
      base::Bind(&CertificateManagerHandler::CancelImportExportProcess,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "exportPersonalCertificate",
      base::Bind(&CertificateManagerHandler::ExportPersonal,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "exportAllPersonalCertificates",
      base::Bind(&CertificateManagerHandler::ExportAllPersonal,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "exportPersonalCertificatePasswordSelected",
      base::Bind(&CertificateManagerHandler::ExportPersonalPasswordSelected,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "importPersonalCertificate",
      base::Bind(&CertificateManagerHandler::StartImportPersonal,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "importPersonalCertificatePasswordSelected",
      base::Bind(&CertificateManagerHandler::ImportPersonalPasswordSelected,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "importCaCertificate",
      base::Bind(&CertificateManagerHandler::ImportCA,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "importCaCertificateTrustSelected",
      base::Bind(&CertificateManagerHandler::ImportCATrustSelected,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "importServerCertificate",
      base::Bind(&CertificateManagerHandler::ImportServer,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "exportCertificate",
      base::Bind(&CertificateManagerHandler::Export,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "deleteCertificate",
      base::Bind(&CertificateManagerHandler::Delete,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "populateCertificateManager",
      base::Bind(&CertificateManagerHandler::Populate,
                 base::Unretained(this)));

#if defined(OS_CHROMEOS)
  web_ui()->RegisterMessageCallback(
      "checkTpmTokenReady",
      base::Bind(&CertificateManagerHandler::CheckTpmTokenReady,
                 base::Unretained(this)));
#endif
}

void CertificateManagerHandler::CertificatesRefreshed() {
  PopulateTree("personalCertsTab", net::USER_CERT);
  PopulateTree("serverCertsTab", net::SERVER_CERT);
  PopulateTree("caCertsTab", net::CA_CERT);
  PopulateTree("otherCertsTab", net::UNKNOWN_CERT);
  VLOG(1) << "populating finished";
}

void CertificateManagerHandler::FileSelected(const FilePath& path, int index,
                                             void* params) {
  switch (reinterpret_cast<intptr_t>(params)) {
    case EXPORT_PERSONAL_FILE_SELECTED:
      ExportPersonalFileSelected(path);
      break;
    case IMPORT_PERSONAL_FILE_SELECTED:
      ImportPersonalFileSelected(path);
      break;
    case IMPORT_SERVER_FILE_SELECTED:
      ImportServerFileSelected(path);
      break;
    case IMPORT_CA_FILE_SELECTED:
      ImportCAFileSelected(path);
      break;
    default:
      NOTREACHED();
  }
}

void CertificateManagerHandler::FileSelectionCanceled(void* params) {
  switch (reinterpret_cast<intptr_t>(params)) {
    case EXPORT_PERSONAL_FILE_SELECTED:
    case IMPORT_PERSONAL_FILE_SELECTED:
    case IMPORT_SERVER_FILE_SELECTED:
    case IMPORT_CA_FILE_SELECTED:
      ImportExportCleanup();
      break;
    default:
      NOTREACHED();
  }
}

void CertificateManagerHandler::View(const ListValue* args) {
  net::X509Certificate* cert = CallbackArgsToCert(args);
  if (!cert)
    return;
  ShowCertificateViewer(GetParentWindow(), cert);
}

void CertificateManagerHandler::GetCATrust(const ListValue* args) {
  net::X509Certificate* cert = CallbackArgsToCert(args);
  if (!cert) {
    web_ui()->CallJavascriptFunction("CertificateEditCaTrustOverlay.dismiss");
    return;
  }

  net::CertDatabase::TrustBits trust_bits =
      certificate_manager_model_->cert_db().GetCertTrust(cert, net::CA_CERT);
  base::FundamentalValue ssl_value(
      static_cast<bool>(trust_bits & net::CertDatabase::TRUSTED_SSL));
  base::FundamentalValue email_value(
      static_cast<bool>(trust_bits & net::CertDatabase::TRUSTED_EMAIL));
  base::FundamentalValue obj_sign_value(
      static_cast<bool>(trust_bits & net::CertDatabase::TRUSTED_OBJ_SIGN));
  web_ui()->CallJavascriptFunction(
      "CertificateEditCaTrustOverlay.populateTrust",
      ssl_value, email_value, obj_sign_value);
}

void CertificateManagerHandler::EditCATrust(const ListValue* args) {
  net::X509Certificate* cert = CallbackArgsToCert(args);
  bool fail = !cert;
  bool trust_ssl = false;
  bool trust_email = false;
  bool trust_obj_sign = false;
  fail |= !CallbackArgsToBool(args, 1, &trust_ssl);
  fail |= !CallbackArgsToBool(args, 2, &trust_email);
  fail |= !CallbackArgsToBool(args, 3, &trust_obj_sign);
  if (fail) {
    LOG(ERROR) << "EditCATrust args fail";
    web_ui()->CallJavascriptFunction("CertificateEditCaTrustOverlay.dismiss");
    return;
  }

  bool result = certificate_manager_model_->SetCertTrust(
      cert,
      net::CA_CERT,
      trust_ssl * net::CertDatabase::TRUSTED_SSL +
          trust_email * net::CertDatabase::TRUSTED_EMAIL +
          trust_obj_sign * net::CertDatabase::TRUSTED_OBJ_SIGN);
  web_ui()->CallJavascriptFunction("CertificateEditCaTrustOverlay.dismiss");
  if (!result) {
    // TODO(mattm): better error messages?
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_SET_TRUST_ERROR_TITLE),
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_UNKNOWN_ERROR));
  }
}

void CertificateManagerHandler::EditServer(const ListValue* args) {
  NOTIMPLEMENTED();
}

void CertificateManagerHandler::ExportPersonal(const ListValue* args) {
  net::X509Certificate* cert = CallbackArgsToCert(args);
  if (!cert)
    return;

  selected_cert_list_.push_back(cert);

  SelectFileDialog::FileTypeInfo file_type_info;
  file_type_info.extensions.resize(1);
  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("p12"));
  file_type_info.extension_description_overrides.push_back(
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_PKCS12_FILES));
  file_type_info.include_all_files = true;
  select_file_dialog_ = SelectFileDialog::Create(this);
  select_file_dialog_->SelectFile(
      SelectFileDialog::SELECT_SAVEAS_FILE, string16(),
      FilePath(), &file_type_info, 1, FILE_PATH_LITERAL("p12"),
      web_ui()->GetWebContents(), GetParentWindow(),
      reinterpret_cast<void*>(EXPORT_PERSONAL_FILE_SELECTED));
}

void CertificateManagerHandler::ExportAllPersonal(const ListValue* args) {
  NOTIMPLEMENTED();
}

void CertificateManagerHandler::ExportPersonalFileSelected(
    const FilePath& path) {
  file_path_ = path;
  web_ui()->CallJavascriptFunction(
      "CertificateManager.exportPersonalAskPassword");
}

void CertificateManagerHandler::ExportPersonalPasswordSelected(
    const ListValue* args) {
  if (!args->GetString(0, &password_)){
    web_ui()->CallJavascriptFunction("CertificateRestoreOverlay.dismiss");
    ImportExportCleanup();
    return;
  }

  // Currently, we don't support exporting more than one at a time.  If we do,
  // this would need to either change this to use UnlockSlotsIfNecessary or
  // change UnlockCertSlotIfNecessary to take a CertificateList.
  DCHECK_EQ(selected_cert_list_.size(), 1U);

  // TODO(mattm): do something smarter about non-extractable keys
  browser::UnlockCertSlotIfNecessary(
      selected_cert_list_[0].get(),
      browser::kCryptoModulePasswordCertExport,
      "",  // unused.
      base::Bind(&CertificateManagerHandler::ExportPersonalSlotsUnlocked,
                 base::Unretained(this)));
}

void CertificateManagerHandler::ExportPersonalSlotsUnlocked() {
  std::string output;
  int num_exported = certificate_manager_model_->cert_db().ExportToPKCS12(
      selected_cert_list_,
      password_,
      &output);
  if (!num_exported) {
    web_ui()->CallJavascriptFunction("CertificateRestoreOverlay.dismiss");
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_PKCS12_EXPORT_ERROR_TITLE),
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_UNKNOWN_ERROR));
    ImportExportCleanup();
    return;
  }
  file_access_provider_->StartWrite(
      file_path_, output, &consumer_,
      base::Bind(&CertificateManagerHandler::ExportPersonalFileWritten,
                 base::Unretained(this)));
}

void CertificateManagerHandler::ExportPersonalFileWritten(int write_errno,
                                                          int bytes_written) {
  web_ui()->CallJavascriptFunction("CertificateRestoreOverlay.dismiss");
  ImportExportCleanup();
  if (write_errno) {
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_PKCS12_EXPORT_ERROR_TITLE),
        l10n_util::GetStringFUTF8(IDS_CERT_MANAGER_WRITE_ERROR_FORMAT,
                                  UTF8ToUTF16(safe_strerror(write_errno))));
  }
}

void CertificateManagerHandler::StartImportPersonal(const ListValue* args) {
  SelectFileDialog::FileTypeInfo file_type_info;
  if (!args->GetBoolean(0, &use_hardware_backed_)){
    // Unable to retrieve the hardware backed attribute from the args,
    // so bail.
    web_ui()->CallJavascriptFunction("CertificateRestoreOverlay.dismiss");
    ImportExportCleanup();
    return;
  }
  file_type_info.extensions.resize(1);
  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("p12"));
  file_type_info.extension_description_overrides.push_back(
      l10n_util::GetStringUTF16(IDS_CERT_MANAGER_PKCS12_FILES));
  file_type_info.include_all_files = true;
  select_file_dialog_ = SelectFileDialog::Create(this);
  select_file_dialog_->SelectFile(
      SelectFileDialog::SELECT_OPEN_FILE, string16(),
      FilePath(), &file_type_info, 1, FILE_PATH_LITERAL("p12"),
      web_ui()->GetWebContents(), GetParentWindow(),
      reinterpret_cast<void*>(IMPORT_PERSONAL_FILE_SELECTED));
}

void CertificateManagerHandler::ImportPersonalFileSelected(
    const FilePath& path) {
  file_path_ = path;
  web_ui()->CallJavascriptFunction(
      "CertificateManager.importPersonalAskPassword");
}

void CertificateManagerHandler::ImportPersonalPasswordSelected(
    const ListValue* args) {
  if (!args->GetString(0, &password_)){
    web_ui()->CallJavascriptFunction("CertificateRestoreOverlay.dismiss");
    ImportExportCleanup();
    return;
  }
  file_access_provider_->StartRead(
      file_path_, &consumer_,
      base::Bind(&CertificateManagerHandler::ImportPersonalFileRead,
                 base::Unretained(this)));
}

void CertificateManagerHandler::ImportPersonalFileRead(
    int read_errno, std::string data) {
  if (read_errno) {
    ImportExportCleanup();
    web_ui()->CallJavascriptFunction("CertificateRestoreOverlay.dismiss");
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_PKCS12_IMPORT_ERROR_TITLE),
        l10n_util::GetStringFUTF8(IDS_CERT_MANAGER_READ_ERROR_FORMAT,
                                  UTF8ToUTF16(safe_strerror(read_errno))));
    return;
  }

  file_data_ = data;

  if (use_hardware_backed_) {
    module_ = certificate_manager_model_->cert_db().GetPrivateModule();
  } else {
    module_ = certificate_manager_model_->cert_db().GetPublicModule();
  }

  net::CryptoModuleList modules;
  modules.push_back(module_);
  browser::UnlockSlotsIfNecessary(
      modules,
      browser::kCryptoModulePasswordCertImport,
      "",  // unused.
      base::Bind(&CertificateManagerHandler::ImportPersonalSlotUnlocked,
                 base::Unretained(this)));
}

void CertificateManagerHandler::ImportPersonalSlotUnlocked() {
  // Determine if the private key should be unextractable after the import.
  // We do this by checking the value of |use_hardware_backed_| which is set
  // to true if importing into a hardware module. Currently, this only happens
  // for Chrome OS when the "Import and Bind" option is chosen.
  bool is_extractable = !use_hardware_backed_;
  int result = certificate_manager_model_->ImportFromPKCS12(
      module_, file_data_, password_, is_extractable);
  ImportExportCleanup();
  web_ui()->CallJavascriptFunction("CertificateRestoreOverlay.dismiss");
  int string_id;
  switch (result) {
    case net::OK:
      return;
    case net::ERR_PKCS12_IMPORT_BAD_PASSWORD:
      // TODO(mattm): if the error was a bad password, we should reshow the
      // password dialog after the user dismisses the error dialog.
      string_id = IDS_CERT_MANAGER_BAD_PASSWORD;
      break;
    case net::ERR_PKCS12_IMPORT_INVALID_MAC:
      string_id = IDS_CERT_MANAGER_PKCS12_IMPORT_INVALID_MAC;
      break;
    case net::ERR_PKCS12_IMPORT_INVALID_FILE:
      string_id = IDS_CERT_MANAGER_PKCS12_IMPORT_INVALID_FILE;
      break;
    case net::ERR_PKCS12_IMPORT_UNSUPPORTED:
      string_id = IDS_CERT_MANAGER_PKCS12_IMPORT_UNSUPPORTED;
      break;
    default:
      string_id = IDS_CERT_MANAGER_UNKNOWN_ERROR;
      break;
  }
  ShowError(
      l10n_util::GetStringUTF8(IDS_CERT_MANAGER_PKCS12_IMPORT_ERROR_TITLE),
      l10n_util::GetStringUTF8(string_id));
}

void CertificateManagerHandler::CancelImportExportProcess(
    const ListValue* args) {
  ImportExportCleanup();
}

void CertificateManagerHandler::ImportExportCleanup() {
  file_path_.clear();
  password_.clear();
  file_data_.clear();
  use_hardware_backed_ = false;
  selected_cert_list_.clear();
  module_ = NULL;

  // There may be pending file dialogs, we need to tell them that we've gone
  // away so they don't try and call back to us.
  if (select_file_dialog_.get())
    select_file_dialog_->ListenerDestroyed();
  select_file_dialog_ = NULL;
}

void CertificateManagerHandler::ImportServer(const ListValue* args) {
  select_file_dialog_ = SelectFileDialog::Create(this);
  ShowCertSelectFileDialog(
      select_file_dialog_.get(),
      SelectFileDialog::SELECT_OPEN_FILE,
      FilePath(),
      web_ui()->GetWebContents(),
      GetParentWindow(),
      reinterpret_cast<void*>(IMPORT_SERVER_FILE_SELECTED));
}

void CertificateManagerHandler::ImportServerFileSelected(const FilePath& path) {
  file_path_ = path;
  file_access_provider_->StartRead(
      file_path_, &consumer_,
      base::Bind(&CertificateManagerHandler::ImportServerFileRead,
                 base::Unretained(this)));
}

void CertificateManagerHandler::ImportServerFileRead(int read_errno,
                                                     std::string data) {
  if (read_errno) {
    ImportExportCleanup();
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_SERVER_IMPORT_ERROR_TITLE),
        l10n_util::GetStringFUTF8(IDS_CERT_MANAGER_READ_ERROR_FORMAT,
                                  UTF8ToUTF16(safe_strerror(read_errno))));
    return;
  }

  selected_cert_list_ = net::X509Certificate::CreateCertificateListFromBytes(
          data.data(), data.size(), net::X509Certificate::FORMAT_AUTO);
  if (selected_cert_list_.empty()) {
    ImportExportCleanup();
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_SERVER_IMPORT_ERROR_TITLE),
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_CERT_PARSE_ERROR));
    return;
  }

  net::CertDatabase::ImportCertFailureList not_imported;
  bool result = certificate_manager_model_->ImportServerCert(
      selected_cert_list_,
      &not_imported);
  if (!result) {
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_SERVER_IMPORT_ERROR_TITLE),
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_UNKNOWN_ERROR));
  } else if (!not_imported.empty()) {
    ShowImportErrors(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_SERVER_IMPORT_ERROR_TITLE),
        not_imported);
  }
  ImportExportCleanup();
}

void CertificateManagerHandler::ImportCA(const ListValue* args) {
  select_file_dialog_ = SelectFileDialog::Create(this);
  ShowCertSelectFileDialog(select_file_dialog_.get(),
                           SelectFileDialog::SELECT_OPEN_FILE,
                           FilePath(),
                           web_ui()->GetWebContents(),
                           GetParentWindow(),
                           reinterpret_cast<void*>(IMPORT_CA_FILE_SELECTED));
}

void CertificateManagerHandler::ImportCAFileSelected(const FilePath& path) {
  file_path_ = path;
  file_access_provider_->StartRead(
      file_path_, &consumer_,
      base::Bind(&CertificateManagerHandler::ImportCAFileRead,
                 base::Unretained(this)));
}

void CertificateManagerHandler::ImportCAFileRead(int read_errno,
                                                 std::string data) {
  if (read_errno) {
    ImportExportCleanup();
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_CA_IMPORT_ERROR_TITLE),
        l10n_util::GetStringFUTF8(IDS_CERT_MANAGER_READ_ERROR_FORMAT,
                                  UTF8ToUTF16(safe_strerror(read_errno))));
    return;
  }

  selected_cert_list_ = net::X509Certificate::CreateCertificateListFromBytes(
          data.data(), data.size(), net::X509Certificate::FORMAT_AUTO);
  if (selected_cert_list_.empty()) {
    ImportExportCleanup();
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_CA_IMPORT_ERROR_TITLE),
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_CERT_PARSE_ERROR));
    return;
  }

  scoped_refptr<net::X509Certificate> root_cert =
      certificate_manager_model_->cert_db().FindRootInList(selected_cert_list_);

  // TODO(mattm): check here if root_cert is not a CA cert and show error.

  StringValue cert_name(root_cert->subject().GetDisplayName());
  web_ui()->CallJavascriptFunction("CertificateEditCaTrustOverlay.showImport",
                                   cert_name);
}

void CertificateManagerHandler::ImportCATrustSelected(const ListValue* args) {
  bool fail = false;
  bool trust_ssl = false;
  bool trust_email = false;
  bool trust_obj_sign = false;
  fail |= !CallbackArgsToBool(args, 0, &trust_ssl);
  fail |= !CallbackArgsToBool(args, 1, &trust_email);
  fail |= !CallbackArgsToBool(args, 2, &trust_obj_sign);
  if (fail) {
    LOG(ERROR) << "ImportCATrustSelected args fail";
    ImportExportCleanup();
    web_ui()->CallJavascriptFunction("CertificateEditCaTrustOverlay.dismiss");
    return;
  }

  net::CertDatabase::ImportCertFailureList not_imported;
  bool result = certificate_manager_model_->ImportCACerts(
      selected_cert_list_,
      trust_ssl * net::CertDatabase::TRUSTED_SSL +
          trust_email * net::CertDatabase::TRUSTED_EMAIL +
          trust_obj_sign * net::CertDatabase::TRUSTED_OBJ_SIGN,
      &not_imported);
  web_ui()->CallJavascriptFunction("CertificateEditCaTrustOverlay.dismiss");
  if (!result) {
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_CA_IMPORT_ERROR_TITLE),
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_UNKNOWN_ERROR));
  } else if (!not_imported.empty()) {
    ShowImportErrors(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_CA_IMPORT_ERROR_TITLE),
        not_imported);
  }
  ImportExportCleanup();
}

void CertificateManagerHandler::Export(const ListValue* args) {
  net::X509Certificate* cert = CallbackArgsToCert(args);
  if (!cert)
    return;
  ShowCertExportDialog(web_ui()->GetWebContents(), GetParentWindow(),
                       cert->os_cert_handle());
}

void CertificateManagerHandler::Delete(const ListValue* args) {
  net::X509Certificate* cert = CallbackArgsToCert(args);
  if (!cert)
    return;
  bool result = certificate_manager_model_->Delete(cert);
  if (!result) {
    // TODO(mattm): better error messages?
    ShowError(
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_DELETE_CERT_ERROR_TITLE),
        l10n_util::GetStringUTF8(IDS_CERT_MANAGER_UNKNOWN_ERROR));
  }
}

void CertificateManagerHandler::Populate(const ListValue* args) {
  certificate_manager_model_->Refresh();
}

void CertificateManagerHandler::PopulateTree(const std::string& tab_name,
                                             net::CertType type) {
  const std::string tree_name = tab_name + "-tree";

  scoped_ptr<icu::Collator> collator;
  UErrorCode error = U_ZERO_ERROR;
  collator.reset(
      icu::Collator::createInstance(
          icu::Locale(g_browser_process->GetApplicationLocale().c_str()),
          error));
  if (U_FAILURE(error))
    collator.reset(NULL);
  DictionaryIdComparator comparator(collator.get());
  CertificateManagerModel::OrgGroupingMap map;

  certificate_manager_model_->FilterAndBuildOrgGroupingMap(type, &map);

  {
    ListValue* nodes = new ListValue;
    for (CertificateManagerModel::OrgGroupingMap::iterator i = map.begin();
         i != map.end(); ++i) {
      // Populate first level (org name).
      DictionaryValue* dict = new DictionaryValue;
      dict->SetString(kKeyId, OrgNameToId(i->first));
      dict->SetString(kNameId, i->first);

      // Populate second level (certs).
      ListValue* subnodes = new ListValue;
      for (net::CertificateList::const_iterator org_cert_it = i->second.begin();
           org_cert_it != i->second.end(); ++org_cert_it) {
        DictionaryValue* cert_dict = new DictionaryValue;
        net::X509Certificate* cert = org_cert_it->get();
        cert_dict->SetString(kKeyId, CertToId(*cert));
        cert_dict->SetString(kNameId, certificate_manager_model_->GetColumnText(
            *cert, CertificateManagerModel::COL_SUBJECT_NAME));
        cert_dict->SetBoolean(
            kReadOnlyId,
            certificate_manager_model_->cert_db().IsReadOnly(cert));
        cert_dict->SetBoolean(
            kUntrustedId,
            certificate_manager_model_->cert_db().IsUntrusted(cert));
        // TODO(mattm): Other columns.
        subnodes->Append(cert_dict);
      }
      std::sort(subnodes->begin(), subnodes->end(), comparator);

      dict->Set(kSubNodesId, subnodes);
      nodes->Append(dict);
    }
    std::sort(nodes->begin(), nodes->end(), comparator);

    ListValue args;
    args.Append(Value::CreateStringValue(tree_name));
    args.Append(nodes);
    web_ui()->CallJavascriptFunction("CertificateManager.onPopulateTree", args);
  }
}

void CertificateManagerHandler::ShowError(const std::string& title,
                                          const std::string& error) const {
  ScopedVector<const Value> args;
  args.push_back(Value::CreateStringValue(title));
  args.push_back(Value::CreateStringValue(error));
  args.push_back(Value::CreateStringValue(l10n_util::GetStringUTF8(IDS_OK)));
  args.push_back(Value::CreateNullValue());  // cancelTitle
  args.push_back(Value::CreateNullValue());  // okCallback
  args.push_back(Value::CreateNullValue());  // cancelCallback
  web_ui()->CallJavascriptFunction("AlertOverlay.show", args.get());
}

void CertificateManagerHandler::ShowImportErrors(
    const std::string& title,
    const net::CertDatabase::ImportCertFailureList& not_imported) const {
  std::string error;
  if (selected_cert_list_.size() == 1)
    error = l10n_util::GetStringUTF8(
        IDS_CERT_MANAGER_IMPORT_SINGLE_NOT_IMPORTED);
  else if (not_imported.size() == selected_cert_list_.size())
    error = l10n_util::GetStringUTF8(IDS_CERT_MANAGER_IMPORT_ALL_NOT_IMPORTED);
  else
    error = l10n_util::GetStringUTF8(IDS_CERT_MANAGER_IMPORT_SOME_NOT_IMPORTED);

  ListValue cert_error_list;
  for (size_t i = 0; i < not_imported.size(); ++i) {
    const net::CertDatabase::ImportCertFailure& failure = not_imported[i];
    DictionaryValue* dict = new DictionaryValue;
    dict->SetString(kNameId, failure.certificate->subject().GetDisplayName());
    dict->SetString(kErrorId, NetErrorToString(failure.net_error));
    cert_error_list.Append(dict);
  }

  StringValue title_value(title);
  StringValue error_value(error);
  web_ui()->CallJavascriptFunction("CertificateImportErrorOverlay.show",
                                   title_value,
                                   error_value,
                                   cert_error_list);
}

#if defined(OS_CHROMEOS)
void CertificateManagerHandler::CheckTpmTokenReady(const ListValue* args) {
  chromeos::CryptohomeLibrary* cryptohome =
      chromeos::CrosLibrary::Get()->GetCryptohomeLibrary();

  // TODO(xiyuan): Use async way when underlying supports it.
  base::FundamentalValue ready(cryptohome->Pkcs11IsTpmTokenReady());
  web_ui()->CallJavascriptFunction("CertificateManager.onCheckTpmTokenReady",
                                   ready);
}
#endif

gfx::NativeWindow CertificateManagerHandler::GetParentWindow() const {
  return web_ui()->GetWebContents()->GetView()->GetTopLevelNativeWindow();
}
