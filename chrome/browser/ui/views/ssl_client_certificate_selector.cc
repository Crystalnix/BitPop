// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/ssl_client_certificate_selector.h"

#include "base/compiler_specific.h"
#include "base/i18n/time_formatting.h"
#include "base/logging.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/certificate_viewer.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/views/constrained_window_views.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/generated_resources.h"
#include "net/base/ssl_cert_request_info.h"
#include "net/base/x509_certificate.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/table_model.h"
#include "ui/base/models/table_model_observer.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/views/controls/button/text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/table/table_view.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"

using content::BrowserThread;

namespace {

// The dimensions of the certificate selector table view, in pixels.
static const int kTableViewWidth = 400;
static const int kTableViewHeight = 100;

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// CertificateSelectorTableModel:

class CertificateSelectorTableModel : public ui::TableModel {
 public:
  explicit CertificateSelectorTableModel(
      net::SSLCertRequestInfo* cert_request_info);

  // ui::TableModel implementation:
  virtual int RowCount() OVERRIDE;
  virtual string16 GetText(int index, int column_id) OVERRIDE;
  virtual void SetObserver(ui::TableModelObserver* observer) OVERRIDE;

 private:
  std::vector<string16> items_;

  DISALLOW_COPY_AND_ASSIGN(CertificateSelectorTableModel);
};

CertificateSelectorTableModel::CertificateSelectorTableModel(
    net::SSLCertRequestInfo* cert_request_info) {
  for (size_t i = 0; i < cert_request_info->client_certs.size(); ++i) {
    net::X509Certificate* cert = cert_request_info->client_certs[i];
    string16 text = l10n_util::GetStringFUTF16(
        IDS_CERT_SELECTOR_TABLE_CERT_FORMAT,
        UTF8ToUTF16(cert->subject().GetDisplayName()),
        UTF8ToUTF16(cert->issuer().GetDisplayName()));
    items_.push_back(text);
  }
}

int CertificateSelectorTableModel::RowCount() {
  return items_.size();
}

string16 CertificateSelectorTableModel::GetText(int index, int column_id) {
  DCHECK_EQ(column_id, 0);
  DCHECK_GE(index, 0);
  DCHECK_LT(index, static_cast<int>(items_.size()));

  return items_[index];
}

void CertificateSelectorTableModel::SetObserver(
    ui::TableModelObserver* observer) {
}

///////////////////////////////////////////////////////////////////////////////
// SSLClientCertificateSelector:

SSLClientCertificateSelector::SSLClientCertificateSelector(
    TabContents* tab_contents,
    const net::HttpNetworkSession* network_session,
    net::SSLCertRequestInfo* cert_request_info,
    const base::Callback<void(net::X509Certificate*)>& callback)
    : SSLClientAuthObserver(network_session, cert_request_info, callback),
      model_(new CertificateSelectorTableModel(cert_request_info)),
      tab_contents_(tab_contents),
      window_(NULL),
      table_(NULL),
      view_cert_button_(NULL),
      view_cert_button_container_(NULL) {
  DVLOG(1) << __FUNCTION__;
}

SSLClientCertificateSelector::~SSLClientCertificateSelector() {
  table_->SetModel(NULL);
}

void SSLClientCertificateSelector::Init() {
  views::GridLayout* layout = views::GridLayout::CreatePanel(this);
  SetLayoutManager(layout);

  const int column_set_id = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(column_set_id);
  column_set->AddColumn(
      views::GridLayout::FILL, views::GridLayout::FILL,
      1, views::GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, column_set_id);
  string16 text = l10n_util::GetStringFUTF16(
      IDS_CLIENT_CERT_DIALOG_TEXT,
      ASCIIToUTF16(cert_request_info()->host_and_port));
  views::Label* label = new views::Label(text);
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  label->SetAllowCharacterBreak(true);
  layout->AddView(label);

  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  CreateCertTable();
  layout->StartRow(1, column_set_id);
  layout->AddView(table_->CreateParentIfNecessary(), 1, 1,
                  views::GridLayout::FILL,
                  views::GridLayout::FILL, kTableViewWidth, kTableViewHeight);

  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  CreateViewCertButton();

  StartObserving();

  window_ = new ConstrainedWindowViews(tab_contents_, this);

  // Select the first row automatically.  This must be done after the dialog has
  // been created.
  table_->Select(0);
}

net::X509Certificate* SSLClientCertificateSelector::GetSelectedCert() const {
  int selected = table_->FirstSelectedRow();
  if (selected >= 0 &&
      selected < static_cast<int>(
          cert_request_info()->client_certs.size()))
    return cert_request_info()->client_certs[selected];
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// SSLClientAuthObserver implementation:

void SSLClientCertificateSelector::OnCertSelectedByNotification() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(window_);
  window_->CloseConstrainedWindow();
}

///////////////////////////////////////////////////////////////////////////////
// DialogDelegateView implementation:

bool SSLClientCertificateSelector::CanResize() const {
  return true;
}

string16 SSLClientCertificateSelector::GetWindowTitle() const {
  return l10n_util::GetStringUTF16(IDS_CLIENT_CERT_DIALOG_TITLE);
}

void SSLClientCertificateSelector::DeleteDelegate() {
  DVLOG(1) << __FUNCTION__;
  delete this;
}

bool SSLClientCertificateSelector::IsDialogButtonEnabled(
    ui::DialogButton button) const {
  if (button == ui::DIALOG_BUTTON_OK)
    return !!GetSelectedCert();
  return true;
}

bool SSLClientCertificateSelector::Cancel() {
  DVLOG(1) << __FUNCTION__;
  StopObserving();
  CertificateSelected(NULL);

  return true;
}

bool SSLClientCertificateSelector::Accept() {
  DVLOG(1) << __FUNCTION__;
  net::X509Certificate* cert = GetSelectedCert();
  if (cert) {
    StopObserving();
    CertificateSelected(cert);
    return true;
  }

  return false;
}

views::View* SSLClientCertificateSelector::GetInitiallyFocusedView() {
  return table_;
}

views::View* SSLClientCertificateSelector::GetContentsView() {
  return this;
}

views::View* SSLClientCertificateSelector::GetExtraView() {
  return view_cert_button_container_;
}

///////////////////////////////////////////////////////////////////////////////
// views::ButtonListener implementation:

void SSLClientCertificateSelector::ButtonPressed(
    views::Button* sender, const views::Event& event) {
  if (sender == view_cert_button_) {
    net::X509Certificate* cert = GetSelectedCert();
    if (cert)
      ShowCertificateViewer(
          tab_contents_->web_contents(),
          tab_contents_->web_contents()->GetView()->GetTopLevelNativeWindow(),
          cert);
  }
}

///////////////////////////////////////////////////////////////////////////////
// views::TableViewObserver implementation:
void SSLClientCertificateSelector::OnSelectionChanged() {
  GetDialogClientView()->ok_button()->SetEnabled(!!GetSelectedCert());
}

void SSLClientCertificateSelector::OnDoubleClick() {
  if (Accept())
    window_->CloseConstrainedWindow();
}

///////////////////////////////////////////////////////////////////////////////
// SSLClientCertificateSelector private methods:

void SSLClientCertificateSelector::CreateCertTable() {
  std::vector<ui::TableColumn> columns;
  columns.push_back(ui::TableColumn());
  table_ = new views::TableView(model_.get(),
                                columns,
                                views::TEXT_ONLY,
                                true,  // single_selection
                                true,  // resizable_columns
                                true);  // autosize_columns
  table_->SetObserver(this);
}

void SSLClientCertificateSelector::CreateViewCertButton() {
  view_cert_button_ = new views::NativeTextButton(this,
      l10n_util::GetStringUTF16(IDS_PAGEINFO_CERT_INFO_BUTTON));

  // Wrap the view cert button in a grid layout in order to left-align it.
  view_cert_button_container_ = new views::View();
  views::GridLayout* layout = new views::GridLayout(
      view_cert_button_container_);
  view_cert_button_container_->SetLayoutManager(layout);

  int column_set_id = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(column_set_id);
  column_set->AddColumn(views::GridLayout::LEADING, views::GridLayout::LEADING,
                        0, views::GridLayout::USE_PREF, 0, 0);
  layout->StartRow(0, column_set_id);
  layout->AddView(view_cert_button_);
}

namespace chrome {

void ShowSSLClientCertificateSelector(
    TabContents* tab_contents,
    const net::HttpNetworkSession* network_session,
    net::SSLCertRequestInfo* cert_request_info,
    const base::Callback<void(net::X509Certificate*)>& callback) {
  DVLOG(1) << __FUNCTION__ << " " << tab_contents;
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  (new SSLClientCertificateSelector(
       tab_contents, network_session, cert_request_info, callback))->Init();
}

}  // namespace chrome
