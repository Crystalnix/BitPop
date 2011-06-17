// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_GTK_GTK_SIGNAL_H_
#define UI_BASE_GTK_GTK_SIGNAL_H_
#pragma once

typedef void* gpointer;
typedef struct _GtkWidget GtkWidget;

// At the time of writing this, there were two common ways of binding our C++
// code to the gobject C system. We either defined a whole bunch of "static
// MethodThunk()" which just called nonstatic Method()s on a class (which hurt
// readability of the headers and signal connection code) OR we declared
// "static Method()" and passed in the current object as the gpointer (and hurt
// readability in the implementation by having "context->" before every
// variable).

// The hopeful result of using these macros is that the code will be more
// readable and regular. There shouldn't be a bunch of static Thunks visible in
// the headers and the implementations shouldn't be filled with "context->"
// de-references.

#define CHROMEG_CALLBACK_0(CLASS, RETURN, METHOD, SENDER)           \
  static RETURN METHOD ## Thunk(SENDER sender, gpointer userdata) { \
    return reinterpret_cast<CLASS*>(userdata)->METHOD(sender);      \
  }                                                                 \
                                                                    \
  virtual RETURN METHOD(SENDER);

#define CHROMEG_CALLBACK_1(CLASS, RETURN, METHOD, SENDER, ARG1)     \
  static RETURN METHOD ## Thunk(SENDER sender, ARG1 one,            \
                                gpointer userdata) {                \
    return reinterpret_cast<CLASS*>(userdata)->METHOD(sender, one); \
  }                                                                 \
                                                                    \
  virtual RETURN METHOD(SENDER, ARG1);

#define CHROMEG_CALLBACK_2(CLASS, RETURN, METHOD, SENDER, ARG1, ARG2)    \
  static RETURN METHOD ## Thunk(SENDER sender, ARG1 one, ARG2 two,       \
                                gpointer userdata) {                     \
    return reinterpret_cast<CLASS*>(userdata)->METHOD(sender, one, two); \
  }                                                                      \
                                                                         \
  virtual RETURN METHOD(SENDER, ARG1, ARG2);

#define CHROMEG_CALLBACK_3(CLASS, RETURN, METHOD, SENDER, ARG1, ARG2, ARG3) \
  static RETURN METHOD ## Thunk(SENDER sender, ARG1 one, ARG2 two,          \
                                ARG3 three, gpointer userdata) {            \
    return reinterpret_cast<CLASS*>(userdata)->                             \
        METHOD(sender, one, two, three);                                    \
  }                                                                         \
                                                                            \
  virtual RETURN METHOD(SENDER, ARG1, ARG2, ARG3);

#define CHROMEG_CALLBACK_4(CLASS, RETURN, METHOD, SENDER, ARG1, ARG2, ARG3, \
                           ARG4)                                            \
  static RETURN METHOD ## Thunk(SENDER sender, ARG1 one, ARG2 two,          \
                                ARG3 three, ARG4 four,                      \
                                gpointer userdata) {                        \
    return reinterpret_cast<CLASS*>(userdata)->                             \
        METHOD(sender, one, two, three, four);                              \
  }                                                                         \
                                                                            \
  virtual RETURN METHOD(SENDER, ARG1, ARG2, ARG3, ARG4);

#define CHROMEG_CALLBACK_5(CLASS, RETURN, METHOD, SENDER, ARG1, ARG2, ARG3, \
                           ARG4, ARG5)                                      \
  static RETURN METHOD ## Thunk(SENDER sender, ARG1 one, ARG2 two,          \
                                ARG3 three, ARG4 four, ARG5 five,           \
                                gpointer userdata) {                        \
    return reinterpret_cast<CLASS*>(userdata)->                             \
        METHOD(sender, one, two, three, four, five);                        \
  }                                                                         \
                                                                            \
  virtual RETURN METHOD(SENDER, ARG1, ARG2, ARG3, ARG4, ARG5);

#define CHROMEG_CALLBACK_6(CLASS, RETURN, METHOD, SENDER, ARG1, ARG2, ARG3, \
                           ARG4, ARG5, ARG6)                                \
  static RETURN METHOD ## Thunk(SENDER sender, ARG1 one, ARG2 two,          \
                                ARG3 three, ARG4 four, ARG5 five,           \
                                ARG6 six, gpointer userdata) {              \
    return reinterpret_cast<CLASS*>(userdata)->                             \
        METHOD(sender, one, two, three, four, five, six);                   \
  }                                                                         \
                                                                            \
  virtual RETURN METHOD(SENDER, ARG1, ARG2, ARG3, ARG4, ARG5, ARG6);

// These macros handle the common case where the sender object will be a
// GtkWidget*.
#define CHROMEGTK_CALLBACK_0(CLASS, RETURN, METHOD) \
  CHROMEG_CALLBACK_0(CLASS, RETURN, METHOD, GtkWidget*);

#define CHROMEGTK_CALLBACK_1(CLASS, RETURN, METHOD, ARG1) \
  CHROMEG_CALLBACK_1(CLASS, RETURN, METHOD, GtkWidget*, ARG1);

#define CHROMEGTK_CALLBACK_2(CLASS, RETURN, METHOD, ARG1, ARG2) \
  CHROMEG_CALLBACK_2(CLASS, RETURN, METHOD, GtkWidget*, ARG1, ARG2);

#define CHROMEGTK_CALLBACK_3(CLASS, RETURN, METHOD, ARG1, ARG2, ARG3) \
  CHROMEG_CALLBACK_3(CLASS, RETURN, METHOD, GtkWidget*, ARG1, ARG2, ARG3);

#define CHROMEGTK_CALLBACK_4(CLASS, RETURN, METHOD, ARG1, ARG2, ARG3, ARG4) \
  CHROMEG_CALLBACK_4(CLASS, RETURN, METHOD, GtkWidget*, ARG1, ARG2, ARG3, ARG4);

#define CHROMEGTK_CALLBACK_5(CLASS, RETURN, METHOD, ARG1, ARG2, ARG3, ARG4, \
                             ARG5)                                          \
  CHROMEG_CALLBACK_5(CLASS, RETURN, METHOD, GtkWidget*, ARG1, ARG2, ARG3,   \
                     ARG4, ARG5);

#define CHROMEGTK_CALLBACK_6(CLASS, RETURN, METHOD, ARG1, ARG2, ARG3, ARG4, \
                             ARG5, ARG6)                                    \
  CHROMEG_CALLBACK_6(CLASS, RETURN, METHOD, GtkWidget*, ARG1, ARG2, ARG3,   \
                     ARG4, ARG5, ARG6);

#endif  // UI_BASE_GTK_GTK_SIGNAL_H_
