// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_EMF_WIN_H_
#define PRINTING_EMF_WIN_H_

#include <windows.h>
#include <vector>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "printing/metafile.h"

class FilePath;

namespace gfx {
class Point;
class Rect;
class Size;
}

namespace printing {

// Simple wrapper class that manage an EMF data stream and its virtual HDC.
class Emf : public Metafile {
 public:
  class Record;
  class Enumerator;
  struct EnumerationContext;

  // Generates a virtual HDC that will record every GDI commands and compile
  // it in a EMF data stream.
  Emf();
  virtual ~Emf();

  // Generates a new metafile that will record every GDI command, and will
  // be saved to |metafile_path|.
  virtual bool InitToFile(const FilePath& metafile_path);

  // Initializes the Emf with the data in |metafile_path|.
  virtual bool InitFromFile(const FilePath& metafile_path);

  // Metafile methods.
  virtual bool Init();
  virtual bool InitFromData(const void* src_buffer, uint32 src_buffer_size);

  virtual skia::PlatformDevice* StartPageForVectorCanvas(
      const gfx::Size& page_size, const gfx::Point& content_origin,
      const float& scale_factor);
  // Inserts a custom GDICOMMENT records indicating StartPage/EndPage calls
  // (since StartPage and EndPage do not work in a metafile DC). Only valid
  // when hdc_ is non-NULL. |page_size| and |content_origin| are ignored.
  // |scale_factor|  must be 1.0.
  virtual bool StartPage(const gfx::Size& page_size,
                         const gfx::Point& content_origin,
                         const float& scale_factor);
  virtual bool FinishPage();
  virtual bool FinishDocument();

  virtual uint32 GetDataSize() const;
  virtual bool GetData(void* buffer, uint32 size) const;

  // Saves the EMF data to a file as-is. It is recommended to use the .emf file
  // extension but it is not enforced. This function synchronously writes to the
  // file. For testing only.
  virtual bool SaveTo(const FilePath& file_path) const;

  // Should be passed to Playback to keep the exact same size.
  virtual gfx::Rect GetPageBounds(unsigned int page_number) const;

  virtual unsigned int GetPageCount() const {
    return page_count_;
  }

  virtual HDC context() const {
    return hdc_;
  }

  virtual bool Playback(HDC hdc, const RECT* rect) const;
  virtual bool SafePlayback(HDC hdc) const;

  virtual HENHMETAFILE emf() const {
    return emf_;
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(EmfTest, DC);
  FRIEND_TEST_ALL_PREFIXES(EmfPrintingTest, PageBreak);
  FRIEND_TEST_ALL_PREFIXES(EmfTest, FileBackedEmf);

  // Retrieves the underlying data stream. It is a helper function.
  bool GetDataAsVector(std::vector<uint8>* buffer) const;

  // Playbacks safely one EMF record.
  static int CALLBACK SafePlaybackProc(HDC hdc,
                                       HANDLETABLE* handle_table,
                                       const ENHMETARECORD* record,
                                       int objects_count,
                                       LPARAM param);

  // Compiled EMF data handle.
  HENHMETAFILE emf_;

  // Valid when generating EMF data through a virtual HDC.
  HDC hdc_;

  int page_count_;

  DISALLOW_COPY_AND_ASSIGN(Emf);
};

struct Emf::EnumerationContext {
  HANDLETABLE* handle_table;
  int objects_count;
  HDC hdc;
};

// One EMF record. It keeps pointers to the EMF buffer held by Emf::emf_.
// The entries become invalid once Emf::CloseEmf() is called.
class Emf::Record {
 public:
  // Plays the record.
  bool Play() const;

  // Plays the record working around quirks with SetLayout,
  // SetWorldTransform and ModifyWorldTransform. See implementation for details.
  bool SafePlayback(const XFORM* base_matrix) const;

  // Access the underlying EMF record.
  const ENHMETARECORD* record() const { return record_; }

 protected:
  Record(const EnumerationContext* context,
         const ENHMETARECORD* record);

 private:
  friend class Emf;
  friend class Enumerator;
  const ENHMETARECORD* record_;
  const EnumerationContext* context_;
};

// Retrieves individual records out of a Emf buffer. The main use is to skip
// over records that are unsupported on a specific printer or to play back
// only a part of an EMF buffer.
class Emf::Enumerator {
 public:
  // Iterator type used for iterating the records.
  typedef std::vector<Record>::const_iterator const_iterator;

  // Enumerates the records at construction time. |hdc| and |rect| are
  // both optional at the same time or must both be valid.
  // Warning: |emf| must be kept valid for the time this object is alive.
  Enumerator(const Emf& emf, HDC hdc, const RECT* rect);

  // Retrieves the first Record.
  const_iterator begin() const;

  // Retrieves the end of the array.
  const_iterator end() const;

 private:
  // Processes one EMF record and saves it in the items_ array.
  static int CALLBACK EnhMetaFileProc(HDC hdc,
                                      HANDLETABLE* handle_table,
                                      const ENHMETARECORD* record,
                                      int objects_count,
                                      LPARAM param);

  // The collection of every EMF records in the currently loaded EMF buffer.
  // Initialized by Enumerate(). It keeps pointers to the EMF buffer held by
  // Emf::emf_. The entries become invalid once Emf::CloseEmf() is called.
  std::vector<Record> items_;

  EnumerationContext context_;

  DISALLOW_COPY_AND_ASSIGN(Enumerator);
};

}  // namespace printing

#endif  // PRINTING_EMF_WIN_H_
