// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/create_application_shortcuts_dialog_gtk.h"

#include <string>

#include "base/bind.h"
#include "base/environment.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/shell_integration_linux.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/web_applications/web_app_ui.h"
#include "chrome/browser/ui/webui/extensions/extension_icon_source.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_resource.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/gtk_util.h"

using content::BrowserThread;
using extensions::Extension;

namespace {

// Size (in pixels) of the icon preview.
const int kIconPreviewSizePixels = 32;

// Height (in lines) of the shortcut description label.
const int kDescriptionLabelHeightLines = 3;

}  // namespace

namespace chrome {

void ShowCreateWebAppShortcutsDialog(gfx::NativeWindow parent_window,
                                     content::WebContents* web_contents) {
  new CreateWebApplicationShortcutsDialogGtk(parent_window, web_contents);
}

}  // namespace chrome

void CreateChromeApplicationShortcutsDialogGtk::Show(GtkWindow* parent,
                                                     Profile* profile,
                                                     const Extension* app) {
  new CreateChromeApplicationShortcutsDialogGtk(parent, profile, app);
}


CreateApplicationShortcutsDialogGtk::CreateApplicationShortcutsDialogGtk(
    GtkWindow* parent)
  : parent_(parent),
    desktop_checkbox_(NULL),
    menu_checkbox_(NULL),
    favicon_pixbuf_(NULL),
    create_dialog_(NULL),
    error_dialog_(NULL) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Will be balanced by Release later.
  AddRef();
}

void CreateApplicationShortcutsDialogGtk::CreateIconPixBuf(
    const gfx::Image& image) {
  // Prepare the icon. Try to scale it if it's too small, otherwise it would
  // look weird.
  GdkPixbuf* pixbuf =
      static_cast<GdkPixbuf*>(g_object_ref(image.ToGdkPixbuf()));
  int pixbuf_width = gdk_pixbuf_get_width(pixbuf);
  int pixbuf_height = gdk_pixbuf_get_height(pixbuf);
  if (pixbuf_width == pixbuf_height && pixbuf_width < kIconPreviewSizePixels) {
    // Only scale the pixbuf if it's a square (for simplicity).
    // Generally it should be square, if it's a favicon or app icon.
    // Use the highest quality interpolation. The scaling is
    // going to have low quality anyway, because the initial image
    // is likely small.
    favicon_pixbuf_ = gdk_pixbuf_scale_simple(pixbuf,
                                              kIconPreviewSizePixels,
                                              kIconPreviewSizePixels,
                                              GDK_INTERP_HYPER);
    g_object_unref(pixbuf);
  } else {
    favicon_pixbuf_ = pixbuf;
  }
}

