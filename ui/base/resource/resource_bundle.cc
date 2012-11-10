// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/resource/resource_bundle.h"

#include <vector>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/string_piece.h"
#include "base/synchronization/lock.h"
#include "base/utf_string_conversions.h"
#include "build/build_config.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/layout.h"
#include "ui/base/resource/data_pack.h"
#include "ui/base/ui_base_paths.h"
#include "ui/base/ui_base_switches.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/skbitmap_operations.h"

namespace ui {

namespace {

// Font sizes relative to base font.
#if defined(OS_CHROMEOS) && defined(CROS_FONTS_USING_BCI)
const int kSmallFontSizeDelta = -3;
const int kMediumFontSizeDelta = 2;
const int kLargeFontSizeDelta = 7;
#else
const int kSmallFontSizeDelta = -2;
const int kMediumFontSizeDelta = 3;
const int kLargeFontSizeDelta = 8;
#endif

// Returns the actual scale factor of |bitmap| given the image representations
// which have already been added to |image|.
// TODO(pkotwicz): Remove this once we are no longer loading 1x resources
// as part of 2x data packs.
ui::ScaleFactor GetActualScaleFactor(const gfx::ImageSkia& image,
                                     const SkBitmap& bitmap,
                                     ui::ScaleFactor data_pack_scale_factor) {
  if (image.empty())
    return data_pack_scale_factor;

  return ui::GetScaleFactorFromScale(
      static_cast<float>(bitmap.width()) / image.width());
}

// If 2x resource is missing from |image| or is the incorrect size,
// logs the resource id and creates a 2x version of the resource.
// Blends the created resource with red to make it distinguishable from
// bitmaps in the resource pak.
void Create2xResourceIfMissing(gfx::ImageSkia image, int idr) {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(
          switches::kHighlightMissing2xResources) &&
      command_line->HasSwitch(switches::kLoad2xResources) &&
      !image.HasRepresentation(ui::SCALE_FACTOR_200P)) {
    gfx::ImageSkiaRep image_rep = image.GetRepresentation(SCALE_FACTOR_200P);

    if (image_rep.scale_factor() == ui::SCALE_FACTOR_100P)
      LOG(INFO) << "Missing 2x resource with id " << idr;
    else
      LOG(INFO) << "Incorrectly sized 2x resource with id " << idr;

    SkBitmap bitmap2x = skia::ImageOperations::Resize(image_rep.sk_bitmap(),
        skia::ImageOperations::RESIZE_LANCZOS3,
        image.width() * 2, image.height() * 2);

    SkBitmap mask;
    mask.setConfig(SkBitmap::kARGB_8888_Config,
                   bitmap2x.width(),
                   bitmap2x.height());
    mask.allocPixels();
    mask.eraseColor(SK_ColorRED);
    SkBitmap result = SkBitmapOperations::CreateBlendedBitmap(bitmap2x, mask,
                                                              0.2);
    image.AddRepresentation(gfx::ImageSkiaRep(result, SCALE_FACTOR_200P));
  }
}

}  // namespace

ResourceBundle* ResourceBundle::g_shared_instance_ = NULL;

// static
std::string ResourceBundle::InitSharedInstanceWithLocale(
    const std::string& pref_locale, Delegate* delegate) {
  DCHECK(g_shared_instance_ == NULL) << "ResourceBundle initialized twice";
  g_shared_instance_ = new ResourceBundle(delegate);

  g_shared_instance_->LoadCommonResources();
  std::string result = g_shared_instance_->LoadLocaleResources(pref_locale);
  return result;
}

// static
void ResourceBundle::InitSharedInstanceWithPakFile(
    base::PlatformFile pak_file, bool should_load_common_resources) {
  DCHECK(g_shared_instance_ == NULL) << "ResourceBundle initialized twice";
  g_shared_instance_ = new ResourceBundle(NULL);

  if (should_load_common_resources)
    g_shared_instance_->LoadCommonResources();

  scoped_ptr<DataPack> data_pack(
      new DataPack(SCALE_FACTOR_100P));
  if (!data_pack->LoadFromFile(pak_file)) {
    NOTREACHED() << "failed to load pak file";
    return;
  }
  g_shared_instance_->locale_resources_data_.reset(data_pack.release());
}

