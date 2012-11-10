// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_WEBREQUEST_WEBREQUEST_ACTION_H_
#define CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_WEBREQUEST_WEBREQUEST_ACTION_H_

#include <list>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/linked_ptr.h"
#include "chrome/browser/extensions/api/declarative_webrequest/request_stage.h"
#include "chrome/browser/extensions/api/declarative_webrequest/webrequest_rule.h"
#include "chrome/browser/extensions/api/web_request/web_request_api_helpers.h"
#include "chrome/common/extensions/api/events.h"
#include "googleurl/src/gurl.h"
#include "unicode/regex.h"

class WebRequestPermission;

namespace base {
class DictionaryValue;
class Time;
class Value;
}

namespace extension_web_request_api_helpers {
struct EventResponseDelta;
}

namespace extensions {
class Extension;
}

namespace net {
class URLRequest;
}

namespace extensions {

typedef linked_ptr<extension_web_request_api_helpers::EventResponseDelta>
    LinkedPtrEventResponseDelta;

// Base class for all WebRequestActions of the declarative Web Request API.
class WebRequestAction {
 public:
  // Type identifiers for concrete WebRequestActions.
  enum Type {
    ACTION_CANCEL_REQUEST,
    ACTION_REDIRECT_REQUEST,
    ACTION_REDIRECT_TO_TRANSPARENT_IMAGE,
    ACTION_REDIRECT_TO_EMPTY_DOCUMENT,
    ACTION_REDIRECT_BY_REGEX_DOCUMENT,
    ACTION_SET_REQUEST_HEADER,
    ACTION_REMOVE_REQUEST_HEADER,
    ACTION_ADD_RESPONSE_HEADER,
    ACTION_REMOVE_RESPONSE_HEADER,
    ACTION_IGNORE_RULES,
    ACTION_MODIFY_REQUEST_COOKIE,
    ACTION_MODIFY_RESPONSE_COOKIE,
  };

  WebRequestAction();
  virtual ~WebRequestAction();

  // Returns a bit vector representing extensions::RequestStage. The bit vector
  // contains a 1 for each request stage during which the condition can be
  // tested.
  virtual int GetStages() const = 0;

  virtual Type GetType() const = 0;

  // Returns the minimum priority of rules that may be evaluated after
  // this rule. Defaults to MIN_INT.
  virtual int GetMinimumPriority() const;

  // Returns whether the specified extension has permission to execute this
  // action on |request|. Checks the host permission if
  // ShouldEnforceHostPermissions instructs to do that.
  // |extension_info_map| may only be NULL for during testing, in which case
  // host permissions are ignored. |crosses_incognito| specifies
  // whether the request comes from a different profile than |extension_id|
  // but was processed because the extension is in spanning mode.
  virtual bool HasPermission(const ExtensionInfoMap* extension_info_map,
                             const std::string& extension_id,
                             const net::URLRequest* request,
                             bool crosses_incognito) const;

  // Returns whether host permissions shall be enforced by this actions.
  // Used by the standard implementation of HasPermission. Defaults to true.
  virtual bool ShouldEnforceHostPermissions() const;

  // Factory method that instantiates a concrete WebRequestAction
  // implementation according to |json_action|, the representation of the
  // WebRequestAction as received from the extension API.
  // Sets |error| and returns NULL in case of a semantic error that cannot
  // be caught by schema validation. Sets |bad_message| and returns NULL
  // in case the input is syntactically unexpected.
  static scoped_ptr<WebRequestAction> Create(const base::Value& json_action,
                                             std::string* error,
                                             bool* bad_message);

  // Returns a description of the modification to the request caused by
  // this action.
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const = 0;
};

// Immutable container for multiple actions.
//
// TODO(battre): As WebRequestActionSet can become the single owner of all
// actions, we can optimize here by making some of them singletons (e.g. Cancel
// actions).
class WebRequestActionSet {
 public:
  typedef std::vector<linked_ptr<json_schema_compiler::any::Any> > AnyVector;
  typedef std::vector<linked_ptr<WebRequestAction> > Actions;

  explicit WebRequestActionSet(const Actions& actions);
  virtual ~WebRequestActionSet();

  // Factory method that instantiates a WebRequestActionSet according to
  // |actions| which represents the array of actions received from the
  // extension API.
  static scoped_ptr<WebRequestActionSet> Create(const AnyVector& actions,
                                                std::string* error,
                                                bool* bad_message);

  // Returns a description of the modifications to |request_data.request| caused
  // by the |actions_| that can be executed at |request.stage|. If |extension|
  // is not NULL, permissions of extensions are checked.
  std::list<LinkedPtrEventResponseDelta> CreateDeltas(
      const ExtensionInfoMap* extension_info_map,
      const std::string& extension_id,
      const WebRequestRule::RequestData& request_data,
      bool crosses_incognito,
      const base::Time& extension_install_time) const;

  // Returns the minimum priority of rules that may be evaluated after
  // this rule. Defaults to MIN_INT.
  int GetMinimumPriority() const;

  const Actions& actions() const { return actions_; }

 private:
  Actions actions_;

  DISALLOW_COPY_AND_ASSIGN(WebRequestActionSet);
};

//
// The following are concrete actions.
//

// Action that instructs to cancel a network request.
class WebRequestCancelAction : public WebRequestAction {
 public:
  WebRequestCancelAction();
  virtual ~WebRequestCancelAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(WebRequestCancelAction);
};

// Action that instructs to redirect a network request.
class WebRequestRedirectAction : public WebRequestAction {
 public:
  explicit WebRequestRedirectAction(const GURL& redirect_url);
  virtual ~WebRequestRedirectAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  GURL redirect_url_;  // Target to which the request shall be redirected.