void CreateApplicationShortcutsDialogGtk::CreateDialogBox(GtkWindow* parent) {
  // Build the dialog.
  create_dialog_ = gtk_dialog_new_with_buttons(
      l10n_util::GetStringUTF8(IDS_CREATE_SHORTCUTS_TITLE).c_str(),
      parent,
      (GtkDialogFlags) (GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR),
      NULL);
  gtk_widget_realize(create_dialog_);
  gtk_window_set_resizable(GTK_WINDOW(create_dialog_), false);
  gtk_util::AddButtonToDialog(create_dialog_,
      l10n_util::GetStringUTF8(IDS_CANCEL).c_str(),
      GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
  gtk_util::AddButtonToDialog(create_dialog_,
      l10n_util::GetStringUTF8(IDS_CREATE_SHORTCUTS_COMMIT).c_str(),
      GTK_STOCK_APPLY, GTK_RESPONSE_ACCEPT);

  GtkWidget* content_area =
      gtk_dialog_get_content_area(GTK_DIALOG(create_dialog_));
  gtk_box_set_spacing(GTK_BOX(content_area), ui::kContentAreaSpacing);

  GtkWidget* vbox = gtk_vbox_new(FALSE, ui::kControlSpacing);
  gtk_container_add(GTK_CONTAINER(content_area), vbox);

  // Create a box containing basic information about the new shortcut: an image
  // on the left, and a description on the right.
  GtkWidget* hbox = gtk_hbox_new(FALSE, ui::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(hbox),
                                 ui::kControlSpacing);

  // Put the icon preview in place.
  GtkWidget* favicon_image = gtk_image_new_from_pixbuf(favicon_pixbuf_);
  gtk_box_pack_start(GTK_BOX(hbox), favicon_image, FALSE, FALSE, 0);

  // Create the label with application shortcut description.
  GtkWidget* description_label = gtk_label_new(NULL);
  gtk_box_pack_start(GTK_BOX(hbox), description_label, FALSE, FALSE, 0);
  gtk_label_set_line_wrap(GTK_LABEL(description_label), TRUE);
  gtk_widget_realize(description_label);

  // Set the size request on the label so it knows where to line wrap. The width
  // is the desired size of the dialog less the space reserved for padding and
  // the image.
  int label_width;
  gtk_util::GetWidgetSizeFromResources(
      description_label,
      IDS_CREATE_SHORTCUTS_DIALOG_WIDTH_CHARS, -1, &label_width, NULL);
  label_width -= ui::kControlSpacing * 3 +
      gdk_pixbuf_get_width(favicon_pixbuf_);
  gtk_util::SetLabelWidth(description_label, label_width);

  std::string description(UTF16ToUTF8(shortcut_info_.description));
  std::string title(UTF16ToUTF8(shortcut_info_.title));
  gtk_label_set_text(GTK_LABEL(description_label),
                     (description.empty() ? title : description).c_str());

  // Label on top of the checkboxes.
  GtkWidget* checkboxes_label = gtk_label_new(
      l10n_util::GetStringUTF8(IDS_CREATE_SHORTCUTS_LABEL).c_str());
  gtk_misc_set_alignment(GTK_MISC(checkboxes_label), 0, 0);
  gtk_box_pack_start(GTK_BOX(vbox), checkboxes_label, FALSE, FALSE, 0);

  // Desktop checkbox.
  desktop_checkbox_ = gtk_check_button_new_with_label(
      l10n_util::GetStringUTF8(IDS_CREATE_SHORTCUTS_DESKTOP_CHKBOX).c_str());
  gtk_box_pack_start(GTK_BOX(vbox), desktop_checkbox_, FALSE, FALSE, 0);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(desktop_checkbox_), true);
  g_signal_connect(desktop_checkbox_, "toggled",
                   G_CALLBACK(OnToggleCheckboxThunk), this);

  // Menu checkbox.
  menu_checkbox_ = gtk_check_button_new_with_label(
      l10n_util::GetStringUTF8(IDS_CREATE_SHORTCUTS_MENU_CHKBOX).c_str());
  gtk_box_pack_start(GTK_BOX(vbox), menu_checkbox_, FALSE, FALSE, 0);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(menu_checkbox_), false);
  g_signal_connect(menu_checkbox_, "toggled",
                   G_CALLBACK(OnToggleCheckboxThunk), this);

  g_signal_connect(create_dialog_, "response",
                   G_CALLBACK(OnCreateDialogResponseThunk), this);
  gtk_widget_show_all(create_dialog_);
}

CreateApplicationShortcutsDialogGtk::~CreateApplicationShortcutsDialogGtk() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  gtk_widget_destroy(create_dialog_);

  if (error_dialog_)
    gtk_widget_destroy(error_dialog_);

  g_object_unref(favicon_pixbuf_);
}

void CreateApplicationShortcutsDialogGtk::OnCreateDialogResponse(
    GtkWidget* widget, int response) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (response == GTK_RESPONSE_ACCEPT) {
    shortcut_info_.create_on_desktop =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(desktop_checkbox_));
    shortcut_info_.create_in_applications_menu =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(menu_checkbox_));
    BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
        base::Bind(&CreateApplicationShortcutsDialogGtk::CreateDesktopShortcut,
                   this,
                   shortcut_info_));

    OnCreatedShortcut();
  } else {
    Release();
  }
}

void CreateApplicationShortcutsDialogGtk::OnErrorDialogResponse(
    GtkWidget* widget, int response) {
  Release();
}

void CreateApplicationShortcutsDialogGtk::CreateDesktopShortcut(
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  scoped_ptr<base::Environment> env(base::Environment::Create());

  std::string shortcut_template;
  if (ShellIntegrationLinux::GetDesktopShortcutTemplate(env.get(),
                                                        &shortcut_template)) {
    ShellIntegrationLinux::CreateDesktopShortcut(shortcut_info,
                                                 shortcut_template);
    Release();
  } else {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
        base::Bind(&CreateApplicationShortcutsDialogGtk::ShowErrorDialog,
                   this));
  }
}