// static
void ResourceBundle::InitSharedInstanceWithPakPath(const FilePath& path) {
  DCHECK(g_shared_instance_ == NULL) << "ResourceBundle initialized twice";
  g_shared_instance_ = new ResourceBundle(NULL);

  g_shared_instance_->LoadTestResources(path, path);
}

// static
void ResourceBundle::CleanupSharedInstance() {
  if (g_shared_instance_) {
    delete g_shared_instance_;
    g_shared_instance_ = NULL;
  }
}

// static
bool ResourceBundle::HasSharedInstance() {
  return g_shared_instance_ != NULL;
}

// static
ResourceBundle& ResourceBundle::GetSharedInstance() {
  // Must call InitSharedInstance before this function.
  CHECK(g_shared_instance_ != NULL);
  return *g_shared_instance_;
}

bool ResourceBundle::LocaleDataPakExists(const std::string& locale) {
  return !GetLocaleFilePath(locale, true).empty();
}

void ResourceBundle::AddDataPackFromPath(const FilePath& path,
                                         ScaleFactor scale_factor) {
  // Do not pass an empty |path| value to this method. If the absolute path is
  // unknown pass just the pack file name.
  DCHECK(!path.empty());

  FilePath pack_path = path;
  if (delegate_)
    pack_path = delegate_->GetPathForResourcePack(pack_path, scale_factor);

  // Don't try to load empty values or values that are not absolute paths.
  if (pack_path.empty() || !pack_path.IsAbsolute())
    return;

  scoped_ptr<DataPack> data_pack(
      new DataPack(scale_factor));
  if (data_pack->LoadFromPath(pack_path)) {
    data_packs_.push_back(data_pack.release());
  } else {
    LOG(ERROR) << "Failed to load " << pack_path.value()
               << "\nSome features may not be available.";
  }
}

void ResourceBundle::AddDataPackFromFile(base::PlatformFile file,
                                         ScaleFactor scale_factor) {
  scoped_ptr<DataPack> data_pack(
      new DataPack(scale_factor));
  if (data_pack->LoadFromFile(file)) {
    data_packs_.push_back(data_pack.release());
  } else {
    LOG(ERROR) << "Failed to load data pack from file."
               << "\nSome features may not be available.";
  }
}

#if !defined(OS_MACOSX)
FilePath ResourceBundle::GetLocaleFilePath(const std::string& app_locale,
                                           bool test_file_exists) {
  if (app_locale.empty())
    return FilePath();

  FilePath locale_file_path;

  PathService::Get(ui::DIR_LOCALES, &locale_file_path);

  if (!locale_file_path.empty())
    locale_file_path = locale_file_path.AppendASCII(app_locale + ".pak");

  if (delegate_) {
    locale_file_path =
        delegate_->GetPathForLocalePack(locale_file_path, app_locale);
  }

  // Don't try to load empty values or values that are not absolute paths.
  if (locale_file_path.empty() || !locale_file_path.IsAbsolute())
    return FilePath();

  if (test_file_exists && !file_util::PathExists(locale_file_path))
    return FilePath();

  return locale_file_path;
}
#endif

std::string ResourceBundle::LoadLocaleResources(
    const std::string& pref_locale) {
  DCHECK(!locale_resources_data_.get()) << "locale.pak already loaded";
  std::string app_locale = l10n_util::GetApplicationLocale(pref_locale);
  FilePath locale_file_path = GetOverriddenPakPath();
  if (locale_file_path.empty()) {
    CommandLine* command_line = CommandLine::ForCurrentProcess();
    if (command_line->HasSwitch(switches::kLocalePak)) {
      locale_file_path =
          command_line->GetSwitchValuePath(switches::kLocalePak);
    } else {
      locale_file_path = GetLocaleFilePath(app_locale, true);
    }
  }

  if (locale_file_path.empty()) {
    // It's possible that there is no locale.pak.
    LOG(WARNING) << "locale_file_path.empty()";
    return std::string();
  }

  scoped_ptr<DataPack> data_pack(
      new DataPack(SCALE_FACTOR_100P));
  if (!data_pack->LoadFromPath(locale_file_path)) {
    UMA_HISTOGRAM_ENUMERATION("ResourceBundle.LoadLocaleResourcesError",
                              logging::GetLastSystemErrorCode(), 16000);
    LOG(ERROR) << "failed to load locale.pak";
    NOTREACHED();
    return std::string();
  }

  locale_resources_data_.reset(data_pack.release());
  return app_locale;
}

