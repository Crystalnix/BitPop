// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_THROTTLER_MANAGER_H_
#define NET_URL_REQUEST_URL_REQUEST_THROTTLER_MANAGER_H_
#pragma once

#include <map>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/threading/non_thread_safe.h"
#include "googleurl/src/gurl.h"
#include "net/url_request/url_request_throttler_entry.h"

namespace net {

// Class that registers URL request throttler entries for URLs being accessed
// in order to supervise traffic. URL requests for HTTP contents should
// register their URLs in this manager on each request.
//
// URLRequestThrottlerManager maintains a map of URL IDs to URL request
// throttler entries. It creates URL request throttler entries when new URLs
// are registered, and does garbage collection from time to time in order to
// clean out outdated entries. URL ID consists of lowercased scheme, host, port
// and path. All URLs converted to the same ID will share the same entry.
//
// NOTE: All usage of this singleton object must be on the same thread,
// although to allow it to be used as a singleton, construction and destruction
// can occur on a separate thread.
class URLRequestThrottlerManager : public base::NonThreadSafe {
 public:
  static URLRequestThrottlerManager* GetInstance();

  // Must be called for every request, returns the URL request throttler entry
  // associated with the URL. The caller must inform this entry of some events.
  // Please refer to url_request_throttler_entry_interface.h for further
  // informations.
  scoped_refptr<URLRequestThrottlerEntryInterface> RegisterRequestUrl(
      const GURL& url);

  // Adds the given host to a list of sites for which exponential back-off
  // throttling will be disabled.  Subdomains are not included, so they
  // must be added separately.
  void AddToOptOutList(const std::string& host);

  // Registers a new entry in this service and overrides the existing entry (if
  // any) for the URL. The service will hold a reference to the entry.
  // It is only used by unit tests.
  void OverrideEntryForTests(const GURL& url, URLRequestThrottlerEntry* entry);

  // Explicitly erases an entry.
  // This is useful to remove those entries which have got infinite lifetime and
  // thus won't be garbage collected.
  // It is only used by unit tests.
  void EraseEntryForTests(const GURL& url);

  // Turns threading model verification on or off.  Any code that correctly
  // uses the network stack should preferably call this function to enable
  // verification of correct adherence to the network stack threading model.
  void set_enable_thread_checks(bool enable);
  bool enable_thread_checks() const;

  // Whether throttling is enabled or not.
  void set_enforce_throttling(bool enforce);
  bool enforce_throttling();

 protected:
  URLRequestThrottlerManager();
  ~URLRequestThrottlerManager();

  // Method that allows us to transform a URL into an ID that can be used in our
  // map. Resulting IDs will be lowercase and consist of the scheme, host, port
  // and path (without query string, fragment, etc.).
  // If the URL is invalid, the invalid spec will be returned, without any
  // transformation.
  std::string GetIdFromUrl(const GURL& url) const;

  // Method that ensures the map gets cleaned from time to time. The period at
  // which garbage collecting happens is adjustable with the
  // kRequestBetweenCollecting constant.
  void GarbageCollectEntriesIfNecessary();

  // Method that does the actual work of garbage collecting.
  void GarbageCollectEntries();

  // Used by tests.
  int GetNumberOfEntriesForTests() const { return url_entries_.size(); }

 private:
  friend struct DefaultSingletonTraits<URLRequestThrottlerManager>;

  // From each URL we generate an ID composed of the scheme, host, port and path
  // that allows us to uniquely map an entry to it.
  typedef std::map<std::string, scoped_refptr<URLRequestThrottlerEntry> >
      UrlEntryMap;

  // We maintain a set of hosts that have opted out of exponential
  // back-off throttling.
  typedef std::set<std::string> OptOutHosts;

  // Maximum number of entries that we are willing to collect in our map.
  static const unsigned int kMaximumNumberOfEntries;
  // Number of requests that will be made between garbage collection.
  static const unsigned int kRequestsBetweenCollecting;

  // Map that contains a list of URL ID and their matching
  // URLRequestThrottlerEntry.
  UrlEntryMap url_entries_;

  // Set of hosts that have opted out.
  OptOutHosts opt_out_hosts_;

  // This keeps track of how many requests have been made. Used with
  // GarbageCollectEntries.
  unsigned int requests_since_last_gc_;

  // Valid after construction.
  GURL::Replacements url_id_replacements_;

  // Whether we would like to reject outgoing HTTP requests during the back-off
  // period.
  bool enforce_throttling_;

  // Certain tests do not obey the net component's threading policy, so we
  // keep track of whether we're being used by tests, and turn off certain
  // checks.
  //
  // TODO(joi): See if we can fix the offending unit tests and remove this
  // workaround.
  bool enable_thread_checks_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestThrottlerManager);
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_THROTTLER_MANAGER_H_
