// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/website_settings_popup_gtk.h"

#include "base/i18n/rtl.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/certificate_viewer.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/gtk/browser_toolbar_gtk.h"
#include "chrome/browser/ui/gtk/browser_window_gtk.h"
#include "chrome/browser/ui/gtk/collected_cookies_gtk.h"
#include "chrome/browser/ui/gtk/gtk_chrome_link_button.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/location_bar_view_gtk.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/website_settings/website_settings.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/cert_store.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

using content::OpenURLParams;

namespace {

// The background color of the tabs if a theme other than the native GTK theme
// is selected.
const GdkColor kBackgroundColor = GDK_COLOR_RGB(0xff, 0xff, 0xff);

}  // namespace

// static
void WebsiteSettingsPopupGtk::Show(gfx::NativeWindow parent,
                                   Profile* profile,
                                   TabContents* tab_contents,
                                   const GURL& url,
                                   const content::SSLStatus& ssl) {
  new WebsiteSettingsPopupGtk(parent, profile, tab_contents, url, ssl);
}

WebsiteSettingsPopupGtk::WebsiteSettingsPopupGtk(
    gfx::NativeWindow parent,
    Profile* profile,
    TabContents* tab_contents,
    const GURL& url,
    const content::SSLStatus& ssl)
    : parent_(parent),
      contents_(NULL),
      theme_service_(GtkThemeService::GetFrom(profile)),
      profile_(profile),
      tab_contents_(tab_contents),
      browser_(NULL),
      cert_id_(0),
      header_box_(NULL),
      cookies_section_contents_(NULL),
      permissions_section_contents_(NULL),
      identity_tab_contents_(NULL),
      first_visit_contents_(NULL),
      presenter_(NULL) {
  BrowserWindowGtk* browser_window =
      BrowserWindowGtk::GetBrowserWindowForNativeWindow(parent);
  browser_ = browser_window->browser();
  anchor_ = browser_window->
      GetToolbar()->GetLocationBarView()->location_icon_widget();

  InitContents();

  BubbleGtk::ArrowLocationGtk arrow_location = base::i18n::IsRTL() ?
      BubbleGtk::ARROW_LOCATION_TOP_RIGHT :
      BubbleGtk::ARROW_LOCATION_TOP_LEFT;
  bubble_ = BubbleGtk::Show(anchor_,
                            NULL,  // |rect|
                            contents_,
                            arrow_location,
                            BubbleGtk::MATCH_SYSTEM_THEME |
                                BubbleGtk::POPUP_WINDOW |
                                BubbleGtk::GRAB_INPUT,
                            theme_service_,
                            this);  // |delegate|
  if (!bubble_) {
    NOTREACHED();
    return;
  }

  presenter_.reset(new WebsiteSettings(this, profile,
                                       tab_contents->content_settings(),
                                       tab_contents->infobar_tab_helper(),
                                       url, ssl,
                                       content::CertStore::GetInstance()));
}

WebsiteSettingsPopupGtk::~WebsiteSettingsPopupGtk() {
}

void WebsiteSettingsPopupGtk::BubbleClosing(BubbleGtk* bubble,
                                            bool closed_by_escape) {
  if (presenter_.get())
    presenter_.reset();
  delete this;
}

