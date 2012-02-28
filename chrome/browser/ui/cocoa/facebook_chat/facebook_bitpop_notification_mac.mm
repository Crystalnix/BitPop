// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/facebook_chat/facebook_bitpop_notification_mac.h"

#include "base/mac/mac_util.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/profiles/profile.h"
#import  "chrome/browser/ui/cocoa/dock_icon.h"
#include "content/browser/browser_thread.h"
#include "content/common/url_fetcher.h"
#include "googleurl/src/gurl.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request_status.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "webkit/glue/image_decoder.h"


static const char* kProfileImageURLPart1 = "http://graph.facebook.com/";
static const char* kProfileImageURLPart2 = "/picture?type=square";


class FacebookProfileImageFetcherDelegate : public URLFetcher::Delegate {
public:
  FacebookProfileImageFetcherDelegate(Profile* profile,
      const std::string &uid, int num_unread_to_set_on_callback);

  virtual void OnURLFetchComplete(const URLFetcher* source,
                                  const GURL& url,
                                  const net::URLRequestStatus& status,
                                  int response_code,
                                  const net::ResponseCookies& cookies,
                                  const std::string& data);

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

  url_fetcher_.reset(new URLFetcher(GURL(std::string(kProfileImageURLPart1) + uid +
                                              std::string(kProfileImageURLPart2)),
                                              URLFetcher::GET,
                                              this
                                              )
                                 );
  url_fetcher_->set_request_context(profile_->GetRequestContext());
  url_fetcher_->Start();
}

void FacebookProfileImageFetcherDelegate::OnURLFetchComplete(const URLFetcher* source,
                                                             const GURL& url,
                                                             const net::URLRequestStatus& status,
                                                             int response_code,
                                                             const net::ResponseCookies& cookies,
                                                             const std::string& data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (status.is_success() && (response_code / 100 == 2)) {
    std::string mime_type;
    if (source->response_headers()->GetMimeType(&mime_type) &&
        (mime_type == "image/gif" || mime_type == "image/png" ||
         mime_type == "image/jpeg")) {

      webkit_glue::ImageDecoder decoder;

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

