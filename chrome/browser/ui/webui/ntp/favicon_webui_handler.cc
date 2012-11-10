// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ntp/favicon_webui_handler.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/history/top_sites.h"
#include "chrome/browser/extensions/extension_icon_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_ui.h"
#include "grit/ui_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/color_analysis.h"

namespace {

StringValue* SkColorToCss(SkColor color) {
  return new StringValue(base::StringPrintf("rgb(%d, %d, %d)",
                                            SkColorGetR(color),
                                            SkColorGetG(color),
                                            SkColorGetB(color)));
}

base::StringValue* GetDominantColorCssString(
    scoped_refptr<base::RefCountedMemory> png) {
  color_utils::GridSampler sampler;
  SkColor color = color_utils::CalculateKMeanColorOfPNG(png, 100, 665, sampler);
  return SkColorToCss(color);
}

}  // namespace

// Thin inheritance-dependent trampoline to forward notification of app
// icon loads to the FaviconWebUIHandler. Base class does caching of icons.
class ExtensionIconColorManager : public ExtensionIconManager {
 public:
  explicit ExtensionIconColorManager(FaviconWebUIHandler* handler)
      : ExtensionIconManager(),
        handler_(handler) {}
  virtual ~ExtensionIconColorManager() {}

  virtual void OnImageLoaded(const gfx::Image& image,
                             const std::string& extension_id,
                             int index) OVERRIDE {
    ExtensionIconManager::OnImageLoaded(image, extension_id, index);
    handler_->NotifyAppIconReady(extension_id);
  }

 private:
  FaviconWebUIHandler* handler_;
};

FaviconWebUIHandler::FaviconWebUIHandler()
    : id_(0),
      app_icon_color_manager_(new ExtensionIconColorManager(this)) {
}

FaviconWebUIHandler::~FaviconWebUIHandler() {
}

void FaviconWebUIHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("getFaviconDominantColor",
      base::Bind(&FaviconWebUIHandler::HandleGetFaviconDominantColor,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("getAppIconDominantColor",
      base::Bind(&FaviconWebUIHandler::HandleGetAppIconDominantColor,
                 base::Unretained(this)));
}

void FaviconWebUIHandler::HandleGetFaviconDominantColor(const ListValue* args) {
  std::string path;
  CHECK(args->GetString(0, &path));
  DCHECK(StartsWithASCII(path, "chrome://favicon/size/16/", false)) <<
      "path is " << path;
  path = path.substr(arraysize("chrome://favicon/size/16/") - 1);

  std::string dom_id;
  CHECK(args->GetString(1, &dom_id));

  FaviconService* favicon_service =
      Profile::FromWebUI(web_ui())->GetFaviconService(Profile::EXPLICIT_ACCESS);
  if (!favicon_service || path.empty())
    return;

  GURL url(path);
  // Intercept requests for prepopulated pages.
  for (size_t i = 0; i < arraysize(history::kPrepopulatedPages); i++) {
    if (url.spec() ==
        l10n_util::GetStringUTF8(history::kPrepopulatedPages[i].url_id)) {
      StringValue dom_id_value(dom_id);
      scoped_ptr<StringValue> color(
          SkColorToCss(history::kPrepopulatedPages[i].color));
      web_ui()->CallJavascriptFunction("ntp.setStripeColor",
                                       dom_id_value, *color);
      return;
    }
  }

  dom_id_map_[id_] = dom_id;
  FaviconService::Handle handle = favicon_service->GetFaviconForURL(
      url,
      history::FAVICON,
      &consumer_,
      base::Bind(&FaviconWebUIHandler::OnFaviconDataAvailable,
                 base::Unretained(this)));
  consumer_.SetClientData(favicon_service, handle, id_++);
}

void FaviconWebUIHandler::OnFaviconDataAvailable(
    FaviconService::Handle request_handle,
    history::FaviconData favicon) {
  FaviconService* favicon_service =
      Profile::FromWebUI(web_ui())->GetFaviconService(Profile::EXPLICIT_ACCESS);
  int id = consumer_.GetClientData(favicon_service, request_handle);
  scoped_ptr<StringValue> color_value;

  if (favicon.is_valid())
    color_value.reset(GetDominantColorCssString(favicon.image_data));
  else
    color_value.reset(new StringValue("#919191"));

  StringValue dom_id(dom_id_map_[id]);
  web_ui()->CallJavascriptFunction("ntp.setStripeColor", dom_id, *color_value);
  dom_id_map_.erase(id);
}

void FaviconWebUIHandler::HandleGetAppIconDominantColor(
    const ListValue* args) {
  std::string extension_id;
  CHECK(args->GetString(0, &extension_id));

  ExtensionService* extension_service =
      Profile::FromWebUI(web_ui())->GetExtensionService();
  const extensions::Extension* extension = extension_service->GetExtensionById(
      extension_id, false);
  if (!extension)
    return;
  app_icon_color_manager_->LoadIcon(extension);
}

void FaviconWebUIHandler::NotifyAppIconReady(const std::string& extension_id) {
  const SkBitmap& bitmap = app_icon_color_manager_->GetIcon(extension_id);
  // TODO(estade): would be nice to avoid a round trip through png encoding.
  std::vector<unsigned char> bits;
  if (!gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, true, &bits))
    return;
  scoped_refptr<base::RefCountedStaticMemory> bits_mem(
      new base::RefCountedStaticMemory(&bits.front(), bits.size()));
  scoped_ptr<StringValue> color_value(GetDominantColorCssString(bits_mem));
  StringValue id(extension_id);
  web_ui()->CallJavascriptFunction(
      "ntp.setStripeColor", id, *color_value);
}
