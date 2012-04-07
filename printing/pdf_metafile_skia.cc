// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/pdf_metafile_skia.h"

#include "base/eintr_wrapper.h"
#include "base/file_descriptor_posix.h"
#include "base/file_util.h"
#include "base/hash_tables.h"
#include "base/metrics/histogram.h"
#include "skia/ext/vector_platform_device_skia.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/pdf/SkPDFDevice.h"
#include "third_party/skia/include/pdf/SkPDFDocument.h"
#include "third_party/skia/include/pdf/SkPDFFont.h"
#include "third_party/skia/include/pdf/SkPDFPage.h"
#include "ui/gfx/point.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/size.h"

#if defined(OS_MACOSX)
#include "printing/pdf_metafile_cg_mac.h"
#endif

namespace printing {

struct PdfMetafileSkiaData {
  SkRefPtr<SkPDFDevice> current_page_;
  SkPDFDocument pdf_doc_;
  SkDynamicMemoryWStream pdf_stream_;
#if defined(OS_MACOSX)
  PdfMetafileCg pdf_cg_;
#endif
};

PdfMetafileSkia::~PdfMetafileSkia() {}

bool PdfMetafileSkia::Init() {
  return true;
}
bool PdfMetafileSkia::InitFromData(const void* src_buffer,
                                   uint32 src_buffer_size) {
  return data_->pdf_stream_.write(src_buffer, src_buffer_size);
}

SkDevice* PdfMetafileSkia::StartPageForVectorCanvas(
    const gfx::Size& page_size, const gfx::Rect& content_area,
    const float& scale_factor) {
  DCHECK(!page_outstanding_);
  page_outstanding_ = true;

  // Adjust for the margins and apply the scale factor.
  SkMatrix transform;
  transform.setTranslate(SkIntToScalar(content_area.x()),
                         SkIntToScalar(content_area.y()));
  transform.preScale(SkFloatToScalar(scale_factor),
                     SkFloatToScalar(scale_factor));

  SkISize pdf_page_size = SkISize::Make(page_size.width(), page_size.height());
  SkISize pdf_content_size =
      SkISize::Make(content_area.width(), content_area.height());
  SkRefPtr<SkPDFDevice> pdf_device =
      new skia::VectorPlatformDeviceSkia(pdf_page_size, pdf_content_size,
                                         transform);
  data_->current_page_ = pdf_device;
  return pdf_device.get();
}

bool PdfMetafileSkia::StartPage(const gfx::Size& page_size,
                                const gfx::Rect& content_area,
                                const float& scale_factor) {
  NOTREACHED();
  return NULL;
}

bool PdfMetafileSkia::FinishPage() {
  DCHECK(data_->current_page_.get());

  data_->pdf_doc_.appendPage(data_->current_page_.get());
  page_outstanding_ = false;
  return true;
}

bool PdfMetafileSkia::FinishDocument() {
  // Don't do anything if we've already set the data in InitFromData.
  if (data_->pdf_stream_.getOffset())
    return true;

  if (page_outstanding_)
    FinishPage();

  data_->current_page_ = NULL;
  base::hash_set<SkFontID> font_set;

  const SkTDArray<SkPDFPage*>& pages = data_->pdf_doc_.getPages();
  for (int page_number = 0; page_number < pages.count(); page_number++) {
    const SkTDArray<SkPDFFont*>& font_resources =
        pages[page_number]->getFontResources();
    for (int font = 0; font < font_resources.count(); font++) {
      SkFontID font_id = font_resources[font]->typeface()->uniqueID();
      if (font_set.find(font_id) == font_set.end()) {
        font_set.insert(font_id);
        UMA_HISTOGRAM_ENUMERATION(
            "PrintPreview.FontType",
            font_resources[font]->getType(),
            SkAdvancedTypefaceMetrics::kNotEmbeddable_Font + 1);
      }
    }
  }

  return data_->pdf_doc_.emitPDF(&data_->pdf_stream_);
}

uint32 PdfMetafileSkia::GetDataSize() const {
  return data_->pdf_stream_.getOffset();
}

bool PdfMetafileSkia::GetData(void* dst_buffer,
                              uint32 dst_buffer_size) const {
  if (dst_buffer_size < GetDataSize())
    return false;

  SkAutoDataUnref data(data_->pdf_stream_.copyToData());
  memcpy(dst_buffer, data.bytes(), dst_buffer_size);
  return true;
}

bool PdfMetafileSkia::SaveTo(const FilePath& file_path) const {
  DCHECK_GT(data_->pdf_stream_.getOffset(), 0U);
  SkAutoDataUnref data(data_->pdf_stream_.copyToData());
  if (file_util::WriteFile(file_path,
                           reinterpret_cast<const char*>(data.data()),
                           GetDataSize()) != static_cast<int>(GetDataSize())) {
    DLOG(ERROR) << "Failed to save file " << file_path.value().c_str();
    return false;
  }
  return true;
}

gfx::Rect PdfMetafileSkia::GetPageBounds(unsigned int page_number) const {
  // TODO(vandebo) add a method to get the page size for a given page to
  // SkPDFDocument.
  NOTIMPLEMENTED();
  return gfx::Rect();
}

unsigned int PdfMetafileSkia::GetPageCount() const {
  // TODO(vandebo) add a method to get the number of pages to SkPDFDocument.
  NOTIMPLEMENTED();
  return 0;
}

gfx::NativeDrawingContext PdfMetafileSkia::context() const {
  NOTREACHED();
  return NULL;
}

#if defined(OS_WIN)
bool PdfMetafileSkia::Playback(gfx::NativeDrawingContext hdc,
                               const RECT* rect) const {
  NOTREACHED();
  return false;
}

bool PdfMetafileSkia::SafePlayback(gfx::NativeDrawingContext hdc) const {
  NOTREACHED();
  return false;
}

HENHMETAFILE PdfMetafileSkia::emf() const {
  NOTREACHED();
  return NULL;
}
#elif defined(OS_MACOSX)
/* TODO(caryclark): The set up of PluginInstance::PrintPDFOutput may result in
   rasterized output.  Even if that flow uses PdfMetafileCg::RenderPage,
   the drawing of the PDF into the canvas may result in a rasterized output.
   PDFMetafileSkia::RenderPage should be not implemented as shown and instead
   should do something like the following CL in PluginInstance::PrintPDFOutput:
http://codereview.chromium.org/7200040/diff/1/webkit/plugins/ppapi/ppapi_plugin_instance.cc
*/
bool PdfMetafileSkia::RenderPage(unsigned int page_number,
                                 CGContextRef context,
                                 const CGRect rect,
                                 bool shrink_to_fit,
                                 bool stretch_to_fit,
                                 bool center_horizontally,
                                 bool center_vertically) const {
  DCHECK_GT(data_->pdf_stream_.getOffset(), 0U);
  if (data_->pdf_cg_.GetDataSize() == 0) {
    SkAutoDataUnref data(data_->pdf_stream_.copyToData());
    data_->pdf_cg_.InitFromData(data.bytes(), data.size());
  }
  return data_->pdf_cg_.RenderPage(page_number, context, rect, shrink_to_fit,
                                   stretch_to_fit, center_horizontally,
                                   center_vertically);
}
#endif

#if defined(OS_CHROMEOS)
bool PdfMetafileSkia::SaveToFD(const base::FileDescriptor& fd) const {
  DCHECK_GT(data_->pdf_stream_.getOffset(), 0U);

  if (fd.fd < 0) {
    DLOG(ERROR) << "Invalid file descriptor!";
    return false;
  }

  bool result = true;
  SkAutoDataUnref data(data_->pdf_stream_.copyToData());
  if (file_util::WriteFileDescriptor(fd.fd,
                                     reinterpret_cast<const char*>(data.data()),
                                     GetDataSize()) !=
      static_cast<int>(GetDataSize())) {
    DLOG(ERROR) << "Failed to save file with fd " << fd.fd;
    result = false;
  }

  if (fd.auto_close) {
    if (HANDLE_EINTR(close(fd.fd)) < 0) {
      DPLOG(WARNING) << "close";
      result = false;
    }
  }
  return result;
}
#endif

PdfMetafileSkia::PdfMetafileSkia()
    : data_(new PdfMetafileSkiaData),
      page_outstanding_(false) {
}

PdfMetafileSkia* PdfMetafileSkia::GetMetafileForCurrentPage() {
  SkPDFDocument pdf_doc(SkPDFDocument::kDraftMode_Flags);
  SkDynamicMemoryWStream pdf_stream;
  if (!pdf_doc.appendPage(data_->current_page_.get()))
    return NULL;

  if (!pdf_doc.emitPDF(&pdf_stream))
    return NULL;

  SkAutoDataUnref data(pdf_stream.copyToData());
  if (data.size() == 0)
    return NULL;

  PdfMetafileSkia* metafile = new PdfMetafileSkia;
  metafile->InitFromData(data.bytes(), data.size());
  return metafile;
}

}  // namespace printing
