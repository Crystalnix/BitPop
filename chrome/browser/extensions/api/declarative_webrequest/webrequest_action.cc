// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/declarative_webrequest/webrequest_action.h"

#include <limits>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/declarative_webrequest/request_stage.h"
#include "chrome/browser/extensions/api/declarative_webrequest/webrequest_constants.h"
#include "chrome/browser/extensions/api/web_request/web_request_api_helpers.h"
#include "chrome/browser/extensions/api/web_request/web_request_permissions.h"
#include "chrome/browser/extensions/extension_info_map.h"
#include "chrome/common/extensions/extension.h"
#include "net/url_request/url_request.h"

namespace extensions {

namespace helpers = extension_web_request_api_helpers;
namespace keys = declarative_webrequest_constants;

namespace {
// Error messages.
const char kInvalidInstanceTypeError[] =
    "An action has an invalid instanceType: %s";

const char kTransparentImageUrl[] = "data:image/png;base64,iVBORw0KGgoAAAANSUh"
    "EUgAAAAEAAAABCAYAAAAfFcSJAAAACklEQVR4nGMAAQAABQABDQottAAAAABJRU5ErkJggg==";
const char kEmptyDocumentUrl[] = "data:text/html,";

#define INPUT_FORMAT_VALIDATE(test) do { \
    if (!(test)) { \
      *bad_message = true; \
      return scoped_ptr<WebRequestAction>(NULL); \
    } \
  } while (0)

scoped_ptr<helpers::RequestCookie> ParseRequestCookie(
    const DictionaryValue* dict) {
  scoped_ptr<helpers::RequestCookie> result(new helpers::RequestCookie);
  std::string tmp;
  if (dict->GetString(keys::kNameKey, &tmp))
    result->name.reset(new std::string(tmp));
  if (dict->GetString(keys::kValueKey, &tmp))
    result->value.reset(new std::string(tmp));
  return result.Pass();
}

scoped_ptr<helpers::ResponseCookie> ParseResponseCookie(
    const DictionaryValue* dict) {
  scoped_ptr<helpers::ResponseCookie> result(new helpers::ResponseCookie);
  std::string string_tmp;
  int int_tmp = 0;
  bool bool_tmp = false;
  if (dict->GetString(keys::kNameKey, &string_tmp))
    result->name.reset(new std::string(string_tmp));
  if (dict->GetString(keys::kValueKey, &string_tmp))
    result->value.reset(new std::string(string_tmp));
  if (dict->GetString(keys::kExpiresKey, &string_tmp))
    result->expires.reset(new std::string(string_tmp));
  if (dict->GetInteger(keys::kMaxAgeKey, &int_tmp))
    result->max_age.reset(new int(int_tmp));
  if (dict->GetString(keys::kDomainKey, &string_tmp))
    result->domain.reset(new std::string(string_tmp));
  if (dict->GetString(keys::kPathKey, &string_tmp))
    result->path.reset(new std::string(string_tmp));
  if (dict->GetBoolean(keys::kSecureKey, &bool_tmp))
    result->secure.reset(new bool(bool_tmp));
  if (dict->GetBoolean(keys::kHttpOnlyKey, &bool_tmp))
    result->http_only.reset(new bool(bool_tmp));
  return result.Pass();
}

// Helper function for WebRequestActions that can be instantiated by just
// calling the constructor.
template <class T>
scoped_ptr<WebRequestAction> CallConstructorFactoryMethod(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  return scoped_ptr<WebRequestAction>(new T);
}

scoped_ptr<WebRequestAction> CreateRedirectRequestAction(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  std::string redirect_url_string;
  INPUT_FORMAT_VALIDATE(
      dict->GetString(keys::kRedirectUrlKey, &redirect_url_string));
  GURL redirect_url(redirect_url_string);
  return scoped_ptr<WebRequestAction>(
      new WebRequestRedirectAction(redirect_url));
}

scoped_ptr<WebRequestAction> CreateRedirectRequestByRegExAction(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  std::string from;
  std::string to;
  INPUT_FORMAT_VALIDATE(dict->GetString(keys::kFromKey, &from));
  INPUT_FORMAT_VALIDATE(dict->GetString(keys::kToKey, &to));

  // TODO(battre): Add this line once we migrate from ICU RegEx to RE2 RegEx.s
  // to = WebRequestRedirectByRegExAction::PerlToRe2Style(to);

  UParseError parse_error;
  UErrorCode status = U_ZERO_ERROR;
  scoped_ptr<icu::RegexPattern> pattern(
      icu::RegexPattern::compile(icu::UnicodeString(from.data(), from.size()),
                                 0, parse_error, status));
  if (U_FAILURE(status) || !pattern.get()) {
    *error = "Invalid pattern '" + from + "' -> '" + to + "'";
    return scoped_ptr<WebRequestAction>(NULL);
  }
  return scoped_ptr<WebRequestAction>(
      new WebRequestRedirectByRegExAction(pattern.Pass(), to));
}

scoped_ptr<WebRequestAction> CreateSetRequestHeaderAction(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  std::string name;
  std::string value;
  INPUT_FORMAT_VALIDATE(dict->GetString(keys::kNameKey, &name));
  INPUT_FORMAT_VALIDATE(dict->GetString(keys::kValueKey, &value));
  return scoped_ptr<WebRequestAction>(
      new WebRequestSetRequestHeaderAction(name, value));
}

scoped_ptr<WebRequestAction> CreateRemoveRequestHeaderAction(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  std::string name;
  INPUT_FORMAT_VALIDATE(dict->GetString(keys::kNameKey, &name));
  return scoped_ptr<WebRequestAction>(
      new WebRequestRemoveRequestHeaderAction(name));
}

scoped_ptr<WebRequestAction> CreateAddResponseHeaderAction(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  std::string name;
  std::string value;
  INPUT_FORMAT_VALIDATE(dict->GetString(keys::kNameKey, &name));
  INPUT_FORMAT_VALIDATE(dict->GetString(keys::kValueKey, &value));
  return scoped_ptr<WebRequestAction>(
      new WebRequestAddResponseHeaderAction(name, value));
}

scoped_ptr<WebRequestAction> CreateRemoveResponseHeaderAction(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  std::string name;
  std::string value;
  INPUT_FORMAT_VALIDATE(dict->GetString(keys::kNameKey, &name));
  bool has_value = dict->GetString(keys::kValueKey, &value);
  return scoped_ptr<WebRequestAction>(
      new WebRequestRemoveResponseHeaderAction(name, value, has_value));
}

scoped_ptr<WebRequestAction> CreateIgnoreRulesAction(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  int minium_priority;
  INPUT_FORMAT_VALIDATE(
      dict->GetInteger(keys::kLowerPriorityThanKey, &minium_priority));
  return scoped_ptr<WebRequestAction>(
      new WebRequestIgnoreRulesAction(minium_priority));
}

scoped_ptr<WebRequestAction> CreateRequestCookieAction(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  using extension_web_request_api_helpers::RequestCookieModification;

  linked_ptr<RequestCookieModification> modification(
      new RequestCookieModification);

  // Get modification type.
  std::string instance_type;
  INPUT_FORMAT_VALIDATE(
      dict->GetString(keys::kInstanceTypeKey, &instance_type));
  if (instance_type == keys::kAddRequestCookieType)
    modification->type = helpers::ADD;
  else if (instance_type == keys::kEditRequestCookieType)
    modification->type = helpers::EDIT;
  else if (instance_type == keys::kRemoveRequestCookieType)
    modification->type = helpers::REMOVE;
  else
    INPUT_FORMAT_VALIDATE(false);

  // Get filter.
  if (modification->type == helpers::EDIT ||
      modification->type == helpers::REMOVE) {
    const DictionaryValue* filter = NULL;
    INPUT_FORMAT_VALIDATE(dict->GetDictionary(keys::kFilterKey, &filter));
    modification->filter = ParseRequestCookie(filter);
  }

  // Get new value.
  if (modification->type == helpers::ADD) {
    const DictionaryValue* value = NULL;
    INPUT_FORMAT_VALIDATE(dict->GetDictionary(keys::kCookieKey, &value));
    modification->modification = ParseRequestCookie(value);
  } else if (modification->type == helpers::EDIT) {
    const DictionaryValue* value = NULL;
    INPUT_FORMAT_VALIDATE(dict->GetDictionary(keys::kModificationKey, &value));
    modification->modification = ParseRequestCookie(value);
  }

  return scoped_ptr<WebRequestAction>(
      new WebRequestRequestCookieAction(modification));
}

scoped_ptr<WebRequestAction> CreateResponseCookieAction(
    const base::DictionaryValue* dict,
    std::string* error,
    bool* bad_message) {
  using extension_web_request_api_helpers::ResponseCookieModification;

  linked_ptr<ResponseCookieModification> modification(
      new ResponseCookieModification);

  // Get modification type.
  std::string instance_type;
  INPUT_FORMAT_VALIDATE(
      dict->GetString(keys::kInstanceTypeKey, &instance_type));
  if (instance_type == keys::kAddResponseCookieType)
    modification->type = helpers::ADD;
  else if (instance_type == keys::kEditResponseCookieType)
    modification->type = helpers::EDIT;
  else if (instance_type == keys::kRemoveResponseCookieType)
    modification->type = helpers::REMOVE;
  else
    INPUT_FORMAT_VALIDATE(false);

  // Get filter.
  if (modification->type == helpers::EDIT ||
      modification->type == helpers::REMOVE) {
    const DictionaryValue* filter = NULL;
    INPUT_FORMAT_VALIDATE(dict->GetDictionary(keys::kFilterKey, &filter));
    modification->filter = ParseResponseCookie(filter);
  }

  // Get new value.
  if (modification->type == helpers::ADD) {
    const DictionaryValue* value = NULL;
    INPUT_FORMAT_VALIDATE(dict->GetDictionary(keys::kCookieKey, &value));
    modification->modification = ParseResponseCookie(value);
  } else if (modification->type == helpers::EDIT) {
    const DictionaryValue* value = NULL;
    INPUT_FORMAT_VALIDATE(dict->GetDictionary(keys::kModificationKey, &value));
    modification->modification = ParseResponseCookie(value);
  }

  return scoped_ptr<WebRequestAction>(
      new WebRequestResponseCookieAction(modification));
}

struct WebRequestActionFactory {
  // Factory methods for WebRequestAction instances. |dict| contains the json
  // dictionary that describes the action. |error| is used to return error
  // messages in case the extension passed an action that was syntactically
  // correct but semantically incorrect. |bad_message| is set to true in case
  // |dict| does not confirm to the validated JSON specification.
  typedef scoped_ptr<WebRequestAction>
      (* FactoryMethod)(const base::DictionaryValue* /* dict */ ,
                        std::string* /* error */,
                        bool* /* bad_message */);
  std::map<std::string, FactoryMethod> factory_methods;

