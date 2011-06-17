// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/automation/automation_util.h"

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/time.h"
#include "base/values.h"
#include "chrome/browser/automation/automation_provider.h"
#include "chrome/browser/automation/automation_provider_json.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser.h"
#include "content/browser/browser_thread.h"
#include "content/browser/renderer_host/browser_render_process_host.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "net/base/cookie_monster.h"
#include "net/base/cookie_store.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

namespace {

void GetCookiesOnIOThread(
    const GURL& url,
    const scoped_refptr<net::URLRequestContextGetter>& context_getter,
    base::WaitableEvent* event,
    std::string* cookies) {
  *cookies =
      context_getter->GetURLRequestContext()->cookie_store()->GetCookies(url);
  event->Signal();
}

void GetCanonicalCookiesOnIOThread(
    const GURL& url,
    const scoped_refptr<net::URLRequestContextGetter>& context_getter,
    base::WaitableEvent* event,
    net::CookieList* cookie_list) {
  *cookie_list =
      context_getter->GetURLRequestContext()->cookie_store()->
      GetCookieMonster()->GetAllCookiesForURL(url);
  event->Signal();
}

void SetCookieOnIOThread(
    const GURL& url,
    const std::string& value,
    const scoped_refptr<net::URLRequestContextGetter>& context_getter,
    base::WaitableEvent* event,
    bool* success) {
  *success =
      context_getter->GetURLRequestContext()->cookie_store()->
      SetCookie(url, value);
  event->Signal();
}

void SetCookieWithDetailsOnIOThread(
    const GURL& url,
    const net::CookieMonster::CanonicalCookie& cookie,
    const std::string& original_domain,
    const scoped_refptr<net::URLRequestContextGetter>& context_getter,
    base::WaitableEvent* event,
    bool* success) {
  net::CookieMonster* cookie_monster =
      context_getter->GetURLRequestContext()->cookie_store()->
      GetCookieMonster();
  *success = cookie_monster->SetCookieWithDetails(
      url, cookie.Name(), cookie.Value(), original_domain,
      cookie.Path(), cookie.ExpiryDate(), cookie.IsSecure(),
      cookie.IsHttpOnly());
  event->Signal();
}

void DeleteCookieOnIOThread(
    const GURL& url,
    const std::string& name,
    const scoped_refptr<net::URLRequestContextGetter>& context_getter,
    base::WaitableEvent* event) {
  context_getter->GetURLRequestContext()->cookie_store()->
      DeleteCookie(url, name);
  event->Signal();
}

}  // namespace

