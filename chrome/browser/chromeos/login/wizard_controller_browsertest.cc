// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/login/enterprise_enrollment_screen.h"
#include "chrome/browser/chromeos/login/existing_user_controller.h"
#include "chrome/browser/chromeos/login/language_switch_menu.h"
#include "chrome/browser/chromeos/login/mock_eula_screen.h"
#include "chrome/browser/chromeos/login/mock_network_screen.h"
#include "chrome/browser/chromeos/login/mock_update_screen.h"
#include "chrome/browser/chromeos/login/network_screen.h"
#include "chrome/browser/chromeos/login/network_selection_view.h"
#include "chrome/browser/chromeos/login/user_image_screen.h"
#include "chrome/browser/chromeos/login/views_oobe_display.h"
#include "chrome/browser/chromeos/login/view_screen.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/login/wizard_in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"
#include "grit/generated_resources.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "unicode/locid.h"
#include "views/accelerator.h"

namespace chromeos {

template <class T>
class MockOutShowHide : public T {
 public:
  template <class P> MockOutShowHide(P p) : T(p) {}
  MOCK_METHOD0(Show, void());
  MOCK_METHOD0(Hide, void());
};

#define MOCK(mock_var, screen_name, mocked_class)                              \
  mock_var = new MockOutShowHide<mocked_class>(controller());                  \
  controller()->screen_name.reset(mock_var);                                   \
  EXPECT_CALL(*mock_var, Show()).Times(0);                                     \
  EXPECT_CALL(*mock_var, Hide()).Times(0);

#define MOCK_OLD(mock_var, screen_name, mocked_class)                          \
  mock_var = new MockOutShowHide<mocked_class>(                                \
      static_cast<ViewsOobeDisplay*>(controller()->oobe_display_.get()));      \
  controller()->screen_name.reset(mock_var);                                   \
  EXPECT_CALL(*mock_var, Show()).Times(0);                                     \
  EXPECT_CALL(*mock_var, Hide()).Times(0);

class WizardControllerTest : public WizardInProcessBrowserTest {
 protected:
  WizardControllerTest() : WizardInProcessBrowserTest(
      WizardController::kTestNoScreenName) {}
  virtual ~WizardControllerTest() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(WizardControllerTest);
};

IN_PROC_BROWSER_TEST_F(WizardControllerTest, SwitchLanguage) {
  ASSERT_TRUE(controller() != NULL);
  controller()->ShowFirstScreen(WizardController::kNetworkScreenName);

  // Checking the default locale. Provided that the profile is cleared in SetUp.
  EXPECT_EQ("en-US", g_browser_process->GetApplicationLocale());
  EXPECT_STREQ("en", icu::Locale::getDefault().getLanguage());
  EXPECT_FALSE(base::i18n::IsRTL());
  const std::wstring en_str =
      UTF16ToWide(l10n_util::GetStringUTF16(IDS_NETWORK_SELECTION_TITLE));

  LanguageSwitchMenu::SwitchLanguage("fr");
  EXPECT_EQ("fr", g_browser_process->GetApplicationLocale());
  EXPECT_STREQ("fr", icu::Locale::getDefault().getLanguage());
  EXPECT_FALSE(base::i18n::IsRTL());
  const std::wstring fr_str =
      UTF16ToWide(l10n_util::GetStringUTF16(IDS_NETWORK_SELECTION_TITLE));

  EXPECT_NE(en_str, fr_str);

  LanguageSwitchMenu::SwitchLanguage("ar");
  EXPECT_EQ("ar", g_browser_process->GetApplicationLocale());
  EXPECT_STREQ("ar", icu::Locale::getDefault().getLanguage());
  EXPECT_TRUE(base::i18n::IsRTL());
  const std::wstring ar_str =
      UTF16ToWide(l10n_util::GetStringUTF16(IDS_NETWORK_SELECTION_TITLE));

  EXPECT_NE(fr_str, ar_str);
}

class WizardControllerFlowTest : public WizardControllerTest {
 protected:
  WizardControllerFlowTest() {}
  // Overriden from InProcessBrowserTest:
  virtual Browser* CreateBrowser(Profile* profile) {
    Browser* ret = WizardControllerTest::CreateBrowser(profile);

    // Make sure that OOBE is run as an "official" build.
    WizardController::default_controller()->is_official_build_ = true;

    // Set up the mocks for all screens.
    MOCK(mock_network_screen_, network_screen_, MockNetworkScreen);
    MOCK(mock_update_screen_, update_screen_, MockUpdateScreen);
    MOCK(mock_eula_screen_, eula_screen_, MockEulaScreen);

    MOCK_OLD(mock_enterprise_enrollment_screen_,
             enterprise_enrollment_screen_,
             EnterpriseEnrollmentScreen);

    // Switch to the initial screen.
    EXPECT_EQ(NULL, controller()->current_screen());
    EXPECT_CALL(*mock_network_screen_, Show()).Times(1);
    controller()->ShowFirstScreen(WizardController::kNetworkScreenName);

    return ret;
  }