  WebRequestActionFactory() {
    factory_methods[keys::kAddRequestCookieType] =
        &CreateRequestCookieAction;
    factory_methods[keys::kAddResponseCookieType] =
        &CreateResponseCookieAction;
    factory_methods[keys::kAddResponseHeaderType] =
        &CreateAddResponseHeaderAction;
    factory_methods[keys::kCancelRequestType] =
        &CallConstructorFactoryMethod<WebRequestCancelAction>;
    factory_methods[keys::kEditRequestCookieType] =
        &CreateRequestCookieAction;
    factory_methods[keys::kEditResponseCookieType] =
        &CreateResponseCookieAction;
    factory_methods[keys::kRedirectByRegExType] =
        &CreateRedirectRequestByRegExAction;
    factory_methods[keys::kRedirectRequestType] =
        &CreateRedirectRequestAction;
    factory_methods[keys::kRedirectToTransparentImageType] =
        &CallConstructorFactoryMethod<
            WebRequestRedirectToTransparentImageAction>;
    factory_methods[keys::kRedirectToEmptyDocumentType] =
        &CallConstructorFactoryMethod<WebRequestRedirectToEmptyDocumentAction>;
    factory_methods[keys::kRemoveRequestCookieType] =
        &CreateRequestCookieAction;
    factory_methods[keys::kRemoveResponseCookieType] =
        &CreateResponseCookieAction;
    factory_methods[keys::kSetRequestHeaderType] =
        &CreateSetRequestHeaderAction;
    factory_methods[keys::kRemoveRequestHeaderType] =
        &CreateRemoveRequestHeaderAction;
    factory_methods[keys::kRemoveResponseHeaderType] =
        &CreateRemoveResponseHeaderAction;
    factory_methods[keys::kIgnoreRulesType] =
        &CreateIgnoreRulesAction;
  }
};

base::LazyInstance<WebRequestActionFactory>::Leaky
    g_web_request_action_factory = LAZY_INSTANCE_INITIALIZER;

}  // namespace

