// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/api/syncable_service.h"
#include "chrome/browser/sync/glue/app_change_processor.h"
#include "chrome/browser/sync/glue/app_data_type_controller.h"
#include "chrome/browser/sync/glue/app_model_associator.h"
#include "chrome/browser/sync/glue/autofill_change_processor.h"
#include "chrome/browser/sync/glue/autofill_data_type_controller.h"
#include "chrome/browser/sync/glue/autofill_model_associator.h"
#include "chrome/browser/sync/glue/autofill_profile_change_processor.h"
#include "chrome/browser/sync/glue/autofill_profile_data_type_controller.h"
#include "chrome/browser/sync/glue/autofill_profile_model_associator.h"
#include "chrome/browser/sync/glue/bookmark_change_processor.h"
#include "chrome/browser/sync/glue/bookmark_data_type_controller.h"
#include "chrome/browser/sync/glue/bookmark_model_associator.h"
#include "chrome/browser/sync/glue/data_type_manager_impl.h"
#include "chrome/browser/sync/glue/extension_change_processor.h"
#include "chrome/browser/sync/glue/extension_data_type_controller.h"
#include "chrome/browser/sync/glue/extension_model_associator.h"
#include "chrome/browser/sync/glue/extension_sync_traits.h"
#include "chrome/browser/sync/glue/generic_change_processor.h"
#include "chrome/browser/sync/glue/password_change_processor.h"
#include "chrome/browser/sync/glue/password_data_type_controller.h"
#include "chrome/browser/sync/glue/password_model_associator.h"
#include "chrome/browser/sync/glue/preference_data_type_controller.h"
#include "chrome/browser/sync/glue/session_change_processor.h"
#include "chrome/browser/sync/glue/session_data_type_controller.h"
#include "chrome/browser/sync/glue/session_model_associator.h"
#include "chrome/browser/sync/glue/syncable_service_adapter.h"
#include "chrome/browser/sync/glue/sync_backend_host.h"
#include "chrome/browser/sync/glue/theme_change_processor.h"
#include "chrome/browser/sync/glue/theme_data_type_controller.h"
#include "chrome/browser/sync/glue/theme_model_associator.h"
#include "chrome/browser/sync/glue/typed_url_change_processor.h"
#include "chrome/browser/sync/glue/typed_url_data_type_controller.h"
#include "chrome/browser/sync/glue/typed_url_model_associator.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_factory_impl.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/common/chrome_switches.h"

using browser_sync::AppChangeProcessor;
using browser_sync::AppDataTypeController;
using browser_sync::AppModelAssociator;
using browser_sync::AutofillChangeProcessor;
using browser_sync::AutofillProfileChangeProcessor;
using browser_sync::AutofillDataTypeController;
using browser_sync::AutofillProfileDataTypeController;
using browser_sync::AutofillModelAssociator;
using browser_sync::AutofillProfileModelAssociator;
using browser_sync::BookmarkChangeProcessor;
using browser_sync::BookmarkDataTypeController;
using browser_sync::BookmarkModelAssociator;
using browser_sync::DataTypeController;
using browser_sync::DataTypeManager;
using browser_sync::DataTypeManagerImpl;
using browser_sync::ExtensionChangeProcessor;
using browser_sync::ExtensionDataTypeController;
using browser_sync::ExtensionModelAssociator;
using browser_sync::GenericChangeProcessor;
using browser_sync::PasswordChangeProcessor;
using browser_sync::PasswordDataTypeController;
using browser_sync::PasswordModelAssociator;
using browser_sync::PreferenceDataTypeController;
using browser_sync::SessionChangeProcessor;
using browser_sync::SessionDataTypeController;
using browser_sync::SessionModelAssociator;
using browser_sync::SyncBackendHost;
using browser_sync::ThemeChangeProcessor;
using browser_sync::ThemeDataTypeController;
using browser_sync::ThemeModelAssociator;
using browser_sync::TypedUrlChangeProcessor;
using browser_sync::TypedUrlDataTypeController;
using browser_sync::TypedUrlModelAssociator;
using browser_sync::UnrecoverableErrorHandler;

ProfileSyncFactoryImpl::ProfileSyncFactoryImpl(Profile* profile,
                                               CommandLine* command_line)
    : profile_(profile),
      command_line_(command_line) {
}

ProfileSyncService* ProfileSyncFactoryImpl::CreateProfileSyncService(
    const std::string& cros_user) {

  ProfileSyncService* pss = new ProfileSyncService(
      this, profile_, cros_user);
  return pss;
}

void ProfileSyncFactoryImpl::RegisterDataTypes(ProfileSyncService* pss) {
  // App sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncApps)) {
    pss->RegisterDataTypeController(
        new AppDataTypeController(this, profile_, pss));
  }

  // Autofill sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncAutofill)) {
    pss->RegisterDataTypeController(
        new AutofillDataTypeController(this, profile_));
  }

  // Bookmark sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncBookmarks)) {
    pss->RegisterDataTypeController(
        new BookmarkDataTypeController(this, profile_, pss));
  }

  // Extension sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncExtensions)) {
    pss->RegisterDataTypeController(
        new ExtensionDataTypeController(this, profile_, pss));
  }

  // Password sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncPasswords)) {
    pss->RegisterDataTypeController(
        new PasswordDataTypeController(this, profile_));
  }

  // Preference sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncPreferences)) {
    pss->RegisterDataTypeController(
        new PreferenceDataTypeController(this, profile_, pss));
  }

  // Theme sync is enabled by default.  Register unless explicitly disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncThemes)) {
    pss->RegisterDataTypeController(
        new ThemeDataTypeController(this, profile_, pss));
  }

  // TypedUrl sync is disabled by default.  Register only if
  // explicitly enabled.
  if (command_line_->HasSwitch(switches::kEnableSyncTypedUrls)) {
    pss->RegisterDataTypeController(
        new TypedUrlDataTypeController(this, profile_));
  }

  // Session sync is disabled by default.  Register only if explicitly
  // enabled.
  if (command_line_->HasSwitch(switches::kEnableSyncSessions)) {
    pss->RegisterDataTypeController(
        new SessionDataTypeController(this, profile_, pss));
  }

  if (!command_line_->HasSwitch(switches::kDisableSyncAutofillProfile)) {
    pss->RegisterDataTypeController(
        new AutofillProfileDataTypeController(this, profile_));
  }
}

