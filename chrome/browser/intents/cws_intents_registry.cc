// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/intents/cws_intents_registry.h"

#include "base/callback.h"
#include "base/json/json_value_serializer.h"
#include "base/memory/scoped_ptr.h"
#include "base/stl_util.h"
#include "base/string16.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/net/browser_url_util.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "content/public/common/url_fetcher.h"
#include "net/base/mime_util.h"
#include "net/base/load_flags.h"

namespace {

// URL for CWS intents API. TODO(groby): points to staging, fix for M18 release.
const char kCWSIntentServiceURL[] =
  "https://www-googleapis-staging.sandbox.google.com"
  "/chromewebstore/v1.1b/items/intent";

// Build a REST query URL to retrieve intent info from CWS.
GURL BuildQueryURL(const string16& action, const string16& type) {
  GURL request(kCWSIntentServiceURL);
  request = chrome_browser_net::AppendQueryParameter(request, "intent",
                                                     UTF16ToUTF8(action));
  return chrome_browser_net::AppendQueryParameter(request, "mime_types",
                                                  UTF16ToUTF8(type));
}

}  // namespace

// Internal object representing all data associated with a single query.
struct CWSIntentsRegistry::IntentsQuery {
  // Underlying URL request query.
  scoped_ptr<content::URLFetcher> url_fetcher_;

  // The callback - invoked on completed retrieval.
  ResultsCallback callback_;
};

CWSIntentsRegistry::CWSIntentsRegistry(net::URLRequestContextGetter* context)
    : request_context_(context) {
}

CWSIntentsRegistry::~CWSIntentsRegistry() {
  // Cancel all pending queries, since we can't handle them any more.
  STLDeleteValues(&queries_);
}

void CWSIntentsRegistry::OnURLFetchComplete(const content::URLFetcher* source) {
  DCHECK(source);

  URLFetcherHandle handle = reinterpret_cast<URLFetcherHandle>(source);
  QueryMap::iterator it = queries_.find(handle);
  DCHECK(it != queries_.end());
  scoped_ptr<IntentsQuery> query(it->second);
  DCHECK(query != NULL);
  queries_.erase(it);

  std::string response;
  source->GetResponseAsString(&response);

  // TODO(groby): Do we really only accept 200, or any 2xx codes?
  if (source->GetResponseCode() != 200)
    return;

  std::string error;
  scoped_ptr<Value> parsed_response;
  JSONStringValueSerializer serializer(response);
  parsed_response.reset(serializer.Deserialize(NULL, &error));
  if (parsed_response == NULL)
    return;

  DictionaryValue* response_dict;
  parsed_response->GetAsDictionary(&response_dict);
  if (!response_dict)
    return;
  ListValue* items;
  if (!response_dict->GetList("items",&items))
    return;

  IntentExtensionList intents;
  for (ListValue::const_iterator iter(items->begin());
       iter != items->end(); ++iter) {
    DictionaryValue* item = static_cast<DictionaryValue*>(*iter);

    // All fields are mandatory - skip this result if we can't find a field.
    IntentExtensionInfo info;
    if (!item->GetInteger("num_ratings", &info.num_ratings))
      continue;

    if (!item->GetDouble("average_rating", &info.average_rating))
      continue;

    if (!item->GetString("manifest", &info.manifest))
      continue;

    string16 url_string;
    if (!item->GetString("icon_url", &url_string))
      continue;
    info.icon_url = GURL(url_string);

    intents.push_back(info);
  }

  if (!query->callback_.is_null())
    query->callback_.Run(intents);
}

void CWSIntentsRegistry::GetIntentProviders(
    const string16& action, const string16& mimetype,
    const ResultsCallback& cb) {
  scoped_ptr<IntentsQuery> query(new IntentsQuery);
  query->callback_ = cb;
  query->url_fetcher_.reset(content::URLFetcher::Create(
      0, BuildQueryURL(action,mimetype), content::URLFetcher::GET, this));

  if (query->url_fetcher_ == NULL)
    return;

  query->url_fetcher_->SetRequestContext(request_context_);
  query->url_fetcher_->SetLoadFlags(
      net::LOAD_DO_NOT_SEND_COOKIES | net::LOAD_DO_NOT_SAVE_COOKIES);

  URLFetcherHandle handle = reinterpret_cast<URLFetcherHandle>(
      query->url_fetcher_.get());
  queries_[handle] = query.release();
  queries_[handle]->url_fetcher_->Start();
}

