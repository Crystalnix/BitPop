// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_DRAGDROP_OS_EXCHANGE_DATA_PROVIDER_AURA_H_
#define UI_BASE_DRAGDROP_OS_EXCHANGE_DATA_PROVIDER_AURA_H_

#include <map>

#include "base/file_path.h"
#include "base/pickle.h"
#include "googleurl/src/gurl.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/point.h"

namespace ui {

class Clipboard;

// OSExchangeData::Provider implementation for aura on linux.
class UI_EXPORT OSExchangeDataProviderAura : public OSExchangeData::Provider {
 public:
  OSExchangeDataProviderAura();
  virtual ~OSExchangeDataProviderAura();

  // Overridden from OSExchangeData::Provider:
  virtual void SetString(const string16& data) OVERRIDE;
  virtual void SetURL(const GURL& url, const string16& title) OVERRIDE;
  virtual void SetFilename(const FilePath& path) OVERRIDE;
  virtual void SetFilenames(
      const std::vector<OSExchangeData::FileInfo>& filenames) OVERRIDE;
  virtual void SetPickledData(OSExchangeData::CustomFormat format,
                              const Pickle& data) OVERRIDE;
  virtual bool GetString(string16* data) const OVERRIDE;
  virtual bool GetURLAndTitle(GURL* url, string16* title) const OVERRIDE;
  virtual bool GetFilename(FilePath* path) const OVERRIDE;
  virtual bool GetFilenames(
      std::vector<OSExchangeData::FileInfo>* filenames) const OVERRIDE;
  virtual bool GetPickledData(OSExchangeData::CustomFormat format,
                              Pickle* data) const OVERRIDE;
  virtual bool HasString() const OVERRIDE;
  virtual bool HasURL() const OVERRIDE;
  virtual bool HasFile() const OVERRIDE;
  virtual bool HasCustomFormat(
      OSExchangeData::CustomFormat format) const OVERRIDE;
#if defined(OS_WIN)
  virtual void SetFileContents(const FilePath& filename,
                               const std::string& file_contents) OVERRIDE;
  virtual bool GetFileContents(FilePath* filename,
                               std::string* file_contents) const OVERRIDE;
  virtual bool HasFileContents() const OVERRIDE;
  virtual void SetDownloadFileInfo(
      const OSExchangeData::DownloadFileInfo& download) OVERRIDE;
#endif

  virtual void SetHtml(const string16& html, const GURL& base_url) OVERRIDE;
  virtual bool GetHtml(string16* html, GURL* base_url) const OVERRIDE;
  virtual bool HasHtml() const OVERRIDE;

  void set_drag_image(const gfx::ImageSkia& drag_image) {
      drag_image_ = drag_image;
  }

  const gfx::ImageSkia& drag_image() const { return drag_image_; }
  void set_drag_image_offset(const gfx::Point& drag_image_offset) {
    drag_image_offset_ = drag_image_offset;
  }
  const gfx::Point& drag_image_offset() const { return drag_image_offset_; }

 private:
  typedef std::map<OSExchangeData::CustomFormat, Pickle>  PickleData;

  // Returns true if |formats_| contains a string format and the string can be
  // parsed as a URL.
  bool GetPlainTextURL(GURL* url) const;

  // Actual formats that have been set. See comment above |known_formats_|
  // for details.
  int formats_;

  // String contents.
  string16 string_;

  // URL contents.
  GURL url_;
  string16 title_;

  // File name.
  std::vector<OSExchangeData::FileInfo> filenames_;

  // PICKLED_DATA contents.
  PickleData pickle_data_;

  // Drag image and offset data.
  gfx::ImageSkia drag_image_;
  gfx::Point drag_image_offset_;

  // For HTML format
  string16 html_;
  GURL base_url_;

  DISALLOW_COPY_AND_ASSIGN(OSExchangeDataProviderAura);
};

}  // namespace ui

#endif  // UI_BASE_DRAGDROP_OS_EXCHANGE_DATA_PROVIDER_AURA_H_