void ResourceBundle::LoadTestResources(const FilePath& path,
                                       const FilePath& locale_path) {
  // Use the given resource pak for both common and localized resources.
  scoped_ptr<DataPack> data_pack(
      new DataPack(SCALE_FACTOR_100P));
  if (!path.empty() && data_pack->LoadFromPath(path))
    data_packs_.push_back(data_pack.release());

  data_pack.reset(new DataPack(ui::SCALE_FACTOR_NONE));
  if (!locale_path.empty() && data_pack->LoadFromPath(locale_path)) {
    locale_resources_data_.reset(data_pack.release());
  } else {
    locale_resources_data_.reset(
        new DataPack(ui::SCALE_FACTOR_NONE));
  }
}

void ResourceBundle::UnloadLocaleResources() {
  locale_resources_data_.reset();
}

void ResourceBundle::OverrideLocalePakForTest(const FilePath& pak_path) {
  overridden_pak_path_ = pak_path;
}

const FilePath& ResourceBundle::GetOverriddenPakPath() {
  return overridden_pak_path_;
}

std::string ResourceBundle::ReloadLocaleResources(
    const std::string& pref_locale) {
  base::AutoLock lock_scope(*locale_resources_data_lock_);
  UnloadLocaleResources();
  return LoadLocaleResources(pref_locale);
}

SkBitmap* ResourceBundle::GetBitmapNamed(int resource_id) {
  const SkBitmap* bitmap = GetImageNamed(resource_id).ToSkBitmap();
  return const_cast<SkBitmap*>(bitmap);
}

gfx::ImageSkia* ResourceBundle::GetImageSkiaNamed(int resource_id) {
  const gfx::ImageSkia* image = GetImageNamed(resource_id).ToImageSkia();
  return const_cast<gfx::ImageSkia*>(image);
}

gfx::Image& ResourceBundle::GetImageNamed(int resource_id) {
  // Check to see if the image is already in the cache.
  {
    base::AutoLock lock_scope(*images_and_fonts_lock_);
    if (images_.count(resource_id))
      return images_[resource_id];
  }

  gfx::Image image;
  if (delegate_)
    image = delegate_->GetImageNamed(resource_id);

  if (image.IsEmpty()) {
    DCHECK(!delegate_ && !data_packs_.empty()) <<
        "Missing call to SetResourcesDataDLL?";
    gfx::ImageSkia image_skia;
    for (size_t i = 0; i < data_packs_.size(); ++i) {
      scoped_ptr<SkBitmap> bitmap(LoadBitmap(*data_packs_[i], resource_id));
      if (bitmap.get()) {
        ui::ScaleFactor scale_factor;
        if (gfx::Screen::IsDIPEnabled()) {
          scale_factor = GetActualScaleFactor(image_skia, *bitmap,
              data_packs_[i]->GetScaleFactor());
        } else {
          scale_factor = ui::SCALE_FACTOR_100P;
        }
        image_skia.AddRepresentation(gfx::ImageSkiaRep(*bitmap, scale_factor));
      }
    }

    if (image_skia.empty()) {
      LOG(WARNING) << "Unable to load image with id " << resource_id;
      NOTREACHED();  // Want to assert in debug mode.
      // The load failed to retrieve the image; show a debugging red square.
      return GetEmptyImage();
    }

    Create2xResourceIfMissing(image_skia, resource_id);

    image = gfx::Image(image_skia);
  }

  // The load was successful, so cache the image.
  base::AutoLock lock_scope(*images_and_fonts_lock_);

  // Another thread raced the load and has already cached the image.
  if (images_.count(resource_id))
    return images_[resource_id];

  images_[resource_id] = image;
  return images_[resource_id];
}