//
// WebRequestAction
//

WebRequestAction::WebRequestAction() {}

WebRequestAction::~WebRequestAction() {}

int WebRequestAction::GetMinimumPriority() const {
  return std::numeric_limits<int>::min();
}

bool WebRequestAction::HasPermission(const ExtensionInfoMap* extension_info_map,
                                     const std::string& extension_id,
                                     const net::URLRequest* request,
                                     bool crosses_incognito) const {
  if (WebRequestPermissions::HideRequest(request))
    return false;

  // In unit tests we don't have an extension_info_map object here and skip host
  // permission checks.
  if (!extension_info_map)
    return true;

  return WebRequestPermissions::CanExtensionAccessURL(
      extension_info_map, extension_id, request->url(), crosses_incognito,
      ShouldEnforceHostPermissions());
}

bool WebRequestAction::ShouldEnforceHostPermissions() const {
  return true;
}

// static
scoped_ptr<WebRequestAction> WebRequestAction::Create(
    const base::Value& json_action,
    std::string* error,
    bool* bad_message) {
  *error = "";
  *bad_message = false;

  const base::DictionaryValue* action_dict = NULL;
  INPUT_FORMAT_VALIDATE(json_action.GetAsDictionary(&action_dict));

  std::string instance_type;
  INPUT_FORMAT_VALIDATE(
      action_dict->GetString(keys::kInstanceTypeKey, &instance_type));

  WebRequestActionFactory& factory = g_web_request_action_factory.Get();
  std::map<std::string, WebRequestActionFactory::FactoryMethod>::iterator
      factory_method_iter = factory.factory_methods.find(instance_type);
  if (factory_method_iter != factory.factory_methods.end())
    return (*factory_method_iter->second)(action_dict, error, bad_message);

  *error = base::StringPrintf(kInvalidInstanceTypeError, instance_type.c_str());
  return scoped_ptr<WebRequestAction>();
}


