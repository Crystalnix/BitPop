// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/print_preview_data_source.h"

#include <algorithm>

#include "base/message_loop.h"
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/printing/print_preview_data_service.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/url_constants.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/google_chrome_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

namespace {

void SetLocalizedStrings(DictionaryValue* localized_strings) {
  localized_strings->SetString(std::string("title"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_TITLE));
  localized_strings->SetString(std::string("loading"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_LOADING));
#if defined(GOOGLE_CHROME_BUILD)
  localized_strings->SetString(std::string("noPlugin"),
      l10n_util::GetStringFUTF8(IDS_PRINT_PREVIEW_NO_PLUGIN,
                                ASCIIToUTF16("chrome://plugins/")));
#else
  localized_strings->SetString(std::string("noPlugin"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_NO_PLUGIN));
#endif
  localized_strings->SetString(std::string("previewFailed"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_FAILED));
  localized_strings->SetString(std::string("initiatorTabClosed"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_INITIATOR_TAB_CLOSED));
  localized_strings->SetString(std::string("reopenPage"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_REOPEN_PAGE));

  localized_strings->SetString(std::string("printButton"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_PRINT_BUTTON));
  localized_strings->SetString(std::string("cancelButton"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_CANCEL_BUTTON));
  localized_strings->SetString(std::string("printing"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_PRINTING));

  localized_strings->SetString(std::string("destinationLabel"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_DESTINATION_LABEL));
  localized_strings->SetString(std::string("copiesLabel"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_COPIES_LABEL));
  localized_strings->SetString(std::string("examplePageRangeText"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_EXAMPLE_PAGE_RANGE_TEXT));
  localized_strings->SetString(std::string("invalidNumberOfCopies"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_INVALID_NUMBER_OF_COPIES));
  localized_strings->SetString(std::string("layoutLabel"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_LAYOUT_LABEL));
  localized_strings->SetString(std::string("optionAllPages"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_OPTION_ALL_PAGES));
  localized_strings->SetString(std::string("optionBw"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_OPTION_BW));
  localized_strings->SetString(std::string("optionCollate"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_OPTION_COLLATE));
  localized_strings->SetString(std::string("optionColor"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_OPTION_COLOR));
  localized_strings->SetString(std::string("optionLandscape"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_OPTION_LANDSCAPE));
  localized_strings->SetString(std::string("optionPortrait"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_OPTION_PORTRAIT));
  localized_strings->SetString(std::string("optionTwoSided"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_OPTION_TWO_SIDED));
  localized_strings->SetString(std::string("pagesLabel"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_PAGES_LABEL));
  localized_strings->SetString(std::string("pageRangeTextBox"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_PAGE_RANGE_TEXT));
  localized_strings->SetString(std::string("pageRangeRadio"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_PAGE_RANGE_RADIO));
  localized_strings->SetString(std::string("printToPDF"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_PRINT_TO_PDF));
  localized_strings->SetString(std::string("printPreviewTitleFormat"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_TITLE_FORMAT));
  localized_strings->SetString(std::string("printPreviewSummaryFormatShort"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_SUMMARY_FORMAT_SHORT));
  localized_strings->SetString(std::string("printPreviewSummaryFormatLong"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_SUMMARY_FORMAT_LONG));
  localized_strings->SetString(std::string("printPreviewSheetsLabelSingular"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_SHEETS_LABEL_SINGULAR));
  localized_strings->SetString(std::string("printPreviewSheetsLabelPlural"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_SHEETS_LABEL_PLURAL));
  localized_strings->SetString(std::string("printPreviewPageLabelSingular"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_PAGE_LABEL_SINGULAR));
  localized_strings->SetString(std::string("printPreviewPageLabelPlural"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_PAGE_LABEL_PLURAL));
  localized_strings->SetString(std::string("systemDialogOption"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_SYSTEM_DIALOG_OPTION));
  localized_strings->SetString(std::string("pageRangeInstruction"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_PAGE_RANGE_INSTRUCTION));
  localized_strings->SetString(std::string("copiesInstruction"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_COPIES_INSTRUCTION));
  localized_strings->SetString(std::string("managePrinters"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_MANAGE_PRINTERS));
}

}  // namespace

PrintPreviewDataSource::PrintPreviewDataSource()
    : DataSource(chrome::kChromeUIPrintHost, MessageLoop::current()) {
}

PrintPreviewDataSource::~PrintPreviewDataSource() {
}

void PrintPreviewDataSource::StartDataRequest(const std::string& path,
                                              bool is_incognito,
                                              int request_id) {
  scoped_refptr<RefCountedBytes> data(new RefCountedBytes);

  bool preview_data_requested = EndsWith(path, "/print.pdf", true);
  if (preview_data_requested) {
    size_t index = path.rfind("/print.pdf");
    PrintPreviewDataService::GetInstance()->GetDataEntry(path.substr(0, index),
                                                         &data);
  }

  if (path.empty()) {
    // Print Preview Index page.
    DictionaryValue localized_strings;
    SetLocalizedStrings(&localized_strings);
    SetFontAndTextDirection(&localized_strings);

    static const base::StringPiece print_html(
        ResourceBundle::GetSharedInstance().GetRawDataResource(
            IDR_PRINT_PREVIEW_HTML));
    const std::string full_html = jstemplate_builder::GetI18nTemplateHtml(
        print_html, &localized_strings);

    scoped_refptr<RefCountedBytes> html_bytes(new RefCountedBytes);
    html_bytes->data.resize(full_html.size());
    std::copy(full_html.begin(), full_html.end(), html_bytes->data.begin());

    SendResponse(request_id, html_bytes);
    return;
  } else if (preview_data_requested && data->front()) {
    // Print Preview data.
    SendResponse(request_id, data);
    return;
 } else {
    // Invalid request.
    scoped_refptr<RefCountedBytes> empty_bytes(new RefCountedBytes);
    SendResponse(request_id, empty_bytes);
    return;
  }
}

std::string PrintPreviewDataSource::GetMimeType(const std::string& path) const {
  if (path.empty())
    return "text/html";  // Print Preview Index Page.
  return "application/pdf";  // Print Preview data
}
