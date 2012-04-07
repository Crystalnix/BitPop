// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_MANAGER_H_
#define CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_MANAGER_H_
#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/observer_list.h"
#include "base/time.h"
#include "base/timer.h"
#include "chrome/browser/chromeos/input_method/ibus_controller.h"
#include "chrome/browser/chromeos/input_method/input_method_util.h"

class GURL;

namespace chromeos {
namespace input_method {

class HotkeyManager;
class VirtualKeyboard;
class XKeyboard;

// This class manages input methodshandles.  Classes can add themselves as
// observers. Clients can get an instance of this library class by:
// InputMethodManager::GetInstance().
class InputMethodManager {
 public:
  class Observer {
   public:
    virtual ~Observer() {}

    // Called when the current input method is changed.
    virtual void InputMethodChanged(
        InputMethodManager* manager,
        const InputMethodDescriptor& current_input_method,
        size_t num_active_input_methods) = 0;

    // Called when the active input methods are changed.
    virtual void ActiveInputMethodsChanged(
        InputMethodManager* manager,
        const InputMethodDescriptor& current_input_method,
        size_t num_active_input_methods) = 0;

    // Called when the list of properties is changed.
    virtual void PropertyListChanged(
        InputMethodManager* manager,
        const ImePropertyList& current_ime_properties) = 0;
  };

  // CandidateWindowObserver is notified of events related to the candidate
  // window.  The "suggestion window" used by IMEs such as ibus-mozc does not
  // count as the candidate window (this may change if we later want suggestion
  // window events as well).  These events also won't occur when the virtual
  // keyboard is used, since it controls its own candidate window.
  class CandidateWindowObserver {
   public:
    virtual ~CandidateWindowObserver() {}

    // Called when the candidate window is opened.
    virtual void CandidateWindowOpened(InputMethodManager* manager) = 0;

    // Called when the candidate window is closed.
    virtual void CandidateWindowClosed(InputMethodManager* manager) = 0;
  };

  class PreferenceObserver {
   public:
    virtual ~PreferenceObserver() {}

    // Called when the preferences have to be updated.
    virtual void PreferenceUpdateNeeded(
        InputMethodManager* manager,
        const InputMethodDescriptor& previous_input_method,
        const InputMethodDescriptor& current_input_method) = 0;

    // Called by AddObserver() when the first observer is added.
    virtual void FirstObserverIsAdded(InputMethodManager* obj) = 0;
  };

  class VirtualKeyboardObserver {
   public:
    virtual ~VirtualKeyboardObserver() {}
    // Called when the current virtual keyboard is changed.
    virtual void VirtualKeyboardChanged(
        InputMethodManager* manager,
        const VirtualKeyboard& virtual_keyboard,
        const std::string& virtual_keyboard_layout) = 0;
  };

  virtual ~InputMethodManager() {}

  static InputMethodManager* GetInstance();