void WebsiteSettingsPopupGtk::InitContents() {
  if (!contents_) {
    contents_ = gtk_vbox_new(FALSE, ui::kContentAreaSpacing);
    gtk_container_set_border_width(GTK_CONTAINER(contents_),
                                   ui::kContentAreaBorder);
  } else {
    gtk_util::RemoveAllChildren(contents_);
  }

  // Create popup header.
  header_box_ = gtk_vbox_new(FALSE, ui::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(contents_), header_box_, FALSE, FALSE, 0);

  // Create the container for the contents of the permissions tab.
  GtkWidget* permission_tab_contents = gtk_vbox_new(FALSE, ui::kControlSpacing);
  gtk_container_set_border_width(GTK_CONTAINER(permission_tab_contents), 10);
  cookies_section_contents_ = gtk_vbox_new(FALSE, ui::kControlSpacing);
  std::string title = l10n_util::GetStringUTF8(
      IDS_WEBSITE_SETTINGS_TITLE_SITE_DATA);
  gtk_box_pack_start(GTK_BOX(permission_tab_contents),
                     CreateSection(title,
                                   cookies_section_contents_),
                     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(permission_tab_contents),
                     gtk_hseparator_new(),
                     FALSE, FALSE, 0);
  permissions_section_contents_ = gtk_vbox_new(FALSE, ui::kControlSpacing);
  title = l10n_util::GetStringUTF8(IDS_WEBSITE_SETTINGS_TITLE_SITE_PERMISSIONS);
  gtk_box_pack_start(GTK_BOX(permission_tab_contents),
                     CreateSection(title,
                                   permissions_section_contents_),
                     FALSE, FALSE, 0);

  // Create the container for the contents of the identity tab.
  GtkWidget* info_tab = gtk_vbox_new(FALSE, ui::kControlSpacing);
  identity_tab_contents_ = gtk_vbox_new(FALSE, ui::kControlSpacing);
  gtk_container_set_border_width(GTK_CONTAINER(identity_tab_contents_), 10);
  gtk_box_pack_start(GTK_BOX(info_tab),
                     identity_tab_contents_,
                     FALSE, FALSE, 0);
  first_visit_contents_ = gtk_vbox_new(FALSE, ui::kControlSpacing);
  GtkWidget* history_contents = CreateSection(
      l10n_util::GetStringUTF8(IDS_PAGE_INFO_SITE_INFO_TITLE),
      first_visit_contents_);
  gtk_container_set_border_width(GTK_CONTAINER(history_contents), 10);
  gtk_box_pack_start(GTK_BOX(info_tab), gtk_hseparator_new(), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(info_tab), history_contents, FALSE, FALSE, 0);

  // Create tab container and add all tabs.
  GtkWidget* notebook = gtk_notebook_new();
  if (theme_service_->UsingNativeTheme())
    gtk_widget_modify_bg(notebook, GTK_STATE_NORMAL, NULL);
  else
    gtk_widget_modify_bg(notebook, GTK_STATE_NORMAL, &kBackgroundColor);

  GtkWidget* label = theme_service_->BuildLabel(
      l10n_util::GetStringUTF8(IDS_WEBSITE_SETTINGS_TAB_LABEL_PERMISSIONS),
      ui::kGdkBlack);
  gtk_widget_show(label);
  gtk_notebook_append_page(
      GTK_NOTEBOOK(notebook), permission_tab_contents, label);

  label = theme_service_->BuildLabel(
      l10n_util::GetStringUTF8(IDS_WEBSITE_SETTINGS_TAB_LABEL_CONNECTION),
      ui::kGdkBlack);
  gtk_widget_show(label);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), info_tab, label);

  gtk_box_pack_start(GTK_BOX(contents_), notebook, FALSE, FALSE, 0);
  gtk_widget_show_all(contents_);
}

void WebsiteSettingsPopupGtk::ClearContainer(GtkWidget* container) {
  GList* child = gtk_container_get_children(GTK_CONTAINER(container));
  while (child) {
    gtk_container_remove(GTK_CONTAINER(container), GTK_WIDGET(child->data));
    child = child->next;
  }
}

GtkWidget* WebsiteSettingsPopupGtk::CreateSection(std::string section_title,
                                                  GtkWidget* section_content) {
  GtkWidget* section_box = gtk_vbox_new(FALSE, ui::kControlSpacing);

  // Add Section title
  GtkWidget* title_hbox = gtk_hbox_new(FALSE, ui::kControlSpacing);

  GtkWidget* label = theme_service_->BuildLabel(section_title,
                                                ui::kGdkBlack);
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  PangoAttrList* attributes = pango_attr_list_new();
  pango_attr_list_insert(attributes,
                         pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(label), attributes);
  pango_attr_list_unref(attributes);
  gtk_util::SetLabelWidth(label, 400);
  gtk_box_pack_start(GTK_BOX(section_box), title_hbox, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(title_hbox), label, FALSE, FALSE, 0);

  // Add section content
  gtk_box_pack_start(GTK_BOX(section_box), section_content, FALSE, FALSE, 0);
  return section_box;

}

