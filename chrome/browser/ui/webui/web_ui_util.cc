// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/web_ui_util.h"

#include <vector>

#include "base/base64.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/values.h"
#include "chrome/browser/disposition_utils.h"
#include "googleurl/src/gurl.h"
#include "net/base/escape.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image_skia.h"

namespace {

struct ScaleFactorMap {
  const char* name;
  ui::ScaleFactor scale_factor;
};

const ScaleFactorMap kScaleFactorMap[] = {
  { "1x", ui::SCALE_FACTOR_100P },
  { "2x", ui::SCALE_FACTOR_200P },
};

}  // namespace

namespace web_ui_util {

std::string GetImageDataUrl(const gfx::ImageSkia& image) {
  std::vector<unsigned char> output;
  gfx::PNGCodec::EncodeBGRASkBitmap(*image.bitmap(), false, &output);
  std::string str_url;
  str_url.insert(str_url.end(), output.begin(), output.end());

  base::Base64Encode(str_url, &str_url);
  str_url.insert(0, "data:image/png;base64,");
  return str_url;
}

std::string GetImageDataUrlFromResource(int res) {
  // Load resource icon and covert to base64 encoded data url
  base::RefCountedStaticMemory* icon_data =
      ResourceBundle::GetSharedInstance().LoadDataResourceBytes(res,
          ui::SCALE_FACTOR_100P);
  if (!icon_data)
    return std::string();
  scoped_refptr<base::RefCountedMemory> raw_icon(icon_data);
  std::string str_url;
  str_url.insert(str_url.end(),
    raw_icon->front(),
    raw_icon->front() + raw_icon->size());
  base::Base64Encode(str_url, &str_url);
  str_url.insert(0, "data:image/png;base64,");
  return str_url;
}

WindowOpenDisposition GetDispositionFromClick(const ListValue* args,
                                              int start_index) {
  double button = 0.0;
  bool alt_key = false;
  bool ctrl_key = false;
  bool meta_key = false;
  bool shift_key = false;

  CHECK(args->GetDouble(start_index++, &button));
  CHECK(args->GetBoolean(start_index++, &alt_key));
  CHECK(args->GetBoolean(start_index++, &ctrl_key));
  CHECK(args->GetBoolean(start_index++, &meta_key));
  CHECK(args->GetBoolean(start_index++, &shift_key));
  return disposition_utils::DispositionFromClick(button == 1.0, alt_key,
                                                 ctrl_key, meta_key, shift_key);

}

ui::ScaleFactor ParseScaleFactor(const base::StringPiece& identifier) {
  for (size_t i = 0; i < arraysize(kScaleFactorMap); i++) {
    if (identifier == kScaleFactorMap[i].name)
      return kScaleFactorMap[i].scale_factor;
  }
  return ui::SCALE_FACTOR_NONE;
}

void ParsePathAndScale(const GURL& url,
                       std::string* path,
                       ui::ScaleFactor* scale_factor) {
  *path = net::UnescapeURLComponent(url.path().substr(1),
                                    (net::UnescapeRule::URL_SPECIAL_CHARS |
                                     net::UnescapeRule::SPACES));
  if (scale_factor)
    *scale_factor = ui::SCALE_FACTOR_100P;

  // Detect and parse resource string ending in @<scale>x.
  std::size_t pos = path->rfind('@');
  if (pos != std::string::npos) {
    base::StringPiece stripped_path(*path);
    if (scale_factor) {
      *scale_factor = ParseScaleFactor(stripped_path.substr(
          pos + 1, stripped_path.length() - pos - 1));
    }
    // Strip scale factor specification from path.
    stripped_path.remove_suffix(stripped_path.length() - pos);
    stripped_path.CopyToString(path);
  }
}

}  // namespace web_ui_util