namespace automation_util {

Browser* GetBrowserAt(int index) {
  if (index < 0 || index >= static_cast<int>(BrowserList::size()))
    return NULL;
  return *(BrowserList::begin() + index);
}

TabContents* GetTabContentsAt(int browser_index, int tab_index) {
  if (tab_index < 0)
    return NULL;
  Browser* browser = GetBrowserAt(browser_index);
  if (!browser || tab_index >= browser->tab_count())
    return NULL;
  return browser->GetTabContentsAt(tab_index);
}

void GetCookies(const GURL& url,
                TabContents* contents,
                int* value_size,
                std::string* value) {
  *value_size = -1;
  if (url.is_valid() && contents) {
    // Since we may be on the UI thread don't call GetURLRequestContext().
    // Get the request context specific to the current TabContents and app.
    const Extension* installed_app = static_cast<BrowserRenderProcessHost*>(
        contents->render_view_host()->process())->installed_app();
    scoped_refptr<net::URLRequestContextGetter> context_getter =
        contents->profile()->GetRequestContextForPossibleApp(installed_app);

    base::WaitableEvent event(true /* manual reset */,
                              false /* not initially signaled */);
    CHECK(BrowserThread::PostTask(
              BrowserThread::IO, FROM_HERE,
              NewRunnableFunction(&GetCookiesOnIOThread,
                                  url, context_getter, &event, value)));
    event.Wait();

    *value_size = static_cast<int>(value->size());
  }
}

void SetCookie(const GURL& url,
               const std::string& value,
               TabContents* contents,
               int* response_value) {
  *response_value = -1;

  if (url.is_valid() && contents) {
    // Since we may be on the UI thread don't call GetURLRequestContext().
    // Get the request context specific to the current TabContents and app.
    const Extension* installed_app = static_cast<BrowserRenderProcessHost*>(
        contents->render_view_host()->process())->installed_app();
    scoped_refptr<net::URLRequestContextGetter> context_getter =
        contents->profile()->GetRequestContextForPossibleApp(installed_app);

    base::WaitableEvent event(true /* manual reset */,
                              false /* not initially signaled */);
    bool success = false;
    CHECK(BrowserThread::PostTask(
              BrowserThread::IO, FROM_HERE,
              NewRunnableFunction(&SetCookieOnIOThread,
                                  url, value, context_getter, &event,
                                  &success)));
    event.Wait();
    if (success)
      *response_value = 1;
  }
}

void DeleteCookie(const GURL& url,
                  const std::string& cookie_name,
                  TabContents* contents,
                  bool* success) {
  *success = false;
  if (url.is_valid() && contents) {
    // Since we may be on the UI thread don't call GetURLRequestContext().
    // Get the request context specific to the current TabContents and app.
    const Extension* installed_app = static_cast<BrowserRenderProcessHost*>(
        contents->render_view_host()->process())->installed_app();
    scoped_refptr<net::URLRequestContextGetter> context_getter =
        contents->profile()->GetRequestContextForPossibleApp(installed_app);

    base::WaitableEvent event(true /* manual reset */,
                              false /* not initially signaled */);
    CHECK(BrowserThread::PostTask(
              BrowserThread::IO, FROM_HERE,
              NewRunnableFunction(&DeleteCookieOnIOThread,
                                  url, cookie_name, context_getter, &event)));
    event.Wait();
    *success = true;
  }
}

void GetCookiesJSON(AutomationProvider* provider,
                    DictionaryValue* args,
                    IPC::Message* reply_message) {
  AutomationJSONReply reply(provider, reply_message);
  std::string url;
  if (!args->GetString("url", &url)) {
    reply.SendError("'url' missing or invalid");
    return;
  }

  // Since we may be on the UI thread don't call GetURLRequestContext().
  scoped_refptr<net::URLRequestContextGetter> context_getter =
      provider->profile()->GetRequestContext();

  net::CookieList cookie_list;
  base::WaitableEvent event(true /* manual reset */,
                            false /* not initially signaled */);
  Task* task = NewRunnableFunction(
      &GetCanonicalCookiesOnIOThread,
      GURL(url), context_getter, &event, &cookie_list);
  if (!BrowserThread::PostTask(BrowserThread::IO, FROM_HERE, task)) {
    reply.SendError("Couldn't post task to get the cookies");
    return;
  }
  event.Wait();

  ListValue* list = new ListValue();
  for (size_t i = 0; i < cookie_list.size(); ++i) {
    const net::CookieMonster::CanonicalCookie& cookie = cookie_list[i];
    DictionaryValue* cookie_dict = new DictionaryValue();
    cookie_dict->SetString("name", cookie.Name());
    cookie_dict->SetString("value", cookie.Value());
    cookie_dict->SetString("path", cookie.Path());
    cookie_dict->SetString("domain", cookie.Domain());
    cookie_dict->SetBoolean("secure", cookie.IsSecure());
    cookie_dict->SetBoolean("http_only", cookie.IsHttpOnly());
    if (cookie.DoesExpire())
      cookie_dict->SetDouble("expiry", cookie.ExpiryDate().ToDoubleT());
    list->Append(cookie_dict);
  }
  DictionaryValue dict;
  dict.Set("cookies", list);
  reply.SendSuccess(&dict);
}

void DeleteCookieJSON(AutomationProvider* provider,
                      DictionaryValue* args,
                      IPC::Message* reply_message) {
  AutomationJSONReply reply(provider, reply_message);
  std::string url, name;
  if (!args->GetString("url", &url)) {
    reply.SendError("'url' missing or invalid");
    return;
  }
  if (!args->GetString("name", &name)) {
    reply.SendError("'name' missing or invalid");
    return;
  }

  // Since we may be on the UI thread don't call GetURLRequestContext().
  scoped_refptr<net::URLRequestContextGetter> context_getter =
      provider->profile()->GetRequestContext();

  base::WaitableEvent event(true /* manual reset */,
                            false /* not initially signaled */);
  Task* task = NewRunnableFunction(
      &DeleteCookieOnIOThread,
      GURL(url), name, context_getter, &event);
  if (!BrowserThread::PostTask(BrowserThread::IO, FROM_HERE, task)) {
    reply.SendError("Couldn't post task to delete the cookie");
    return;
  }
  event.Wait();
  reply.SendSuccess(NULL);
}

void SetCookieJSON(AutomationProvider* provider,
                   DictionaryValue* args,
                   IPC::Message* reply_message) {
  AutomationJSONReply reply(provider, reply_message);
  std::string url;
  if (!args->GetString("url", &url)) {
    reply.SendError("'url' missing or invalid");
    return;
  }
  DictionaryValue* cookie_dict;
  if (!args->GetDictionary("cookie", &cookie_dict)) {
    reply.SendError("'cookie' missing or invalid");
    return;
  }
  std::string name, value;
  std::string domain;
  std::string path = "/";
  bool secure = false;
  double expiry = 0;
  bool http_only = false;
  if (!cookie_dict->GetString("name", &name)) {
    reply.SendError("'name' missing or invalid");
    return;
  }
  if (!cookie_dict->GetString("value", &value)) {
    reply.SendError("'value' missing or invalid");
    return;
  }
  if (cookie_dict->HasKey("domain") &&
      !cookie_dict->GetString("domain", &domain)) {
    reply.SendError("optional 'domain' invalid");
    return;
  }
  if (cookie_dict->HasKey("path") &&
      !cookie_dict->GetString("path", &path)) {
    reply.SendError("optional 'path' invalid");
    return;
  }
  if (cookie_dict->HasKey("secure") &&
      !cookie_dict->GetBoolean("secure", &secure)) {
    reply.SendError("optional 'secure' invalid");
    return;
  }
  if (cookie_dict->HasKey("expiry")) {
    int expiry_int;
    if (cookie_dict->GetInteger("expiry", &expiry_int)) {
      expiry = expiry_int;
    } else if (!cookie_dict->GetDouble("expiry", &expiry)) {
      reply.SendError("optional 'expiry' invalid");
      return;
    }
  }
  if (cookie_dict->HasKey("http_only") &&
      !cookie_dict->GetBoolean("http_only", &http_only)) {
    reply.SendError("optional 'http_only' invalid");
    return;
  }

  scoped_ptr<net::CookieMonster::CanonicalCookie> cookie(
      net::CookieMonster::CanonicalCookie::Create(
          GURL(url), name, value, domain, path, base::Time(),
          base::Time::FromDoubleT(expiry), secure, http_only));
  if (!cookie.get()) {
    reply.SendError("given 'cookie' parameters are invalid");
    return;
  }

  // Since we may be on the UI thread don't call GetURLRequestContext().
  scoped_refptr<net::URLRequestContextGetter> context_getter =
      provider->profile()->GetRequestContext();

  base::WaitableEvent event(true /* manual reset */,
                            false /* not initially signaled */);
  bool success = false;
  Task* task = NewRunnableFunction(
      &SetCookieWithDetailsOnIOThread,
      GURL(url), *cookie.get(), domain, context_getter, &event, &success);
  if (!BrowserThread::PostTask(BrowserThread::IO, FROM_HERE, task)) {
    reply.SendError("Couldn't post task to set the cookie");
    return;
  }
  event.Wait();

  if (!success) {
    reply.SendError("Could not set the cookie");
    return;
  }
  reply.SendSuccess(NULL);
}

}  // namespace automation_util