//
// WebRequestActionSet
//

WebRequestActionSet::WebRequestActionSet(const Actions& actions)
    : actions_(actions) {}

WebRequestActionSet::~WebRequestActionSet() {}

// static
scoped_ptr<WebRequestActionSet> WebRequestActionSet::Create(
    const AnyVector& actions,
    std::string* error,
    bool* bad_message) {
  *error = "";
  *bad_message = false;
  Actions result;

  for (AnyVector::const_iterator i = actions.begin();
       i != actions.end(); ++i) {
    CHECK(i->get());
    scoped_ptr<WebRequestAction> action =
        WebRequestAction::Create((*i)->value(), error, bad_message);
    if (!error->empty() || *bad_message)
      return scoped_ptr<WebRequestActionSet>(NULL);
    result.push_back(make_linked_ptr(action.release()));
  }

  return scoped_ptr<WebRequestActionSet>(new WebRequestActionSet(result));
}

std::list<LinkedPtrEventResponseDelta> WebRequestActionSet::CreateDeltas(
    const ExtensionInfoMap* extension_info_map,
    const std::string& extension_id,
    const WebRequestRule::RequestData& request_data,
    bool crosses_incognito,
    const base::Time& extension_install_time) const {
  std::list<LinkedPtrEventResponseDelta> result;
  for (Actions::const_iterator i = actions_.begin(); i != actions_.end(); ++i) {
    if (!(*i)->HasPermission(extension_info_map, extension_id,
                             request_data.request, crosses_incognito))
      continue;
    if ((*i)->GetStages() & request_data.stage) {
      LinkedPtrEventResponseDelta delta = (*i)->CreateDelta(
          request_data, extension_id, extension_install_time);
      if (delta.get())
        result.push_back(delta);
    }
  }
  return result;
}

