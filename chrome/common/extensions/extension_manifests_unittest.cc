// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "chrome/common/extensions/extension_sidebar_defaults.h"
#include "chrome/common/extensions/file_browser_handler.h"
#include "chrome/common/extensions/url_pattern.h"
#include "content/common/json_value_serializer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace errors = extension_manifest_errors;
namespace keys = extension_manifest_keys;

class ExtensionManifestTest : public testing::Test {
 public:
  ExtensionManifestTest() : enable_apps_(true) {}

 protected:
  DictionaryValue* LoadManifestFile(const std::string& filename,
                                    std::string* error) {
    FilePath path;
    PathService::Get(chrome::DIR_TEST_DATA, &path);
    path = path.AppendASCII("extensions")
        .AppendASCII("manifest_tests")
        .AppendASCII(filename.c_str());
    EXPECT_TRUE(file_util::PathExists(path));

    JSONFileValueSerializer serializer(path);
    return static_cast<DictionaryValue*>(serializer.Deserialize(NULL, error));
  }

  scoped_refptr<Extension> LoadExtensionWithLocation(
      DictionaryValue* value,
      Extension::Location location,
      bool strict_error_checks,
      std::string* error) {
    FilePath path;
    PathService::Get(chrome::DIR_TEST_DATA, &path);
    path = path.AppendASCII("extensions").AppendASCII("manifest_tests");
    int flags = Extension::NO_FLAGS;
    if (strict_error_checks)
      flags |= Extension::STRICT_ERROR_CHECKS;
    return Extension::Create(path.DirName(), location, *value, flags, error);
  }

  scoped_refptr<Extension> LoadExtension(const std::string& name,
                                         std::string* error) {
    return LoadExtensionWithLocation(name, Extension::INTERNAL, false, error);
  }

  scoped_refptr<Extension> LoadExtensionStrict(const std::string& name,
                                               std::string* error) {
    return LoadExtensionWithLocation(name, Extension::INTERNAL, true, error);
  }

  scoped_refptr<Extension> LoadExtension(DictionaryValue* value,
                                         std::string* error) {
    // Loading as an installed extension disables strict error checks.
    return LoadExtensionWithLocation(value, Extension::INTERNAL, false, error);
  }

  scoped_refptr<Extension> LoadExtensionWithLocation(
      const std::string& name,
      Extension::Location location,
      bool strict_error_checks,
      std::string* error) {
    scoped_ptr<DictionaryValue> value(LoadManifestFile(name, error));
    if (!value.get())
      return NULL;
    return LoadExtensionWithLocation(value.get(), location,
                                     strict_error_checks, error);
  }

  scoped_refptr<Extension> LoadAndExpectSuccess(const std::string& name) {
    std::string error;
    scoped_refptr<Extension> extension = LoadExtension(name, &error);
    EXPECT_TRUE(extension) << name;
    EXPECT_EQ("", error) << name;
    return extension;
  }

  scoped_refptr<Extension> LoadStrictAndExpectSuccess(const std::string& name) {
    std::string error;
    scoped_refptr<Extension> extension = LoadExtensionStrict(name, &error);
    EXPECT_TRUE(extension) << name;
    EXPECT_EQ("", error) << name;
    return extension;
  }

  scoped_refptr<Extension> LoadAndExpectSuccess(DictionaryValue* manifest,
                                                const std::string& name) {
    std::string error;
    scoped_refptr<Extension> extension = LoadExtension(manifest, &error);
    EXPECT_TRUE(extension) << "Unexpected success for " << name;
    EXPECT_EQ("", error) << "Unexpected no error for " << name;
    return extension;
  }

  void VerifyExpectedError(Extension* extension,
                           const std::string& name,
                           const std::string& error,
                           const std::string& expected_error) {
    EXPECT_FALSE(extension) <<
        "Expected failure loading extension '" << name <<
        "', but didn't get one.";
    EXPECT_TRUE(MatchPattern(error, expected_error)) << name <<
        " expected '" << expected_error << "' but got '" << error << "'";
  }

