// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"

#include "base/logging.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/autocomplete_edit.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_popup_model.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/ui/views/autocomplete/autocomplete_popup_contents_view.h"
#include "chrome/browser/ui/views/autocomplete/touch_autocomplete_popup_contents_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/notification_service.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/font.h"
#include "views/border.h"
#include "views/controls/textfield/textfield.h"
#include "views/layout/fill_layout.h"

namespace {

// Textfield for autocomplete that intercepts events that are necessary
// for OmniboxViewViews.
class AutocompleteTextfield : public views::Textfield {
 public:
  explicit AutocompleteTextfield(OmniboxViewViews* omnibox_view)
      : views::Textfield(views::Textfield::STYLE_DEFAULT),
        omnibox_view_(omnibox_view) {
    DCHECK(omnibox_view_);
    RemoveBorder();
  }

  // views::View implementation
  virtual void OnFocus() OVERRIDE {
    views::Textfield::OnFocus();
    omnibox_view_->HandleFocusIn();
  }

  virtual void OnBlur() OVERRIDE {
    views::Textfield::OnBlur();
    omnibox_view_->HandleFocusOut();
  }

  virtual bool OnKeyPressed(const views::KeyEvent& event) OVERRIDE {
    bool handled = views::Textfield::OnKeyPressed(event);
    return omnibox_view_->HandleAfterKeyEvent(event, handled) || handled;
  }

  virtual bool OnKeyReleased(const views::KeyEvent& event) OVERRIDE {
    return omnibox_view_->HandleKeyReleaseEvent(event);
  }

  virtual bool IsFocusable() const OVERRIDE {
    // Bypass Textfield::IsFocusable. The omnibox in popup window requires
    // focus in order for text selection to work.
    return views::View::IsFocusable();
  }

 private:
  OmniboxViewViews* omnibox_view_;

  DISALLOW_COPY_AND_ASSIGN(AutocompleteTextfield);
};

// Stores omnibox state for each tab.
struct ViewState {
  explicit ViewState(const ui::Range& selection_range)
      : selection_range(selection_range) {
  }

  // Range of selected text.
  ui::Range selection_range;
};

struct AutocompleteEditState {
  AutocompleteEditState(const AutocompleteEditModel::State& model_state,
                        const ViewState& view_state)
      : model_state(model_state),
        view_state(view_state) {
  }

  const AutocompleteEditModel::State model_state;
  const ViewState view_state;
};

// Returns a lazily initialized property bag accessor for saving our state in a
// TabContents.
PropertyAccessor<AutocompleteEditState>* GetStateAccessor() {
  static PropertyAccessor<AutocompleteEditState> state;
  return &state;
}

const int kAutocompleteVerticalMargin = 4;

}  // namespace

OmniboxViewViews::OmniboxViewViews(AutocompleteEditController* controller,
                                   ToolbarModel* toolbar_model,
                                   Profile* profile,
                                   CommandUpdater* command_updater,
                                   bool popup_window_mode,
                                   const views::View* location_bar)
    : model_(new AutocompleteEditModel(this, controller, profile)),
      popup_view_(CreatePopupView(profile, location_bar)),
      controller_(controller),
      toolbar_model_(toolbar_model),
      command_updater_(command_updater),
      popup_window_mode_(popup_window_mode),
      security_level_(ToolbarModel::NONE),
      ime_composing_before_change_(false),
      delete_at_end_pressed_(false) {
  set_border(views::Border::CreateEmptyBorder(kAutocompleteVerticalMargin, 0,
                                              kAutocompleteVerticalMargin, 0));
}