gfx::Image& ResourceBundle::GetNativeImageNamed(int resource_id) {
  return GetNativeImageNamed(resource_id, RTL_DISABLED);
}

base::RefCountedStaticMemory* ResourceBundle::LoadDataResourceBytes(
    int resource_id,
    ScaleFactor scale_factor) const {
  base::RefCountedStaticMemory* bytes = NULL;
  if (delegate_)
    bytes = delegate_->LoadDataResourceBytes(resource_id, scale_factor);

  if (!bytes) {
    base::StringPiece data = GetRawDataResource(resource_id, scale_factor);
    if (!data.empty()) {
      bytes = new base::RefCountedStaticMemory(
          reinterpret_cast<const unsigned char*>(data.data()), data.length());
    }
  }

  return bytes;
}

base::StringPiece ResourceBundle::GetRawDataResource(
    int resource_id,
    ScaleFactor scale_factor) const {
  base::StringPiece data;
  if (delegate_ &&
      delegate_->GetRawDataResource(resource_id, scale_factor, &data))
    return data;

  if (scale_factor != ui::SCALE_FACTOR_100P) {
    for (size_t i = 0; i < data_packs_.size(); i++) {
      if (data_packs_[i]->GetScaleFactor() == scale_factor &&
          data_packs_[i]->GetStringPiece(resource_id, &data))
        return data;
    }
  }
  for (size_t i = 0; i < data_packs_.size(); i++) {
    if (data_packs_[i]->GetScaleFactor() == ui::SCALE_FACTOR_100P &&
        data_packs_[i]->GetStringPiece(resource_id, &data))
      return data;
  }

  return base::StringPiece();
}

string16 ResourceBundle::GetLocalizedString(int message_id) {
  string16 string;
  if (delegate_ && delegate_->GetLocalizedString(message_id, &string))
    return string;

  // Ensure that ReloadLocaleResources() doesn't drop the resources while
  // we're using them.
  base::AutoLock lock_scope(*locale_resources_data_lock_);

  // If for some reason we were unable to load the resources , return an empty
  // string (better than crashing).
  if (!locale_resources_data_.get()) {
    LOG(WARNING) << "locale resources are not loaded";
    return string16();
  }

  base::StringPiece data;
  if (!locale_resources_data_->GetStringPiece(message_id, &data)) {
    // Fall back on the main data pack (shouldn't be any strings here except in
    // unittests).
    data = GetRawDataResource(message_id, ui::SCALE_FACTOR_NONE);
    if (data.empty()) {
      NOTREACHED() << "unable to find resource: " << message_id;
      return string16();
    }
  }

  // Strings should not be loaded from a data pack that contains binary data.
  ResourceHandle::TextEncodingType encoding =
      locale_resources_data_->GetTextEncodingType();
  DCHECK(encoding == ResourceHandle::UTF16 || encoding == ResourceHandle::UTF8)
      << "requested localized string from binary pack file";

  // Data pack encodes strings as either UTF8 or UTF16.
  string16 msg;
  if (encoding == ResourceHandle::UTF16) {
    msg = string16(reinterpret_cast<const char16*>(data.data()),
                   data.length() / 2);
  } else if (encoding == ResourceHandle::UTF8) {
    msg = UTF8ToUTF16(data);
  }
  return msg;
}

const gfx::Font& ResourceBundle::GetFont(FontStyle style) {
  {
    base::AutoLock lock_scope(*images_and_fonts_lock_);
    LoadFontsIfNecessary();
  }
  switch (style) {
    case BoldFont:
      return *bold_font_;
    case SmallFont:
      return *small_font_;
    case MediumFont:
      return *medium_font_;
    case MediumBoldFont:
      return *medium_bold_font_;
    case LargeFont:
      return *large_font_;
    case LargeBoldFont:
      return *large_bold_font_;
    default:
      return *base_font_;
  }
}