  void LoadAndExpectError(const std::string& name,
                          const std::string& expected_error) {
    std::string error;
    scoped_refptr<Extension> extension(LoadExtension(name, &error));
    VerifyExpectedError(extension.get(), name, error, expected_error);
  }

  void LoadAndExpectErrorStrict(const std::string& name,
                                const std::string& expected_error) {
    std::string error;
    scoped_refptr<Extension> extension(LoadExtensionStrict(name, &error));
    VerifyExpectedError(extension.get(), name, error, expected_error);
  }

  void LoadAndExpectError(DictionaryValue* manifest,
                          const std::string& name,
                          const std::string& expected_error) {
    std::string error;
    scoped_refptr<Extension> extension(LoadExtension(manifest, &error));
    VerifyExpectedError(extension.get(), name, error, expected_error);
  }

  bool enable_apps_;
};

TEST_F(ExtensionManifestTest, ValidApp) {
  scoped_refptr<Extension> extension(LoadAndExpectSuccess("valid_app.json"));
  ASSERT_EQ(2u, extension->web_extent().patterns().size());
  EXPECT_EQ("http://www.google.com/mail/*",
            extension->web_extent().patterns()[0].GetAsString());
  EXPECT_EQ("http://www.google.com/foobar/*",
            extension->web_extent().patterns()[1].GetAsString());
  EXPECT_EQ(extension_misc::LAUNCH_TAB, extension->launch_container());
  EXPECT_EQ("http://www.google.com/mail/", extension->launch_web_url());
}

TEST_F(ExtensionManifestTest, AppWebUrls) {
  LoadAndExpectError("web_urls_wrong_type.json",
                     errors::kInvalidWebURLs);
  LoadAndExpectError(
      "web_urls_invalid_1.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidWebURL,
          base::IntToString(0),
          errors::kExpectString));

  LoadAndExpectError(
      "web_urls_invalid_2.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidWebURL,
          base::IntToString(0),
          URLPattern::GetParseResultString(
              URLPattern::PARSE_ERROR_MISSING_SCHEME_SEPARATOR)));

  LoadAndExpectError(
      "web_urls_invalid_3.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidWebURL,
          base::IntToString(0),
          errors::kNoWildCardsInPaths));

  LoadAndExpectError(
      "web_urls_invalid_4.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidWebURL,
          base::IntToString(0),
          errors::kCannotClaimAllURLsInExtent));

  LoadAndExpectError(
      "web_urls_invalid_5.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidWebURL,
          base::IntToString(1),
          errors::kCannotClaimAllHostsInExtent));

  // Ports in app.urls only raise an error when loading as a
  // developer would.
  LoadAndExpectSuccess("web_urls_invalid_has_port.json");
  LoadAndExpectErrorStrict(
      "web_urls_invalid_has_port.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidWebURL,
          base::IntToString(1),
          URLPattern::GetParseResultString(URLPattern::PARSE_ERROR_HAS_COLON)));


  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("web_urls_default.json"));
  ASSERT_EQ(1u, extension->web_extent().patterns().size());
  EXPECT_EQ("*://www.google.com/*",
            extension->web_extent().patterns()[0].GetAsString());
}

TEST_F(ExtensionManifestTest, AppLaunchContainer) {
  scoped_refptr<Extension> extension;

  extension = LoadAndExpectSuccess("launch_tab.json");
  EXPECT_EQ(extension_misc::LAUNCH_TAB, extension->launch_container());

  extension = LoadAndExpectSuccess("launch_panel.json");
  EXPECT_EQ(extension_misc::LAUNCH_PANEL, extension->launch_container());

  extension = LoadAndExpectSuccess("launch_default.json");
  EXPECT_EQ(extension_misc::LAUNCH_TAB, extension->launch_container());

  extension = LoadAndExpectSuccess("launch_width.json");
  EXPECT_EQ(640, extension->launch_width());

  extension = LoadAndExpectSuccess("launch_height.json");
  EXPECT_EQ(480, extension->launch_height());

  LoadAndExpectError("launch_window.json",
                     errors::kInvalidLaunchContainer);
  LoadAndExpectError("launch_container_invalid_type.json",
                     errors::kInvalidLaunchContainer);
  LoadAndExpectError("launch_container_invalid_value.json",
                     errors::kInvalidLaunchContainer);
  LoadAndExpectError("launch_container_without_launch_url.json",
                     errors::kLaunchURLRequired);
  LoadAndExpectError("launch_width_invalid.json",
                     errors::kInvalidLaunchWidthContainer);
  LoadAndExpectError("launch_width_negative.json",
                     errors::kInvalidLaunchWidth);
  LoadAndExpectError("launch_height_invalid.json",
                     errors::kInvalidLaunchHeightContainer);
  LoadAndExpectError("launch_height_negative.json",
                     errors::kInvalidLaunchHeight);
}