void CreateApplicationShortcutsDialogGtk::ShowErrorDialog() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Hide the create dialog so that the user can no longer interact with it.
  gtk_widget_hide(create_dialog_);

  error_dialog_ = gtk_dialog_new_with_buttons(
      l10n_util::GetStringUTF8(IDS_CREATE_SHORTCUTS_ERROR_TITLE).c_str(),
      NULL,
      (GtkDialogFlags) (GTK_DIALOG_NO_SEPARATOR),
      GTK_STOCK_OK,
      GTK_RESPONSE_ACCEPT,
      NULL);
  gtk_widget_realize(error_dialog_);
  gtk_util::SetWindowSizeFromResources(
      GTK_WINDOW(error_dialog_),
      IDS_CREATE_SHORTCUTS_ERROR_DIALOG_WIDTH_CHARS,
      IDS_CREATE_SHORTCUTS_ERROR_DIALOG_HEIGHT_LINES,
      false);  // resizable
  GtkWidget* content_area =
      gtk_dialog_get_content_area(GTK_DIALOG(error_dialog_));
  gtk_box_set_spacing(GTK_BOX(content_area), ui::kContentAreaSpacing);

  GtkWidget* vbox = gtk_vbox_new(FALSE, ui::kControlSpacing);
  gtk_container_add(GTK_CONTAINER(content_area), vbox);

  // Label on top of the checkboxes.
  GtkWidget* description = gtk_label_new(
      l10n_util::GetStringUTF8(IDS_CREATE_SHORTCUTS_ERROR_LABEL).c_str());
  gtk_label_set_line_wrap(GTK_LABEL(description), TRUE);
  gtk_misc_set_alignment(GTK_MISC(description), 0, 0);
  gtk_box_pack_start(GTK_BOX(vbox), description, FALSE, FALSE, 0);

  g_signal_connect(error_dialog_, "response",
                   G_CALLBACK(OnErrorDialogResponseThunk), this);
  gtk_widget_show_all(error_dialog_);
}

void CreateApplicationShortcutsDialogGtk::OnToggleCheckbox(GtkWidget* sender) {
  gboolean can_accept = FALSE;

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(desktop_checkbox_)) ||
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(menu_checkbox_))) {
    can_accept = TRUE;
  }

  gtk_dialog_set_response_sensitive(GTK_DIALOG(create_dialog_),
                                    GTK_RESPONSE_ACCEPT,
                                    can_accept);
}

CreateWebApplicationShortcutsDialogGtk::CreateWebApplicationShortcutsDialogGtk(
    GtkWindow* parent,
    content::WebContents* web_contents)
  : CreateApplicationShortcutsDialogGtk(parent),
    web_contents_(web_contents) {

  // Get shortcut information now, it's needed for our UI.
  web_app::GetShortcutInfoForTab(web_contents, &shortcut_info_);
  CreateIconPixBuf(shortcut_info_.favicon);

  CreateDialogBox(parent);
}

void CreateWebApplicationShortcutsDialogGtk::OnCreatedShortcut() {
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents_);
  if (browser)
    chrome::ConvertTabToAppWindow(browser, web_contents_);
}

CreateChromeApplicationShortcutsDialogGtk::
    CreateChromeApplicationShortcutsDialogGtk(
        GtkWindow* parent,
        Profile* profile,
        const Extension* app)
      : CreateApplicationShortcutsDialogGtk(parent),
        app_(app),
        profile_path_(profile->GetPath()),
        ALLOW_THIS_IN_INITIALIZER_LIST(tracker_(this))  {

  // Get shortcut information now, it's needed for our UI.
  web_app::UpdateShortcutInfoForApp(*app, profile, &shortcut_info_);

  // Get the icon.
  const gfx::Size max_size(kIconPreviewSizePixels, kIconPreviewSizePixels);
  ExtensionResource icon_resource = app_->GetIconResource(
      kIconPreviewSizePixels, ExtensionIconSet::MATCH_BIGGER);

  // If no icon exists that is the desired size or larger, get the
  // largest icon available:
  if (icon_resource.empty())
    icon_resource = app_->GetIconResource(
        kIconPreviewSizePixels, ExtensionIconSet::MATCH_SMALLER);

  // Note that tracker_.LoadImage() can call OnImageLoaded() before it returns,
  // if the image is cached.  This is very rare.  Do not do anything after
  // calling LoadImage() that OnImageLoaded() depends on.
  tracker_.LoadImage(app_,
                     icon_resource,
                     max_size,
                     ImageLoadingTracker::DONT_CACHE);
}

// Called by tracker_ when the app's icon is loaded.
void CreateChromeApplicationShortcutsDialogGtk::OnImageLoaded(
    const gfx::Image& image,
    const std::string& extension_id,
    int index) {
  if (image.IsEmpty()) {
    shortcut_info_.favicon =
        ResourceBundle::GetSharedInstance().GetImageNamed(IDR_APP_DEFAULT_ICON);
  } else {
    shortcut_info_.favicon = image;
  }

  CreateIconPixBuf(shortcut_info_.favicon);
  CreateDialogBox(parent_);
}

void CreateChromeApplicationShortcutsDialogGtk::CreateDesktopShortcut(
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  if (web_app::CreateShortcutsOnFileThread(shortcut_info)) {
    Release();
  } else {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
        base::Bind(&CreateChromeApplicationShortcutsDialogGtk::ShowErrorDialog,
                   this));
  }
}
