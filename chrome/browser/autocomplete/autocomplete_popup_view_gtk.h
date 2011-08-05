// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_VIEW_GTK_H_
#define CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_VIEW_GTK_H_
#pragma once

#include <gtk/gtk.h>

#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_popup_view.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/gfx/font.h"
#include "webkit/glue/window_open_disposition.h"

class AutocompleteEditModel;
class AutocompletePopupModel;
class GtkThemeService;
class OmniboxView;
class Profile;
class SkBitmap;

class AutocompletePopupViewGtk : public AutocompletePopupView,
                                 public NotificationObserver {
 public:
  AutocompletePopupViewGtk(const gfx::Font& font,
                           OmniboxView* omnibox_view,
                           AutocompleteEditModel* edit_model,
                           Profile* profile,
                           GtkWidget* location_bar);
  virtual ~AutocompletePopupViewGtk();

  // Overridden from AutocompletePopupView:
  virtual bool IsOpen() const;
  virtual void InvalidateLine(size_t line);
  virtual void UpdatePopupAppearance();
  virtual gfx::Rect GetTargetBounds();
  virtual void PaintUpdatesNow();
  virtual void OnDragCanceled();

  // Overridden from NotificationObserver:
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  // Be friendly for unit tests.
  friend class AutocompletePopupViewGtkTest;
  static void SetupLayoutForMatch(
      PangoLayout* layout,
      const string16& text,
      const AutocompleteMatch::ACMatchClassifications& classifications,
      const GdkColor* base_color,
      const GdkColor* dim_color,
      const GdkColor* url_color,
      const std::string& prefix_text);

  void Show(size_t num_results);
  void Hide();

  // Restack the popup window directly above the browser's toplevel window.
  void StackWindow();

  // Convert a y-coordinate to the closest line / result.
  size_t LineFromY(int y);

  // Accept a line of the results, for example, when the user clicks a line.
  void AcceptLine(size_t line, WindowOpenDisposition disposition);

  GdkPixbuf* IconForMatch(const AutocompleteMatch& match, bool selected);

  CHROMEGTK_CALLBACK_1(AutocompletePopupViewGtk, gboolean, HandleMotion,
                       GdkEventMotion*);

  CHROMEGTK_CALLBACK_1(AutocompletePopupViewGtk, gboolean, HandleButtonPress,
                       GdkEventButton*);

  CHROMEGTK_CALLBACK_1(AutocompletePopupViewGtk, gboolean, HandleButtonRelease,
                       GdkEventButton*);

  CHROMEGTK_CALLBACK_1(AutocompletePopupViewGtk, gboolean, HandleExpose,
                       GdkEventExpose*);

  scoped_ptr<AutocompletePopupModel> model_;
  OmniboxView* omnibox_view_;
  GtkWidget* location_bar_;

  // Our popup window, which is the only widget used, and we paint it on our
  // own.  This widget shouldn't be exposed outside of this class.
  GtkWidget* window_;
  // The pango layout object created from the window, cached across exposes.
  PangoLayout* layout_;

  GtkThemeService* theme_service_;
  NotificationRegistrar registrar_;

  // Font used for suggestions after being derived from the constructor's
  // |font|.
  gfx::Font font_;

  // Used to cache GdkPixbufs and map them from the SkBitmaps they were created
  // from.
  typedef std::map<const SkBitmap*, GdkPixbuf*> PixbufMap;
  PixbufMap pixbufs_;

  // A list of colors which we should use for drawing the popup. These change
  // between gtk and normal mode.
  GdkColor border_color_;
  GdkColor background_color_;
  GdkColor selected_background_color_;
  GdkColor hovered_background_color_;
  GdkColor content_text_color_;
  GdkColor selected_content_text_color_;
  GdkColor content_dim_text_color_;
  GdkColor selected_content_dim_text_color_;
  GdkColor url_text_color_;
  GdkColor url_selected_text_color_;

  // If the user cancels a dragging action (i.e. by pressing ESC), we don't have
  // a convenient way to release mouse capture. Instead we use this flag to
  // simply ignore all remaining drag events, and the eventual mouse release
  // event. Since OnDragCanceled() can be called when we're not dragging, this
  // flag is reset to false on a mouse pressed event, to make sure we don't
  // erroneously ignore the next drag.
  bool ignore_mouse_drag_;

  // Whether our popup is currently open / shown, or closed / hidden.
  bool opened_;

  DISALLOW_COPY_AND_ASSIGN(AutocompletePopupViewGtk);
};

#endif  // CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_VIEW_GTK_H_