TEST_F(ExtensionManifestTest, AppLaunchURL) {
  LoadAndExpectError("launch_path_and_url.json",
                     errors::kLaunchPathAndURLAreExclusive);
  LoadAndExpectError("launch_path_invalid_type.json",
                     errors::kInvalidLaunchLocalPath);
  LoadAndExpectError("launch_path_invalid_value.json",
                     errors::kInvalidLaunchLocalPath);
  LoadAndExpectError("launch_url_invalid_type_1.json",
                     errors::kInvalidLaunchWebURL);
  LoadAndExpectError("launch_url_invalid_type_2.json",
                     errors::kInvalidLaunchWebURL);
  LoadAndExpectError("launch_url_invalid_type_3.json",
                     errors::kInvalidLaunchWebURL);

  scoped_refptr<Extension> extension;
  extension = LoadAndExpectSuccess("launch_local_path.json");
  EXPECT_EQ(extension->url().spec() + "launch.html",
            extension->GetFullLaunchURL().spec());

  LoadAndExpectError("launch_web_url_relative.json",
                     errors::kInvalidLaunchWebURL);

  extension = LoadAndExpectSuccess("launch_web_url_absolute.json");
  EXPECT_EQ(GURL("http://www.google.com/launch.html"),
            extension->GetFullLaunchURL());
}

TEST_F(ExtensionManifestTest, Override) {
  LoadAndExpectError("override_newtab_and_history.json",
                     errors::kMultipleOverrides);
  LoadAndExpectError("override_invalid_page.json",
                     errors::kInvalidChromeURLOverrides);

  scoped_refptr<Extension> extension;

  extension = LoadAndExpectSuccess("override_new_tab.json");
  EXPECT_EQ(extension->url().spec() + "newtab.html",
            extension->GetChromeURLOverrides().find("newtab")->second.spec());

  extension = LoadAndExpectSuccess("override_history.json");
  EXPECT_EQ(extension->url().spec() + "history.html",
            extension->GetChromeURLOverrides().find("history")->second.spec());
}

TEST_F(ExtensionManifestTest, ChromeURLPermissionInvalid) {
  LoadAndExpectError("permission_chrome_url_invalid.json",
      errors::kInvalidPermissionScheme);
}

TEST_F(ExtensionManifestTest, ChromeResourcesPermissionValidOnlyForComponents) {
  LoadAndExpectError("permission_chrome_resources_url.json",
      errors::kInvalidPermissionScheme);
  std::string error;
  scoped_refptr<Extension> extension;
  extension = LoadExtensionWithLocation(
      "permission_chrome_resources_url.json",
      Extension::COMPONENT,
      true,  // Strict error checking
      &error);
  EXPECT_EQ("", error);
}

TEST_F(ExtensionManifestTest, InvalidContentScriptMatchPattern) {

  // chrome:// urls are not allowed.
  LoadAndExpectError(
      "content_script_chrome_url_invalid.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidMatch,
          base::IntToString(0),
          base::IntToString(0),
          URLPattern::GetParseResultString(
              URLPattern::PARSE_ERROR_INVALID_SCHEME)));

  // Match paterns must be strings.
  LoadAndExpectError(
      "content_script_match_pattern_not_string.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidMatch,
          base::IntToString(0),
          base::IntToString(0),
          errors::kExpectString));

  // Ports in match patterns cause an error, but only when loading
  // in developer mode.
  LoadAndExpectSuccess("forbid_ports_in_content_scripts.json");

  // Loading as a developer would should give an error.
  LoadAndExpectErrorStrict(
      "forbid_ports_in_content_scripts.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidMatch,
          base::IntToString(1),
          base::IntToString(0),
          URLPattern::GetParseResultString(
              URLPattern::PARSE_ERROR_HAS_COLON)));
}