OmniboxViewViews::~OmniboxViewViews() {
  NotificationService::current()->Notify(NotificationType::OMNIBOX_DESTROYED,
                                         Source<OmniboxViewViews>(this),
                                         NotificationService::NoDetails());
  // Explicitly teardown members which have a reference to us.  Just to be safe
  // we want them to be destroyed before destroying any other internal state.
  popup_view_.reset();
  model_.reset();
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxViewViews public:

void OmniboxViewViews::Init() {
  // The height of the text view is going to change based on the font used.  We
  // don't want to stretch the height, and we want it vertically centered.
  // TODO(oshima): make sure the above happens with views.
  textfield_ = new AutocompleteTextfield(this);
  textfield_->SetController(this);

#if defined(TOUCH_UI)
  textfield_->SetFont(ui::ResourceBundle::GetSharedInstance().GetFont(
                      ResourceBundle::LargeFont));
#endif

  if (popup_window_mode_)
    textfield_->SetReadOnly(true);

  // Manually invoke SetBaseColor() because TOOLKIT_VIEWS doesn't observe
  // themes.
  SetBaseColor();
}

void OmniboxViewViews::SetBaseColor() {
  // TODO(oshima): Implment style change.
  NOTIMPLEMENTED();
}

bool OmniboxViewViews::HandleAfterKeyEvent(const views::KeyEvent& event,
                                           bool handled) {
  if (event.key_code() == ui::VKEY_RETURN) {
    bool alt_held = event.IsAltDown();
    model_->AcceptInput(alt_held ? NEW_FOREGROUND_TAB : CURRENT_TAB, false);
    handled = true;
  } else if (!handled && event.key_code() == ui::VKEY_ESCAPE) {
    // We can handle the Escape key if textfield did not handle it.
    // If it's not handled by us, then we need to propagate it up to the parent
    // widgets, so that Escape accelerator can still work.
    handled = model_->OnEscapeKeyPressed();
  } else if (event.key_code() == ui::VKEY_CONTROL) {
    // Omnibox2 can switch its contents while pressing a control key. To switch
    // the contents of omnibox2, we notify the AutocompleteEditModel class when
    // the control-key state is changed.
    model_->OnControlKeyChanged(true);
  } else if (!handled && event.key_code() == ui::VKEY_DELETE &&
             event.IsShiftDown()) {
    // If shift+del didn't change the text, we let this delete an entry from
    // the popup.  We can't check to see if the IME handled it because even if
    // nothing is selected, the IME or the TextView still report handling it.
    if (model_->popup_model()->IsOpen())
      model_->popup_model()->TryDeletingCurrentItem();
  } else if (!handled && event.key_code() == ui::VKEY_UP) {
    model_->OnUpOrDownKeyPressed(-1);
    handled = true;
  } else if (!handled && event.key_code() == ui::VKEY_DOWN) {
    model_->OnUpOrDownKeyPressed(1);
    handled = true;
  } else if (!handled &&
             event.key_code() == ui::VKEY_TAB &&
             !event.IsShiftDown() &&
             !event.IsControlDown()) {
    if (model_->is_keyword_hint()) {
      handled = model_->AcceptKeyword();
    } else {
      string16::size_type start = 0;
      string16::size_type end = 0;
      size_t length = GetTextLength();
      GetSelectionBounds(&start, &end);
      if (start != end || start < length) {
        OnBeforePossibleChange();
        SelectRange(length, length);
        OnAfterPossibleChange();
        handled = true;
      }

      // TODO(Oshima): handle instant
    }
  }
  // TODO(oshima): page up & down

  return handled;
}

bool OmniboxViewViews::HandleKeyReleaseEvent(const views::KeyEvent& event) {
  // Omnibox2 can switch its contents while pressing a control key. To switch
  // the contents of omnibox2, we notify the AutocompleteEditModel class when
  // the control-key state is changed.
  if (event.key_code() == ui::VKEY_CONTROL) {
    // TODO(oshima): investigate if we need to support keyboard with two
    // controls. See omnibox_view_gtk.cc.
    model_->OnControlKeyChanged(false);
    return true;
  }
  return false;
}

void OmniboxViewViews::HandleFocusIn() {
  // TODO(oshima): Get control key state.
  model_->OnSetFocus(false);
  // Don't call controller_->OnSetFocus as this view has already
  // acquired the focus.
}

void OmniboxViewViews::HandleFocusOut() {
  // TODO(oshima): we don't have native view. This requires
  // further refactoring.
  model_->OnWillKillFocus(NULL);
  // Close the popup.
  ClosePopup();
  // Tell the model to reset itself.
  model_->OnKillFocus();
  controller_->OnKillFocus();
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxViewViews, views::View implementation:
void OmniboxViewViews::Layout() {
  gfx::Insets insets = GetInsets();
  textfield_->SetBounds(insets.left(), insets.top(),
                        width() - insets.width(),
                        height() - insets.height());
}

void OmniboxViewViews::GetAccessibleState(ui::AccessibleViewState* state) {
  state->name = l10n_util::GetStringUTF16(IDS_ACCNAME_LOCATION);
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxViewViews, AutocopmleteEditView implementation:

AutocompleteEditModel* OmniboxViewViews::model() {
  return model_.get();
}

const AutocompleteEditModel* OmniboxViewViews::model() const {
  return model_.get();
}

void OmniboxViewViews::SaveStateToTab(TabContents* tab) {
  DCHECK(tab);

  // NOTE: GetStateForTabSwitch may affect GetSelection, so order is important.
  AutocompleteEditModel::State model_state = model_->GetStateForTabSwitch();
  ui::Range selection;
  textfield_->GetSelectedRange(&selection);
  GetStateAccessor()->SetProperty(
      tab->property_bag(),
      AutocompleteEditState(model_state, ViewState(selection)));
}

void OmniboxViewViews::Update(const TabContents* contents) {
  // NOTE: We're getting the URL text here from the ToolbarModel.
  bool visibly_changed_permanent_text =
      model_->UpdatePermanentText(WideToUTF16Hack(toolbar_model_->GetText()));

  ToolbarModel::SecurityLevel security_level =
        toolbar_model_->GetSecurityLevel();
  bool changed_security_level = (security_level != security_level_);
  security_level_ = security_level;

  // TODO(oshima): Copied from gtk implementation which is
  // slightly different from WIN impl. Find out the correct implementation
  // for views-implementation.
  if (contents) {
    RevertAll();
    const AutocompleteEditState* state =
        GetStateAccessor()->GetProperty(contents->property_bag());
    if (state) {
      model_->RestoreState(state->model_state);

      // Move the marks for the cursor and the other end of the selection to
      // the previously-saved offsets (but preserve PRIMARY).
      textfield_->SelectRange(state->view_state.selection_range);
    }
  } else if (visibly_changed_permanent_text) {
    RevertAll();
  } else if (changed_security_level) {
    EmphasizeURLComponents();
  }
}

void OmniboxViewViews::OpenMatch(const AutocompleteMatch& match,
                                 WindowOpenDisposition disposition,
                                 const GURL& alternate_nav_url,
                                 size_t selected_line,
                                 const string16& keyword) {
  if (!match.destination_url.is_valid())
    return;

  model_->OpenMatch(match, disposition, alternate_nav_url,
                    selected_line, keyword);
}

string16 OmniboxViewViews::GetText() const {
  // TODO(oshima): IME support
  return textfield_->text();
}

bool OmniboxViewViews::IsEditingOrEmpty() const {
  return model_->user_input_in_progress() || (GetTextLength() == 0);
}

int OmniboxViewViews::GetIcon() const {
  return IsEditingOrEmpty() ?
      AutocompleteMatch::TypeToIcon(model_->CurrentTextType()) :
      toolbar_model_->GetIcon();
}

void OmniboxViewViews::SetUserText(const string16& text) {
  SetUserText(text, text, true);
}

void OmniboxViewViews::SetUserText(const string16& text,
                                   const string16& display_text,
                                   bool update_popup) {
  model_->SetUserText(text);
  SetWindowTextAndCaretPos(display_text, display_text.length());
  if (update_popup)
    UpdatePopup();
  TextChanged();
}

void OmniboxViewViews::SetWindowTextAndCaretPos(const string16& text,
                                                size_t caret_pos) {
  const ui::Range range(caret_pos, caret_pos);
  SetTextAndSelectedRange(text, range);
}

void OmniboxViewViews::SetForcedQuery() {
  const string16 current_text(GetText());
  const size_t start = current_text.find_first_not_of(kWhitespaceUTF16);
  if (start == string16::npos || (current_text[start] != '?')) {
    SetUserText(ASCIIToUTF16("?"));
  } else {
    SelectRange(current_text.size(), start + 1);
  }
}

bool OmniboxViewViews::IsSelectAll() {
  // TODO(oshima): IME support.
  return textfield_->text() == textfield_->GetSelectedText();
}

bool OmniboxViewViews::DeleteAtEndPressed() {
  return delete_at_end_pressed_;
}

void OmniboxViewViews::GetSelectionBounds(string16::size_type* start,
                                          string16::size_type* end) {
  ui::Range range;
  textfield_->GetSelectedRange(&range);
  *start = static_cast<size_t>(range.end());
  *end = static_cast<size_t>(range.start());
}

void OmniboxViewViews::SelectAll(bool reversed) {
  if (reversed)
    SelectRange(GetTextLength(), 0);
  else
    SelectRange(0, GetTextLength());
}

void OmniboxViewViews::RevertAll() {
  ClosePopup();
  model_->Revert();
  TextChanged();
}

void OmniboxViewViews::UpdatePopup() {
  model_->SetInputInProgress(true);
  if (!model_->has_focus())
    return;

  // Don't inline autocomplete when the caret/selection isn't at the end of
  // the text, or in the middle of composition.
  ui::Range sel;
  textfield_->GetSelectedRange(&sel);
  bool no_inline_autocomplete =
      sel.GetMax() < GetTextLength() || textfield_->IsIMEComposing();

  model_->StartAutocomplete(!sel.is_empty(), no_inline_autocomplete);
}

void OmniboxViewViews::ClosePopup() {
  model_->StopAutocomplete();
}

void OmniboxViewViews::SetFocus() {
  // In views-implementation, the focus is on textfield rather than OmniboxView.
  textfield_->RequestFocus();
}

void OmniboxViewViews::OnTemporaryTextMaybeChanged(
    const string16& display_text,
    bool save_original_selection) {
  if (save_original_selection)
    textfield_->GetSelectedRange(&saved_temporary_selection_);

  SetWindowTextAndCaretPos(display_text, display_text.length());
  TextChanged();
}

bool OmniboxViewViews::OnInlineAutocompleteTextMaybeChanged(
    const string16& display_text,
    size_t user_text_length) {
  if (display_text == GetText())
    return false;
  ui::Range range(display_text.size(), user_text_length);
  SetTextAndSelectedRange(display_text, range);
  TextChanged();
  return true;
}

void OmniboxViewViews::OnRevertTemporaryText() {
  textfield_->SelectRange(saved_temporary_selection_);
  TextChanged();
}

void OmniboxViewViews::OnBeforePossibleChange() {
  // Record our state.
  text_before_change_ = GetText();
  textfield_->GetSelectedRange(&sel_before_change_);
  ime_composing_before_change_ = textfield_->IsIMEComposing();
}

bool OmniboxViewViews::OnAfterPossibleChange() {
  ui::Range new_sel;
  textfield_->GetSelectedRange(&new_sel);

  // See if the text or selection have changed since OnBeforePossibleChange().
  const string16 new_text = GetText();
  const bool text_changed = (new_text != text_before_change_) ||
      (ime_composing_before_change_ != textfield_->IsIMEComposing());
  const bool selection_differs =
      !((sel_before_change_.is_empty() && new_sel.is_empty()) ||
        sel_before_change_.EqualsIgnoringDirection(new_sel));

  // When the user has deleted text, we don't allow inline autocomplete.  Make
  // sure to not flag cases like selecting part of the text and then pasting
  // (or typing) the prefix of that selection.  (We detect these by making
  // sure the caret, which should be after any insertion, hasn't moved
  // forward of the old selection start.)
  const bool just_deleted_text =
      (text_before_change_.length() > new_text.length()) &&
      (new_sel.start() <= sel_before_change_.GetMin());

  const bool something_changed = model_->OnAfterPossibleChange(
      new_text, new_sel.start(), new_sel.end(), selection_differs,
      text_changed, just_deleted_text, !textfield_->IsIMEComposing());

  // If only selection was changed, we don't need to call |model_|'s
  // OnChanged() method, which is called in TextChanged().
  // But we still need to call EmphasizeURLComponents() to make sure the text
  // attributes are updated correctly.
  if (something_changed && text_changed)
    TextChanged();
  else if (selection_differs)
    EmphasizeURLComponents();
  else if (delete_at_end_pressed_)
    model_->OnChanged();

  return something_changed;
}

gfx::NativeView OmniboxViewViews::GetNativeView() const {
  return GetWidget()->GetNativeView();
}

CommandUpdater* OmniboxViewViews::GetCommandUpdater() {
  return command_updater_;
}

void OmniboxViewViews::SetInstantSuggestion(const string16& input,
                                            bool animate_to_complete) {
  NOTIMPLEMENTED();
}

string16 OmniboxViewViews::GetInstantSuggestion() const {
  NOTIMPLEMENTED();
  return string16();
}

int OmniboxViewViews::TextWidth() const {
  // TODO(oshima): add horizontal margin.
  return textfield_->font().GetStringWidth(textfield_->text());
}

bool OmniboxViewViews::IsImeComposing() const {
  return false;
}

views::View* OmniboxViewViews::AddToView(views::View* parent) {
  parent->AddChildView(this);
  AddChildView(textfield_);
  return this;
}

int OmniboxViewViews::OnPerformDrop(const views::DropTargetEvent& event) {
  NOTIMPLEMENTED();
  return ui::DragDropTypes::DRAG_NONE;
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxViewViews, NotificationObserver implementation:

void OmniboxViewViews::Observe(NotificationType type,
                               const NotificationSource& source,
                               const NotificationDetails& details) {
  DCHECK(type == NotificationType::BROWSER_THEME_CHANGED);
  SetBaseColor();
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxViewViews, views::TextfieldController implementation:

void OmniboxViewViews::ContentsChanged(views::Textfield* sender,
                                       const string16& new_contents) {
}

bool OmniboxViewViews::HandleKeyEvent(views::Textfield* textfield,
                                      const views::KeyEvent& event) {
  delete_at_end_pressed_ = false;

  if (event.key_code() == ui::VKEY_BACK) {
    // Checks if it's currently in keyword search mode.
    if (model_->is_keyword_hint() || model_->keyword().empty())
      return false;
    // If there is selection, let textfield handle the backspace.
    if (textfield_->HasSelection())
      return false;
    // If not at the begining of the text, let textfield handle the backspace.
    if (textfield_->GetCursorPosition())
      return false;
    model_->ClearKeyword(GetText());
    return true;
  }

  if (event.key_code() == ui::VKEY_DELETE && !event.IsAltDown()) {
    delete_at_end_pressed_ =
        (!textfield_->HasSelection() &&
         textfield_->GetCursorPosition() == textfield_->text().length());
  }

  return false;
}

void OmniboxViewViews::OnBeforeUserAction(views::Textfield* sender) {
  OnBeforePossibleChange();
}

void OmniboxViewViews::OnAfterUserAction(views::Textfield* sender) {
  OnAfterPossibleChange();
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxViewViews, private:

size_t OmniboxViewViews::GetTextLength() const {
  // TODO(oshima): Support instant, IME.
  return textfield_->text().length();
}

void OmniboxViewViews::EmphasizeURLComponents() {
  // TODO(oshima): Update URL visual style
  NOTIMPLEMENTED();
}

void OmniboxViewViews::TextChanged() {
  EmphasizeURLComponents();
  model_->OnChanged();
}

void OmniboxViewViews::SetTextAndSelectedRange(const string16& text,
                                               const ui::Range& range) {
  if (text != GetText())
    textfield_->SetText(text);
  textfield_->SelectRange(range);
}

string16 OmniboxViewViews::GetSelectedText() const {
  // TODO(oshima): Support instant, IME.
  return textfield_->GetSelectedText();
}

void OmniboxViewViews::SelectRange(size_t caret, size_t end) {
  const ui::Range range(caret, end);
  textfield_->SelectRange(range);
}

AutocompletePopupView* OmniboxViewViews::CreatePopupView(
    Profile* profile,
    const View* location_bar) {
#if defined(TOUCH_UI)
  return new TouchAutocompletePopupContentsView(
#else
  return new AutocompletePopupContentsView(
#endif
      gfx::Font(), this, model_.get(), profile, location_bar);
}
