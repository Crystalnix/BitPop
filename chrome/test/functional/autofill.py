#!/usr/bin/python
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os
import pickle
import re

import autofill_dataset_converter
import autofill_dataset_generator
import pyauto_functional  # Must be imported before pyauto
import pyauto


class AutofillTest(pyauto.PyUITest):
  """Tests that autofill works correctly"""

  def Debug(self):
    """Test method for experimentation.

    This method will not run automatically.
    """
    import pprint
    pp = pprint.PrettyPrinter(indent=2)
    while True:
      raw_input('Hit <enter> to dump info.. ')
      info = self.GetAutofillProfile()
      pp.pprint(info)

  def testFillProfile(self):
    """Test filling profiles and overwriting with new profiles."""
    profiles = [{'NAME_FIRST': 'Bob',
                 'NAME_LAST': 'Smith', 'ADDRESS_HOME_ZIP': '94043',},
                {'EMAIL_ADDRESS': 'sue@example.com',
                 'COMPANY_NAME': 'Company X',}]
    credit_cards = [{'CREDIT_CARD_NUMBER': '6011111111111117',
                     'CREDIT_CARD_EXP_MONTH': '12',
                     'CREDIT_CARD_EXP_4_DIGIT_YEAR': '2011'},
                    {'CREDIT_CARD_NAME': 'Bob C. Smith'}]

    self.FillAutofillProfile(profiles=profiles, credit_cards=credit_cards)
    profile = self.GetAutofillProfile()
    self.assertEqual(profiles, profile['profiles'])
    self.assertEqual(credit_cards, profile['credit_cards'])

    profiles = [ {'NAME_FIRST': 'Larry'}]
    self.FillAutofillProfile(profiles=profiles)
    profile = self.GetAutofillProfile()
    self.assertEqual(profiles, profile['profiles'])
    self.assertEqual(credit_cards, profile['credit_cards'])

  def testFillProfileCrazyCharacters(self):
    """Test filling profiles with unicode strings and crazy characters."""
    # Adding autofill profiles.
    file_path = os.path.join(self.DataDir(), 'autofill', 'functional',
                             'crazy_autofill.txt')
    profiles = self.EvalDataFrom(file_path)
    self.FillAutofillProfile(profiles=profiles)

    self.assertEqual(profiles, self.GetAutofillProfile()['profiles'],
                     msg='Autofill profile data does not match.')

    # Adding credit cards.
    file_path = os.path.join(self.DataDir(), 'autofill', 'functional',
                             'crazy_creditcards.txt')
    test_data = self.EvalDataFrom(file_path)
    credit_cards_input = test_data['input']
    self.FillAutofillProfile(credit_cards=credit_cards_input)

    self.assertEqual(test_data['expected'],
                     self.GetAutofillProfile()['credit_cards'],
                     msg='Autofill credit card data does not match.')

  def testSegmentNumbersNotSupportedInDOMUI(self):
    """Test segmented phone/fax numbers no longer supported in DOM UI.

    Phone and fax |COUNTRY_CODE| and |CITY_CODE| are only supported when
    aggregating a web form and no longer processed in parts through the DOM UI
    (when adding profiles through prefs settings).
    """
    profile_input = [{'NAME_FIRST': 'John',
                     'NAME_LAST': 'Doe',
                     'ADDRESS_HOME_LINE1': '123 H St.',
                     'ADDRESS_HOME_CITY': 'San Jose',
                     'ADDRESS_HOME_STATE': 'CA',
                     'ADDRESS_HOME_ZIP': '95110',
                     'ADDRESS_HOME_COUNTRY': 'China',
                     'PHONE_HOME_COUNTRY_CODE': '86',
                     'PHONE_HOME_CITY_CODE': '108',
                     'PHONE_HOME_NUMBER': '8828000',
                     'PHONE_FAX_COUNTRY_CODE': '86',
                     'PHONE_FAX_CITY_CODE': '108',
                     'PHONE_FAX_NUMBER': '8828000'}]

    profile_expected = [{'NAME_FIRST': 'John',
                        'NAME_LATE': 'Doe',
                        'ADDRESS_HOME_LINE1': '123 H St.',
                        'ADDRESS_HOME_CITY': 'San Jose',
                        'ADDRESS_HOME_STATE': 'CA',
                        'ADDRESS_HOME_ZIP': '95110',
                        'ADDRESS_HOME_COUNTRY': 'China'}]

    self.FillAutofillProfile(profiles=profile_input)
    self.assertEqual(profile_expected, self.GetAutofillProfile()['profiles'],
                     msg='Segmented phone/fax numbers supported in DOM UI.')

  def testGetProfilesEmpty(self):
    """Test getting profiles when none have been filled."""
    profile = self.GetAutofillProfile()
    self.assertEqual([], profile['profiles'])
    self.assertEqual([], profile['credit_cards'])

  def testAutofillInvalid(self):
    """Test filling in invalid values for profiles are saved as-is.

    Phone/Fax information entered into the prefs UI is not validated or rejected
    except for duplicates.
    """
    # First try profiles with invalid ZIP input.
    without_invalid = {'NAME_FIRST': u'Will',
                       'ADDRESS_HOME_CITY': 'Sunnyvale',
                       'ADDRESS_HOME_STATE': 'CA',
                       'ADDRESS_HOME_ZIP': 'my_zip',
                       'ADDRESS_HOME_COUNTRY': 'United States'}
    # Add invalid data for phone and fax fields.
    with_invalid = without_invalid.copy()
    with_invalid['PHONE_HOME_WHOLE_NUMBER'] = 'Invalid_Phone_Number'
    with_invalid['PHONE_FAX_WHOLE_NUMBER'] = 'Invalid_Fax_Number'
    self.FillAutofillProfile(profiles=[with_invalid])
    self.assertNotEqual(
        [without_invalid], self.GetAutofillProfile()['profiles'],
        msg='Phone/Fax data entered into prefs UI is validated.')

  def testAutofillPrefsStringSavedAsIs(self):
    """Test invalid credit card numbers typed in prefs should be saved as-is."""
    credit_card = {'CREDIT_CARD_NUMBER': 'Not_0123-5Checked'}
    self.FillAutofillProfile(credit_cards=[credit_card])
    self.assertEqual([credit_card],
                     self.GetAutofillProfile()['credit_cards'],
                     msg='Credit card number in prefs not saved as-is.')

  def _FillFormAndSubmit(self, dictionary, filename, tab_index=0, windex=0):
    """Navigate to the form, input values into the fields, and submit form.

    Args:
      dictionary: Dictionary key/value pairs for profiles or credit card values.
      filename: HTML form website file. The file is the basic file name and not
                the path to the file. File is assumed to be located in
                autofill/functional directory of the data folder.
      tab_index: Integer index of the tab to work on; defaults to 0 (first tab).
      windex: Integer index of the browser window to work on; defaults to 0
              (first window).
    """
    url = self.GetHttpURLForDataPath('autofill', 'functional', filename)
    self.NavigateToURL(url)
    for key, value in dictionary.iteritems():
      # Fill data into form field.
      script = """
        document.getElementById("%s").value = "%s";
        window.domAutomationController.send("done");
      """ % (key, value)
      self.ExecuteJavascript(script, tab_index, windex)
    # Submit form.
    js_code = """
      document.getElementById("testform").submit();
      window.addEventListener("unload", function() {
        window.domAutomationController.send("done");
      });
    """
    self.ExecuteJavascript(js_code, tab_index, windex)
    # Wait for Autofill to aggregate profile into preferences.
    self.WaitUntil(
        lambda: self.GetDOMValue('document.readyState'),
        expect_retval='complete')

  def _LuhnCreditCardNumberValidator(self, number):
    """Validates whether a number is valid or invalid using the Luhn test.

    Validation example:
      1. Example number: 49927398716
      2. Reverse the digits: 61789372994
      3. Sum the digits in the odd-numbered position for s1:
      6 + 7 + 9 + 7 + 9 + 4 = 42
      4. Take the digits in the even-numbered position: 1, 8, 3, 2, 9
        4.1. Two times each digit in the even-numbered position: 2, 16, 6, 4, 18
        4.2. For each resulting value that is now 2 digits, add the digits
        together: 2, 7, 6, 4, 9
        (0 + 2 = 2, 1 + 6 = 7, 0 + 6 = 6, 0 + 4 = 4, 1 + 8 = 9)
        4.3. Sum together the digits for s2: 2 + 7 + 6 + 4 + 9 = 28
      5. Sum together s1 + s2 and if the sum ends in zero, the number passes the
      Luhn test: 42 + 28 = 70 which is a valid credit card number.

    Args:
      number: the credit card number being validated, as a string.

    Return:
      boolean whether the credit card number is valid or not.
    """
    # Filters out non-digit characters.
    number = re.sub('[^0-9]', '', number)
    reverse = [int(ch) for ch in str(number)][::-1]
    # The divmod of the function splits a number into two digits, ready for
    # summing.
    return ((sum(reverse[0::2]) + sum(sum(divmod(d*2, 10))
                                      for d in reverse[1::2])) % 10 == 0)

  def testInvalidCreditCardNumberIsNotAggregated(self):
    """Test credit card info with an invalid number is not aggregated.

    When filling out a form with an invalid credit card number (one that
    does not pass the Luhn test) the credit card info should not be saved into
    Autofill preferences.
    """
    invalid_cc_info = {'CREDIT_CARD_NAME': 'Bob Smith',
                       'CREDIT_CARD_NUMBER': '4408 0412 3456 7890',
                       'CREDIT_CARD_EXP_MONTH': '12',
                       'CREDIT_CARD_EXP_4_DIGIT_YEAR': '2014'}

    cc_number = invalid_cc_info['CREDIT_CARD_NUMBER']
    self._FillFormAndSubmit(
        invalid_cc_info, 'autofill_creditcard_form.html', tab_index=0, windex=0)
    self.assertFalse(self._LuhnCreditCardNumberValidator(cc_number),
                     msg='This test requires an invalid credit card number.')
    cc_infobar = self.GetBrowserInfo()['windows'][0]['tabs'][0]['infobars']
    self.assertFalse(
        cc_infobar, msg='Save credit card infobar offered to save CC info.')

  def testWhitespacesAndSeparatorCharsStrippedForValidCCNums(self):
    """Test whitespaces and separator chars are stripped for valid CC numbers.

    The credit card numbers used in this test pass the Luhn test.
    For reference: http://www.merriampark.com/anatomycc.htm
    """
    credit_card_info = [{'CREDIT_CARD_NAME': 'Bob Smith',
                         'CREDIT_CARD_NUMBER': '4408 0412 3456 7893',
                         'CREDIT_CARD_EXP_MONTH': '12',
                         'CREDIT_CARD_EXP_4_DIGIT_YEAR': '2014'},
                        {'CREDIT_CARD_NAME': 'Jane Doe',
                         'CREDIT_CARD_NUMBER': '4417-1234-5678-9113',
                         'CREDIT_CARD_EXP_MONTH': '10',
                         'CREDIT_CARD_EXP_4_DIGIT_YEAR': '2013'}]

    url = self.GetHttpURLForDataPath(
        'autofill', 'functional', 'autofill_creditcard_form.html')
    for cc_info in credit_card_info:
      self.NavigateToURL(url)
      for key, value in cc_info.iteritems():
        cc_number = cc_info['CREDIT_CARD_NUMBER']
        self.assertTrue(self._LuhnCreditCardNumberValidator(cc_number),
                        msg='This test requires a valid credit card number.')
        script = ('document.getElementById("%s").value = "%s"; '
                  'window.domAutomationController.send("done");') % (key, value)
        self.ExecuteJavascript(script, 0, 0)
      js_code = """
        document.getElementById("testform").submit();
        window.addEventListener("unload", function() {
          window.domAutomationController.send("done");
        });
      """
      self.ExecuteJavascript(js_code, 0, 0)
      # Wait until form is submitted and page completes loading.
      self.WaitUntil(
          lambda: self.GetDOMValue('document.readyState'),
          expect_retval='complete')
      self.PerformActionOnInfobar('accept', infobar_index=0)

    # Verify the filled-in credit card number against the aggregated number.
    aggregated_cc_1 = (
        self.GetAutofillProfile()['credit_cards'][0]['CREDIT_CARD_NUMBER'])
    aggregated_cc_2 = (
        self.GetAutofillProfile()['credit_cards'][1]['CREDIT_CARD_NUMBER'])
    self.assertFalse((' ' in aggregated_cc_1 or ' ' in aggregated_cc_2 or
                      '-' in aggregated_cc_1 or '-' in aggregated_cc_2),
                     msg='Whitespaces or separator chars not stripped.')

  def testAggregatesMinValidProfile(self):
    """Test that Autofill aggregates a minimum valid profile.

    The minimum required address fields must be specified: First Name,
    Last Name, Address Line 1, City, Zip Code, and State.
    """
    profile = {'NAME_FIRST': 'Bob',
               'NAME_LAST': 'Smith',
               'ADDRESS_HOME_LINE1': '1234 H St.',
               'ADDRESS_HOME_CITY': 'Mountain View',
               'ADDRESS_HOME_STATE': 'CA',
               'ADDRESS_HOME_ZIP': '95110'}
    self._FillFormAndSubmit(
        profile, 'duplicate_profiles_test.html', tab_index=0, windex=0)
    self.assertTrue(self.GetAutofillProfile()['profiles'],
                    msg='Profile with minimum address values not aggregated.')

  def testProfilesNotAggregatedWithNoAddress(self):
    """Test Autofill does not aggregate profiles with no address info.

    The minimum required address fields must be specified: First Name,
    Last Name, Address Line 1, City, Zip Code, and State.
    """
    profile = {'NAME_FIRST': 'Bob',
               'NAME_LAST': 'Smith',
               'EMAIL_ADDRESS': 'bsmith@example.com',
               'COMPANY_NAME': 'Company X',
               'ADDRESS_HOME_CITY': 'Mountain View',
               'PHONE_HOME_WHOLE_NUMBER': '650-555-4567',}
    self._FillFormAndSubmit(
        profile, 'duplicate_profiles_test.html', tab_index=0, windex=0)
    self.assertFalse(self.GetAutofillProfile()['profiles'],
                     msg='Profile with no address info was aggregated.')

  def testProfilesNotAggregatedWithInvalidEmail(self):
    """Test Autofill does not aggregate profiles with an invalid email."""
    profile = {'NAME_FIRST': 'Bob',
               'NAME_LAST': 'Smith',
               'EMAIL_ADDRESS': 'garbage',
               'ADDRESS_HOME_LINE1': '1234 H St.',
               'ADDRESS_HOME_CITY': 'San Jose',
               'ADDRESS_HOME_STATE': 'CA',
               'ADDRESS_HOME_ZIP': '95110',
               'COMPANY_NAME': 'Company X',
               'PHONE_HOME_WHOLE_NUMBER': '408-871-4567',}
    self._FillFormAndSubmit(
        profile, 'duplicate_profiles_test.html', tab_index=0, windex=0)
    self.assertFalse(self.GetAutofillProfile()['profiles'],
                     msg='Profile with invalid email was aggregated.')

  def testComparePhoneNumbers(self):
    """Test phone fields parse correctly from a given profile.

    The high level key presses execute the following: Select the first text
    field, invoke the autofill popup list, select the first profile within the
    list, and commit to the profile to populate the form.
    """
    profile_path = os.path.join(self.DataDir(), 'autofill', 'functional',
                                'phone_pinput_autofill.txt')
    profile_expected_path = os.path.join(
        self.DataDir(), 'autofill', 'functional',
        'phone_pexpected_autofill.txt')
    profiles = self.EvalDataFrom(profile_path)
    profiles_expected = self.EvalDataFrom(profile_expected_path)
    self.FillAutofillProfile(profiles=profiles)
    url = self.GetHttpURLForDataPath(
        'autofill', 'functional', 'form_phones.html')
    for profile_expected in profiles_expected:
      self.NavigateToURL(url)
      self.assertTrue(self.AutofillPopulateForm('NAME_FIRST'),
                      msg='Autofill form could not be populated.')
      form_values = {}
      for key, value in profile_expected.iteritems():
        js_returning_field_value = (
            'var field_value = document.getElementById("%s").value;'
            'window.domAutomationController.send(field_value);'
            ) % key
        form_values[key] = self.ExecuteJavascript(
            js_returning_field_value, 0, 0)
        self.assertEqual(
            form_values[key], value,
            msg=('Original profile not equal to expected profile at key: "%s"\n'
                 'Expected: "%s"\nReturned: "%s"' % (
                     key, value, form_values[key])))

  def testCCInfoNotStoredWhenAutocompleteOff(self):
    """Test CC info not offered to be saved when autocomplete=off for CC field.

    If the credit card number field has autocomplete turned off, then the credit
    card infobar should not offer to save the credit card info. The credit card
    number must be a valid Luhn number.
    """
    credit_card_info = {'CREDIT_CARD_NAME': 'Bob Smith',
                        'CREDIT_CARD_NUMBER': '4408041234567893',
                        'CREDIT_CARD_EXP_MONTH': '12',
                        'CREDIT_CARD_EXP_4_DIGIT_YEAR': '2014'}

    self._FillFormAndSubmit(
        credit_card_info, 'cc_autocomplete_off_test.html',
        tab_index=0, windex=0)
    cc_infobar = self.GetBrowserInfo()['windows'][0]['tabs'][0]['infobars']
    self.assertFalse(cc_infobar,
                     msg='Save credit card infobar offered to save CC info.')

  def testNoAutofillForReadOnlyFields(self):
    """Test that Autofill does not fill in read-only fields."""
    profile = {'NAME_FIRST': 'Bob',
               'NAME_LAST': 'Smith',
               'EMAIL_ADDRESS': 'bsmith@gmail.com',
               'ADDRESS_HOME_LINE1': '1234 H St.',
               'ADDRESS_HOME_CITY': 'San Jose',
               'ADDRESS_HOME_STATE': 'CA',
               'ADDRESS_HOME_ZIP': '95110',
               'COMPANY_NAME': 'Company X',
               'PHONE_HOME_WHOLE_NUMBER': '408-871-4567',}

    self.FillAutofillProfile(profiles=[profile])
    url = self.GetHttpURLForDataPath(
        'autofill', 'functional', 'read_only_field_test.html')
    self.NavigateToURL(url)
    self.assertTrue(self.AutofillPopulateForm('firstname'),
                    msg='Autofill form could not be populated.')
    js_return_readonly_field = (
        'var field_value = document.getElementById("email").value;'
        'window.domAutomationController.send(field_value);')
    readonly_field_value = self.ExecuteJavascript(
        js_return_readonly_field, 0, 0)
    js_return_addrline1_field = (
        'var field_value = document.getElementById("address").value;'
        'window.domAutomationController.send(field_value);')
    addrline1_field_value = self.ExecuteJavascript(
        js_return_addrline1_field, 0, 0)
    self.assertNotEqual(
        readonly_field_value, profile['EMAIL_ADDRESS'],
        'Autofill filled in value "%s" for a read-only field.'
        % readonly_field_value)
    self.assertEqual(
        addrline1_field_value, profile['ADDRESS_HOME_LINE1'],
        'Unexpected value "%s" in the Address field.' % addrline1_field_value)

  def testFormFillableOnReset(self):
    """Test form is fillable from a profile after form was reset.

    Steps:
      1. Fill form using a saved profile.
      2. Reset the form.
      3. Fill form using a saved profile.
    """
    profile = {'NAME_FIRST': 'Bob',
               'NAME_LAST': 'Smith',
               'EMAIL_ADDRESS': 'bsmith@gmail.com',
               'ADDRESS_HOME_LINE1': '1234 H St.',
               'ADDRESS_HOME_CITY': 'San Jose',
               'PHONE_HOME_WHOLE_NUMBER': '4088714567',}

    self.FillAutofillProfile(profiles=[profile])
    url = self.GetHttpURLForDataPath(
        'autofill', 'functional', 'autofill_test_form.html')
    self.NavigateToURL(url)
    # Fill form using an address profile.
    self.assertTrue(self.AutofillPopulateForm('NAME_FIRST'),
                    msg='Autofill form could not be populated.')
    # Reset the form.
    self.ExecuteJavascript('document.getElementById("testform").reset();'
                           'window.domAutomationController.send("done");',
                           0, 0)
    # Fill in the form using an Autofill profile.
    self.assertTrue(self.AutofillPopulateForm('NAME_FIRST'),
                    msg='Autofill form could not be populated.')
    # Verify value in fields match value in the profile dictionary.
    form_values = {}
    for key, value in profile.iteritems():
      js_returning_field_value = (
          'var field_value = document.getElementById("%s").value;'
          'window.domAutomationController.send(field_value);'
          ) % key
      form_values[key] = self.ExecuteJavascript(
          js_returning_field_value, 0, 0)
      self.assertEqual(
          form_values[key], value,
          msg=('Original profile not equal to expected profile at key: "%s"\n'
               'Expected: "%s"\nReturned: "%s"' % (
                   key, value, form_values[key])))

  def testDistinguishMiddleInitialWithinName(self):
    """Test Autofill distinguishes a middle initial in a name."""
    profile = {'NAME_FIRST': 'Bob',
               'NAME_MIDDLE': 'Leo',
               'NAME_LAST': 'Smith',
               'EMAIL_ADDRESS': 'bsmith@gmail.com',
               'ADDRESS_HOME_LINE1': '1234 H St.',
               'ADDRESS_HOME_CITY': 'San Jose',
               'PHONE_HOME_WHOLE_NUMBER': '4088714567',}

    middle_initial = profile['NAME_MIDDLE'][0]
    self.FillAutofillProfile(profiles=[profile])
    url = self.GetHttpURLForDataPath(
        'autofill', 'functional', 'autofill_middleinit_form.html')
    self.NavigateToURL(url)
    # Fill form using an address profile.
    self.assertTrue(self.AutofillPopulateForm('NAME_FIRST'),
                    msg='Autofill form could not be populated.')
    js_return_middleinit_field = (
        'var field_value = document.getElementById("NAME_MIDDLE").value;'
        'window.domAutomationController.send(field_value);')
    middleinit_field_value = self.ExecuteJavascript(
        js_return_middleinit_field, 0, 0)
    self.assertEqual(middleinit_field_value, middle_initial,
                     msg=('Middle initial "%s" not distinguished from "%s".' %
                          (middleinit_field_value, profile['NAME_MIDDLE'])))

  def testMultipleEmailFilledByOneUserGesture(self):
    """Test forms with multiple email addresses are filled properly.

    Entire form should be filled with one user gesture.
    """
    profile = {'NAME_FIRST': 'Bob',
               'NAME_LAST': 'Smith',
               'EMAIL_ADDRESS': 'bsmith@gmail.com',
               'PHONE_HOME_WHOLE_NUMBER': '4088714567',}

    self.FillAutofillProfile(profiles=[profile])
    url = self.GetHttpURLForDataPath(
        'autofill', 'functional', 'autofill_confirmemail_form.html')
    self.NavigateToURL(url)
    # Fill form using an address profile.
    self.assertTrue(self.AutofillPopulateForm('NAME_FIRST'),
                    msg='Autofill form could not be populated.')
    js_return_confirmemail_field = (
        'var field_value = document.getElementById("EMAIL_CONFIRM").value;'
        'window.domAutomationController.send(field_value);')
    confirmemail_field_value = self.ExecuteJavascript(
        js_return_confirmemail_field, 0, 0)
    self.assertEqual(confirmemail_field_value, profile['EMAIL_ADDRESS'],
                     msg=('Confirmation Email address "%s" not equal to Email\n'
                          'address "%s".' % (confirmemail_field_value,
                                             profile['EMAIL_ADDRESS'])))

  def testProfileWithEmailInOtherFieldNotSaved(self):
    """Test profile not aggregated if email found in non-email field."""
    profile = {'NAME_FIRST': 'Bob',
               'NAME_LAST': 'Smith',
               'ADDRESS_HOME_LINE1': 'bsmith@gmail.com',
               'ADDRESS_HOME_CITY': 'San Jose',
               'ADDRESS_HOME_STATE': 'CA',
               'ADDRESS_HOME_ZIP': '95110',
               'COMPANY_NAME': 'Company X',
               'PHONE_HOME_WHOLE_NUMBER': '408-871-4567',}
    self._FillFormAndSubmit(
        profile, 'duplicate_profiles_test.html', tab_index=0, windex=0)
    self.assertFalse(self.GetAutofillProfile()['profiles'],
                     msg='Profile with email in a non-email field was '
                         'aggregated.')

  def FormFillLatencyAfterSubmit(self):
    """Test latency time on form submit with lots of stored Autofill profiles.

    This test verifies when a profile is selected from the Autofill dictionary
    that consists of thousands of profiles, the form does not hang after being
    submitted.

    The high level key presses execute the following: Select the first text
    field, invoke the autofill popup list, select the first profile within the
    list, and commit to the profile to populate the form.

    This test is partially automated. The bulk of the work is done, such as
    generating 1500 plus profiles, inserting those profiles into Autofill,
    selecting a profile from the list. The tester will need to click on the
    submit button and check if the browser hangs.
    """
    # HTML file needs to be run from a http:// url.
    url = self.GetHttpURLForDataPath(
        'autofill', 'functional', 'latency_after_submit_test.html')
    # Run the generator script to generate the dictionary list needed for the
    # profiles.
    gen = autofill_dataset_generator.DatasetGenerator(
        logging_level=logging.ERROR)
    list_of_dict = gen.GenerateDataset(num_of_dict_to_generate=1501)
    self.FillAutofillProfile(profiles=list_of_dict)
    self.NavigateToURL(url)
    self.assertTrue(self.AutofillPopulateForm('NAME_FIRST'),
                    msg='Autofill form could not be populated.')
    # TODO(dyu): add automated form hang or crash verification.
    raw_input(
        'Verify the test manually. Test hang time after submitting the form.')


  def AutofillCrowdsourcing(self):
    """Test able to send POST request of web form to Autofill server.

    The Autofill server processes the data offline, so it can take a few days
    for the result to be detectable. Manual verification is required.
    """
    # HTML file needs to be run from a specific http:// url to be able to verify
    # the results a few days later by visiting the same url.
    url = 'http://www.corp.google.com/~dyu/autofill/crowdsourcing-test.html'
    # Adding crowdsourcing Autofill profile.
    file_path = os.path.join(self.DataDir(), 'autofill', 'functional',
                             'crowdsource_autofill.txt')
    profiles = self.EvalDataFrom(file_path)
    self.FillAutofillProfile(profiles=profiles)
    # Autofill server captures 2.5% of the data posted.
    # Looping 1000 times is a safe minimum to exceed the server's threshold or
    # noise.
    for i in range(1000):
      fname = self.GetAutofillProfile()['profiles'][0]['NAME_FIRST']
      lname = self.GetAutofillProfile()['profiles'][0]['NAME_LAST']
      email = self.GetAutofillProfile()['profiles'][0]['EMAIL_ADDRESS']
      # Submit form to collect crowdsourcing data for Autofill.
      self.NavigateToURL(url, 0, 0)
      fname_field = ('document.getElementById("fn").value = "%s"; '
                     'window.domAutomationController.send("done");') % fname
      lname_field = ('document.getElementById("ln").value = "%s"; '
                     'window.domAutomationController.send("done");') % lname
      email_field = ('document.getElementById("em").value = "%s"; '
                     'window.domAutomationController.send("done");') % email
      self.ExecuteJavascript(fname_field, 0, 0);
      self.ExecuteJavascript(lname_field, 0, 0);
      self.ExecuteJavascript(email_field, 0, 0);
      self.ExecuteJavascript('document.getElementById("testform").submit();'
                             'window.domAutomationController.send("done");',
                             0, 0)

  def testMergeAggregatedProfilesWithSameAddress(self):
    """Test that profiles merge for aggregated data with same address.

    The criterion for when two profiles are expected to be merged is when their
    'Address Line 1' and 'City' data match. When two profiles are merged, any
    remaining address fields are expected to be overwritten. Any non-address
    fields should accumulate multi-valued data.
    """
    self._AggregateProfilesIntoAutofillPrefs('dataset_2.txt')
    # Expecting 4 profiles out of the original 14 within Autofill preferences.
    self.assertEqual(4, len(self.GetAutofillProfile()['profiles']),
                     msg='Aggregated profiles did not merge correctly.')

  def testProfilesNotMergedWhenNoMinAddressData(self):
    """Test profiles are not merged without mininum address values.

    Mininum address values needed during aggregation are: address line 1, city,
    state, and zip code.

    Profiles are merged when data for address line 1 and city match.
    """
    self._AggregateProfilesIntoAutofillPrefs('dataset_no_address.txt')
    self.assertFalse(self.GetAutofillProfile()['profiles'],
                     msg='Profile with no min address data was merged.')

  def MergeAggregatedDuplicatedProfiles(self):
    """Test Autofill ability to merge duplicate profiles and throw away junk."""
    num_of_profiles = self._AggregateProfilesIntoAutofillPrefs('dataset.txt')
    # Verify total number of inputted profiles is greater than the final number
    # of profiles after merging.
    self.assertTrue(
        num_of_profiles > len(self.GetAutofillProfile()['profiles']))

  def _AggregateProfilesIntoAutofillPrefs(self, data):
    """Aggregate profiles from forms into Autofill preferences.

    Args:
      data: Name of the data set file.

    Returns:
      Number of profiles in the dictionary list.
    """
    # HTML file needs to be run from a http:// url.
    url = self.GetHttpURLForDataPath(
        'autofill', 'functional', 'duplicate_profiles_test.html')
    # Run the parser script to generate the dictionary list needed for the
    # profiles.
    c = autofill_dataset_converter.DatasetConverter(
        os.path.abspath(
            os.path.join(self.DataDir(), 'autofill', 'functional', data)),
        logging_level=logging.INFO)  # Set verbosity to INFO, WARNING, ERROR.
    list_of_dict = c.Convert()

    for profile in list_of_dict:
      self.NavigateToURL(url)
      for key, value in profile.iteritems():
        script = ('document.getElementById("%s").value = "%s"; '
                  'window.domAutomationController.send("done");') % (key, value)
        self.ExecuteJavascript(script, 0, 0)
      self.ExecuteJavascript('document.getElementById("testform").submit();'
                             'window.domAutomationController.send("done");',
                             0, 0)
    return len(list_of_dict)


if __name__ == '__main__':
  pyauto_functional.Main()