TEST_F(ExtensionManifestTest, ExperimentalPermission) {
  LoadAndExpectError("experimental.json", errors::kExperimentalFlagRequired);
  CommandLine old_command_line = *CommandLine::ForCurrentProcess();
  CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kEnableExperimentalExtensionApis);
  LoadAndExpectSuccess("experimental.json");
  *CommandLine::ForCurrentProcess() = old_command_line;
}

TEST_F(ExtensionManifestTest, DevToolsExtensions) {
  LoadAndExpectError("devtools_extension_no_permissions.json",
      errors::kDevToolsExperimental);
  LoadAndExpectError("devtools_extension_url_invalid_type.json",
      errors::kInvalidDevToolsPage);

  CommandLine old_command_line = *CommandLine::ForCurrentProcess();
  CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kEnableExperimentalExtensionApis);

  scoped_refptr<Extension> extension;
  extension = LoadAndExpectSuccess("devtools_extension.json");
  EXPECT_EQ(extension->url().spec() + "devtools.html",
            extension->devtools_url().spec());
  EXPECT_TRUE(extension->HasEffectiveAccessToAllHosts());

  *CommandLine::ForCurrentProcess() = old_command_line;
}

TEST_F(ExtensionManifestTest, Sidebar) {
  LoadAndExpectError("sidebar.json",
      errors::kExperimentalFlagRequired);

  CommandLine old_command_line = *CommandLine::ForCurrentProcess();
  CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kEnableExperimentalExtensionApis);

  LoadAndExpectError("sidebar_no_permissions.json",
      errors::kSidebarExperimental);

  LoadAndExpectError("sidebar_icon_empty.json",
      errors::kInvalidSidebarDefaultIconPath);
  LoadAndExpectError("sidebar_icon_invalid_type.json",
      errors::kInvalidSidebarDefaultIconPath);
  LoadAndExpectError("sidebar_page_empty.json",
      errors::kInvalidSidebarDefaultPage);
  LoadAndExpectError("sidebar_page_invalid_type.json",
      errors::kInvalidSidebarDefaultPage);
  LoadAndExpectError("sidebar_title_invalid_type.json",
      errors::kInvalidSidebarDefaultTitle);

  scoped_refptr<Extension> extension(LoadAndExpectSuccess("sidebar.json"));
  ASSERT_TRUE(extension->sidebar_defaults() != NULL);
  EXPECT_EQ(extension->sidebar_defaults()->default_title(),
            ASCIIToUTF16("Default title"));
  EXPECT_EQ(extension->sidebar_defaults()->default_icon_path(),
            "icon.png");
  EXPECT_EQ(extension->url().spec() + "sidebar.html",
            extension->sidebar_defaults()->default_page().spec());

  *CommandLine::ForCurrentProcess() = old_command_line;
}

TEST_F(ExtensionManifestTest, DisallowHybridApps) {
  LoadAndExpectError("disallow_hybrid_1.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kHostedAppsCannotIncludeExtensionFeatures,
          keys::kBrowserAction));
  LoadAndExpectError("disallow_hybrid_2.json",
                     errors::kBackgroundPermissionNeeded);
}

TEST_F(ExtensionManifestTest, OptionsPageInApps) {
  scoped_refptr<Extension> extension;

  // Allow options page with absolute URL in hosed apps.
  extension = LoadAndExpectSuccess("hosted_app_absolute_options.json");
  EXPECT_STREQ("http",
               extension->options_url().scheme().c_str());
  EXPECT_STREQ("example.com",
               extension->options_url().host().c_str());
  EXPECT_STREQ("options.html",
               extension->options_url().ExtractFileName().c_str());

  // Forbid options page with relative URL in hosted apps.
  LoadAndExpectError("hosted_app_relative_options.json",
                     errors::kInvalidOptionsPageInHostedApp);

  // Forbid options page with non-(http|https) scheme in hosted app.
  LoadAndExpectError("hosted_app_file_options.json",
                     errors::kInvalidOptionsPageInHostedApp);

  // Forbid absolute URL for options page in packaged apps.
  LoadAndExpectError("packaged_app_absolute_options.json",
                     errors::kInvalidOptionsPageExpectUrlInPackage);
}

