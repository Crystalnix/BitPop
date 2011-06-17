// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_PLUGINS_PPAPI_PPB_URL_REQUEST_INFO_IMPL_H_
#define WEBKIT_PLUGINS_PPAPI_PPB_URL_REQUEST_INFO_IMPL_H_

#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "ppapi/c/ppb_url_request_info.h"
#include "webkit/plugins/ppapi/resource.h"

namespace WebKit {
class WebFrame;
class WebURLRequest;
}

namespace webkit {
namespace ppapi {

class PPB_FileRef_Impl;

class PPB_URLRequestInfo_Impl : public Resource {
 public:
  explicit PPB_URLRequestInfo_Impl(PluginInstance* instance);
  virtual ~PPB_URLRequestInfo_Impl();

  // Returns a pointer to the interface implementing PPB_URLRequestInfo that is
  // exposed to the plugin.
  static const PPB_URLRequestInfo* GetInterface();

  // Resource overrides.
  virtual PPB_URLRequestInfo_Impl* AsPPB_URLRequestInfo_Impl();

  // PPB_URLRequestInfo implementation.
  bool SetUndefinedProperty(PP_URLRequestProperty property);
  bool SetBooleanProperty(PP_URLRequestProperty property, bool value);
  bool SetIntegerProperty(PP_URLRequestProperty property, int32_t value);
  bool SetStringProperty(PP_URLRequestProperty property,
                         const std::string& value);
  bool AppendDataToBody(const std::string& data);
  bool AppendFileToBody(PPB_FileRef_Impl* file_ref,
                        int64_t start_offset,
                        int64_t number_of_bytes,
                        PP_Time expected_last_modified_time);

  WebKit::WebURLRequest ToWebURLRequest(WebKit::WebFrame* frame) const;

  // Whether universal access is required to use this request.
  bool RequiresUniversalAccess() const;

  bool follow_redirects() { return follow_redirects_; }

  bool record_download_progress() const { return record_download_progress_; }
  bool record_upload_progress() const { return record_upload_progress_; }

  bool allow_cross_origin_requests() const {
    return allow_cross_origin_requests_;
  }
  bool allow_credentials() const { return allow_credentials_; }

  int32_t prefetch_buffer_upper_threshold() const {
      return prefetch_buffer_upper_threshold_;
  }
  int32_t prefetch_buffer_lower_threshold() const {
      return prefetch_buffer_lower_threshold_;
  }

 private:
  struct BodyItem;
  typedef std::vector<BodyItem> Body;

  std::string url_;
  std::string method_;
  std::string headers_;
  Body body_;

  bool stream_to_file_;
  bool follow_redirects_;
  bool record_download_progress_;
  bool record_upload_progress_;

  // |has_custom_referrer_url_| is set to false if a custom referrer hasn't been
  // set (or has been set to an Undefined Var) and the default referrer should
  // be used. (Setting the custom referrer to an empty string indicates that no
  // referrer header should be generated.)
  bool has_custom_referrer_url_;
  std::string custom_referrer_url_;

  bool allow_cross_origin_requests_;
  bool allow_credentials_;

  // Similar to the custom referrer (above), but for custom content transfer
  // encoding.
  bool has_custom_content_transfer_encoding_;
  std::string custom_content_transfer_encoding_;

  // Specify permitted range for the size of the buffer unconsumed by plugin.
  int32_t prefetch_buffer_upper_threshold_;
  int32_t prefetch_buffer_lower_threshold_;

  DISALLOW_COPY_AND_ASSIGN(PPB_URLRequestInfo_Impl);
};

}  // namespace ppapi
}  // namespace webkit

#endif  // WEBKIT_PLUGINS_PPAPI_PPB_URL_REQUEST_INFO_IMPL_H_
