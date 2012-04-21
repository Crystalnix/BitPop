// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/facebook_chat/facebook_bitpop_notification_mac.h"

#include "base/mac/mac_util.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/profiles/profile.h"
#import  "chrome/browser/ui/cocoa/dock_icon.h"
#include "content/common/net/url_fetcher_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/url_fetcher.h"
#include "content/public/common/url_fetcher_delegate.h"
#include "content/public/common/url_fetcher_factory.h"
#include "googleurl/src/gurl.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request_status.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "webkit/glue/image_decoder.h"

using content::BrowserThread;
using content::URLFetcher;

static const char* kProfileImageURLPart1 = "http://graph.facebook.com/";
static const char* kProfileImageURLPart2 = "/picture?type=square";


class FacebookProfileImageFetcherDelegate : public content::URLFetcherDelegate {
public:
  FacebookProfileImageFetcherDelegate(Profile* profile,
      const std::string &uid, int num_unread_to_set_on_callback);

  virtual void OnURLFetchComplete(const URLFetcher* source);

private:
  scoped_ptr<URLFetcher> url_fetcher_;
  Profile* profile_;
  int num_unread_to_set_;
};

FacebookProfileImageFetcherDelegate::FacebookProfileImageFetcherDelegate(
    Profile* profile, const std::string &uid, int num_unread_to_set_on_callback)
 :
  profile_(profile),
  num_unread_to_set_(num_unread_to_set_on_callback) {

  url_fetcher_.reset(new URLFetcherImpl(
      GURL(std::string(kProfileImageURLPart1) + uid +
              std::string(kProfileImageURLPart2)),
          URLFetcher::GET,
          this)
  );
  url_fetcher_->SetRequestContext(profile_->GetRequestContext());
  url_fetcher_->Start();
}

void FacebookProfileImageFetcherDelegate::OnURLFetchComplete(const URLFetcher* source) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (source->GetStatus().is_success() && (source->GetResponseCode() / 100 == 2)) {
    std::string mime_type;
    if (source->GetResponseHeaders()->GetMimeType(&mime_type) &&
        (mime_type == "image/gif" || mime_type == "image/png" ||
         mime_type == "image/jpeg")) {

      webkit_glue::ImageDecoder decoder;

      std::string data;
      if (source->GetResponseAsString(&data)) {
        SkBitmap decoded = decoder.Decode(
            reinterpret_cast<const unsigned char*>(data.c_str()),
            data.length());

        CGColorSpaceRef color_space = base::mac::GetSystemColorSpace();
        NSImage* image = gfx::SkBitmapToNSImageWithColorSpace(decoded, color_space);

        [[DockIcon sharedDockIcon] setUnreadNumber:num_unread_to_set_
                                  withProfileImage:image];
        [[DockIcon sharedDockIcon] updateIcon];

        [NSApp requestUserAttention:NSInformationalRequest];
      }
    }
  }

  delete this;
}

// ---------------------------------------------------------------------------
FacebookBitpopNotificationMac::FacebookBitpopNotificationMac(Profile *profile)
  : profile_(profile), delegate_(NULL) {
}

FacebookBitpopNotificationMac::~FacebookBitpopNotificationMac() {
  // delegate_ should delete itself after url fetch finish
}

void FacebookBitpopNotificationMac::ClearNotification() {
  [[DockIcon sharedDockIcon] setUnreadNumber:0 withProfileImage:nil];
  [[DockIcon sharedDockIcon] updateIcon];
}

void FacebookBitpopNotificationMac::NotifyUnreadMessagesWithLastUser(
    int num_unread, std::string user_id) {
  if (![NSApp isActive]) {
    delegate_ = new FacebookProfileImageFetcherDelegate(profile_, user_id,
        num_unread);
  }
}