TEST_F(ExtensionManifestTest, AllowUnrecognizedPermissions) {
  std::string error;
  scoped_ptr<DictionaryValue> manifest(
      LoadManifestFile("valid_app.json", &error));
  ASSERT_TRUE(manifest.get());

  ListValue *permissions = new ListValue();
  manifest->Set(keys::kPermissions, permissions);
  for (size_t i = 0; i < Extension::kNumPermissions; i++) {
    const char* name = Extension::kPermissions[i].name;
    StringValue* p = new StringValue(name);
    permissions->Clear();
    permissions->Append(p);
    std::string message_name = base::StringPrintf("permission-%s", name);

    // Extensions are allowed to contain unrecognized API permissions,
    // so there shouldn't be any errors.
    scoped_refptr<Extension> extension;
    extension = LoadAndExpectSuccess(manifest.get(), message_name);
  }
}

TEST_F(ExtensionManifestTest, NormalizeIconPaths) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("normalize_icon_paths.json"));
  EXPECT_EQ("16.png",
            extension->icons().Get(16, ExtensionIconSet::MATCH_EXACTLY));
  EXPECT_EQ("48.png",
            extension->icons().Get(48, ExtensionIconSet::MATCH_EXACTLY));
}

TEST_F(ExtensionManifestTest, DisallowMultipleUISurfaces) {
  LoadAndExpectError("multiple_ui_surfaces_1.json", errors::kOneUISurfaceOnly);
  LoadAndExpectError("multiple_ui_surfaces_2.json", errors::kOneUISurfaceOnly);
  LoadAndExpectError("multiple_ui_surfaces_3.json", errors::kOneUISurfaceOnly);
}

TEST_F(ExtensionManifestTest, ParseHomepageURLs) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("homepage_valid.json"));
  LoadAndExpectError("homepage_empty.json",
                     extension_manifest_errors::kInvalidHomepageURL);
  LoadAndExpectError("homepage_invalid.json",
                     extension_manifest_errors::kInvalidHomepageURL);
}

TEST_F(ExtensionManifestTest, GetHomepageURL) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("homepage_valid.json"));
  EXPECT_EQ(GURL("http://foo.com#bar"), extension->GetHomepageURL());

  // The Google Gallery URL ends with the id, which depends on the path, which
  // can be different in testing, so we just check the part before id.
  extension = LoadAndExpectSuccess("homepage_google_hosted.json");
  EXPECT_TRUE(StartsWithASCII(extension->GetHomepageURL().spec(),
                              "https://chrome.google.com/webstore/detail/",
                              false));

  extension = LoadAndExpectSuccess("homepage_externally_hosted.json");
  EXPECT_EQ(GURL(), extension->GetHomepageURL());
}

TEST_F(ExtensionManifestTest, DefaultPathForExtent) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("default_path_for_extent.json"));

  ASSERT_EQ(1u, extension->web_extent().patterns().size());
  EXPECT_EQ("/*", extension->web_extent().patterns()[0].path());
  EXPECT_TRUE(extension->web_extent().ContainsURL(
      GURL("http://www.google.com/monkey")));
}

TEST_F(ExtensionManifestTest, DefaultLocale) {
  LoadAndExpectError("default_locale_invalid.json",
                     extension_manifest_errors::kInvalidDefaultLocale);

  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("default_locale_valid.json"));
  EXPECT_EQ("de-AT", extension->default_locale());
}

