// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/emf_win.h"

// For quick access.
#include <wingdi.h>
#include <winspool.h>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/win/scoped_hdc.h"
#include "printing/printing_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/point.h"
#include "ui/gfx/size.h"

namespace {

// This test is automatically disabled if no printer named "UnitTest Printer" is
// available.
class EmfPrintingTest : public testing::Test {
 public:
  typedef testing::Test Parent;
  static bool IsTestCaseDisabled() {
    // It is assumed this printer is a HP Color LaserJet 4550 PCL or 4700.
    HDC hdc = CreateDC(L"WINSPOOL", L"UnitTest Printer", NULL, NULL);
    if (!hdc)
      return true;
    DeleteDC(hdc);
    return false;
  }
};

const uint32 EMF_HEADER_SIZE = 128;

}  // namespace

namespace printing {

TEST(EmfTest, DC) {
  // Simplest use case.
  uint32 size;
  std::vector<BYTE> data;
  {
    printing::Emf emf;
    EXPECT_TRUE(emf.Init());
    EXPECT_TRUE(emf.context() != NULL);
    // An empty EMF is invalid, so we put at least a rectangle in it.
    ::Rectangle(emf.context(), 10, 10, 190, 190);
    EXPECT_TRUE(emf.FinishDocument());
    size = emf.GetDataSize();
    EXPECT_GT(size, EMF_HEADER_SIZE);
    EXPECT_TRUE(emf.GetDataAsVector(&data));
    EXPECT_EQ(data.size(), size);
  }

  // Playback the data.
  printing::Emf emf;
  EXPECT_TRUE(emf.InitFromData(&data.front(), size));
  HDC hdc = CreateCompatibleDC(NULL);
  EXPECT_TRUE(hdc);
  RECT output_rect = {0, 0, 10, 10};
  EXPECT_TRUE(emf.Playback(hdc, &output_rect));
  EXPECT_TRUE(DeleteDC(hdc));
}

// Disabled if no "UnitTest printer" exist. Useful to reproduce bug 1186598.
TEST_F(EmfPrintingTest, Enumerate) {
  if (IsTestCaseDisabled())
    return;

  printing::PrintSettings settings;

  // My test case is a HP Color LaserJet 4550 PCL.
  settings.set_device_name(L"UnitTest Printer");

  // Initialize it.
  scoped_ptr<printing::PrintingContext> context(
      printing::PrintingContext::Create(std::string()));
  EXPECT_EQ(context->InitWithSettings(settings), printing::PrintingContext::OK);

  FilePath emf_file;
  EXPECT_TRUE(PathService::Get(base::DIR_SOURCE_ROOT, &emf_file));
  emf_file = emf_file.Append(FILE_PATH_LITERAL("printing"))
                     .Append(FILE_PATH_LITERAL("test"))
                     .Append(FILE_PATH_LITERAL("data"))
                     .Append(FILE_PATH_LITERAL("test4.emf"));
  // Load any EMF with an image.
  printing::Emf emf;
  std::string emf_data;
  file_util::ReadFileToString(emf_file, &emf_data);
  ASSERT_TRUE(emf_data.size());
  EXPECT_TRUE(emf.InitFromData(&emf_data[0], emf_data.size()));

  // This will print to file. The reason is that when running inside a
  // unit_test, printing::PrintingContext automatically dumps its files to the
  // current directory.
  // TODO(maruel):  Clean the .PRN file generated in current directory.
  context->NewDocument(L"EmfTest.Enumerate");
  context->NewPage();
  // Process one at a time.
  printing::Emf::Enumerator emf_enum(emf, context->context(),
                                &emf.GetPageBounds(1).ToRECT());
  for (printing::Emf::Enumerator::const_iterator itr = emf_enum.begin();
       itr != emf_enum.end();
       ++itr) {
    // To help debugging.
    ptrdiff_t index = itr - emf_enum.begin();
    // If you get this assert, you need to lookup iType in wingdi.h. It starts
    // with EMR_HEADER.
    EMR_HEADER;
    EXPECT_TRUE(itr->SafePlayback(NULL)) <<
        " index: " << index << " type: " << itr->record()->iType;
  }
  context->PageDone();
  context->DocumentDone();
}

// Disabled if no "UnitTest printer" exists.
TEST_F(EmfPrintingTest, PageBreak) {
  base::win::ScopedHDC dc(
      CreateDC(L"WINSPOOL", L"UnitTest Printer", NULL, NULL));
  if (!dc.Get())
    return;
  uint32 size;
  std::vector<BYTE> data;
  {
    printing::Emf emf;
    EXPECT_TRUE(emf.Init());
    EXPECT_TRUE(emf.context() != NULL);
    int pages = 3;
    while (pages) {
      EXPECT_TRUE(emf.StartPage(gfx::Size(), gfx::Point(), 1));
      ::Rectangle(emf.context(), 10, 10, 190, 190);
      EXPECT_TRUE(emf.FinishPage());
      --pages;
    }
    EXPECT_EQ(3U, emf.GetPageCount());
    EXPECT_TRUE(emf.FinishDocument());
    size = emf.GetDataSize();
    EXPECT_TRUE(emf.GetDataAsVector(&data));
    EXPECT_EQ(data.size(), size);
  }

  // Playback the data.
  DOCINFO di = {0};
  di.cbSize = sizeof(DOCINFO);
  di.lpszDocName = L"Test Job";
  int job_id = ::StartDoc(dc.Get(), &di);
  printing::Emf emf;
  EXPECT_TRUE(emf.InitFromData(&data.front(), size));
  EXPECT_TRUE(emf.SafePlayback(dc.Get()));
  ::EndDoc(dc.Get());
  // Since presumably the printer is not real, let us just delete the job from
  // the queue.
  HANDLE printer = NULL;
  if (::OpenPrinter(L"UnitTest Printer", &printer, NULL)) {
    ::SetJob(printer, job_id, 0, NULL, JOB_CONTROL_DELETE);
    ClosePrinter(printer);
  }
}

TEST(EmfTest, FileBackedEmf) {
  // Simplest use case.
  ScopedTempDir scratch_metafile_dir;
  ASSERT_TRUE(scratch_metafile_dir.CreateUniqueTempDir());
  FilePath metafile_path;
  EXPECT_TRUE(file_util::CreateTemporaryFileInDir(scratch_metafile_dir.path(),
                                                  &metafile_path));
  uint32 size;
  std::vector<BYTE> data;
  {
    printing::Emf emf;
    EXPECT_TRUE(emf.InitToFile(metafile_path));
    EXPECT_TRUE(emf.context() != NULL);
    // An empty EMF is invalid, so we put at least a rectangle in it.
    ::Rectangle(emf.context(), 10, 10, 190, 190);
    EXPECT_TRUE(emf.FinishDocument());
    size = emf.GetDataSize();
    EXPECT_GT(size, EMF_HEADER_SIZE);
    EXPECT_TRUE(emf.GetDataAsVector(&data));
    EXPECT_EQ(data.size(), size);
  }
  int64 file_size = 0;
  file_util::GetFileSize(metafile_path, &file_size);
  EXPECT_EQ(size, file_size);

  // Playback the data.
  HDC hdc = CreateCompatibleDC(NULL);
  EXPECT_TRUE(hdc);
  printing::Emf emf;
  EXPECT_TRUE(emf.InitFromFile(metafile_path));
  RECT output_rect = {0, 0, 10, 10};
  EXPECT_TRUE(emf.Playback(hdc, &output_rect));
  EXPECT_TRUE(DeleteDC(hdc));
}

}  // namespace printing