  void OnExit(ScreenObserver::ExitCodes exit_code) {
    controller()->OnExit(exit_code);
  }

  MockOutShowHide<MockNetworkScreen>* mock_network_screen_;
  MockOutShowHide<MockUpdateScreen>* mock_update_screen_;
  MockOutShowHide<MockEulaScreen>* mock_eula_screen_;
  MockOutShowHide<EnterpriseEnrollmentScreen>*
      mock_enterprise_enrollment_screen_;

 private:
  DISALLOW_COPY_AND_ASSIGN(WizardControllerFlowTest);
};

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest, ControlFlowMain) {
  EXPECT_TRUE(ExistingUserController::current_controller() == NULL);
  EXPECT_EQ(controller()->GetNetworkScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_network_screen_, Hide()).Times(1);
  EXPECT_CALL(*mock_eula_screen_, Show()).Times(1);
  OnExit(ScreenObserver::NETWORK_CONNECTED);

  EXPECT_EQ(controller()->GetEulaScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_eula_screen_, Hide()).Times(1);
  EXPECT_CALL(*mock_update_screen_, StartUpdate()).Times(1);
  EXPECT_CALL(*mock_update_screen_, Show()).Times(1);
  OnExit(ScreenObserver::EULA_ACCEPTED);
  // Let update screen smooth time process (time = 0ms).
  ui_test_utils::RunAllPendingInMessageLoop();

  EXPECT_EQ(controller()->GetUpdateScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_update_screen_, Hide()).Times(0);
  EXPECT_CALL(*mock_eula_screen_, Show()).Times(0);
  OnExit(ScreenObserver::UPDATE_INSTALLED);

  EXPECT_FALSE(ExistingUserController::current_controller() == NULL);
  set_controller(NULL);
}

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest, ControlFlowErrorUpdate) {
  EXPECT_EQ(controller()->GetNetworkScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_update_screen_, StartUpdate()).Times(0);
  EXPECT_CALL(*mock_eula_screen_, Show()).Times(1);
  EXPECT_CALL(*mock_update_screen_, Show()).Times(0);
  EXPECT_CALL(*mock_network_screen_, Hide()).Times(1);
  OnExit(ScreenObserver::NETWORK_CONNECTED);

  EXPECT_EQ(controller()->GetEulaScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_eula_screen_, Hide()).Times(1);
  EXPECT_CALL(*mock_update_screen_, StartUpdate()).Times(1);
  EXPECT_CALL(*mock_update_screen_, Show()).Times(1);
  OnExit(ScreenObserver::EULA_ACCEPTED);
  // Let update screen smooth time process (time = 0ms).
  ui_test_utils::RunAllPendingInMessageLoop();

  EXPECT_EQ(controller()->GetUpdateScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_update_screen_, Hide()).Times(0);
  EXPECT_CALL(*mock_eula_screen_, Show()).Times(0);
  EXPECT_CALL(*mock_eula_screen_, Hide()).Times(0);  // last transition
  OnExit(ScreenObserver::UPDATE_ERROR_UPDATING);

  EXPECT_FALSE(ExistingUserController::current_controller() == NULL);
  set_controller(NULL);
}

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest, ControlFlowEulaDeclined) {
  EXPECT_EQ(controller()->GetNetworkScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_update_screen_, StartUpdate()).Times(0);
  EXPECT_CALL(*mock_eula_screen_, Show()).Times(1);
  EXPECT_CALL(*mock_network_screen_, Hide()).Times(1);
  OnExit(ScreenObserver::NETWORK_CONNECTED);

  EXPECT_EQ(controller()->GetEulaScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_eula_screen_, Hide()).Times(1);
  EXPECT_CALL(*mock_network_screen_, Show()).Times(1);
  EXPECT_CALL(*mock_network_screen_, Hide()).Times(0);  // last transition
  OnExit(ScreenObserver::EULA_BACK);

  EXPECT_EQ(controller()->GetNetworkScreen(), controller()->current_screen());
}

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest, ControlFlowErrorNetwork) {
  EXPECT_EQ(controller()->GetNetworkScreen(), controller()->current_screen());
  OnExit(ScreenObserver::NETWORK_OFFLINE);

  EXPECT_FALSE(ExistingUserController::current_controller() == NULL);
  set_controller(NULL);
}

