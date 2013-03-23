// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AUTOFILL_AUTOFILL_DIALOG_VIEWS_H_
#define CHROME_BROWSER_UI_VIEWS_AUTOFILL_AUTOFILL_DIALOG_VIEWS_H_

#include "chrome/browser/ui/autofill/autofill_dialog_controller.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/combobox/combobox_listener.h"
#include "ui/views/window/dialog_delegate.h"

class ConstrainedWindowViews;

namespace views {
class Checkbox;
class Textfield;
}

namespace ui {
class ComboboxModel;
}

namespace autofill {

struct DetailInput;

// Views toolkit implementation of the Autofill dialog that handles the
// imperative autocomplete API call.
class AutofillDialogViews : public AutofillDialogView,
                            public views::DialogDelegate,
                            public views::ButtonListener,
                            public views::ComboboxListener {
 public:
  explicit AutofillDialogViews(AutofillDialogController* controller);
  virtual ~AutofillDialogViews();

  // AutofillDialogView implementation:
  virtual void Show() OVERRIDE;
  virtual int GetSuggestionSelection(DialogSection section) OVERRIDE;
  virtual void GetUserInput(DialogSection section,
                            DetailOutputMap* output) OVERRIDE;
  virtual bool UseBillingForShipping() OVERRIDE;

  // views::DialogDelegate implementation:
  virtual string16 GetWindowTitle() const OVERRIDE;
  virtual void DeleteDelegate() OVERRIDE;
  virtual views::Widget* GetWidget() OVERRIDE;
  virtual const views::Widget* GetWidget() const OVERRIDE;
  virtual views::View* GetContentsView() OVERRIDE;
  virtual string16 GetDialogButtonLabel(ui::DialogButton button) const OVERRIDE;
  virtual bool IsDialogButtonEnabled(ui::DialogButton button) const OVERRIDE;
  virtual bool Cancel() OVERRIDE;
  virtual bool Accept() OVERRIDE;

  // views::ButtonListener implementation:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  // views::ComboboxListener implementation:
  virtual void OnSelectedIndexChanged(views::Combobox* combobox) OVERRIDE;

 private:
  typedef std::map<const DetailInput*, views::Textfield*> TextfieldMap;
  typedef std::map<const DetailInput*, views::Combobox*> ComboboxMap;

  // A convenience struct for holding pointers to views within each detail
  // section. None of the member pointers are owned.
  struct DetailsGroup {
    DetailsGroup();
    ~DetailsGroup();

    // The view that contains the entire section (label + input).
    views::View* container;
    // The combobox that holds suggested values.
    views::Combobox* suggested_input;
    // The view that allows manual input.
    views::View* manual_input;
    // The textfields in |manual_input|, tracked by their DetailInput.
    TextfieldMap textfields;
    // The comboboxes in |manual_input|, tracked by their DetailInput.
    ComboboxMap comboboxes;
  };

  void InitChildViews();

  // Creates and returns a view that holds all detail sections.
  views::View* CreateDetailsContainer();

  // Creates and returns a view that holds the requesting host and intro text.
  views::View* CreateIntroContainer();

  // Creates a detail section (Shipping, Email, etc.) with the given label,
  // inputs View, and suggestion model. Relevant pointers are stored in |group|.
  void CreateDetailsSection(DialogSection section);

  // Like CreateDetailsSection, but creates the combined billing/cc section,
  // which is somewhat more complicated than the others.
  void CreateBillingSection();

  // Creates the view that holds controls for inputing or selecting data for
  // a given section.
  views::View* CreateInputsContainer(DialogSection section);

  // Creates a grid of textfield views for the given section, and stores them
  // in the appropriate DetailsGroup. The top level View in the hierarchy is
  // returned.
  views::View* InitInputsView(DialogSection section);

  // Updates the visual state of the given group as per the model.
  void UpdateDetailsGroupState(const DetailsGroup& group);

  // Gets a pointer to the DetailsGroup that's associated with the given section
  // of the dialog.
  DetailsGroup* GroupForSection(DialogSection section);

  // The controller that drives this view. Weak pointer, always non-NULL.
  AutofillDialogController* const controller_;

  // True if the termination action was a submit.
  bool did_submit_;

  // The window that displays |contents_|. Weak pointer; may be NULL when the
  // dialog is closing.
  ConstrainedWindowViews* window_;

  // The top-level View for the dialog. Owned by the constrained window.
  views::View* contents_;

  DetailsGroup email_;
  // The credit card and billing sections are combined, so cc_.container is
  // not used.
  DetailsGroup cc_;
  DetailsGroup billing_;
  DetailsGroup shipping_;

  // The checkbox that controls whether to use the billing details for shipping
  // as well.
  views::Checkbox* use_billing_for_shipping_;

  DISALLOW_COPY_AND_ASSIGN(AutofillDialogViews);
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_VIEWS_AUTOFILL_AUTOFILL_DIALOG_VIEWS_H_
