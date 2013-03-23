// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ntp/thumbnail_source.h"

#include "base/callback.h"
#include "base/message_loop.h"
#include "base/memory/ref_counted_memory.h"
#include "chrome/browser/thumbnails/thumbnail_service.h"
#include "chrome/browser/thumbnails/thumbnail_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/theme_resources.h"
#include "ui/base/resource/resource_bundle.h"

ThumbnailSource::ThumbnailSource(Profile* profile)
    : DataSource(chrome::kChromeUIThumbnailHost, MessageLoop::current()),
      // Set ThumbnailService now as Profile isn't thread safe.
      thumbnail_service_(ThumbnailServiceFactory::GetForProfile(profile)) {
}

ThumbnailSource::~ThumbnailSource() {
}

void ThumbnailSource::StartDataRequest(const std::string& path,
                                       bool is_incognito,
                                       int request_id) {
  scoped_refptr<base::RefCountedMemory> data;
  if (thumbnail_service_->GetPageThumbnail(GURL(path), &data)) {
    // We have the thumbnail.
    SendResponse(request_id, data.get());
  } else {
    SendDefaultThumbnail(request_id);
  }
}

std::string ThumbnailSource::GetMimeType(const std::string&) const {
  // We need to explicitly return a mime type, otherwise if the user tries to
  // drag the image they get no extension.
  return "image/png";
}

MessageLoop* ThumbnailSource::MessageLoopForRequestPath(
    const std::string& path) const {
  // TopSites can be accessed from the IO thread.
  return thumbnail_service_.get() ?
      NULL : DataSource::MessageLoopForRequestPath(path);
}

void ThumbnailSource::SendDefaultThumbnail(int request_id) {
  SendResponse(request_id, default_thumbnail_);
}