TEST_F(ExtensionManifestTest, TtsProvider) {
  LoadAndExpectError("tts_provider_invalid_1.json",
                     extension_manifest_errors::kInvalidTts);
  LoadAndExpectError("tts_provider_invalid_2.json",
                     extension_manifest_errors::kInvalidTtsVoices);
  LoadAndExpectError("tts_provider_invalid_3.json",
                     extension_manifest_errors::kInvalidTtsVoices);
  LoadAndExpectError("tts_provider_invalid_4.json",
                     extension_manifest_errors::kInvalidTtsVoicesVoiceName);
  LoadAndExpectError("tts_provider_invalid_5.json",
                     extension_manifest_errors::kInvalidTtsVoicesLocale);
  LoadAndExpectError("tts_provider_invalid_6.json",
                     extension_manifest_errors::kInvalidTtsVoicesLocale);
  LoadAndExpectError("tts_provider_invalid_7.json",
                     extension_manifest_errors::kInvalidTtsVoicesGender);

  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("tts_provider_valid.json"));

  ASSERT_EQ(1u, extension->tts_voices().size());
  EXPECT_EQ("name", extension->tts_voices()[0].voice_name);
  EXPECT_EQ("en-US", extension->tts_voices()[0].locale);
  EXPECT_EQ("female", extension->tts_voices()[0].gender);
}

TEST_F(ExtensionManifestTest, ForbidPortsInPermissions) {
  // Loading as a user would shoud not trigger an error.
  LoadAndExpectSuccess("forbid_ports_in_permissions.json");

  // Ideally, loading as a developer would give an error.
  // To ensure that we do not error out on a valid permission
  // in a future version of chrome, validation is to loose
  // to flag this case.
  LoadStrictAndExpectSuccess("forbid_ports_in_permissions.json");
}

TEST_F(ExtensionManifestTest, IsolatedApps) {
  // Requires --enable-experimental-app-manifests
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("isolated_app_valid.json"));
  EXPECT_FALSE(extension->is_storage_isolated());

  CommandLine old_command_line = *CommandLine::ForCurrentProcess();
  CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kEnableExperimentalAppManifests);
  scoped_refptr<Extension> extension2(
      LoadAndExpectSuccess("isolated_app_valid.json"));
  EXPECT_TRUE(extension2->is_storage_isolated());
  *CommandLine::ForCurrentProcess() = old_command_line;
}


TEST_F(ExtensionManifestTest, FileBrowserHandlers) {
  LoadAndExpectError("filebrowser_invalid_actions_1.json",
      errors::kInvalidFileBrowserHandler);
  LoadAndExpectError("filebrowser_invalid_actions_2.json",
      errors::kInvalidFileBrowserHandler);
  LoadAndExpectError("filebrowser_invalid_action_id.json",
      errors::kInvalidPageActionId);
  LoadAndExpectError("filebrowser_invalid_action_title.json",
      errors::kInvalidPageActionDefaultTitle);
  LoadAndExpectError("filebrowser_invalid_action_id.json",
      errors::kInvalidPageActionId);
  LoadAndExpectError("filebrowser_invalid_file_filters_1.json",
      errors::kInvalidFileFiltersList);
  LoadAndExpectError("filebrowser_invalid_file_filters_2.json",
      ExtensionErrorUtils::FormatErrorMessage(
          errors::kInvalidFileFilterValue, base::IntToString(0)));
  LoadAndExpectError("filebrowser_invalid_file_filters_url.json",
      ExtensionErrorUtils::FormatErrorMessage(errors::kInvalidURLPatternError,
                                              "http:*.html"));

  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("filebrowser_valid.json"));
  ASSERT_TRUE(extension->file_browser_handlers() != NULL);
  ASSERT_EQ(extension->file_browser_handlers()->size(), 1U);
  const FileBrowserHandler* action =
      extension->file_browser_handlers()->at(0).get();
  EXPECT_EQ(action->title(), "Default title");
  EXPECT_EQ(action->icon_path(), "icon.png");
  const FileBrowserHandler::PatternList& patterns = action->file_url_patterns();
  ASSERT_EQ(patterns.size(), 1U);
  ASSERT_TRUE(action->MatchesURL(
      GURL("filesystem:chrome-extension://foo/local/test.txt")));
}
