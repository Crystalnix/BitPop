// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/page_overlays.h"

#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "printing/printed_document.h"
#include "printing/printed_page.h"

namespace {

// Replaces a subpart of a string by other value, and returns the position right
// after the new value.
size_t ReplaceKey(std::wstring* string,
                  size_t offset,
                  size_t old_string_len,
                  const std::wstring& new_string) {
  string->replace(offset, old_string_len, new_string);
  return offset + new_string.size();
}

}  // namespace

namespace printing {

const wchar_t* const PageOverlays::kTitle = L"{title}";
const wchar_t* const PageOverlays::kTime = L"{time}";
const wchar_t* const PageOverlays::kDate = L"{date}";
const wchar_t* const PageOverlays::kPage = L"{page}";
const wchar_t* const PageOverlays::kPageCount = L"{pagecount}";
const wchar_t* const PageOverlays::kPageOnTotal = L"{pageontotal}";
const wchar_t* const PageOverlays::kUrl = L"{url}";

PageOverlays::PageOverlays()
    : top_left(kDate),
      top_center(kTitle),
      top_right(),
      bottom_left(kUrl),
      bottom_center(),
      bottom_right(kPageOnTotal) {
}

PageOverlays::~PageOverlays() {}

bool PageOverlays::Equals(const PageOverlays& rhs) const {
  return top_left == rhs.top_left &&
      top_center == rhs.top_center &&
      top_right == rhs.top_right &&
      bottom_left == rhs.bottom_left &&
      bottom_center == rhs.bottom_center &&
      bottom_right == rhs.bottom_right;
}

const std::wstring& PageOverlays::GetOverlay(HorizontalPosition x,
                                             VerticalPosition y) const {
  switch (x) {
    case LEFT:
      switch (y) {
        case TOP:
          return top_left;
        case BOTTOM:
          return bottom_left;
      }
      break;
    case CENTER:
      switch (y) {
        case TOP:
          return top_center;
        case BOTTOM:
          return bottom_center;
      }
      break;
    case RIGHT:
      switch (y) {
        case TOP:
          return top_right;
        case BOTTOM:
          return bottom_right;
      }
      break;
  }
  NOTREACHED();
  return EmptyWString();
}

void PageOverlays::SetOverlay(HorizontalPosition x,
                              VerticalPosition y,
                              const std::wstring& input) {
  switch (x) {
    case LEFT:
      switch (y) {
        case TOP:
          top_left = input;
          break;
        case BOTTOM:
          bottom_left = input;
          break;
        default:
          NOTREACHED();
          break;
      }
      break;
    case CENTER:
      switch (y) {
        case TOP:
          top_center = input;
          break;
        case BOTTOM:
          bottom_center = input;
          break;
        default:
          NOTREACHED();
          break;
      }
      break;
    case RIGHT:
      switch (y) {
        case TOP:
          top_right = input;
          break;
        case BOTTOM:
          bottom_right = input;
          break;
        default:
          NOTREACHED();
          break;
      }
      break;
    default:
      NOTREACHED();
      break;
  }
}

// static
std::wstring PageOverlays::ReplaceVariables(const std::wstring& input,
                                            const PrintedDocument& document,
                                            const PrintedPage& page) {
  std::wstring output(input);
  for (size_t offset = output.find(L'{', 0);
       offset != std::wstring::npos;
       offset = output.find(L'{', offset)) {
    if (0 == output.compare(offset,
                            wcslen(kTitle),
                            kTitle)) {
      offset = ReplaceKey(&output,
                          offset,
                          wcslen(kTitle),
                          UTF16ToWideHack(document.name()));
    } else if (0 == output.compare(offset,
                                   wcslen(kTime),
                                   kTime)) {
      offset = ReplaceKey(&output,
                          offset,
                          wcslen(kTime),
                          UTF16ToWideHack(document.time()));
    } else if (0 == output.compare(offset,
                                   wcslen(kDate),
                                   kDate)) {
      offset = ReplaceKey(&output,
                          offset,
                          wcslen(kDate),
                          UTF16ToWideHack(document.date()));
    } else if (0 == output.compare(offset,
                                   wcslen(kPage),
                                   kPage)) {
      offset = ReplaceKey(&output,
                          offset,
                          wcslen(kPage),
                          UTF8ToWide(base::IntToString(page.page_number())));
    } else if (0 == output.compare(offset,
                                   wcslen(kPageCount),
                                   kPageCount)) {
      offset = ReplaceKey(&output,
                          offset,
                          wcslen(kPageCount),
                          UTF8ToWide(base::IntToString(document.page_count())));
    } else if (0 == output.compare(offset,
                                   wcslen(kPageOnTotal),
                                   kPageOnTotal)) {
      std::wstring replacement;
      replacement = UTF8ToWide(base::IntToString(page.page_number()));
      replacement += L"/";
      replacement += UTF8ToWide(base::IntToString(document.page_count()));
      offset = ReplaceKey(&output,
                          offset,
                          wcslen(kPageOnTotal),
                          replacement);
    } else if (0 == output.compare(offset,
                                   wcslen(kUrl),
                                   kUrl)) {
      // TODO(maruel):  http://b/1126373 ui::ElideUrl(document.url(), ...)
      offset = ReplaceKey(&output,
                          offset,
                          wcslen(kUrl),
                          UTF8ToWide(document.url().spec()));
    } else {
      // There is just a { in the string.
      ++offset;
    }
  }
  return output;
}

}  // namespace printing