#if !defined(OFFICIAL_BUILD)
// TODO(mnissler): These tests are not yet enabled for official builds. Remove
// the guards once we enable the enrollment feature for official builds.

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest,
                       ControlFlowEnterpriseEnrollmentCompleted) {
  EXPECT_EQ(controller()->GetNetworkScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_update_screen_, StartUpdate()).Times(0);
  EXPECT_CALL(*mock_enterprise_enrollment_screen_, Show()).Times(1);
  EXPECT_CALL(*mock_network_screen_, Hide()).Times(1);

  controller()->ShowEnterpriseEnrollmentScreen();
  EXPECT_EQ(controller()->GetEnterpriseEnrollmentScreen(),
            controller()->current_screen());
  OnExit(ScreenObserver::ENTERPRISE_ENROLLMENT_COMPLETED);

  EXPECT_FALSE(ExistingUserController::current_controller() == NULL);
  set_controller(NULL);
}

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest,
                       ControlFlowEnterpriseEnrollmentCancelled) {
  EXPECT_EQ(controller()->GetNetworkScreen(), controller()->current_screen());
  EXPECT_CALL(*mock_update_screen_, StartUpdate()).Times(0);
  EXPECT_CALL(*mock_enterprise_enrollment_screen_, Show()).Times(1);
  EXPECT_CALL(*mock_network_screen_, Hide()).Times(1);

  controller()->ShowEnterpriseEnrollmentScreen();
  EXPECT_EQ(controller()->GetEnterpriseEnrollmentScreen(),
            controller()->current_screen());
  OnExit(ScreenObserver::ENTERPRISE_ENROLLMENT_CANCELLED);

  EXPECT_FALSE(ExistingUserController::current_controller() == NULL);
  set_controller(NULL);
}
#endif

#if defined(OFFICIAL_BUILD)
// This test is supposed to fail on official build.
#define MAYBE_Accelerators DISABLED_Accelerators
#else
#define MAYBE_Accelerators Accelerators
#endif

IN_PROC_BROWSER_TEST_F(WizardControllerFlowTest, MAYBE_Accelerators) {
  //TODO(altimofeev): do not depend on the display realization.

  ViewsOobeDisplay* display =
      static_cast<ViewsOobeDisplay*>(controller()->oobe_display_.get());
  views::View* contents = display->contents_;

  EXPECT_EQ(controller()->GetNetworkScreen(), controller()->current_screen());

  views::Accelerator accel_network_screen(ui::VKEY_N, false, true, true);
  views::Accelerator accel_update_screen(ui::VKEY_U, false, true, true);
  views::Accelerator accel_image_screen(ui::VKEY_I, false, true, true);
  views::Accelerator accel_eula_screen(ui::VKEY_E, false, true, true);
  views::Accelerator accel_enterprise_enrollment_screen(
      ui::VKEY_P, false, true, true);

  views::FocusManager* focus_manager = NULL;

  focus_manager = contents->GetFocusManager();
  EXPECT_CALL(*mock_network_screen_, Hide()).Times(1);
  EXPECT_CALL(*mock_enterprise_enrollment_screen_, Show()).Times(1);
  EXPECT_TRUE(
      focus_manager->ProcessAccelerator(accel_enterprise_enrollment_screen));
  EXPECT_EQ(controller()->GetEnterpriseEnrollmentScreen(),
            controller()->current_screen());

  focus_manager = contents->GetFocusManager();
  EXPECT_CALL(*mock_enterprise_enrollment_screen_, Hide()).Times(1);
  EXPECT_CALL(*mock_network_screen_, Show()).Times(1);
  EXPECT_TRUE(focus_manager->ProcessAccelerator(accel_network_screen));
  EXPECT_EQ(controller()->GetNetworkScreen(), controller()->current_screen());

  focus_manager = contents->GetFocusManager();
  EXPECT_CALL(*mock_network_screen_, Hide()).Times(1);
  EXPECT_CALL(*mock_update_screen_, Show()).Times(1);
  EXPECT_TRUE(focus_manager->ProcessAccelerator(accel_update_screen));
  EXPECT_EQ(controller()->GetUpdateScreen(), controller()->current_screen());

  focus_manager = contents->GetFocusManager();
  EXPECT_CALL(*mock_update_screen_, Hide()).Times(1);
  EXPECT_TRUE(focus_manager->ProcessAccelerator(accel_image_screen));
  EXPECT_EQ(controller()->GetUserImageScreen(), controller()->current_screen());

  focus_manager = contents->GetFocusManager();
  EXPECT_CALL(*mock_eula_screen_, Show()).Times(1);
  EXPECT_TRUE(focus_manager->ProcessAccelerator(accel_eula_screen));
  EXPECT_EQ(controller()->GetEulaScreen(), controller()->current_screen());
}

COMPILE_ASSERT(ScreenObserver::EXIT_CODES_COUNT == 17,
               add_tests_for_new_control_flow_you_just_introduced);

}  // namespace chromeos