int WebRequestActionSet::GetMinimumPriority() const {
  int minimum_priority = std::numeric_limits<int>::min();
  for (Actions::const_iterator i = actions_.begin(); i != actions_.end(); ++i) {
    minimum_priority = std::max(minimum_priority, (*i)->GetMinimumPriority());
  }
  return minimum_priority;
}

//
// WebRequestCancelAction
//

WebRequestCancelAction::WebRequestCancelAction() {}

WebRequestCancelAction::~WebRequestCancelAction() {}

int WebRequestCancelAction::GetStages() const {
  return ON_BEFORE_REQUEST | ON_BEFORE_SEND_HEADERS | ON_HEADERS_RECEIVED |
      ON_AUTH_REQUIRED;
}

WebRequestAction::Type WebRequestCancelAction::GetType() const {
  return WebRequestAction::ACTION_CANCEL_REQUEST;
}

LinkedPtrEventResponseDelta WebRequestCancelAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  LinkedPtrEventResponseDelta result(
      new helpers::EventResponseDelta(extension_id, extension_install_time));
  result->cancel = true;
  return result;
}

//
// WebRequestRedirectAction
//

WebRequestRedirectAction::WebRequestRedirectAction(const GURL& redirect_url)
    : redirect_url_(redirect_url) {}

WebRequestRedirectAction::~WebRequestRedirectAction() {}

int WebRequestRedirectAction::GetStages() const {
  return ON_BEFORE_REQUEST;
}

WebRequestAction::Type WebRequestRedirectAction::GetType() const {
  return WebRequestAction::ACTION_REDIRECT_REQUEST;
}

LinkedPtrEventResponseDelta WebRequestRedirectAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  if (request_data.request->url() == redirect_url_)
    return LinkedPtrEventResponseDelta(NULL);
  LinkedPtrEventResponseDelta result(
      new helpers::EventResponseDelta(extension_id, extension_install_time));
  result->new_url = redirect_url_;
  return result;
}

//
// WebRequestRedirectToTransparentImageAction
//

WebRequestRedirectToTransparentImageAction::
WebRequestRedirectToTransparentImageAction() {}

WebRequestRedirectToTransparentImageAction::
~WebRequestRedirectToTransparentImageAction() {}

int WebRequestRedirectToTransparentImageAction::GetStages() const {
  return ON_BEFORE_REQUEST;
}

WebRequestAction::Type
WebRequestRedirectToTransparentImageAction::GetType() const {
  return WebRequestAction::ACTION_REDIRECT_TO_TRANSPARENT_IMAGE;
}

bool WebRequestRedirectToTransparentImageAction::ShouldEnforceHostPermissions()
    const {
  return false;
}

LinkedPtrEventResponseDelta
WebRequestRedirectToTransparentImageAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  LinkedPtrEventResponseDelta result(
      new helpers::EventResponseDelta(extension_id, extension_install_time));
  result->new_url = GURL(kTransparentImageUrl);
  return result;
}

//
// WebRequestRedirectToEmptyDocumentAction
//

WebRequestRedirectToEmptyDocumentAction::
WebRequestRedirectToEmptyDocumentAction() {}