  // Adds an observer to receive notifications of input method related
  // changes as desribed in the Observer class above.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void AddCandidateWindowObserver(
      CandidateWindowObserver* observer) = 0;
  virtual void AddPreLoginPreferenceObserver(PreferenceObserver* observer) = 0;
  virtual void AddPostLoginPreferenceObserver(PreferenceObserver* observer) = 0;
  virtual void AddVirtualKeyboardObserver(
      VirtualKeyboardObserver* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
  virtual void RemoveCandidateWindowObserver(
      CandidateWindowObserver* observer) = 0;
  virtual void RemovePreLoginPreferenceObserver(
      PreferenceObserver* observer) = 0;
  virtual void RemovePostLoginPreferenceObserver(
      PreferenceObserver* observer) = 0;
  virtual void RemoveVirtualKeyboardObserver(
      VirtualKeyboardObserver* observer) = 0;

  // Returns all input methods that are supported, including ones not active.
  // Caller has to delete the returned list. This function never returns NULL.
  virtual InputMethodDescriptors* GetSupportedInputMethods() = 0;

  // Returns the list of input methods we can select (i.e. active). If the cros
  // library is not found or IBus/DBus daemon is not alive, this function
  // returns a fallback input method list (and never returns NULL).
  virtual InputMethodDescriptors* GetActiveInputMethods() = 0;

  // Returns the number of active input methods.
  virtual size_t GetNumActiveInputMethods() = 0;

  // Changes the current input method to |input_method_id|.
  virtual void ChangeInputMethod(const std::string& input_method_id) = 0;

  // Enables input methods (e.g. Chinese, Japanese) and keyboard layouts (e.g.
  // US qwerty, US dvorak, French azerty) that are necessary for the language
  // code and then switches to |initial_input_method_id| if the string is not
  // empty. For example, if |language_code| is "en-US", US qwerty and US dvorak
  // layouts would be enabled. Likewise, for Germany locale, US qwerty layout
  // and several keyboard layouts for Germany would be enabled.
  // If |type| is kAllInputMethods, all keyboard layouts and all input methods
  // are enabled. If it's kKeyboardLayoutsOnly, only keyboard layouts are
  // enabled. For example, for Japanese, xkb:jp::jpn is enabled when
  // kKeyboardLayoutsOnly, and xkb:jp::jpn, mozc, mozc-jp, mozc-dv are enabled
  // when kAllInputMethods.
  //
  // Note that this function does not save the input methods in the user's
  // preferences, as this function is designed for the login screen and the
  // screen locker, where we shouldn't change the user's preferences.
  virtual void EnableInputMethods(
      const std::string& language_code,
      InputMethodType type,
      const std::string& initial_input_method_id) = 0;

  // Sets whether the input method property specified by |key| is activated. If
  // |activated| is true, activates the property. If |activate| is false,
  // deactivates the property. Examples of keys:
  // - "InputMode.Katakana"
  // - "InputMode.HalfWidthKatakana"
  // - "TypingMode.Romaji"
  // - "TypingMode.Kana"
  virtual void SetImePropertyActivated(const std::string& key,
                                       bool activated) = 0;

  // Returns true if the input method specified by |input_method_id| is active.
  virtual bool InputMethodIsActivated(const std::string& input_method_id) = 0;

  // Updates a configuration of ibus-daemon or IBus engines with |value|.
  // Returns true if the configuration (and all pending configurations, if any)
  // are processed. If ibus-daemon is not running, this function just queues
  // the request and returns false.
  // When you would like to set 'panel/custom_font', |section| should
  // be "panel", and |config_name| should be "custom_font".
  // Notice: This function might call the Observer::ActiveInputMethodsChanged()
  // callback function immediately, before returning from the SetImeConfig
  // function. See also http://crosbug.com/5217.
  virtual bool SetImeConfig(const std::string& section,
                            const std::string& config_name,
                            const ImeConfigValue& value) = 0;

  // Add an input method to insert into the language menu.
  virtual void AddActiveIme(const std::string& id,
                            const std::string& name,
                            const std::vector<std::string>& layouts,
                            const std::string& language) = 0;

  // Remove an input method from the language menu.
  virtual void RemoveActiveIme(const std::string& id) = 0;

  virtual bool GetExtraDescriptor(const std::string& id,
                                  InputMethodDescriptor* descriptor) = 0;

  // Sets the IME state to enabled, and launches input method daemon if needed.
  // Returns true if the daemon is started. Otherwise, e.g. the daemon is
  // already started, returns false.
  virtual bool StartInputMethodDaemon() = 0;

  // Disables the IME, and kills the daemon process if they are running.
  // Returns true if the daemon is stopped. Otherwise, e.g. the daemon is
  // already stopped, returns false.
  virtual bool StopInputMethodDaemon() = 0;

  // Controls whether the IME process is started when preload engines are
  // specificed, or defered until a non-default method is activated.
  virtual void SetDeferImeStartup(bool defer) = 0;

  // Controls whether the IME process is stopped when all non-default preload
  // engines are removed.
  virtual void SetEnableAutoImeShutdown(bool enable) = 0;

  // Sends a handwriting stroke to libcros. See chromeos::SendHandwritingStroke
  // for details.
  virtual void SendHandwritingStroke(
      const HandwritingStroke& stroke) = 0;

  // Clears last N handwriting strokes in libcros. See
  // chromeos::CancelHandwriting for details.
  virtual void CancelHandwritingStrokes(int stroke_count) = 0;

  // Registers a new virtual keyboard for |layouts|. Set |is_system| true when
  // the keyboard is provided as a content extension. System virtual keyboards
  // have lower priority than non-system ones. See virtual_keyboard_selector.h
  // for details.
  // TODO(yusukes): Add UnregisterVirtualKeyboard function as well.
  virtual void RegisterVirtualKeyboard(const GURL& launch_url,
                                       const std::string& name,
                                       const std::set<std::string>& layouts,
                                       bool is_system) = 0;

  // Sets user preference on virtual keyboard selection.
  // See virtual_keyboard_selector.h for details.
  virtual bool SetVirtualKeyboardPreference(const std::string& input_method_id,
                                            const GURL& extention_url) = 0;

  // Clears all preferences on virtual keyboard selection.
  // See virtual_keyboard_selector.h for details.
  virtual void ClearAllVirtualKeyboardPreferences() = 0;

  // Returns a map from extension URL to virtual keyboard extension.
  virtual const std::map<GURL, const VirtualKeyboard*>&
  GetUrlToKeyboardMapping() const = 0;

  // Returns a multi map from layout name to virtual keyboard extension.
  virtual const std::multimap<std::string, const VirtualKeyboard*>&
  GetLayoutNameToKeyboardMapping() const = 0;

  // Returns an X keyboard object which could be used to change the current XKB
  // layout, change the caps lock status, and set the auto repeat rate/interval.
  virtual XKeyboard* GetXKeyboard() = 0;

  // Returns a InputMethodUtil object.
  virtual InputMethodUtil* GetInputMethodUtil() = 0;

  // Returns a hotkey manager object which could be used to detect Control+space
  // and Shift+Alt key presses.
  virtual HotkeyManager* GetHotkeyManager() = 0;
  // Register all global input method hotkeys: Control+space and Shift+Alt.
  virtual void AddHotkeys() = 0;
  // Removes all global input method hotkeys.
  virtual void RemoveHotkeys() = 0;

  // Switches the current input method (or keyboard layout) to the next one.
  virtual void SwitchToNextInputMethod() = 0;

  virtual InputMethodDescriptor previous_input_method() const = 0;
  virtual InputMethodDescriptor current_input_method() const = 0;

  virtual const ImePropertyList& current_ime_properties() const = 0;
};

}  // namespace input_method
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_MANAGER_H_
