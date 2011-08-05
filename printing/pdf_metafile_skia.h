// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_PDF_METAFILE_SKIA_H_
#define PRINTING_PDF_METAFILE_SKIA_H_

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/scoped_ptr.h"
#include "build/build_config.h"
#include "printing/metafile.h"

#if defined(OS_WIN)
#include <windows.h>
#endif

namespace printing {

struct PdfMetafileSkiaData;

// This class uses Skia graphics library to generate a PDF document.
class PdfMetafileSkia : public Metafile {
 public:
  PdfMetafileSkia();
  virtual ~PdfMetafileSkia();

  // Metafile methods.
  virtual bool Init();
  virtual bool InitFromData(const void* src_buffer, uint32 src_buffer_size);

  virtual SkDevice* StartPageForVectorCanvas(
      const gfx::Size& page_size,
      const gfx::Rect& content_area,
      const float& scale_factor);

  virtual bool StartPage(const gfx::Size& page_size,
                         const gfx::Rect& content_area,
                         const float& scale_factor);
  virtual bool FinishPage();
  virtual bool FinishDocument();

  virtual uint32 GetDataSize() const;
  virtual bool GetData(void* dst_buffer, uint32 dst_buffer_size) const;

  virtual bool SaveTo(const FilePath& file_path) const;

  virtual gfx::Rect GetPageBounds(unsigned int page_number) const;
  virtual unsigned int GetPageCount() const;

  virtual gfx::NativeDrawingContext context() const;

#if defined(OS_WIN)
  virtual bool Playback(gfx::NativeDrawingContext hdc, const RECT* rect) const;
  virtual bool SafePlayback(gfx::NativeDrawingContext hdc) const;
  virtual HENHMETAFILE emf() const;
#endif  // if defined(OS_WIN)

#if defined(OS_CHROMEOS)
  virtual bool SaveToFD(const base::FileDescriptor& fd) const;
#endif  // if defined(OS_CHROMEOS)
 private:
  scoped_ptr<PdfMetafileSkiaData> data_;

  DISALLOW_COPY_AND_ASSIGN(PdfMetafileSkia);
};

}  // namespace printing

#endif  // PRINTING_PDF_METAFILE_SKIA_H_
