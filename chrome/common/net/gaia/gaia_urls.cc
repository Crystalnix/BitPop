// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/net/gaia/gaia_urls.h"

#include "base/command_line.h"
#include "chrome/common/chrome_switches.h"

namespace {

// Gaia service constants
const char kDefaultGaiaBaseUrl[] = "accounts.google.com";

// Gaia service constants
const char kDefaultGaiaOAuthBaseUrl[] = "www.google.com";

const char kCaptchaUrlPrefixSuffix[] = "/";
const char kClientLoginUrlSuffix[] = "/ClientLogin";
const char kServiceLoginUrlSuffix[] = "/ServiceLogin";
const char kIssueAuthTokenUrlSuffix[] = "/IssueAuthToken";
const char kGetUserInfoUrlSuffix[] = "/GetUserInfo";
const char kTokenAuthUrlSuffix[] = "/TokenAuth";
const char kMergeSessionUrlSuffix[] = "/MergeSession";

const char kOAuthGetAccessTokenUrlSuffix[] = "/OAuthGetAccessToken";
const char kOAuthWrapBridgeUrlSuffix[] = "/OAuthWrapBridge";
const char kOAuth1LoginUrlSuffix[] = "/OAuthLogin";
const char kOAuthRevokeTokenUrlSuffix[] = "/AuthSubRevokeToken";

// Federated login constants
const char kDefaultFederatedLoginHost[] = "www.google.com";
const char kDefaultFederatedLoginPath[] = "/accounts";
const char kGetOAuthTokenUrlSuffix[] = "/o8/GetOAuthToken";

// OAuth2 client id for Google Chrome which is registered as an
// installed application.
static const char kOAuth2ChromeClientId[] =
    "77185425430.apps.googleusercontent.com";
// For an installed application, client secret is not really a secret since
// it is expected to be embeeded in the application.
// See documentation at
// http://code.google.com/apis/accounts/docs/OAuth2InstalledApp.html
static const char kOAuth2ChromeClientSecret[] =
    "OTJgUOQcT7lO7GsGZq2G4IlT";
const char kClientLoginToOAuth2Url[] =
    "https://accounts.google.com/o/oauth2/programmatic_auth";
const char kOAuth2TokenUrl[] =
    "https://accounts.google.com/o/oauth2/token";
const char kOAuth2IssueTokenUrl[] =
    "https://www.googleapis.com/oauth2/v2/IssueToken";

}  // namespacce

GaiaUrls* GaiaUrls::GetInstance() {
  return Singleton<GaiaUrls>::get();
}