void WebsiteSettingsPopupGtk::SetCookieInfo(
    const CookieInfoList& cookie_info_list) {
  DCHECK(cookies_section_contents_);
  ClearContainer(cookies_section_contents_);

  // Create cookies info rows.
  for (CookieInfoList::const_iterator it = cookie_info_list.begin();
       it != cookie_info_list.end();
       ++it) {
    GtkWidget* cookies_info = gtk_hbox_new(FALSE, 0);
    GtkWidget* label = CreateTextLabel(it->cookie_source, 200);
    gtk_box_pack_start(GTK_BOX(cookies_info), label, FALSE, FALSE, 0);

    std::string allowed_count = base::IntToString(it->allowed);
    std::string blocked_count = base::IntToString(it->blocked);
    // TODO(markusheintz): Add a localized label here once we decided how this
    // information should be displayed.
    std::string info_str = " (" + allowed_count + " allowed / "
                           + blocked_count + " blocked)";

    GtkWidget* info = theme_service_->BuildLabel(info_str, ui::kGdkBlack);
    gtk_label_set_selectable(GTK_LABEL(info), TRUE);
    gtk_box_pack_start(GTK_BOX(cookies_info), info, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(cookies_section_contents_),
                       cookies_info,
                       FALSE, FALSE, 0);
  }

  // Create row with links for cookie settings and for the cookies dialog.
  GtkWidget* link_hbox = gtk_hbox_new(FALSE, 0);

  GtkWidget* view_cookies_link = theme_service_->BuildChromeLinkButton(
      l10n_util::GetStringUTF8(IDS_WEBSITE_SETTINGS_SHOW_SITE_DATA));
  g_signal_connect(view_cookies_link, "clicked",
                   G_CALLBACK(OnCookiesLinkClickedThunk), this);
  gtk_box_pack_start(GTK_BOX(link_hbox), view_cookies_link,
                     FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(cookies_section_contents_), link_hbox,
                     TRUE, FALSE, 0);

  gtk_widget_show_all(cookies_section_contents_);
}

GtkWidget* WebsiteSettingsPopupGtk::CreateTextLabel(const std::string& text,
                                                    int width) {
  GtkWidget* label = theme_service_->BuildLabel(text, ui::kGdkBlack);
  gtk_util::SetLabelWidth(label, width);
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  return label;
}

void WebsiteSettingsPopupGtk::SetIdentityInfo(
    const IdentityInfo& identity_info) {
  // Create popup header.
  DCHECK(header_box_);
  ClearContainer(header_box_);

  GtkWidget* identity_label = theme_service_->BuildLabel(
      identity_info.site_identity, ui::kGdkBlack);
  gtk_label_set_selectable(GTK_LABEL(identity_label), TRUE);
  PangoAttrList* attributes = pango_attr_list_new();
  pango_attr_list_insert(attributes,
                         pango_attr_weight_new(PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes(GTK_LABEL(identity_label), attributes);
  pango_attr_list_unref(attributes);
  gtk_util::SetLabelWidth(identity_label, 400);
  gtk_box_pack_start(GTK_BOX(header_box_), identity_label, FALSE, FALSE, 0);

  std::string identity_status_text;
  switch (identity_info.identity_status) {
    case WebsiteSettings::SITE_IDENTITY_STATUS_CERT:
    case WebsiteSettings::SITE_IDENTITY_STATUS_DNSSEC_CERT:
    case WebsiteSettings::SITE_IDENTITY_STATUS_EV_CERT:
      identity_status_text =
          l10n_util::GetStringUTF8(IDS_WEBSITE_SETTINGS_IDENTITY_VERIFIED);
      break;
    default:
      identity_status_text =
          l10n_util::GetStringUTF8(IDS_WEBSITE_SETTINGS_IDENTITY_NOT_VERIFIED);
      break;
  }
  GtkWidget* status_label = CreateTextLabel(identity_status_text, 400);
  gtk_box_pack_start(
      GTK_BOX(header_box_), status_label, FALSE, FALSE, 0);
  gtk_widget_show_all(header_box_);

  // Create identity tab contents.
  DCHECK(identity_tab_contents_);
  ClearContainer(identity_tab_contents_);

  // Create identity section.
  GtkWidget* identity_description =
      CreateTextLabel(identity_info.identity_status_description, 300);
  GtkWidget* identity_box = gtk_vbox_new(FALSE, ui::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(identity_box), identity_description, FALSE, FALSE,
                     0);
  if (identity_info.cert_id) {
    cert_id_ = identity_info.cert_id;
    GtkWidget* view_cert_link = theme_service_->BuildChromeLinkButton(
        l10n_util::GetStringUTF8(IDS_PAGEINFO_CERT_INFO_BUTTON));
    g_signal_connect(view_cert_link, "clicked",
                     G_CALLBACK(OnViewCertLinkClickedThunk), this);
    GtkWidget* link_hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(link_hbox), view_cert_link,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_box), link_hbox, FALSE, FALSE, 0);
  }

  // Create connection section.
  GtkWidget* connection_description =
      CreateTextLabel(identity_info.connection_status_description, 300);
  GtkWidget* connection_box = gtk_vbox_new(FALSE, ui::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(connection_box), connection_description, FALSE,
                     FALSE, 0);

  // Add to contents.
  gtk_box_pack_start(
      GTK_BOX(identity_tab_contents_), CreateSection(
          l10n_util::GetStringUTF8(IDS_WEBSITE_SETTINGS_TITLE_IDENTITY),
          identity_box),
      TRUE,
      FALSE,
      0);
  gtk_box_pack_start(GTK_BOX(identity_tab_contents_),
                     gtk_hseparator_new(),
                     FALSE, FALSE, 0);
  gtk_box_pack_start(
       GTK_BOX(identity_tab_contents_),
       CreateSection(
           l10n_util::GetStringUTF8(
               IDS_WEBSITE_SETTINGS_TITLE_CONNECTION),
           connection_box),
       TRUE,
       FALSE,
       0);

  gtk_widget_show_all(identity_tab_contents_);
}

