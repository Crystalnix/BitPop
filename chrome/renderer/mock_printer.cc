// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/mock_printer.h"

#include "base/file_util.h"
#include "base/shared_memory.h"
#include "chrome/common/print_messages.h"
#include "ipc/ipc_message_utils.h"
#include "printing/metafile_impl.h"
#include "printing/units.h"
#include "testing/gtest/include/gtest/gtest.h"

MockPrinterPage::MockPrinterPage(const void* source_data,
                                 uint32 source_size,
                                 const printing::Image& image)
    : source_size_(source_size),
      image_(image) {
  // Create copies of the source data
  source_data_.reset(new uint8[source_size]);
  if (source_data_.get())
    memcpy(source_data_.get(), source_data, source_size);
}

MockPrinterPage::~MockPrinterPage() {}

MockPrinter::MockPrinter()
  : dpi_(printing::kPointsPerInch),
    max_shrink_(2.0),
    min_shrink_(1.25),
    desired_dpi_(printing::kPointsPerInch),
    selection_only_(false),
    document_cookie_(-1),
    current_document_cookie_(0),
    printer_status_(PRINTER_READY),
    number_pages_(0),
    page_number_(0) {
  page_size_.SetSize(static_cast<int>(8.5 * dpi_),
                     static_cast<int>(11.0 * dpi_));
  printable_size_.SetSize(static_cast<int>((7.5 * dpi_)),
                          static_cast<int>((10.0 * dpi_)));
  margin_left_ = margin_top_ = static_cast<int>(0.5 * dpi_);
}

MockPrinter::~MockPrinter() {
}

void MockPrinter::ResetPrinter() {
  printer_status_ = PRINTER_READY;
  document_cookie_ = -1;
}

void MockPrinter::GetDefaultPrintSettings(PrintMsg_Print_Params* params) {
  // Verify this printer is not processing a job.
  // Sorry, this mock printer is very fragile.
  EXPECT_EQ(-1, document_cookie_);

  // Assign a unit document cookie and set the print settings.
  document_cookie_ = CreateDocumentCookie();
  memset(params, 0, sizeof(PrintMsg_Print_Params));
  params->dpi = dpi_;
  params->max_shrink = max_shrink_;
  params->min_shrink = min_shrink_;
  params->desired_dpi = desired_dpi_;
  params->selection_only = selection_only_;
  params->document_cookie = document_cookie_;
  params->page_size = page_size_;
  params->printable_size = printable_size_;
  params->margin_left = margin_left_;
  params->margin_top = margin_top_;
}

void MockPrinter::SetDefaultPrintSettings(const PrintMsg_Print_Params& params) {
  dpi_ = params.dpi;
  max_shrink_ = params.max_shrink;
  min_shrink_ = params.min_shrink;
  desired_dpi_ = params.desired_dpi;
  selection_only_ = params.selection_only;
  page_size_ = params.page_size;
  printable_size_ = params.printable_size;
  margin_left_ = params.margin_left;
  margin_top_ = params.margin_top;
}

void MockPrinter::ScriptedPrint(int cookie,
                                int expected_pages_count,
                                bool has_selection,
                                PrintMsg_PrintPages_Params* settings) {
  // Verify the input parameters.
  EXPECT_EQ(document_cookie_, cookie);

  memset(settings, 0, sizeof(PrintMsg_PrintPages_Params));
  settings->params.dpi = dpi_;
  settings->params.max_shrink = max_shrink_;
  settings->params.min_shrink = min_shrink_;
  settings->params.desired_dpi = desired_dpi_;
  settings->params.selection_only = selection_only_;
  settings->params.document_cookie = document_cookie_;
  settings->params.page_size = page_size_;
  settings->params.printable_size = printable_size_;
  printer_status_ = PRINTER_PRINTING;
}

