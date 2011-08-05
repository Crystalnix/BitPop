// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_AUTOFILL_METRICS_H_
#define CHROME_BROWSER_AUTOFILL_AUTOFILL_METRICS_H_
#pragma once

#include <stddef.h>
#include <string>

#include "base/basictypes.h"
#include "chrome/browser/autofill/field_types.h"

class AutofillMetrics {
 public:
  enum InfoBarMetric {
    INFOBAR_SHOWN = 0,  // We showed an infobar, e.g. prompting to save credit
                        // card info.
    INFOBAR_ACCEPTED,   // The user explicitly accepted the infobar.
    INFOBAR_DENIED,     // The user explicitly denied the infobar.
    INFOBAR_IGNORED,    // The user completely ignored the infobar (logged on
                        // tab close).
    NUM_INFO_BAR_METRICS
  };

  // Metrics measuring how well we predict field types.  Exactly three such
  // metrics are logged for each fillable field in a submitted form: for
  // the heuristic prediction, for the crowd-sourced prediction, and for the
  // overall prediction.
  enum FieldTypeQualityMetric {
    TYPE_UNKNOWN = 0,  // Offered no prediction.
    TYPE_MATCH,        // Predicted correctly.
    TYPE_MISMATCH,     // Predicted incorrectly.
    NUM_FIELD_TYPE_QUALITY_METRICS
  };

  enum QualityMetric {
    // Logged for each potentially fillable field in a submitted form.
    FIELD_SUBMITTED = 0,

    // A simple successs metric, logged for each field that returns true for
    // |is_autofilled()|.
    FIELD_AUTOFILLED,

    // A simple failure metric, logged for each field that returns false for
    // |is_autofilled()| but has a value that is present in the personal data
    // manager.
    FIELD_NOT_AUTOFILLED,

    // The below are only logged when |FIELD_AUTOFILL_FAILED| is also logged.
    NOT_AUTOFILLED_HEURISTIC_TYPE_UNKNOWN,
    NOT_AUTOFILLED_HEURISTIC_TYPE_MATCH,
    NOT_AUTOFILLED_HEURISTIC_TYPE_MISMATCH,
    NOT_AUTOFILLED_SERVER_TYPE_UNKNOWN,
    NOT_AUTOFILLED_SERVER_TYPE_MATCH,
    NOT_AUTOFILLED_SERVER_TYPE_MISMATCH,
    NUM_QUALITY_METRICS
  };

  // Each of these is logged at most once per query to the server, which in turn
  // occurs at most once per page load.
  enum ServerQueryMetric {
    QUERY_SENT = 0,           // Sent a query to the server.
    QUERY_RESPONSE_RECEIVED,  // Received a response.
    QUERY_RESPONSE_PARSED,    // Successfully parsed the server response.

    // The response was parseable, but provided no improvements relative to our
    // heuristics.
    QUERY_RESPONSE_MATCHED_LOCAL_HEURISTICS,

    // Our heuristics detected at least one auto-fillable field, and the server
    // response overrode the type of at least one field.
    QUERY_RESPONSE_OVERRODE_LOCAL_HEURISTICS,

    // Our heuristics did not detect any auto-fillable fields, but the server
    // response did detect at least one.
    QUERY_RESPONSE_WITH_NO_LOCAL_HEURISTICS,
    NUM_SERVER_QUERY_METRICS
  };

  AutofillMetrics();
  virtual ~AutofillMetrics();

  virtual void LogCreditCardInfoBarMetric(InfoBarMetric metric) const;

  virtual void LogHeuristicTypePrediction(
      FieldTypeQualityMetric metric,
      AutofillFieldType field_type,
      const std::string& experiment_id) const;
  virtual void LogOverallTypePrediction(
      FieldTypeQualityMetric metric,
      AutofillFieldType field_type,
      const std::string& experiment_id) const;
  virtual void LogServerTypePrediction(FieldTypeQualityMetric metric,
                                       AutofillFieldType field_type,
                                       const std::string& experiment_id) const;

  virtual void LogQualityMetric(QualityMetric metric,
                                const std::string& experiment_id) const;

  virtual void LogServerQueryMetric(ServerQueryMetric metric) const;


  // This should be called each time a page containing forms is loaded.
  virtual void LogIsAutofillEnabledAtPageLoad(bool enabled) const;

  // This should be called each time a new profile is launched.
  virtual void LogIsAutofillEnabledAtStartup(bool enabled) const;

  // This should be called each time a new profile is launched.
  virtual void LogStoredProfileCount(size_t num_profiles) const;

  // Log the number of Autofill suggestions presented to the user when filling a
  // form.
  virtual void LogAddressSuggestionsCount(size_t num_suggestions) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(AutofillMetrics);
};

#endif  // CHROME_BROWSER_AUTOFILL_AUTOFILL_METRICS_H_