WebRequestRedirectToEmptyDocumentAction::
~WebRequestRedirectToEmptyDocumentAction() {}

int WebRequestRedirectToEmptyDocumentAction::GetStages() const {
  return ON_BEFORE_REQUEST;
}

WebRequestAction::Type
WebRequestRedirectToEmptyDocumentAction::GetType() const {
  return WebRequestAction::ACTION_REDIRECT_TO_EMPTY_DOCUMENT;
}

bool
WebRequestRedirectToEmptyDocumentAction::ShouldEnforceHostPermissions() const {
  return false;
}

LinkedPtrEventResponseDelta
WebRequestRedirectToEmptyDocumentAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  LinkedPtrEventResponseDelta result(
      new helpers::EventResponseDelta(extension_id, extension_install_time));
  result->new_url = GURL(kEmptyDocumentUrl);
  return result;
}

//
// WebRequestRedirectByRegExAction
//

WebRequestRedirectByRegExAction::WebRequestRedirectByRegExAction(
    scoped_ptr<icu::RegexPattern> from_pattern,
    const std::string& to_pattern)
    : from_pattern_(from_pattern.Pass()),
      to_pattern_(to_pattern.data(), to_pattern.size()) {}

WebRequestRedirectByRegExAction::~WebRequestRedirectByRegExAction() {}

// About the syntax of the two languages:
//
// ICU (Perl) states:
// $n The text of capture group n will be substituted for $n. n must be >= 0
//    and not greater than the number of capture groups. A $ not followed by a
//    digit has no special meaning, and will appear in the substitution text
//    as itself, a $.
// \  Treat the following character as a literal, suppressing any special
//    meaning. Backslash escaping in substitution text is only required for
//    '$' and '\', but may be used on any other character without bad effects.
//
// RE2, derived from RE2::Rewrite()
// \  May only be followed by a digit or another \. If followed by a single
//    digit, both characters represent the respective capture group. If followed
//    by another \, it is used as an escape sequence.

// static
std::string WebRequestRedirectByRegExAction::PerlToRe2Style(
    const std::string& perl) {
  std::string::const_iterator i = perl.begin();
  std::string result;
  while (i != perl.end()) {
    if (*i == '$') {
      ++i;
      if (i == perl.end()) {
        result += '$';
        return result;
      } else if (isdigit(*i)) {
        result += '\\';
        result += *i;
      } else {
        result += '$';
        result += *i;
      }
    } else if (*i == '\\') {
      ++i;
      if (i == perl.end()) {
        result += '\\';
      } else if (*i == '$') {
        result += '$';
      } else if (*i == '\\') {
        result += "\\\\";
      } else {
        result += *i;
      }
    } else {
      result += *i;
    }
    ++i;
  }
  return result;
}

int WebRequestRedirectByRegExAction::GetStages() const {
  return ON_BEFORE_REQUEST;
}

WebRequestAction::Type WebRequestRedirectByRegExAction::GetType() const {
  return WebRequestAction::ACTION_REDIRECT_BY_REGEX_DOCUMENT;
}

LinkedPtrEventResponseDelta WebRequestRedirectByRegExAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  CHECK(from_pattern_.get());

  UErrorCode status = U_ZERO_ERROR;
  const std::string& old_url = request_data.request->url().spec();
  icu::UnicodeString old_url_unicode(old_url.data(), old_url.size());

  scoped_ptr<icu::RegexMatcher> matcher(
      from_pattern_->matcher(old_url_unicode, status));
  if (U_FAILURE(status) || !matcher.get())
    return LinkedPtrEventResponseDelta(NULL);

  icu::UnicodeString new_url = matcher->replaceAll(to_pattern_, status);
  if (U_FAILURE(status))
    return LinkedPtrEventResponseDelta(NULL);

  std::string new_url_utf8;
  UTF16ToUTF8(new_url.getBuffer(), new_url.length(), &new_url_utf8);

  if (new_url_utf8 == request_data.request->url().spec())
    return LinkedPtrEventResponseDelta(NULL);

  LinkedPtrEventResponseDelta result(
      new extension_web_request_api_helpers::EventResponseDelta(
          extension_id, extension_install_time));
  result->new_url = GURL(new_url_utf8);
  return result;
}