void WebsiteSettingsPopupGtk::SetFirstVisit(const string16& first_visit) {
  DCHECK(first_visit_contents_);
  ClearContainer(first_visit_contents_);

  GtkWidget* first_visit_label = CreateTextLabel(UTF16ToUTF8(first_visit), 400);
  gtk_box_pack_start(
      GTK_BOX(first_visit_contents_), first_visit_label, FALSE, FALSE, 0);
  gtk_widget_show_all(first_visit_contents_);
}

void WebsiteSettingsPopupGtk::SetPermissionInfo(
      const PermissionInfoList& permission_info_list) {
  DCHECK(permissions_section_contents_);
  ClearContainer(permissions_section_contents_);

  for (PermissionInfoList::const_iterator permission =
           permission_info_list.begin();
       permission != permission_info_list.end();
       ++permission) {
    // Add a label for the permission type.
    GtkWidget* label = CreateTextLabel(UTF16ToUTF8(
        WebsiteSettingsUI::PermissionTypeToUIString(permission->type)), 250);
    GtkWidget* hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    GtkListStore* store =
        gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
    GtkTreeIter iter;
    // Add option for permission "Global Default" to the combobox model.
    std::string setting_str = l10n_util::GetStringFUTF8(
        IDS_WEBSITE_SETTINGS_DEFAULT_PERMISSION_LABEL,
        WebsiteSettingsUI::PermissionValueToUIString(
            permission->default_setting));
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, setting_str.c_str(), 1,
                       CONTENT_SETTING_DEFAULT, 2, permission->type, -1);
    GtkWidget* combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    // Add option for permission "Allow" to the combobox model.
    setting_str = l10n_util::GetStringFUTF8(
        IDS_WEBSITE_SETTINGS_PERMISSION_LABEL,
        WebsiteSettingsUI::PermissionValueToUIString(CONTENT_SETTING_ALLOW));
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, setting_str.c_str(), 1,
                       CONTENT_SETTING_ALLOW, 2, permission->type, -1);
    // The content settings type fullscreen does not support the concept of
    // blocking.
    if (permission->type != CONTENT_SETTINGS_TYPE_FULLSCREEN) {
      // Add option for permission "BLOCK" to the combobox model.
      setting_str = l10n_util::GetStringFUTF8(
          IDS_WEBSITE_SETTINGS_PERMISSION_LABEL,
          WebsiteSettingsUI::PermissionValueToUIString(CONTENT_SETTING_BLOCK));
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, 0, setting_str.c_str(), 1,
                         CONTENT_SETTING_BLOCK, 2, permission->type, -1);
    }
    // Remove reference to the store to prevent leaking.
    g_object_unref(G_OBJECT(store));

    GtkCellRenderer* cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo_box), cell, TRUE );
    gtk_cell_layout_set_attributes(
        GTK_CELL_LAYOUT(combo_box), cell, "text", 0, NULL);

    // Select the combobox entry for the currently configured permission value.
    int active = -1;
    switch (permission->setting) {
      case CONTENT_SETTING_DEFAULT: active = 0;
        break;
      case CONTENT_SETTING_ALLOW: active = 1;
        break;
      case CONTENT_SETTING_BLOCK: active = 2;
        break;
      default:
        NOTREACHED() << "Bad content setting:" << permission->setting;
        break;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), active);

    // Add change listener to the combobox.
    g_signal_connect(combo_box, "changed",
                     G_CALLBACK(OnPermissionChangedThunk), this);
    // Once the popup (window) for a combobox is shown, the bubble container
    // (window) loses it's focus. Therefore it necessary to reset the focus to
    // the bubble container after the combobox popup is closed.
    g_signal_connect(combo_box, "notify::popup-shown",
                     G_CALLBACK(OnComboBoxShownThunk), this);
    gtk_box_pack_start(GTK_BOX(hbox), combo_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(permissions_section_contents_), hbox, FALSE,
                       FALSE, 0);
  }

  GtkWidget* show_content_settings_link = theme_service_->BuildChromeLinkButton(
      l10n_util::GetStringUTF8(IDS_WEBSITE_SETTINGS_SHOW_PERMISSION_SETTINGS));
  g_signal_connect(show_content_settings_link, "clicked",
                   G_CALLBACK(OnPermissionsSettingsLinkClickedThunk), this);
  GtkWidget* link_hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(link_hbox), show_content_settings_link,
                     FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(permissions_section_contents_), link_hbox,
                     FALSE, FALSE, 0);

  gtk_widget_show_all(permissions_section_contents_);
}

