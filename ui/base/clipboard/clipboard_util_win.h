// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Some helper functions for working with the clipboard and IDataObjects.

#ifndef UI_BASE_CLIPBOARD_CLIPBOARD_UTIL_WIN_H_
#define UI_BASE_CLIPBOARD_CLIPBOARD_UTIL_WIN_H_
#pragma once

#include <shlobj.h>
#include <string>
#include <vector>

namespace ui {

class ClipboardUtil {
 public:
  /////////////////////////////////////////////////////////////////////////////
  // Clipboard formats.
  static FORMATETC* GetUrlFormat();
  static FORMATETC* GetUrlWFormat();
  static FORMATETC* GetMozUrlFormat();
  static FORMATETC* GetPlainTextFormat();
  static FORMATETC* GetPlainTextWFormat();
  static FORMATETC* GetFilenameFormat();
  static FORMATETC* GetFilenameWFormat();
  // MS HTML Format
  static FORMATETC* GetHtmlFormat();
  // Firefox text/html
  static FORMATETC* GetTextHtmlFormat();
  static FORMATETC* GetCFHDropFormat();
  static FORMATETC* GetFileDescriptorFormat();
  static FORMATETC* GetFileContentFormatZero();
  static FORMATETC* GetWebKitSmartPasteFormat();

  /////////////////////////////////////////////////////////////////////////////
  // These methods check to see if |data_object| has the requested type.
  // Returns true if it does.
  static bool HasUrl(IDataObject* data_object);
  static bool HasFilenames(IDataObject* data_object);
  static bool HasPlainText(IDataObject* data_object);
  static bool HasFileContents(IDataObject* data_object);
  static bool HasHtml(IDataObject* data_object);

  /////////////////////////////////////////////////////////////////////////////
  // Helper methods to extract information from an IDataObject.  These methods
  // return true if the requested data type is found in |data_object|.
  static bool GetUrl(IDataObject* data_object,
      std::wstring* url, std::wstring* title, bool convert_filenames);
  static bool GetFilenames(IDataObject* data_object,
                           std::vector<std::wstring>* filenames);
  static bool GetPlainText(IDataObject* data_object, std::wstring* plain_text);
  static bool GetHtml(IDataObject* data_object, std::wstring* text_html,
                      std::string* base_url);
  static bool GetFileContents(IDataObject* data_object,
                              std::wstring* filename,
                              std::string* file_contents);

  // A helper method for converting between MS CF_HTML format and plain
  // text/html.
  static std::string HtmlToCFHtml(const std::string& html,
                                  const std::string& base_url);
  static void CFHtmlToHtml(const std::string& cf_html, std::string* html,
                           std::string* base_url);
};

}

#endif  // UI_BASE_CLIPBOARD_CLIPBOARD_UTIL_WIN_H_