//
// WebRequestSetRequestHeaderAction
//

WebRequestSetRequestHeaderAction::WebRequestSetRequestHeaderAction(
    const std::string& name,
    const std::string& value)
    : name_(name),
      value_(value) {
}

WebRequestSetRequestHeaderAction::~WebRequestSetRequestHeaderAction() {}

int WebRequestSetRequestHeaderAction::GetStages() const {
  return ON_BEFORE_SEND_HEADERS;
}

WebRequestAction::Type
WebRequestSetRequestHeaderAction::GetType() const {
  return WebRequestAction::ACTION_SET_REQUEST_HEADER;
}

LinkedPtrEventResponseDelta
WebRequestSetRequestHeaderAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  LinkedPtrEventResponseDelta result(
      new helpers::EventResponseDelta(extension_id, extension_install_time));
  result->modified_request_headers.SetHeader(name_, value_);
  return result;
}

//
// WebRequestRemoveRequestHeaderAction
//

WebRequestRemoveRequestHeaderAction::WebRequestRemoveRequestHeaderAction(
    const std::string& name)
    : name_(name) {
}

WebRequestRemoveRequestHeaderAction::~WebRequestRemoveRequestHeaderAction() {}

int WebRequestRemoveRequestHeaderAction::GetStages() const {
  return ON_BEFORE_SEND_HEADERS;
}

WebRequestAction::Type
WebRequestRemoveRequestHeaderAction::GetType() const {
  return WebRequestAction::ACTION_REMOVE_REQUEST_HEADER;
}

LinkedPtrEventResponseDelta
WebRequestRemoveRequestHeaderAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  LinkedPtrEventResponseDelta result(
      new helpers::EventResponseDelta(extension_id, extension_install_time));
  result->deleted_request_headers.push_back(name_);
  return result;
}

//
// WebRequestAddResponseHeaderAction
//

WebRequestAddResponseHeaderAction::WebRequestAddResponseHeaderAction(
    const std::string& name,
    const std::string& value)
    : name_(name),
      value_(value) {
}

WebRequestAddResponseHeaderAction::~WebRequestAddResponseHeaderAction() {}

int WebRequestAddResponseHeaderAction::GetStages() const {
  return ON_HEADERS_RECEIVED;
}

WebRequestAction::Type
WebRequestAddResponseHeaderAction::GetType() const {
  return WebRequestAction::ACTION_ADD_RESPONSE_HEADER;
}

LinkedPtrEventResponseDelta
WebRequestAddResponseHeaderAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  net::HttpResponseHeaders* headers =
      request_data.original_response_headers;
  if (!headers)
    return LinkedPtrEventResponseDelta(NULL);

  // Don't generate the header if it exists already.
  if (headers->HasHeaderValue(name_, value_))
    return LinkedPtrEventResponseDelta(NULL);

  LinkedPtrEventResponseDelta result(
      new helpers::EventResponseDelta(extension_id, extension_install_time));
  result->added_response_headers.push_back(make_pair(name_, value_));
  return result;
}

//
// WebRequestRemoveResponseHeaderAction
//

WebRequestRemoveResponseHeaderAction::WebRequestRemoveResponseHeaderAction(
    const std::string& name,
    const std::string& value,
    bool has_value)
    : name_(name),
      value_(value),
      has_value_(has_value) {
}

WebRequestRemoveResponseHeaderAction::~WebRequestRemoveResponseHeaderAction() {}

int WebRequestRemoveResponseHeaderAction::GetStages() const {
  return ON_HEADERS_RECEIVED;
}

WebRequestAction::Type
WebRequestRemoveResponseHeaderAction::GetType() const {
  return WebRequestAction::ACTION_REMOVE_RESPONSE_HEADER;
}

