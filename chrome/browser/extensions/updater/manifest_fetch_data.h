// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_UPDATER_MANIFEST_FETCH_DATA_H_
#define CHROME_BROWSER_EXTENSIONS_UPDATER_MANIFEST_FETCH_DATA_H_

#include <map>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "googleurl/src/gurl.h"

namespace extensions {

// To save on server resources we can request updates for multiple extensions
// in one manifest check. This class helps us keep track of the id's for a
// given fetch, building up the actual URL, and what if anything to include
// in the ping parameter.
class ManifestFetchData {
 public:
  static const int kNeverPinged = -1;

  // Each ping type is sent at most once per day.
  enum PingType {
    // Used for counting total installs of an extension/app/theme.
    ROLLCALL,

    // Used for counting number of active users of an app, where "active" means
    // the app was launched at least once since the last active ping.
    ACTIVE,
  };

  struct PingData {
    // The number of days it's been since our last rollcall or active ping,
    // respectively. These are calculated based on the start of day from the
    // server's perspective.
    int rollcall_days;
    int active_days;

    PingData() : rollcall_days(0), active_days(0) {}
    PingData(int rollcall, int active)
        : rollcall_days(rollcall), active_days(active) {}
  };

  explicit ManifestFetchData(const GURL& update_url);
  ~ManifestFetchData();

  // Returns true if this extension information was successfully added. If the
  // return value is false it means the full_url would have become too long, and
  // this ManifestFetchData object remains unchanged.
  bool AddExtension(std::string id, std::string version,
                    const PingData* ping_data,
                    const std::string& update_url_data,
                    const std::string& install_source);

  const GURL& base_url() const { return base_url_; }
  const GURL& full_url() const { return full_url_; }
  int extension_count() { return extension_ids_.size(); }
  const std::set<std::string>& extension_ids() const { return extension_ids_; }

  // Returns true if the given id is included in this manifest fetch.
  bool Includes(const std::string& extension_id) const;

  // Returns true if a ping parameter for |type| was added to full_url for this
  // extension id.
  bool DidPing(std::string extension_id, PingType type) const;

 private:
  // The set of extension id's for this ManifestFetchData.
  std::set<std::string> extension_ids_;

  // The set of ping data we actually sent.
  std::map<std::string, PingData> pings_;

  // The base update url without any arguments added.
  GURL base_url_;

  // The base update url plus arguments indicating the id, version, etc.
  // information about each extension.
  GURL full_url_;

  DISALLOW_COPY_AND_ASSIGN(ManifestFetchData);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_UPDATER_MANIFEST_FETCH_DATA_H_
