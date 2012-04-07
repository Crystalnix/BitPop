// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Keep this file in sync with the .proto files in this directory.

#include "chrome/browser/sync/protocol/proto_enum_conversions.h"

#include "base/basictypes.h"
#include "base/logging.h"

namespace browser_sync {

#define ASSERT_ENUM_BOUNDS(enum_parent, enum_type, enum_min, enum_max)  \
  COMPILE_ASSERT(enum_parent::enum_type##_MIN == enum_parent::enum_min, \
                 enum_type##_MIN_not_##enum_min);                       \
  COMPILE_ASSERT(enum_parent::enum_type##_MAX == enum_parent::enum_max, \
                 enum_type##_MAX_not_##enum_max);

#define ENUM_CASE(enum_parent, enum_value)              \
  case enum_parent::enum_value: return #enum_value

const char* GetBrowserTypeString(
    sync_pb::SessionWindow::BrowserType browser_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::SessionWindow, BrowserType,
                     TYPE_TABBED, TYPE_POPUP);
  switch (browser_type) {
    ENUM_CASE(sync_pb::SessionWindow, TYPE_TABBED);
    ENUM_CASE(sync_pb::SessionWindow, TYPE_POPUP);
  }
  NOTREACHED();
  return "";
}

const char* GetPageTransitionString(
    sync_pb::TabNavigation::PageTransition page_transition) {
  ASSERT_ENUM_BOUNDS(sync_pb::TabNavigation, PageTransition,
                     LINK, CHAIN_END);
  switch (page_transition) {
    ENUM_CASE(sync_pb::TabNavigation, LINK);
    ENUM_CASE(sync_pb::TabNavigation, TYPED);
    ENUM_CASE(sync_pb::TabNavigation, AUTO_BOOKMARK);
    ENUM_CASE(sync_pb::TabNavigation, AUTO_SUBFRAME);
    ENUM_CASE(sync_pb::TabNavigation, MANUAL_SUBFRAME);
    ENUM_CASE(sync_pb::TabNavigation, GENERATED);
    ENUM_CASE(sync_pb::TabNavigation, START_PAGE);
    ENUM_CASE(sync_pb::TabNavigation, FORM_SUBMIT);
    ENUM_CASE(sync_pb::TabNavigation, RELOAD);
    ENUM_CASE(sync_pb::TabNavigation, KEYWORD);
    ENUM_CASE(sync_pb::TabNavigation, KEYWORD_GENERATED);
    ENUM_CASE(sync_pb::TabNavigation, CHAIN_START);
    ENUM_CASE(sync_pb::TabNavigation, CHAIN_END);
  }
  NOTREACHED();
  return "";
}

const char* GetPageTransitionQualifierString(
    sync_pb::TabNavigation::PageTransitionQualifier
        page_transition_qualifier) {
  ASSERT_ENUM_BOUNDS(sync_pb::TabNavigation, PageTransitionQualifier,
                     CLIENT_REDIRECT, SERVER_REDIRECT);
  switch (page_transition_qualifier) {
    ENUM_CASE(sync_pb::TabNavigation, CLIENT_REDIRECT);
    ENUM_CASE(sync_pb::TabNavigation, SERVER_REDIRECT);
  }
  NOTREACHED();
  return "";
}

const char* GetUpdatesSourceString(
    sync_pb::GetUpdatesCallerInfo::GetUpdatesSource updates_source) {
  ASSERT_ENUM_BOUNDS(sync_pb::GetUpdatesCallerInfo, GetUpdatesSource,
                     UNKNOWN, DATATYPE_REFRESH);
  switch (updates_source) {
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, UNKNOWN);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, FIRST_UPDATE);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, LOCAL);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, NOTIFICATION);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, PERIODIC);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, SYNC_CYCLE_CONTINUATION);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, CLEAR_PRIVATE_DATA);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, NEWLY_SUPPORTED_DATATYPE);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, MIGRATION);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, NEW_CLIENT);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, RECONFIGURATION);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, DATATYPE_REFRESH);
  }
  NOTREACHED();
  return "";
}

const char* GetDeviceTypeString(
    sync_pb::SessionHeader::DeviceType device_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::SessionHeader, DeviceType, TYPE_WIN, TYPE_OTHER);
  switch (device_type) {
    ENUM_CASE(sync_pb::SessionHeader, TYPE_WIN);
    ENUM_CASE(sync_pb::SessionHeader, TYPE_MAC);
    ENUM_CASE(sync_pb::SessionHeader, TYPE_LINUX);
    ENUM_CASE(sync_pb::SessionHeader, TYPE_CROS);
    ENUM_CASE(sync_pb::SessionHeader, TYPE_OTHER);
  }
  NOTREACHED();
  return "";
}

#undef ASSERT_ENUM_BOUNDS
#undef ENUM_CASE

}  // namespace
