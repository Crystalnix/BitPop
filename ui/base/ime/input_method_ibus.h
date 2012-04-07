// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_INPUT_METHOD_IBUS_H_
#define UI_BASE_IME_INPUT_METHOD_IBUS_H_
#pragma once

#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "ui/base/glib/glib_integers.h"
#include "ui/base/glib/glib_signal.h"
#include "ui/base/ime/character_composer.h"
#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/ibus_client.h"
#include "ui/base/ime/input_method_base.h"

// Forward declarations, so that we don't need to include ibus.h in this file.
typedef struct _GAsyncResult GAsyncResult;
typedef struct _IBusBus IBusBus;
typedef struct _IBusInputContext IBusInputContext;
typedef struct _IBusText IBusText;

namespace ui {

// A ui::InputMethod implementation based on IBus.
class UI_EXPORT InputMethodIBus : public InputMethodBase {
 public:
  explicit InputMethodIBus(internal::InputMethodDelegate* delegate);
  virtual ~InputMethodIBus();

  // Overridden from InputMethod:
  virtual void OnFocus() OVERRIDE;
  virtual void OnBlur() OVERRIDE;
  virtual void Init(bool focused) OVERRIDE;
  virtual void DispatchKeyEvent(
      const base::NativeEvent& native_key_event) OVERRIDE;
  virtual void OnTextInputTypeChanged(const TextInputClient* client) OVERRIDE;
  virtual void OnCaretBoundsChanged(const TextInputClient* client) OVERRIDE;
  virtual void CancelComposition(const TextInputClient* client) OVERRIDE;
  virtual std::string GetInputLocale() OVERRIDE;
  virtual base::i18n::TextDirection GetInputTextDirection() OVERRIDE;
  virtual bool IsActive() OVERRIDE;

  // Sets |new_client| as a new IBusClient. InputMethodIBus owns the object.
  // A client has to be set before InputMethodIBus::Init() is called.
  void set_ibus_client(scoped_ptr<internal::IBusClient> new_client);

  // The caller is not allowed to delete the object.
  internal::IBusClient* ibus_client() const;

 protected:
  // Returns the global IBusBus instance. Protected: for testing.
  IBusBus* GetBus();

 private:
  class PendingKeyEventImpl;
  class PendingCreateICRequestImpl;

  // Overridden from InputMethodBase:
  virtual void OnWillChangeFocusedClient(TextInputClient* focused_before,
                                         TextInputClient* focused) OVERRIDE;
  virtual void OnDidChangeFocusedClient(TextInputClient* focused_before,
                                        TextInputClient* focused) OVERRIDE;

  // Creates |context_| instance asynchronously.
  void CreateContext();

  // Sets |context_| and hooks necessary signals.
  void SetContext(IBusInputContext* ic);

  // Destroys |context_| instance.
  void DestroyContext();

  // Asks the client to confirm current composition text.
  void ConfirmCompositionText();

  // Resets |context_| and abandon all pending results and key events.
  void ResetContext();

  // Checks the availability of focused text input client and update focus state
  // of |context_|.
  void UpdateContextFocusState();

  // Process a key returned from the input method.
  void ProcessKeyEventPostIME(const base::NativeEvent& native_key_event,
                              guint32 ibus_keycode,
                              bool handled);

  // Processes a key event that was already filtered by the input method.
  // A VKEY_PROCESSKEY may be dispatched to the focused View.
  void ProcessFilteredKeyPressEvent(const base::NativeEvent& native_key_event);

  // Processes a key event that was not filtered by the input method.
  void ProcessUnfilteredKeyPressEvent(const base::NativeEvent& native_key_event,
                                      guint32 ibus_keycode);
  void ProcessUnfilteredFabricatedKeyPressEvent(EventType type,
                                                KeyboardCode key_code,
                                                int flags,
                                                guint32 ibus_keyval);

  // Sends input method result caused by the given key event to the focused text
  // input client.
  void ProcessInputMethodResult(const base::NativeEvent& native_key_event,
                                bool filtered);

  // Checks if the pending input method result needs inserting into the focused
  // text input client as a single character.
  bool NeedInsertChar() const;

  // Checks if there is pending input method result.
  bool HasInputMethodResult() const;

  // Fabricates a key event with VKEY_PROCESSKEY key code and dispatches it to
  // the focused View.
  void SendFakeProcessKeyEvent(bool pressed) const;

  // Called when a pending key event has finished. The event will be removed
  // from |pending_key_events_|.
  void FinishPendingKeyEvent(PendingKeyEventImpl* pending_key);

  // Abandons all pending key events. It usually happends when we lose keyboard
  // focus, the text input type is changed or we are destroyed.
  void AbandonAllPendingKeyEvents();

  // Event handlers for IBusInputContext:
  CHROMEG_CALLBACK_1(InputMethodIBus, void, OnCommitText,
                     IBusInputContext*, IBusText*);
  CHROMEG_CALLBACK_3(InputMethodIBus, void, OnForwardKeyEvent,
                     IBusInputContext*, guint, guint, guint);
  CHROMEG_CALLBACK_0(InputMethodIBus, void, OnShowPreeditText,
                     IBusInputContext*);
  CHROMEG_CALLBACK_3(InputMethodIBus, void, OnUpdatePreeditText,
                     IBusInputContext*, IBusText*, guint, gboolean);
  CHROMEG_CALLBACK_0(InputMethodIBus, void, OnHidePreeditText,
                     IBusInputContext*);
  CHROMEG_CALLBACK_0(InputMethodIBus, void, OnDestroy, IBusInputContext*);

  // Event handlers for IBusBus:
  CHROMEG_CALLBACK_0(InputMethodIBus, void, OnIBusConnected, IBusBus*);
  CHROMEG_CALLBACK_0(InputMethodIBus, void, OnIBusDisconnected, IBusBus*);

  scoped_ptr<internal::IBusClient> ibus_client_;

  // The input context for actual text input. Note that we don't have to support
  // a "fake" IBus input context anymore since the latest Chrome for Chrome OS
  // can handle input method hot keys (e.g. Shift+Alt) by itself.
  IBusInputContext* context_;

  // All pending key events. Note: we do not own these object, we just save
  // pointers to these object so that we can abandon them when necessary.
  // They will be deleted in ProcessKeyEventDone().
  std::set<PendingKeyEventImpl*> pending_key_events_;

  // The pending request for creating the |context_| instance. We need to keep
  // this pointer so that we can receive or abandon the result.
  PendingCreateICRequestImpl* pending_create_ic_request_;

  // Pending composition text generated by the current pending key event.
  // It'll be sent to the focused text input client as soon as we receive the
  // processing result of the pending key event.
  CompositionText composition_;

  // Pending result text generated by the current pending key event.
  // It'll be sent to the focused text input client as soon as we receive the
  // processing result of the pending key event.
  string16 result_text_;

  // Indicates if |context_| is focused or not.
  bool context_focused_;

  // Indicates if there is an ongoing composition text.
  bool composing_text_;

  // Indicates if the composition text is changed or deleted.
  bool composition_changed_;

  // If it's true then all input method result received before the next key
  // event will be discarded.
  bool suppress_next_result_;

  // An object to compose a character from a sequence of key presses
  // including dead key etc.
  CharacterComposer character_composer_;

  DISALLOW_COPY_AND_ASSIGN(InputMethodIBus);
};

}  // namespace ui

#endif  // UI_BASE_IME_INPUT_METHOD_IBUS_H_