void MockPrinter::SetPrintedPagesCount(int cookie, int number_pages) {
  // Verify the input parameter and update the printer status so that the
  // RenderViewTest class can verify the this function finishes without errors.
  EXPECT_EQ(document_cookie_, cookie);
  EXPECT_EQ(PRINTER_PRINTING, printer_status_);
  EXPECT_EQ(0, number_pages_);
  EXPECT_EQ(0, page_number_);

  // Initialize the job status.
  number_pages_ = number_pages;
  page_number_ = 0;
  pages_.clear();
}

void MockPrinter::PrintPage(const PrintHostMsg_DidPrintPage_Params& params) {
  // Verify the input parameter and update the printer status so that the
  // RenderViewTest class can verify the this function finishes without errors.
  EXPECT_EQ(PRINTER_PRINTING, printer_status_);
  EXPECT_EQ(document_cookie_, params.document_cookie);
  EXPECT_EQ(page_number_, params.page_number);
  EXPECT_LE(params.page_number, number_pages_);

#if defined(OS_WIN) || defined(OS_MACOSX)
  // Load the data sent from a RenderView object and create a PageData object.
  // We duplicate the given file handle when creating a base::SharedMemory
  // instance so that its destructor closes the copy.
  EXPECT_GT(params.data_size, 0U);
#if defined(OS_WIN)
  base::SharedMemory metafile_data(params.metafile_data_handle, true,
                                   GetCurrentProcess());
#elif defined(OS_MACOSX)
  base::SharedMemory metafile_data(params.metafile_data_handle, true);
#endif
  metafile_data.Map(params.data_size);
  printing::NativeMetafile metafile;
  metafile.InitFromData(metafile_data.memory(), params.data_size);
  printing::Image image(metafile);
  MockPrinterPage* page_data = new MockPrinterPage(metafile_data.memory(),
                                                   params.data_size,
                                                   image);
  if (!page_data) {
    printer_status_ = PRINTER_ERROR;
    return;
  }

  scoped_refptr<MockPrinterPage> page(page_data);
  pages_.push_back(page);
#endif

  // We finish printing a printing job.
  // Reset the job status and the printer status.
  ++page_number_;
  if (number_pages_ == page_number_)
    ResetPrinter();
}

int MockPrinter::GetPrintedPages() const {
  if (printer_status_ != PRINTER_READY)
    return -1;
  return page_number_;
}

const MockPrinterPage* MockPrinter::GetPrintedPage(unsigned int pageno) const {
  if (pages_.size() > pageno)
    return pages_[pageno];
  else
    return NULL;
}

int MockPrinter::GetWidth(unsigned int page) const {
  if (printer_status_ != PRINTER_READY || page >= pages_.size())
    return -1;
  return pages_[page]->width();
}

int MockPrinter::GetHeight(unsigned int page) const {
  if (printer_status_ != PRINTER_READY || page >= pages_.size())
    return -1;
  return pages_[page]->height();
}

bool MockPrinter::GetBitmapChecksum(
    unsigned int page, std::string* checksum) const {
  if (printer_status_ != PRINTER_READY || page >= pages_.size())
    return false;
  *checksum = pages_[page]->image().checksum();
  return true;
}

bool MockPrinter::SaveSource(
    unsigned int page, const FilePath& filepath) const {
  if (printer_status_ != PRINTER_READY || page >= pages_.size())
    return false;
  const uint8* source_data = pages_[page]->source_data();
  uint32 source_size = pages_[page]->source_size();
  file_util::WriteFile(filepath, reinterpret_cast<const char*>(source_data),
                       source_size);
  return true;
}

bool MockPrinter::SaveBitmap(
    unsigned int page, const FilePath& filepath) const {
  if (printer_status_ != PRINTER_READY || page >= pages_.size())
    return false;

  pages_[page]->image().SaveToPng(filepath);
  return true;
}

int MockPrinter::CreateDocumentCookie() {
  return ++current_document_cookie_;
}
