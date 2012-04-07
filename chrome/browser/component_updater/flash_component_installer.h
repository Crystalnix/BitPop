// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COMPONENT_UPDATER_FLASH_COMPONENT_INSTALLER_H_
#define CHROME_BROWSER_COMPONENT_UPDATER_FLASH_COMPONENT_INSTALLER_H_
#pragma once

class ComponentUpdateService;
class Version;

namespace base {
class DictionaryValue;
}

// Component update registration for built-in flash player (non-Pepper).
void RegisterNPAPIFlashComponent(ComponentUpdateService* cus);

// Our job is to 1) find what Pepper flash is installed (if any) and 2) register
// with the component updater to download the latest version when available.
// The first part is IO intensive so we do it asynchronously in the file thread.
void RegisterPepperFlashComponent(ComponentUpdateService* cus);

// Returns true if this browser implements all the interfaces that Flash
// specifies in its component installer manifest.
bool VetoPepperFlashIntefaces(base::DictionaryValue* manifest);

// Returns true if this browser is compatible with the given Pepper Flash
// manifest, with the version specified in the manifest in |version_out|.
bool CheckPepperFlashManifest(base::DictionaryValue* manifest,
                              Version* version_out);

#endif  // CHROME_BROWSER_COMPONENT_UPDATER_FLASH_COMPONENT_INSTALLER_H_