LinkedPtrEventResponseDelta
WebRequestRemoveResponseHeaderAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  net::HttpResponseHeaders* headers =
      request_data.original_response_headers;
  if (!headers)
    return LinkedPtrEventResponseDelta(NULL);

  LinkedPtrEventResponseDelta result(
      new helpers::EventResponseDelta(extension_id, extension_install_time));
  void* iter = NULL;
  std::string current_value;
  while (headers->EnumerateHeader(&iter, name_, &current_value)) {
    if (has_value_ &&
           (current_value.size() != value_.size() ||
            !std::equal(current_value.begin(), current_value.end(),
                        value_.begin(),
                        base::CaseInsensitiveCompare<char>()))) {
      continue;
    }
    result->deleted_response_headers.push_back(make_pair(name_, current_value));
  }
  return result;
}

//
// WebRequestIgnoreRulesAction
//

WebRequestIgnoreRulesAction::WebRequestIgnoreRulesAction(
    int minimum_priority)
    : minimum_priority_(minimum_priority) {
}

WebRequestIgnoreRulesAction::~WebRequestIgnoreRulesAction() {}

int WebRequestIgnoreRulesAction::GetStages() const {
  return ON_BEFORE_REQUEST | ON_BEFORE_SEND_HEADERS | ON_HEADERS_RECEIVED |
      ON_AUTH_REQUIRED;
}

WebRequestAction::Type WebRequestIgnoreRulesAction::GetType() const {
  return WebRequestAction::ACTION_IGNORE_RULES;
}

int WebRequestIgnoreRulesAction::GetMinimumPriority() const {
  return minimum_priority_;
}

bool WebRequestIgnoreRulesAction::ShouldEnforceHostPermissions() const {
  return false;
}

LinkedPtrEventResponseDelta WebRequestIgnoreRulesAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  return LinkedPtrEventResponseDelta(NULL);
}

//
// WebRequestRequestCookieAction
//

WebRequestRequestCookieAction::WebRequestRequestCookieAction(
    linked_ptr<RequestCookieModification> request_cookie_modification)
    : request_cookie_modification_(request_cookie_modification) {
  CHECK(request_cookie_modification_.get());
}

WebRequestRequestCookieAction::~WebRequestRequestCookieAction() {}

int WebRequestRequestCookieAction::GetStages() const {
  return ON_BEFORE_SEND_HEADERS;
}

WebRequestAction::Type WebRequestRequestCookieAction::GetType() const {
  return WebRequestAction::ACTION_MODIFY_REQUEST_COOKIE;
}

LinkedPtrEventResponseDelta WebRequestRequestCookieAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  LinkedPtrEventResponseDelta result(
      new extension_web_request_api_helpers::EventResponseDelta(
          extension_id, extension_install_time));
  result->request_cookie_modifications.push_back(
      request_cookie_modification_);
  return result;
}

//
// WebRequestResponseCookieAction
//

WebRequestResponseCookieAction::WebRequestResponseCookieAction(
    linked_ptr<ResponseCookieModification> response_cookie_modification)
    : response_cookie_modification_(response_cookie_modification) {
  CHECK(response_cookie_modification_.get());
}

WebRequestResponseCookieAction::~WebRequestResponseCookieAction() {}

int WebRequestResponseCookieAction::GetStages() const {
  return ON_HEADERS_RECEIVED;
}

WebRequestAction::Type WebRequestResponseCookieAction::GetType() const {
  return WebRequestAction::ACTION_MODIFY_RESPONSE_COOKIE;
}

LinkedPtrEventResponseDelta WebRequestResponseCookieAction::CreateDelta(
    const WebRequestRule::RequestData& request_data,
    const std::string& extension_id,
    const base::Time& extension_install_time) const {
  CHECK(request_data.stage & GetStages());
  LinkedPtrEventResponseDelta result(
      new extension_web_request_api_helpers::EventResponseDelta(
          extension_id, extension_install_time));
  result->response_cookie_modifications.push_back(
      response_cookie_modification_);
  return result;
}

}  // namespace extensions
