// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "printing/metafile_skia_wrapper.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkDevice.h"
#include "third_party/skia/include/core/SkMetaData.h"

namespace printing {

namespace {

static const char* kMetafileKey = "CrMetafile";

SkMetaData& getMetaData(SkCanvas* canvas) {
  DCHECK(canvas != NULL);

  SkDevice* device = canvas->getDevice();
  DCHECK(device != NULL);
  return device->getMetaData();
}

}  // namespace

// static
void MetafileSkiaWrapper::SetMetafileOnCanvas(SkCanvas* canvas,
                                              Metafile* metafile) {
  MetafileSkiaWrapper* wrapper = NULL;
  if (metafile)
    wrapper = new MetafileSkiaWrapper(metafile);

  SkMetaData& meta = getMetaData(canvas);
  meta.setRefCnt(kMetafileKey, wrapper);
  SkSafeUnref(wrapper);
}

// static
Metafile* MetafileSkiaWrapper::GetMetafileFromCanvas(SkCanvas* canvas) {
  SkMetaData& meta = getMetaData(canvas);
  SkRefCnt* value;
  if (!meta.findRefCnt(kMetafileKey, &value) || !value)
    return NULL;

  return static_cast<MetafileSkiaWrapper*>(value)->metafile_;
}

MetafileSkiaWrapper::MetafileSkiaWrapper(Metafile* metafile)
    : metafile_(metafile) {
}

}  // namespace printing
