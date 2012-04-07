// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/geolocation/device_data_provider.h"

// statics
template<> DeviceDataProvider<RadioData>*
    DeviceDataProvider<RadioData>::instance_ = NULL;
template<> DeviceDataProvider<RadioData>::ImplFactoryFunction
    DeviceDataProvider<RadioData>::factory_function_ = DefaultFactoryFunction;
template<> DeviceDataProvider<WifiData>*
    DeviceDataProvider<WifiData>::instance_ = NULL;
template<> DeviceDataProvider<WifiData>::ImplFactoryFunction
    DeviceDataProvider<WifiData>::factory_function_ = DefaultFactoryFunction;

namespace {

bool CellDataMatches(const CellData &data1, const CellData &data2) {
  return data1.Matches(data2);
}

}  // namespace

CellData::CellData()
    : cell_id(kint32min),
      location_area_code(kint32min),
      mobile_network_code(kint32min),
      mobile_country_code(kint32min),
      radio_signal_strength(kint32min),
      timing_advance(kint32min) {
}

RadioData::RadioData()
    : home_mobile_network_code(kint32min),
      home_mobile_country_code(kint32min),
      radio_type(RADIO_TYPE_UNKNOWN) {
}

RadioData::~RadioData() {}

bool RadioData::Matches(const RadioData &other) const {
  if (cell_data.size() != other.cell_data.size()) {
    return false;
  }
  if (!std::equal(cell_data.begin(), cell_data.end(), other.cell_data.begin(),
                  CellDataMatches)) {
    return false;
  }
  return device_id == other.device_id &&
         home_mobile_network_code == other.home_mobile_network_code &&
         home_mobile_country_code == other.home_mobile_country_code &&
         radio_type == other.radio_type &&
         carrier == other.carrier;
}

AccessPointData::AccessPointData()
    : radio_signal_strength(kint32min),
      channel(kint32min),
      signal_to_noise(kint32min) {
}

AccessPointData::~AccessPointData() {}

WifiData::WifiData() {}

WifiData::~WifiData() {}

bool WifiData::DiffersSignificantly(const WifiData& other) const {
  // More than 4 or 50% of access points added or removed is significant.
  static const size_t kMinChangedAccessPoints = 4;
  const size_t min_ap_count =
      std::min(access_point_data.size(), other.access_point_data.size());
  const size_t max_ap_count =
      std::max(access_point_data.size(), other.access_point_data.size());
  const size_t difference_threadhold = std::min(kMinChangedAccessPoints,
                                                min_ap_count / 2);
  if (max_ap_count > min_ap_count + difference_threadhold)
    return true;
  // Compute size of interesction of old and new sets.
  size_t num_common = 0;
  for (AccessPointDataSet::const_iterator iter = access_point_data.begin();
       iter != access_point_data.end();
       iter++) {
    if (other.access_point_data.find(*iter) !=
        other.access_point_data.end()) {
      ++num_common;
    }
  }
  DCHECK(num_common <= min_ap_count);

  // Test how many have changed.
  return max_ap_count > num_common + difference_threadhold;
}
