// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/wallpaper_private_api.h"

#include "ash/desktop_background/desktop_background_controller.h"
#include "ash/shell.h"
#include "base/file_util.h"
#include "base/json/json_writer.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/synchronization/cancellation_flag.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/wallpaper_manager.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/image_decoder.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/browser_thread.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request_status.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

using base::BinaryValue;
using content::BrowserThread;

bool WallpaperStringsFunction::RunImpl() {
  DictionaryValue* dict = new DictionaryValue();
  SetResult(dict);

#define SET_STRING(ns, id) \
  dict->SetString(#id, l10n_util::GetStringUTF16(ns##_##id))
  SET_STRING(IDS_WALLPAPER_MANAGER, SEARCH_TEXT_LABEL);
  SET_STRING(IDS_WALLPAPER_MANAGER, AUTHOR_LABEL);
  SET_STRING(IDS_WALLPAPER_MANAGER, CUSTOM_CATEGORY_LABEL);
  SET_STRING(IDS_WALLPAPER_MANAGER, SELECT_CUSTOM_LABEL);
  SET_STRING(IDS_WALLPAPER_MANAGER, POSITION_LABEL);
  SET_STRING(IDS_WALLPAPER_MANAGER, COLOR_LABEL);
  SET_STRING(IDS_WALLPAPER_MANAGER, PREVIEW_LABEL);
  SET_STRING(IDS_OPTIONS, SET_WALLPAPER_DAILY);
#undef SET_STRING

  ChromeURLDataManager::DataSource::SetFontAndTextDirection(dict);

  return true;
}

class WallpaperSetWallpaperFunction::WallpaperDecoder
    : public ImageDecoder::Delegate {
 public:
  explicit WallpaperDecoder(
      scoped_refptr<WallpaperSetWallpaperFunction> function)
      : function_(function) {
  }

  void Start(const std::string& image_data) {
    image_decoder_ = new ImageDecoder(this, image_data);
    image_decoder_->Start();
  }

  void Cancel() {
    cancel_flag_.Set();
    function_->SendResponse(false);
  }

  virtual void OnImageDecoded(const ImageDecoder* decoder,
                              const SkBitmap& decoded_image) OVERRIDE {
    gfx::ImageSkia final_image(decoded_image);
    if (cancel_flag_.IsSet()) {
      delete this;
      return;
    }
    function_->OnWallpaperDecoded(final_image);
    delete this;
  }

  virtual void OnDecodeImageFailed(const ImageDecoder* decoder) OVERRIDE {
    if (cancel_flag_.IsSet()) {
      delete this;
      return;
    }
    function_->OnFail();
    // TODO(bshe): Dispatches an encoding error event.
    delete this;
  }

 private:
  scoped_refptr<WallpaperSetWallpaperFunction> function_;
  scoped_refptr<ImageDecoder> image_decoder_;
  base::CancellationFlag cancel_flag_;

  DISALLOW_COPY_AND_ASSIGN(WallpaperDecoder);
};

WallpaperSetWallpaperFunction::WallpaperDecoder*
    WallpaperSetWallpaperFunction::wallpaper_decoder_;

WallpaperSetWallpaperFunction::WallpaperSetWallpaperFunction() {
}

WallpaperSetWallpaperFunction::~WallpaperSetWallpaperFunction() {
}

bool WallpaperSetWallpaperFunction::RunImpl() {
  BinaryValue* input = NULL;
  if (args_ == NULL || !args_->GetBinary(0, &input)) {
    return false;
  }
  std::string layout_string;
  if (!args_->GetString(1, &layout_string) || layout_string.empty()) {
    return false;
  }
  layout_ = ash::GetLayoutEnum(layout_string);
  std::string url;
  if (!args_->GetString(2, &url) || url.empty()) {
    return false;
  }
  file_name_ = GURL(url).ExtractFileName();

  // Gets email address while at UI thread.
  email_ = chromeos::UserManager::Get()->GetLoggedInUser().email();

  image_data_.assign(input->GetBuffer(), input->GetSize());
  if (wallpaper_decoder_)
    wallpaper_decoder_->Cancel();
  wallpaper_decoder_ = new WallpaperDecoder(this);
  wallpaper_decoder_->Start(image_data_);

  return true;
}

void WallpaperSetWallpaperFunction::OnWallpaperDecoded(
    const gfx::ImageSkia& wallpaper) {
  wallpaper_ = wallpaper;
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&WallpaperSetWallpaperFunction::SaveToFile,
                 this));
}

void WallpaperSetWallpaperFunction::OnFail() {
  wallpaper_decoder_ = NULL;
  SendResponse(false);
}

void WallpaperSetWallpaperFunction::SaveToFile() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  FilePath wallpaper_dir;
  CHECK(PathService::Get(chrome::DIR_CHROMEOS_WALLPAPERS, &wallpaper_dir));
  if (!file_util::DirectoryExists(wallpaper_dir) &&
      !file_util::CreateDirectory(wallpaper_dir)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&WallpaperSetWallpaperFunction::OnFail,
                   this));
    return;
  }
  FilePath file_path = wallpaper_dir.Append(file_name_);
  if (file_util::PathExists(file_path) ||
      file_util::WriteFile(file_path, image_data_.c_str(),
                           image_data_.size()) != -1 ) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&WallpaperSetWallpaperFunction::SetDecodedWallpaper,
                   this));
  } else {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&WallpaperSetWallpaperFunction::OnFail,
                   this));
  }
}

void WallpaperSetWallpaperFunction::SetDecodedWallpaper() {
  chromeos::WallpaperManager* wallpaper_manager =
      chromeos::WallpaperManager::Get();
  wallpaper_manager->SetWallpaperFromImageSkia(wallpaper_, layout_);
  wallpaper_manager->SaveUserWallpaperInfo(email_, file_name_, layout_,
                                           chromeos::User::DEFAULT);
  wallpaper_decoder_ = NULL;
  SendResponse(true);
}