void WebsiteSettingsPopupGtk::OnComboBoxShown(GtkWidget* widget,
                                              GParamSpec* property) {
  // GtkComboBox grabs the keyboard and pointer when it displays its popup,
  // which steals the grabs that BubbleGtk had installed.  When the popup is
  // hidden, we notify BubbleGtk so it can try to reacquire the grabs
  // (otherwise, GTK won't activate our widgets when the user clicks in them).
  gboolean popup_shown = FALSE;
  g_object_get(G_OBJECT(GTK_COMBO_BOX(widget)), "popup-shown", &popup_shown,
               NULL);
  if (!popup_shown)
    bubble_->HandlePointerAndKeyboardUngrabbedByContent();
}

void WebsiteSettingsPopupGtk::OnCookiesLinkClicked(GtkWidget* widget) {
  new CollectedCookiesGtk(GTK_WINDOW(parent_),
                          tab_contents_);
  bubble_->Close();
}

void WebsiteSettingsPopupGtk::OnPermissionsSettingsLinkClicked(
    GtkWidget* widget) {
  browser_->OpenURL(OpenURLParams(
      GURL(std::string(
           chrome::kChromeUISettingsURL) + chrome::kContentSettingsSubPage),
      content::Referrer(),
      NEW_FOREGROUND_TAB,
      content::PAGE_TRANSITION_LINK,
      false));
  bubble_->Close();
}

void WebsiteSettingsPopupGtk::OnPermissionChanged(GtkWidget* widget) {
  GtkTreeIter it;
  bool has_active = gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), &it);
  DCHECK(has_active);
  GtkTreeModel* store =
      GTK_TREE_MODEL(gtk_combo_box_get_model(GTK_COMBO_BOX(widget)));

  int value = -1;
  int type = -1;
  gtk_tree_model_get(store, &it, 1, &value, 2, &type, -1);

  if (presenter_.get())
    presenter_->OnSitePermissionChanged(ContentSettingsType(type),
                                        ContentSetting(value));
}

void WebsiteSettingsPopupGtk::OnViewCertLinkClicked(GtkWidget* widget) {
  DCHECK_NE(cert_id_, 0);
  ShowCertificateViewerByID(
      tab_contents_->web_contents(), GTK_WINDOW(parent_), cert_id_);
  bubble_->Close();
}
