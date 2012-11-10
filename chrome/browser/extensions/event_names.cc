// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/event_names.h"

namespace extensions {

namespace event_names {

const char kOnTabActivated[] = "tabs.onActivated";
const char kOnTabActiveChanged[] = "tabs.onActiveChanged";
const char kOnTabAttached[] = "tabs.onAttached";
const char kOnTabCreated[] = "tabs.onCreated";
const char kOnTabDetached[] = "tabs.onDetached";
const char kOnTabHighlightChanged[] = "tabs.onHighlightChanged";
const char kOnTabHighlighted[] = "tabs.onHighlighted";
const char kOnTabMoved[] = "tabs.onMoved";
const char kOnTabRemoved[] = "tabs.onRemoved";
const char kOnTabSelectionChanged[] = "tabs.onSelectionChanged";
const char kOnTabUpdated[] = "tabs.onUpdated";

const char kOnWindowCreated[] = "windows.onCreated";
const char kOnWindowFocusedChanged[] = "windows.onFocusChanged";
const char kOnWindowRemoved[] = "windows.onRemoved";

const char kOnExtensionInstalled[] = "management.onInstalled";
const char kOnExtensionUninstalled[] = "management.onUninstalled";
const char kOnExtensionEnabled[] = "management.onEnabled";
const char kOnExtensionDisabled[] = "management.onDisabled";

const char kOnFileBrowserDiskChanged[] = "fileBrowserPrivate.onDiskChanged";
const char kOnFileChanged[] = "fileBrowserPrivate.onFileChanged";
const char kOnFileBrowserMountCompleted[] =
    "fileBrowserPrivate.onMountCompleted";
const char kOnFileTransfersUpdated[] =
    "fileBrowserPrivate.onFileTransfersUpdated";
const char kOnDocumentFeedFetched[] =
    "fileBrowserPrivate.onDocumentFeedFetched";
const char kOnFileBrowserGDataPreferencesChanged[] =
    "fileBrowserPrivate.onGDataPreferencesChanged";
const char kOnFileBrowserNetworkConnectionChanged[] =
    "fileBrowserPrivate.onNetworkConnectionChanged";

const char kOnInputMethodChanged[] = "inputMethodPrivate.onChanged";

const char kOnContextMenus[] = "contextMenus";
const char kOnContextMenuClicked[] = "contextMenus.onClicked";

const char kOnDownloadCreated[] = "downloads.onCreated";
const char kOnDownloadChanged[] = "downloads.onChanged";
const char kOnDownloadErased[] = "downloads.onErased";

const char kOnSettingsChanged[] = "storage.onChanged";

const char kOnTerminalProcessOutput[] = "terminalPrivate.onProcessOutput";

const char kOnOffscreenTabUpdated[] = "experimental.offscreenTabs.onUpdated";

#if defined(OS_CHROMEOS)
const char kBluetoothOnAvailabilityChanged[] =
    "experimental.bluetooth.onAvailabilityChanged";
const char kBluetoothOnDeviceDiscovered[] =
    "experimental.bluetooth.onDeviceDiscovered";
const char kBluetoothOnDiscoveringChanged[] =
    "experimental.bluetooth.onDiscoveringChanged";
const char kBluetoothOnPowerChanged[] =
    "experimental.bluetooth.onPowerChanged";
#endif

const char kOnPushMessage[] = "experimental.pushMessaging.onMessage";

}  // namespace event_names

}  // namespace extensions
