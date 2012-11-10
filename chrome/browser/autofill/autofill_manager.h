// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_AUTOFILL_MANAGER_H_
#define CHROME_BROWSER_AUTOFILL_AUTOFILL_MANAGER_H_

#include <list>
#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "base/string16.h"
#include "base/time.h"
#include "chrome/browser/autofill/autofill_download.h"
#include "chrome/browser/autofill/field_types.h"
#include "chrome/browser/autofill/form_structure.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "chrome/browser/sync/profile_sync_service_observer.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_contents_observer.h"

class AutofillExternalDelegate;
class AutofillField;
class AutofillProfile;
class AutofillMetrics;
class CreditCard;
class PersonalDataManager;
class PrefService;
class ProfileSyncService;
class TabContents;

struct ViewHostMsg_FrameNavigate_Params;

namespace autofill {
class PasswordGenerator;
}

namespace content {
class RenderViewHost;
}

namespace gfx {
class Rect;
}

namespace IPC {
class Message;
}

namespace webkit {
namespace forms {
struct FormData;
struct FormField;
struct PasswordForm;
struct PasswordFormFillData;
}
}

// Manages saving and restoring the user's personal information entered into web
// forms.
class AutofillManager : public content::NotificationObserver,
                        public content::WebContentsObserver,
                        public AutofillDownloadManager::Observer,
                        public ProfileSyncServiceObserver,
                        public base::RefCounted<AutofillManager> {
 public:
  explicit AutofillManager(TabContents* tab_contents);

  // Registers our Enable/Disable Autofill pref.
  static void RegisterUserPrefs(PrefService* prefs);

  // Set our external delegate.
  // TODO(jrg): consider passing delegate into the ctor.  That won't
  // work if the delegate has a pointer to the AutofillManager, but
  // future directions may not need such a pointer.
  void SetExternalDelegate(AutofillExternalDelegate* delegate) {
    external_delegate_ = delegate;
  }

  // Used to say if this class has an external delegate that it is using.
  bool HasExternalDelegate();

  // Called from our external delegate so they cannot be private.
  virtual void OnFillAutofillFormData(int query_id,
                                      const webkit::forms::FormData& form,
                                      const webkit::forms::FormField& field,
                                      int unique_id);
  void OnDidShowAutofillSuggestions(bool is_new_popup);
  void OnDidFillAutofillFormData(const base::TimeTicks& timestamp);
  void OnShowAutofillDialog();
  void OnDidPreviewAutofillFormData();
  void OnShowPasswordGenerationPopup(const gfx::Rect& bounds,
                                     int max_length,
                                     const webkit::forms::PasswordForm& form);

  // Remove the credit card or Autofill profile that matches |unique_id|
  // from the database.
  void RemoveAutofillProfileOrCreditCard(int unique_id);

 protected:
  // Only test code should subclass AutofillManager.
  friend class base::RefCounted<AutofillManager>;
  virtual ~AutofillManager();

  // The string/int pair is composed of the guid string and variant index
  // respectively.  The variant index is an index into the multi-valued item
  // (where applicable).
  typedef std::pair<std::string, size_t> GUIDPair;

  // Test code should prefer to use this constructor.
  AutofillManager(TabContents* tab_contents,
                  PersonalDataManager* personal_data);

  // Returns the value of the AutofillEnabled pref.
  virtual bool IsAutofillEnabled() const;

  // Uploads the form data to the Autofill server.
  virtual void UploadFormData(const FormStructure& submitted_form);

  // Reset cache.
  void Reset();

  // Informs the renderer of the current password generation state. This is a
  // separate function to aid with testing.
  virtual void SendPasswordGenerationStateToRenderer(
      content::RenderViewHost* host,
      bool enabled);

  // Logs quality metrics for the |submitted_form| and uploads the form data
  // to the crowdsourcing server, if appropriate.
  virtual void UploadFormDataAsyncCallback(
      const FormStructure* submitted_form,
      const base::TimeTicks& load_time,
      const base::TimeTicks& interaction_time,
      const base::TimeTicks& submission_time);

  // Maps GUIDs to and from IDs that are used to identify profiles and credit
  // cards sent to and from the renderer process.
  virtual int GUIDToID(const GUIDPair& guid) const;
  virtual const GUIDPair IDToGUID(int id) const;

  // Methods for packing and unpacking credit card and profile IDs for sending
  // and receiving to and from the renderer process.
  int PackGUIDs(const GUIDPair& cc_guid, const GUIDPair& profile_guid) const;
  void UnpackGUIDs(int id, GUIDPair* cc_guid, GUIDPair* profile_guid) const;

  const AutofillMetrics* metric_logger() const { return metric_logger_.get(); }
  void set_metric_logger(const AutofillMetrics* metric_logger);

  ScopedVector<FormStructure>* form_structures() { return &form_structures_; }

  // Exposed for testing.
  AutofillExternalDelegate* external_delegate() {
    return external_delegate_;
  }

  // Processes the submitted |form|, saving any new Autofill data and uploading
  // the possible field types for the submitted fields to the crowdsouring
  // server.  Returns false if this form is not relevant for Autofill.
  bool OnFormSubmitted(const webkit::forms::FormData& form,
                       const base::TimeTicks& timestamp);

 private:
  // content::WebContentsObserver:
  virtual void RenderViewCreated(content::RenderViewHost* host) OVERRIDE;
  virtual void DidNavigateMainFrame(
      const content::LoadCommittedDetails& details,
      const content::FrameNavigateParams& params) OVERRIDE;
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  // AutofillDownloadManager::Observer:
  virtual void OnLoadedServerPredictions(
      const std::string& response_xml) OVERRIDE;

  // ProfileSyncServiceObserver:
  virtual void OnStateChanged() OVERRIDE;

  // Register as an observer with the sync service.
  void RegisterWithSyncService();

  // content::NotificationObserver override
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Determines what the current state of password generation is, and if it has
  // changed from |password_generation_enabled_|. If it has changed or if
  // |new_renderer| is true, it notifies the renderer of this change via
  // SendPasswordGenerationStateToRenderer.
  void UpdatePasswordGenerationState(content::RenderViewHost* host,
                                     bool new_renderer);

  void OnFormsSeen(const std::vector<webkit::forms::FormData>& forms,
                   const base::TimeTicks& timestamp);
  void OnTextFieldDidChange(const webkit::forms::FormData& form,
                            const webkit::forms::FormField& field,
                            const base::TimeTicks& timestamp);

  // The |bounding_box| is a window relative value.
  void OnQueryFormFieldAutofill(int query_id,
                                const webkit::forms::FormData& form,
                                const webkit::forms::FormField& field,
                                const gfx::Rect& bounding_box,
                                bool display_warning);
  void OnDidEndTextFieldEditing();
  void OnHideAutofillPopup();
  void OnAddPasswordFormMapping(
      const webkit::forms::FormField& form,
      const webkit::forms::PasswordFormFillData& fill_data);
  void OnShowPasswordSuggestions(const webkit::forms::FormField& field,
                                 const gfx::Rect& bounds,
                                 const std::vector<string16>& suggestions);
  void OnSetDataList(const std::vector<string16>& values,
                     const std::vector<string16>& labels,
                     const std::vector<string16>& icons,
                     const std::vector<int>& unique_ids);

  // Fills |host| with the RenderViewHost for this tab.
  // Returns false if Autofill is disabled or if the host is unavailable.
  bool GetHost(const std::vector<AutofillProfile*>& profiles,
               const std::vector<CreditCard*>& credit_cards,
               content::RenderViewHost** host) const WARN_UNUSED_RESULT;

  // Unpacks |unique_id| and fills |profile| or |credit_card| with the
  // appropriate data source.  Returns false if the unpacked id cannot be found.
  bool GetProfileOrCreditCard(int unique_id,
                              const std::vector<AutofillProfile*>& profiles,
                              const std::vector<CreditCard*>& credit_cards,
                              const AutofillProfile** profile,
                              const CreditCard** credit_card,
                              size_t* variant) const WARN_UNUSED_RESULT;

  // Fills |form_structure| cached element corresponding to |form|.
  // Returns false if the cached element was not found.
  bool FindCachedForm(const webkit::forms::FormData& form,
                      FormStructure** form_structure) const WARN_UNUSED_RESULT;

  // Fills |form_structure| and |autofill_field| with the cached elements
  // corresponding to |form| and |field|.  This might have the side-effect of
  // updating the cache.  Returns false if the |form| is not autofillable, or if
  // it is not already present in the cache and the cache is full.
  bool GetCachedFormAndField(const webkit::forms::FormData& form,
                             const webkit::forms::FormField& field,
                             FormStructure** form_structure,
                             AutofillField** autofill_field) WARN_UNUSED_RESULT;

  // Re-parses |live_form| and adds the result to |form_structures_|.
  // |cached_form| should be a pointer to the existing version of the form, or
  // NULL if no cached version exists.  The updated form is then written into
  // |updated_form|.  Returns false if the cache could not be updated.
  bool UpdateCachedForm(const webkit::forms::FormData& live_form,
                        const FormStructure* cached_form,
                        FormStructure** updated_form) WARN_UNUSED_RESULT;

  // Returns a list of values from the stored profiles that match |type| and the
  // value of |field| and returns the labels of the matching profiles. |labels|
  // is filled with the Profile label.
  void GetProfileSuggestions(FormStructure* form,
                             const webkit::forms::FormField& field,
                             AutofillFieldType type,
                             std::vector<string16>* values,
                             std::vector<string16>* labels,
                             std::vector<string16>* icons,
                             std::vector<int>* unique_ids) const;

  // Returns a list of values from the stored credit cards that match |type| and
  // the value of |field| and returns the labels of the matching credit cards.
  void GetCreditCardSuggestions(FormStructure* form,
                                const webkit::forms::FormField& field,
                                AutofillFieldType type,
                                std::vector<string16>* values,
                                std::vector<string16>* labels,
                                std::vector<string16>* icons,
                                std::vector<int>* unique_ids) const;

  // Set |field|'s value based on |type| and contents of the |credit_card|.
  void FillCreditCardFormField(const CreditCard& credit_card,
                               AutofillFieldType type,
                               webkit::forms::FormField* field);

  // Set |field|'s value based on |cached_field|'s type and contents of the
  // |profile|. The |variant| parameter specifies which value in a multi-valued
  // profile.
  void FillFormField(const AutofillProfile& profile,
                     const AutofillField& cached_field,
                     size_t variant,
                     webkit::forms::FormField* field);

  // Set |field|'s value for phone number based on contents of the |profile|.
  // The |cached_field| specifies the type of the phone and whether this is a
  // phone prefix or suffix.  The |variant| parameter specifies which value in a
  // multi-valued profile.
  void FillPhoneNumberField(const AutofillProfile& profile,
                            const AutofillField& cached_field,
                            size_t variant,
                            webkit::forms::FormField* field);

  // Parses the forms using heuristic matching and querying the Autofill server.
  void ParseForms(const std::vector<webkit::forms::FormData>& forms);

  // Imports the form data, submitted by the user, into |personal_data_|.
  void ImportFormData(const FormStructure& submitted_form);

  // If |initial_interaction_timestamp_| is unset or is set to a later time than
  // |interaction_timestamp|, updates the cached timestamp.  The latter check is
  // needed because IPC messages can arrive out of order.
  void UpdateInitialInteractionTimestamp(
      const base::TimeTicks& interaction_timestamp);

  // Send our current field type predictions to the renderer. This is a no-op if
  // the appropriate command-line flag is not set.
  void SendAutofillTypePredictions(
      const std::vector<FormStructure*>& forms) const;

  // The owning TabContents.
  TabContents* tab_contents_;

  // The personal data manager, used to save and load personal data to/from the
  // web database.  This is overridden by the AutofillManagerTest.
  // Weak reference.
  // May be NULL.  NULL indicates OTR.
  PersonalDataManager* personal_data_;

  std::list<std::string> autofilled_form_signatures_;
  // Handles queries and uploads to Autofill servers.
  AutofillDownloadManager download_manager_;

  // Should be set to true in AutofillManagerTest and other tests, false in
  // AutofillDownloadManagerTest and in non-test environment. Is false by
  // default for the public constructor, and true by default for the test-only
  // constructors.
  bool disable_download_manager_requests_;

  // For logging UMA metrics. Overridden by metrics tests.
  scoped_ptr<const AutofillMetrics> metric_logger_;
  // Have we logged whether Autofill is enabled for this page load?
  bool has_logged_autofill_enabled_;
  // Have we logged an address suggestions count metric for this page?
  bool has_logged_address_suggestions_count_;
  // Have we shown Autofill suggestions at least once?
  bool did_show_suggestions_;
  // Has the user manually edited at least one form field among the autofillable
  // ones?
  bool user_did_type_;
  // Has the user autofilled a form on this page?
  bool user_did_autofill_;
  // Has the user edited a field that was previously autofilled?
  bool user_did_edit_autofilled_field_;
  // When the page finished loading.
  base::TimeTicks forms_loaded_timestamp_;
  // When the user first interacted with a potentially fillable form on this
  // page.
  base::TimeTicks initial_interaction_timestamp_;
  // If password generation is enabled. We cache this value so that we don't
  // spam the renderer with messages during startup when the sync state
  // is changing rapidly.
  bool password_generation_enabled_;
  // Listens for changes to the 'enabled' state for password generation.
  PrefChangeRegistrar registrar_;
  // Listens for TabContents destruction to avoid using pointer during
  // destruction.
  content::NotificationRegistrar notification_registrar_;

  // To be passed to the password generation UI to generate the password.
  scoped_ptr<autofill::PasswordGenerator> password_generator_;

  // Our copy of the form data.
  ScopedVector<FormStructure> form_structures_;

  // GUID to ID mapping.  We keep two maps to convert back and forth.
  mutable std::map<GUIDPair, int> guid_id_map_;
  mutable std::map<int, GUIDPair> id_guid_map_;

  // Delegate to perform external processing (display, selection) on
  // our behalf.  Weak.
  AutofillExternalDelegate* external_delegate_;

  friend class AutofillManagerTest;
  friend class FormStructureBrowserTest;
  FRIEND_TEST_ALL_PREFIXES(AutofillManagerTest,
                           DeterminePossibleFieldTypesForUpload);
  FRIEND_TEST_ALL_PREFIXES(AutofillManagerTest,
                           DeterminePossibleFieldTypesForUploadStressTest);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest, AddressSuggestionsCount);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest, AutofillIsEnabledAtPageLoad);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest,
                           NoQualityMetricsForNonAutofillableForms);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest, QualityMetrics);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest, QualityMetricsForFailure);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest, QualityMetricsWithExperimentId);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest, SaneMetricsWithCacheMismatch);
  FRIEND_TEST_ALL_PREFIXES(AutofillManagerTest, TestExternalDelegate);
  FRIEND_TEST_ALL_PREFIXES(AutofillManagerTest,
                           TestTabContentsWithExternalDelegate);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest,
                           UserHappinessFormLoadAndSubmission);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest, UserHappinessFormInteraction);
  FRIEND_TEST_ALL_PREFIXES(AutofillMetricsTest, FormFillDuration);

  DISALLOW_COPY_AND_ASSIGN(AutofillManager);
};

#endif  // CHROME_BROWSER_AUTOFILL_AUTOFILL_MANAGER_H_