DataTypeManager* ProfileSyncFactoryImpl::CreateDataTypeManager(
    SyncBackendHost* backend,
    const DataTypeController::TypeMap& controllers) {
  return new DataTypeManagerImpl(backend, controllers);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateAppSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  // For now we simply use extensions sync objects with the app sync
  // traits.  If apps become more than simply extensions, we may have
  // to write our own apps model associator and/or change processor.
  ExtensionServiceInterface* extension_service =
      profile_sync_service->profile()->GetExtensionService();
  sync_api::UserShare* user_share = profile_sync_service->GetUserShare();
  AppModelAssociator* model_associator =
      new AppModelAssociator(extension_service, user_share);
  AppChangeProcessor* change_processor =
      new AppChangeProcessor(error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateAutofillSyncComponents(
    ProfileSyncService* profile_sync_service,
    WebDatabase* web_database,
    PersonalDataManager* personal_data,
    browser_sync::UnrecoverableErrorHandler* error_handler) {

  AutofillModelAssociator* model_associator =
      new AutofillModelAssociator(profile_sync_service,
                                  web_database,
                                  personal_data);
  AutofillChangeProcessor* change_processor =
      new AutofillChangeProcessor(model_associator,
                                  web_database,
                                  personal_data,
                                  error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateAutofillProfileSyncComponents(
    ProfileSyncService* profile_sync_service,
    WebDatabase* web_database,
    PersonalDataManager* personal_data,
    browser_sync::UnrecoverableErrorHandler* error_handler) {

  AutofillProfileModelAssociator* model_associator =
      new AutofillProfileModelAssociator(profile_sync_service,
                                  web_database,
                                  personal_data);
  AutofillProfileChangeProcessor* change_processor =
      new AutofillProfileChangeProcessor(model_associator,
                                  web_database,
                                  personal_data,
                                  error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateBookmarkSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  BookmarkModel* bookmark_model =
      profile_sync_service->profile()->GetBookmarkModel();
  sync_api::UserShare* user_share = profile_sync_service->GetUserShare();
  BookmarkModelAssociator* model_associator =
      new BookmarkModelAssociator(bookmark_model,
                                  user_share,
                                  error_handler);
  BookmarkChangeProcessor* change_processor =
      new BookmarkChangeProcessor(model_associator,
                                  error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateExtensionSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  ExtensionServiceInterface* extension_service =
      profile_sync_service->profile()->GetExtensionService();
  sync_api::UserShare* user_share = profile_sync_service->GetUserShare();
  ExtensionModelAssociator* model_associator =
      new ExtensionModelAssociator(extension_service, user_share);
  ExtensionChangeProcessor* change_processor =
      new ExtensionChangeProcessor(error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreatePasswordSyncComponents(
    ProfileSyncService* profile_sync_service,
    PasswordStore* password_store,
    UnrecoverableErrorHandler* error_handler) {
  PasswordModelAssociator* model_associator =
      new PasswordModelAssociator(profile_sync_service,
                                  password_store);
  PasswordChangeProcessor* change_processor =
      new PasswordChangeProcessor(model_associator,
                                  password_store,
                                  error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreatePreferenceSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  SyncableService* pref_sync_service =
      profile_->GetPrefs()->GetSyncableService();
  sync_api::UserShare* user_share = profile_sync_service->GetUserShare();
  GenericChangeProcessor* change_processor =
      new GenericChangeProcessor(pref_sync_service, error_handler, user_share);
  browser_sync::SyncableServiceAdapter* sync_service_adapter =
      new browser_sync::SyncableServiceAdapter(syncable::PREFERENCES,
                                               pref_sync_service,
                                               change_processor);
  return SyncComponents(sync_service_adapter, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateThemeSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  ThemeModelAssociator* model_associator =
      new ThemeModelAssociator(profile_sync_service);
  ThemeChangeProcessor* change_processor =
      new ThemeChangeProcessor(error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateTypedUrlSyncComponents(
    ProfileSyncService* profile_sync_service,
    history::HistoryBackend* history_backend,
    browser_sync::UnrecoverableErrorHandler* error_handler) {
  TypedUrlModelAssociator* model_associator =
      new TypedUrlModelAssociator(profile_sync_service,
                                  history_backend);
  TypedUrlChangeProcessor* change_processor =
      new TypedUrlChangeProcessor(model_associator,
                                  history_backend,
                                  error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateSessionSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  SessionModelAssociator* model_associator =
      new SessionModelAssociator(profile_sync_service);
  SessionChangeProcessor* change_processor =
      new SessionChangeProcessor(error_handler, model_associator);
  return SyncComponents(model_associator, change_processor);
}