  DISALLOW_COPY_AND_ASSIGN(WebRequestRedirectAction);
};

// Action that instructs to redirect a network request to a transparent image.
class WebRequestRedirectToTransparentImageAction : public WebRequestAction {
 public:
  WebRequestRedirectToTransparentImageAction();
  virtual ~WebRequestRedirectToTransparentImageAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual bool ShouldEnforceHostPermissions() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(WebRequestRedirectToTransparentImageAction);
};


// Action that instructs to redirect a network request to an empty document.
class WebRequestRedirectToEmptyDocumentAction : public WebRequestAction {
 public:
  WebRequestRedirectToEmptyDocumentAction();
  virtual ~WebRequestRedirectToEmptyDocumentAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual bool ShouldEnforceHostPermissions() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(WebRequestRedirectToEmptyDocumentAction);
};

// Action that instructs to redirect a network request.
class WebRequestRedirectByRegExAction : public WebRequestAction {
 public:
  // The |to_pattern| has to be passed in ICU syntax.
  // TODO(battre): Change this to Perl style when migrated to RE2.
  explicit WebRequestRedirectByRegExAction(
      scoped_ptr<icu::RegexPattern> from_pattern,
      const std::string& to_pattern);
  virtual ~WebRequestRedirectByRegExAction();

  // Conversion of capture group styles between Perl style ($1, $2, ...) and
  // RE2 (\1, \2, ...).
  static std::string PerlToRe2Style(const std::string& perl);

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  scoped_ptr<icu::RegexPattern> from_pattern_;
  icu::UnicodeString to_pattern_;

  DISALLOW_COPY_AND_ASSIGN(WebRequestRedirectByRegExAction);
};

// Action that instructs to set a request header.
class WebRequestSetRequestHeaderAction : public WebRequestAction {
 public:
  WebRequestSetRequestHeaderAction(const std::string& name,
                                   const std::string& value);
  virtual ~WebRequestSetRequestHeaderAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  std::string name_;
  std::string value_;
  DISALLOW_COPY_AND_ASSIGN(WebRequestSetRequestHeaderAction);
};

// Action that instructs to remove a request header.
class WebRequestRemoveRequestHeaderAction : public WebRequestAction {
 public:
  explicit WebRequestRemoveRequestHeaderAction(const std::string& name);
  virtual ~WebRequestRemoveRequestHeaderAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  std::string name_;
  DISALLOW_COPY_AND_ASSIGN(WebRequestRemoveRequestHeaderAction);
};

// Action that instructs to add a response header.
class WebRequestAddResponseHeaderAction : public WebRequestAction {
 public:
  WebRequestAddResponseHeaderAction(const std::string& name,
                                    const std::string& value);
  virtual ~WebRequestAddResponseHeaderAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  std::string name_;
  std::string value_;
  DISALLOW_COPY_AND_ASSIGN(WebRequestAddResponseHeaderAction);
};

// Action that instructs to remove a response header.
class WebRequestRemoveResponseHeaderAction : public WebRequestAction {
 public:
  explicit WebRequestRemoveResponseHeaderAction(const std::string& name,
                                                const std::string& value,
                                                bool has_value);
  virtual ~WebRequestRemoveResponseHeaderAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  std::string name_;
  std::string value_;
  bool has_value_;
  DISALLOW_COPY_AND_ASSIGN(WebRequestRemoveResponseHeaderAction);
};

// Action that instructs to ignore rules below a certain priority.
class WebRequestIgnoreRulesAction : public WebRequestAction {
 public:
  explicit WebRequestIgnoreRulesAction(int minimum_priority);
  virtual ~WebRequestIgnoreRulesAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual int GetMinimumPriority() const OVERRIDE;
  virtual bool ShouldEnforceHostPermissions() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  int minimum_priority_;
  DISALLOW_COPY_AND_ASSIGN(WebRequestIgnoreRulesAction);
};

// Action that instructs to modify (add, edit, remove) a request cookie.
class WebRequestRequestCookieAction : public WebRequestAction {
 public:
  typedef extension_web_request_api_helpers::RequestCookieModification
      RequestCookieModification;

  explicit WebRequestRequestCookieAction(
      linked_ptr<RequestCookieModification> request_cookie_modification);
  virtual ~WebRequestRequestCookieAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  linked_ptr<RequestCookieModification> request_cookie_modification_;
  DISALLOW_COPY_AND_ASSIGN(WebRequestRequestCookieAction);
};

// Action that instructs to modify (add, edit, remove) a response cookie.
class WebRequestResponseCookieAction : public WebRequestAction {
 public:
  typedef extension_web_request_api_helpers::ResponseCookieModification
      ResponseCookieModification;

  explicit WebRequestResponseCookieAction(
      linked_ptr<ResponseCookieModification> response_cookie_modification);
  virtual ~WebRequestResponseCookieAction();

  // Implementation of WebRequestAction:
  virtual int GetStages() const OVERRIDE;
  virtual Type GetType() const OVERRIDE;
  virtual LinkedPtrEventResponseDelta CreateDelta(
      const WebRequestRule::RequestData& request_data,
      const std::string& extension_id,
      const base::Time& extension_install_time) const OVERRIDE;

 private:
  linked_ptr<ResponseCookieModification> response_cookie_modification_;
  DISALLOW_COPY_AND_ASSIGN(WebRequestResponseCookieAction);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_WEBREQUEST_WEBREQUEST_ACTION_H_
