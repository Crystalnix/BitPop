// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/user.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/login/default_user_images.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "grit/theme_resources.h"
#include "ui/base/resource/resource_bundle.h"

namespace chromeos {

namespace {

// Returns account name portion of an email.
std::string GetUserName(const std::string& email) {
  std::string::size_type i = email.find('@');
  if (i == 0 || i == std::string::npos) {
    return email;
  }
  return email.substr(0, i);
}

}  // namespace

// Magic e-mail addresses are bad. They exist here because some code already
// depends on them and it is hard to figure out what. Any user types added in
// the future should be identified by a new |UserType|, not a new magic e-mail
// address.
// The guest user has a magic, empty e-mail address.
const char kGuestUserEMail[] = "";
// The retail mode user has a magic, domainless e-mail address.
const char kRetailModeUserEMail[] = "demouser@";

class RegularUser : public User {
 public:
  explicit RegularUser(const std::string& email);
  virtual ~RegularUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;
  virtual bool can_lock() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(RegularUser);
};

class GuestUser : public User {
 public:
  GuestUser();
  virtual ~GuestUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(GuestUser);
};

class RetailModeUser : public User {
 public:
  RetailModeUser();
  virtual ~RetailModeUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(RetailModeUser);
};

class PublicAccountUser : public User {
 public:
  explicit PublicAccountUser(const std::string& email);
  virtual ~PublicAccountUser();

  // Overridden from User:
  virtual UserType GetType() const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(PublicAccountUser);
};

string16 User::GetDisplayName() const {
  // Fallback to the email account name in case display name haven't been set.
  return display_name_.empty() ?
      UTF8ToUTF16(GetAccountName(true)) :
      display_name_;
}

std::string User::GetAccountName(bool use_display_email) const {
  if (use_display_email && !display_email_.empty())
    return GetUserName(display_email_);
  else
    return GetUserName(email_);
}

bool User::HasDefaultImage() const {
  return image_index_ >= 0 && image_index_ < kDefaultImagesCount;
}

bool User::can_lock() const {
  return false;
}

User* User::CreateRegularUser(const std::string& email) {
  return new RegularUser(email);
}

User* User::CreateGuestUser() {
  return new GuestUser;
}

User* User::CreateRetailModeUser() {
  return new RetailModeUser;
}

User* User::CreatePublicAccountUser(const std::string& email) {
  return new PublicAccountUser(email);
}

User::User(const std::string& email)
    : email_(email),
      oauth_token_status_(OAUTH_TOKEN_STATUS_UNKNOWN),
      image_index_(kInvalidImageIndex),
      image_is_stub_(false),
      image_is_loading_(false) {
}

User::~User() {}

void User::SetImage(const UserImage& user_image, int image_index) {
  user_image_ = user_image;
  image_index_ = image_index;
  image_is_stub_ = false;
  image_is_loading_ = false;
  DCHECK(HasDefaultImage() || user_image.has_raw_image());
}

void User::SetImageURL(const GURL& image_url) {
  user_image_.set_url(image_url);
}

void User::SetStubImage(int image_index, bool is_loading) {
  user_image_ = UserImage(
      *ResourceBundle::GetSharedInstance().
          GetImageSkiaNamed(IDR_PROFILE_PICTURE_LOADING));
  image_index_ = image_index;
  image_is_stub_ = true;
  image_is_loading_ = is_loading;
}

RegularUser::RegularUser(const std::string& email) : User(email) {
  set_display_email(email);
}

RegularUser::~RegularUser() {}

User::UserType RegularUser::GetType() const {
  return USER_TYPE_REGULAR;
}

bool RegularUser::can_lock() const {
  return true;
}

GuestUser::GuestUser() : User(kGuestUserEMail) {
  set_display_email("");
}

GuestUser::~GuestUser() {}

User::UserType GuestUser::GetType() const {
  return USER_TYPE_GUEST;
}

RetailModeUser::RetailModeUser() : User(kRetailModeUserEMail) {
  set_display_email("");
}

RetailModeUser::~RetailModeUser() {}

User::UserType RetailModeUser::GetType() const {
  return USER_TYPE_RETAIL_MODE;
}

PublicAccountUser::PublicAccountUser(const std::string& email) : User(email) {
}

PublicAccountUser::~PublicAccountUser() {}

User::UserType PublicAccountUser::GetType() const {
  return USER_TYPE_PUBLIC_ACCOUNT;
}

}  // namespace chromeos