GaiaUrls::GaiaUrls() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  std::string host_base;
  if (command_line->HasSwitch(switches::kGaiaHost)) {
    host_base = command_line->GetSwitchValueASCII(switches::kGaiaHost);
  } else {
    host_base = kDefaultGaiaBaseUrl;
  }

  captcha_url_prefix_ = "http://" + host_base + kCaptchaUrlPrefixSuffix;
  gaia_origin_url_ = "https://" + host_base;
  std::string gaia_url_base = gaia_origin_url_;
  if (command_line->HasSwitch(switches::kGaiaUrlPath)) {
    std::string path =
        command_line->GetSwitchValueASCII(switches::kGaiaUrlPath);
    if (!path.empty()) {
      if (path[0] != '/')
        gaia_url_base.append("/");

      gaia_url_base.append(path);
    }
  }

  client_login_url_ = gaia_url_base + kClientLoginUrlSuffix;
  service_login_url_ = gaia_url_base + kServiceLoginUrlSuffix;
  issue_auth_token_url_ = gaia_url_base + kIssueAuthTokenUrlSuffix;
  get_user_info_url_ = gaia_url_base + kGetUserInfoUrlSuffix;
  token_auth_url_ = gaia_url_base + kTokenAuthUrlSuffix;
  merge_session_url_ = gaia_url_base + kMergeSessionUrlSuffix;

  // Federated login is not part of Gaia and has its own endpoints.
  std::string oauth_host_base;
  if (command_line->HasSwitch(switches::kGaiaOAuthHost)) {
    oauth_host_base =
        command_line->GetSwitchValueASCII(switches::kGaiaOAuthHost);
  } else {
    oauth_host_base = kDefaultFederatedLoginHost;
  }

  std::string gaia_oauth_url_base = "https://"+oauth_host_base;
  if (command_line->HasSwitch(switches::kGaiaOAuthUrlPath)) {
    std::string path =
        command_line->GetSwitchValueASCII(switches::kGaiaOAuthUrlPath);
    if (!path.empty()) {
      if (path[0] != '/')
        gaia_oauth_url_base.append("/");

      gaia_oauth_url_base.append(path);
    }
  } else {
    gaia_oauth_url_base.append(kDefaultFederatedLoginPath);
  }
  get_oauth_token_url_ = gaia_oauth_url_base +
                         kGetOAuthTokenUrlSuffix;

  oauth_get_access_token_url_ = gaia_url_base +
                                kOAuthGetAccessTokenUrlSuffix;
  oauth_wrap_bridge_url_ = gaia_url_base + kOAuthWrapBridgeUrlSuffix;
  oauth_revoke_token_url_ = gaia_url_base + kOAuthRevokeTokenUrlSuffix;
  oauth1_login_url_ = gaia_url_base + kOAuth1LoginUrlSuffix;

  // TODO(joaodasilva): these aren't configurable for now, but are managed here
  // so that users of Gaia URLs don't have to use static constants.
  // http://crbug.com/97126
  oauth1_login_scope_ = "https://www.google.com/accounts/OAuthLogin";
  oauth_user_info_url_ = "https://www.googleapis.com/oauth2/v1/userinfo";
  oauth_wrap_bridge_user_info_scope_ =
      "https://www.googleapis.com/auth/userinfo.email";
  client_oauth_url_ = "https://accounts.google.com/ClientOAuth";

  oauth2_chrome_client_id_ = kOAuth2ChromeClientId;
  oauth2_chrome_client_secret_ = kOAuth2ChromeClientSecret;
  client_login_to_oauth2_url_ = kClientLoginToOAuth2Url;
  oauth2_token_url_ = kOAuth2TokenUrl;
  oauth2_issue_token_url_ = kOAuth2IssueTokenUrl;

  gaia_login_form_realm_ = "https://accounts.google.com/";

}

GaiaUrls::~GaiaUrls() {
}

const std::string& GaiaUrls::captcha_url_prefix() {
  return captcha_url_prefix_;
}

const std::string& GaiaUrls::gaia_origin_url() {
  return gaia_origin_url_;
}

const std::string& GaiaUrls::client_login_url() {
  return client_login_url_;
}

const std::string& GaiaUrls::service_login_url() {
  return service_login_url_;
}

const std::string& GaiaUrls::issue_auth_token_url() {
  return issue_auth_token_url_;
}

const std::string& GaiaUrls::get_user_info_url() {
  return get_user_info_url_;
}

const std::string& GaiaUrls::token_auth_url() {
  return token_auth_url_;
}

const std::string& GaiaUrls::merge_session_url() {
  return merge_session_url_;
}

const std::string& GaiaUrls::get_oauth_token_url() {
  return get_oauth_token_url_;
}

const std::string& GaiaUrls::oauth_get_access_token_url() {
  return oauth_get_access_token_url_;
}

const std::string& GaiaUrls::oauth_wrap_bridge_url() {
  return oauth_wrap_bridge_url_;
}

const std::string& GaiaUrls::oauth_user_info_url() {
  return oauth_user_info_url_;
}

const std::string& GaiaUrls::oauth_revoke_token_url() {
  return oauth_revoke_token_url_;
}

const std::string& GaiaUrls::oauth1_login_url() {
  return oauth1_login_url_;
}

const std::string& GaiaUrls::oauth1_login_scope() {
  return oauth1_login_scope_;
}

const std::string& GaiaUrls::oauth_wrap_bridge_user_info_scope() {
  return oauth_wrap_bridge_user_info_scope_;
}

const std::string& GaiaUrls::client_oauth_url() {
  return client_oauth_url_;
}

const std::string& GaiaUrls::oauth2_chrome_client_id() {
  return oauth2_chrome_client_id_;
}

const std::string& GaiaUrls::oauth2_chrome_client_secret() {
  return oauth2_chrome_client_secret_;
}

const std::string& GaiaUrls::client_login_to_oauth2_url() {
  return client_login_to_oauth2_url_;
}

const std::string& GaiaUrls::oauth2_token_url() {
  return oauth2_token_url_;
}

const std::string& GaiaUrls::oauth2_issue_token_url() {
  return oauth2_issue_token_url_;
}


const std::string& GaiaUrls::gaia_login_form_realm() {
  return gaia_login_form_realm_;
}
