// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_keybinding_registry.h"

#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/extension_set.h"

namespace extensions {

ExtensionKeybindingRegistry::ExtensionKeybindingRegistry(Profile* profile)
    : profile_(profile) {
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_LOADED,
                 content::Source<Profile>(profile->GetOriginalProfile()));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(profile->GetOriginalProfile()));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_COMMAND_ADDED,
                 content::Source<Profile>(profile->GetOriginalProfile()));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_COMMAND_REMOVED,
                 content::Source<Profile>(profile->GetOriginalProfile()));
}

ExtensionKeybindingRegistry::~ExtensionKeybindingRegistry() {
}

void ExtensionKeybindingRegistry::Init() {
  ExtensionService* service = profile_->GetExtensionService();
  if (!service)
    return;  // ExtensionService can be null during testing.

  const ExtensionSet* extensions = service->extensions();
  ExtensionSet::const_iterator iter = extensions->begin();
  for (; iter != extensions->end(); ++iter)
    AddExtensionKeybinding(*iter, std::string());
}

bool ExtensionKeybindingRegistry::ShouldIgnoreCommand(
    const std::string& command) const {
  return command == extension_manifest_values::kPageActionCommandEvent ||
         command == extension_manifest_values::kBrowserActionCommandEvent ||
         command == extension_manifest_values::kScriptBadgeCommandEvent;
}

void ExtensionKeybindingRegistry::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_LOADED:
      AddExtensionKeybinding(
          content::Details<const extensions::Extension>(details).ptr(),
          std::string());
      break;
    case chrome::NOTIFICATION_EXTENSION_UNLOADED:
      RemoveExtensionKeybinding(
          content::Details<UnloadedExtensionInfo>(details)->extension,
          std::string());
      break;
    case chrome::NOTIFICATION_EXTENSION_COMMAND_ADDED:
    case chrome::NOTIFICATION_EXTENSION_COMMAND_REMOVED: {
      std::pair<const std::string, const std::string>* payload =
          content::Details<std::pair<const std::string, const std::string> >(
              details).ptr();

      const extensions::Extension* extension =
          ExtensionSystem::Get(profile_)->extension_service()->
              extensions()->GetByID(payload->first);
      // During install and uninstall the extension won't be found. We'll catch
      // those events above, with the LOADED/UNLOADED, so we ignore this event.
      if (!extension)
        return;

      if (type == chrome::NOTIFICATION_EXTENSION_COMMAND_ADDED)
        AddExtensionKeybinding(extension, payload->second);
      else
        RemoveExtensionKeybinding(extension, payload->second);
      break;
    }
    default:
      NOTREACHED();
      break;
  }
}

}  // namespace extensions