void ResourceBundle::ReloadFonts() {
  base::AutoLock lock_scope(*images_and_fonts_lock_);
  base_font_.reset();
  LoadFontsIfNecessary();
}

ResourceBundle::ResourceBundle(Delegate* delegate)
    : delegate_(delegate),
      images_and_fonts_lock_(new base::Lock),
      locale_resources_data_lock_(new base::Lock) {
}

ResourceBundle::~ResourceBundle() {
  FreeImages();
  UnloadLocaleResources();
}

void ResourceBundle::FreeImages() {
  images_.clear();
}

void ResourceBundle::LoadFontsIfNecessary() {
  images_and_fonts_lock_->AssertAcquired();
  if (!base_font_.get()) {
    if (delegate_) {
      base_font_.reset(delegate_->GetFont(BaseFont).release());
      bold_font_.reset(delegate_->GetFont(BoldFont).release());
      small_font_.reset(delegate_->GetFont(SmallFont).release());
      medium_font_.reset(delegate_->GetFont(MediumFont).release());
      medium_bold_font_.reset(delegate_->GetFont(MediumBoldFont).release());
      large_font_.reset(delegate_->GetFont(LargeFont).release());
      large_bold_font_.reset(delegate_->GetFont(LargeBoldFont).release());
    }

    if (!base_font_.get())
      base_font_.reset(new gfx::Font());

    if (!bold_font_.get()) {
      bold_font_.reset(new gfx::Font());
      *bold_font_ =
          base_font_->DeriveFont(0, base_font_->GetStyle() | gfx::Font::BOLD);
    }

    if (!small_font_.get()) {
      small_font_.reset(new gfx::Font());
      *small_font_ = base_font_->DeriveFont(kSmallFontSizeDelta);
    }

    if (!medium_font_.get()) {
      medium_font_.reset(new gfx::Font());
      *medium_font_ = base_font_->DeriveFont(kMediumFontSizeDelta);
    }

    if (!medium_bold_font_.get()) {
      medium_bold_font_.reset(new gfx::Font());
      *medium_bold_font_ =
          base_font_->DeriveFont(kMediumFontSizeDelta,
                                 base_font_->GetStyle() | gfx::Font::BOLD);
    }

    if (!large_font_.get()) {
      large_font_.reset(new gfx::Font());
      *large_font_ = base_font_->DeriveFont(kLargeFontSizeDelta);
    }

    if (!large_bold_font_.get()) {
       large_bold_font_.reset(new gfx::Font());
      *large_bold_font_ =
          base_font_->DeriveFont(kLargeFontSizeDelta,
                                 base_font_->GetStyle() | gfx::Font::BOLD);
    }
  }
}

SkBitmap* ResourceBundle::LoadBitmap(const ResourceHandle& data_handle,
                                     int resource_id) {
  scoped_refptr<base::RefCountedMemory> memory(
      data_handle.GetStaticMemory(resource_id));
  if (!memory)
    return NULL;

  SkBitmap bitmap;
  if (gfx::PNGCodec::Decode(memory->front(), memory->size(), &bitmap))
    return new SkBitmap(bitmap);

  // 99% of our assets are PNGs, however fallback to JPEG.
  SkBitmap* allocated_bitmap =
      gfx::JPEGCodec::Decode(memory->front(), memory->size());
  if (allocated_bitmap)
    return allocated_bitmap;

  NOTREACHED() << "Unable to decode theme image resource " << resource_id;
  return NULL;
}

gfx::Image& ResourceBundle::GetEmptyImage() {
  base::AutoLock lock(*images_and_fonts_lock_);

  if (empty_image_.IsEmpty()) {
    // The placeholder bitmap is bright red so people notice the problem.
    SkBitmap bitmap;
    bitmap.setConfig(SkBitmap::kARGB_8888_Config, 32, 32);
    bitmap.allocPixels();
    bitmap.eraseARGB(255, 255, 0, 0);
    empty_image_ = gfx::Image(bitmap);
  }
  return empty_image_;
}

}  // namespace ui
