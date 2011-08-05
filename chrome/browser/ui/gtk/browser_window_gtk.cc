// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/browser_window_gtk.h"

#include <gdk/gdkkeysyms.h>

#include <dlfcn.h>
#include <string>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/i18n/file_util_icu.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/message_loop.h"
#include "base/nix/xdg_util.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/debugger/devtools_window.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_manager.h"
#include "chrome/browser/page_info_window.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/app_modal_dialogs/app_modal_dialog_queue.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/find_bar/find_tab_helper.h"
#include "chrome/browser/ui/gtk/about_chrome_dialog.h"
#include "chrome/browser/ui/gtk/accelerators_gtk.h"
#include "chrome/browser/ui/gtk/bookmarks/bookmark_bar_gtk.h"
#include "chrome/browser/ui/gtk/browser_titlebar.h"
#include "chrome/browser/ui/gtk/browser_toolbar_gtk.h"
#include "chrome/browser/ui/gtk/cairo_cached_surface.h"
#include "chrome/browser/ui/gtk/collected_cookies_gtk.h"
#include "chrome/browser/ui/gtk/create_application_shortcuts_dialog_gtk.h"
#include "chrome/browser/ui/gtk/download/download_in_progress_dialog_gtk.h"
#include "chrome/browser/ui/gtk/download/download_shelf_gtk.h"
#include "chrome/browser/ui/gtk/edit_search_engine_dialog.h"
#include "chrome/browser/ui/gtk/find_bar_gtk.h"
#include "chrome/browser/ui/gtk/fullscreen_exit_bubble_gtk.h"
#include "chrome/browser/ui/gtk/global_menu_bar.h"
#include "chrome/browser/ui/gtk/gtk_floating_container.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/gtk/infobars/infobar_container_gtk.h"
#include "chrome/browser/ui/gtk/infobars/infobar_gtk.h"
#include "chrome/browser/ui/gtk/location_bar_view_gtk.h"
#include "chrome/browser/ui/gtk/nine_box.h"
#include "chrome/browser/ui/gtk/reload_button_gtk.h"
#include "chrome/browser/ui/gtk/repost_form_warning_gtk.h"
#include "chrome/browser/ui/gtk/status_bubble_gtk.h"
#include "chrome/browser/ui/gtk/tab_contents_container_gtk.h"
#include "chrome/browser/ui/gtk/tabs/tab_strip_gtk.h"
#include "chrome/browser/ui/gtk/task_manager_gtk.h"
#include "chrome/browser/ui/gtk/theme_install_bubble_view_gtk.h"
#include "chrome/browser/ui/gtk/update_recommended_dialog.h"
#include "chrome/browser/ui/omnibox/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/webui/bug_report_ui.h"
#include "chrome/browser/ui/window_sizer.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/tab_contents_view.h"
#include "content/common/native_web_keyboard_event.h"
#include "content/common/notification_service.h"
#include "content/common/view_messages.h"
#include "grit/app_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/skia_utils_gtk.h"

namespace {

// The number of milliseconds between loading animation frames.
const int kLoadingAnimationFrameTimeMs = 30;

// Default height of dev tools pane when docked to the browser window.  This
// matches the value in Views.
const int kDefaultDevToolsHeight = 200;

const int kMinDevToolsHeight = 50;

const char* kBrowserWindowKey = "__BROWSER_WINDOW_GTK__";

// The frame border is only visible in restored mode and is hardcoded to 4 px
// on each side regardless of the system window border size.
const int kFrameBorderThickness = 4;
// While resize areas on Windows are normally the same size as the window
// borders, our top area is shrunk by 1 px to make it easier to move the window
// around with our thinner top grabbable strip.  (Incidentally, our side and
// bottom resize areas don't match the frame border thickness either -- they
// span the whole nonclient area, so there's no "dead zone" for the mouse.)
const int kTopResizeAdjust = 1;
// In the window corners, the resize areas don't actually expand bigger, but
// the 16 px at the end of each edge triggers diagonal resizing.
const int kResizeAreaCornerSize = 16;
// The thickness of the shadow around the toolbar+web content area.  There are
// actually a couple pixels more that should overlap the toolbar and web
// content area, but we don't use those pixels.
const int kContentShadowThickness = 2;
// The offset to the background when the custom frame is off.  We want the
// window background to line up with the tab background regardless of whether
// we're in custom frame mode or not.  Since themes are designed with the
// custom frame in mind, we need to offset the background when the custom frame
// is off.
const int kCustomFrameBackgroundVerticalOffset = 15;

// The timeout in milliseconds before we'll get the true window position with
// gtk_window_get_position() after the last GTK configure-event signal.
const int kDebounceTimeoutMilliseconds = 100;

// Ubuntu patches their verrsion of GTK+ so that there is always a
// gripper in the bottom right corner of the window. We dynamically
// look up this symbol because it's a non-standard Ubuntu extension to
// GTK+. We always need to disable this feature since we can't
// communicate this to WebKit easily.
typedef void (*gtk_window_set_has_resize_grip_func)(GtkWindow*, gboolean);
gtk_window_set_has_resize_grip_func gtk_window_set_has_resize_grip_sym;

void  EnsureResizeGripFunction() {
  static bool resize_grip_looked_up = false;
  if (!resize_grip_looked_up) {
    resize_grip_looked_up = true;
    gtk_window_set_has_resize_grip_sym =
        reinterpret_cast<gtk_window_set_has_resize_grip_func>(
            dlsym(NULL, "gtk_window_set_has_resize_grip"));
  }
}

// Using gtk_window_get_position/size creates a race condition, so only use
// this to get the initial bounds.  After window creation, we pick up the
// normal bounds by connecting to the configure-event signal.
gfx::Rect GetInitialWindowBounds(GtkWindow* window) {
  gint x, y, width, height;
  gtk_window_get_position(window, &x, &y);
  gtk_window_get_size(window, &width, &height);
  return gfx::Rect(x, y, width, height);
}

// Get the command ids of the key combinations that are not valid gtk
// accelerators.
int GetCustomCommandId(GdkEventKey* event) {
  // Filter modifier to only include accelerator modifiers.
  guint modifier = event->state & gtk_accelerator_get_default_mod_mask();
  switch (event->keyval) {
    // Gtk doesn't allow GDK_Tab or GDK_ISO_Left_Tab to be an accelerator (see
    // gtk_accelerator_valid), so we need to handle these accelerators
    // manually.
    // Some X clients (e.g. cygwin, NX client, etc.) also send GDK_KP_Tab when
    // typing a tab key. We should also handle GDK_KP_Tab for such X clients as
    // Firefox does.
    case GDK_Tab:
    case GDK_ISO_Left_Tab:
    case GDK_KP_Tab:
      if (GDK_CONTROL_MASK == modifier) {
        return IDC_SELECT_NEXT_TAB;
      } else if ((GDK_CONTROL_MASK | GDK_SHIFT_MASK) == modifier) {
        return IDC_SELECT_PREVIOUS_TAB;
      }
      break;

    default:
      break;
  }
  return -1;
}

// Get the command ids of the accelerators that we don't want the native widget
// to be able to override.
int GetPreHandleCommandId(GdkEventKey* event) {
  // Filter modifier to only include accelerator modifiers.
  guint modifier = event->state & gtk_accelerator_get_default_mod_mask();
  switch (event->keyval) {
    case GDK_Page_Down:
      if (GDK_CONTROL_MASK == modifier) {
        return IDC_SELECT_NEXT_TAB;
      } else if ((GDK_CONTROL_MASK | GDK_SHIFT_MASK) == modifier) {
        return IDC_MOVE_TAB_NEXT;
      }
      break;

    case GDK_Page_Up:
      if (GDK_CONTROL_MASK == modifier) {
        return IDC_SELECT_PREVIOUS_TAB;
      } else if ((GDK_CONTROL_MASK | GDK_SHIFT_MASK) == modifier) {
        return IDC_MOVE_TAB_PREVIOUS;
      }
      break;

    default:
      break;
  }
  return -1;
}

GdkCursorType GdkWindowEdgeToGdkCursorType(GdkWindowEdge edge) {
  switch (edge) {
    case GDK_WINDOW_EDGE_NORTH_WEST:
      return GDK_TOP_LEFT_CORNER;
    case GDK_WINDOW_EDGE_NORTH:
      return GDK_TOP_SIDE;
    case GDK_WINDOW_EDGE_NORTH_EAST:
      return GDK_TOP_RIGHT_CORNER;
    case GDK_WINDOW_EDGE_WEST:
      return GDK_LEFT_SIDE;
    case GDK_WINDOW_EDGE_EAST:
      return GDK_RIGHT_SIDE;
    case GDK_WINDOW_EDGE_SOUTH_WEST:
      return GDK_BOTTOM_LEFT_CORNER;
    case GDK_WINDOW_EDGE_SOUTH:
      return GDK_BOTTOM_SIDE;
    case GDK_WINDOW_EDGE_SOUTH_EAST:
      return GDK_BOTTOM_RIGHT_CORNER;
    default:
      NOTREACHED();
  }
  return GDK_LAST_CURSOR;
}

// A helper method for setting the GtkWindow size that should be used in place
// of calling gtk_window_resize directly.  This is done to avoid a WM "feature"
// where setting the window size to the monitor size causes the WM to set the
// EWMH for full screen mode.
void SetWindowSize(GtkWindow* window, const gfx::Size& size) {
  GdkScreen* screen = gtk_window_get_screen(window);
  gint num_monitors = gdk_screen_get_n_monitors(screen);
  // Make sure the window doesn't match any monitor size.  We compare against
  // all monitors because we don't know which monitor the window is going to
  // open on (the WM decides that).
  for (gint i = 0; i < num_monitors; ++i) {
    GdkRectangle monitor_size;
    gdk_screen_get_monitor_geometry(screen, i, &monitor_size);
    if (gfx::Size(monitor_size.width, monitor_size.height) == size) {
      gtk_window_resize(window, size.width(), size.height() - 1);
      return;
    }
  }
  gtk_window_resize(window, size.width(), size.height());
}

GQuark GetBrowserWindowQuarkKey() {
  static GQuark quark = g_quark_from_static_string(kBrowserWindowKey);
  return quark;
}

}  // namespace

std::map<XID, GtkWindow*> BrowserWindowGtk::xid_map_;

BrowserWindowGtk::BrowserWindowGtk(Browser* browser)
    :  browser_(browser),
       state_(GDK_WINDOW_STATE_WITHDRAWN),
       bookmark_bar_is_floating_(false),
       frame_cursor_(NULL),
       is_active_(!ui::ActiveWindowWatcherX::WMSupportsActivation()),
       last_click_time_(0),
       maximize_after_show_(false),
       suppress_window_raise_(false),
       accel_group_(NULL),
       debounce_timer_disabled_(false),
       infobar_arrow_model_(this) {
}

BrowserWindowGtk::~BrowserWindowGtk() {
  ui::ActiveWindowWatcherX::RemoveObserver(this);

  browser_->tabstrip_model()->RemoveObserver(this);
}

void BrowserWindowGtk::Init() {
  // We register first so that other views like the toolbar can use the
  // is_active() function in their ActiveWindowChanged() handlers.
  ui::ActiveWindowWatcherX::AddObserver(this);

  use_custom_frame_pref_.Init(prefs::kUseCustomChromeFrame,
      browser_->profile()->GetPrefs(), this);

  // In some (older) versions of compiz, raising top-level windows when they
  // are partially off-screen causes them to get snapped back on screen, not
  // always even on the current virtual desktop.  If we are running under
  // compiz, suppress such raises, as they are not necessary in compiz anyway.
  std::string wm_name;
  if (ui::GetWindowManagerName(&wm_name) && wm_name == "compiz")
    suppress_window_raise_ = true;

  window_ = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  g_object_set_qdata(G_OBJECT(window_), GetBrowserWindowQuarkKey(), this);
  gtk_widget_add_events(GTK_WIDGET(window_), GDK_BUTTON_PRESS_MASK |
                                             GDK_POINTER_MOTION_MASK);

  // Disable the resize gripper on Ubuntu.
  EnsureResizeGripFunction();
  if (gtk_window_set_has_resize_grip_sym)
    gtk_window_set_has_resize_grip_sym(GTK_WINDOW(window_), FALSE);

  // Add this window to its own unique window group to allow for
  // window-to-parent modality.
  gtk_window_group_add_window(gtk_window_group_new(), window_);
  g_object_unref(gtk_window_get_group(window_));

  if (browser_->is_app()) {
    std::string app_name = browser_->app_name();
    if (app_name != DevToolsWindow::kDevToolsApp) {
      std::string wmclassname = web_app::GetWMClassFromAppName(app_name);

      scoped_ptr<base::Environment> env(base::Environment::Create());
      if (base::nix::GetDesktopEnvironment(env.get()) ==
          base::nix::DESKTOP_ENVIRONMENT_XFCE) {
        // Workaround for XFCE. XFCE seems to treat the class as a user
        // displayed title, which our app name certainly isn't. They don't have
        // a dock or application based behaviour so do what looks good.
        gtk_window_set_wmclass(window_,
                               wmclassname.c_str(),
                               gdk_get_program_class());
      } else {
        // Most everything else uses the wmclass_class to group windows
        // together (docks, per application stuff, etc). Hopefully they won't
        // display wmclassname to the user.
        gtk_window_set_wmclass(window_,
                               g_get_prgname(),
                               wmclassname.c_str());
      }

      gtk_window_set_role(window_, wmclassname.c_str());
    }
  }

  // For popups, we initialize widgets then set the window geometry, because
  // popups need the widgets inited before they can set the window size
  // properly. For other windows, we set the geometry first to prevent resize
  // flicker.
  if (browser_->is_type_popup() || browser_->is_type_panel()) {
    InitWidgets();
    SetGeometryHints();
  } else {
    SetGeometryHints();
    InitWidgets();
  }

  ConnectAccelerators();

  // Set the initial background color of widgets.
  SetBackgroundColor();
  HideUnsupportedWindowFeatures();

  registrar_.Add(this, NotificationType::BOOKMARK_BAR_VISIBILITY_PREF_CHANGED,
                 NotificationService::AllSources());
}

gboolean BrowserWindowGtk::OnCustomFrameExpose(GtkWidget* widget,
                                               GdkEventExpose* event) {
  // Draw the default background.
  cairo_t* cr = gdk_cairo_create(GDK_DRAWABLE(widget->window));
  gdk_cairo_rectangle(cr, &event->area);
  cairo_clip(cr);

  if (UsingCustomPopupFrame()) {
    DrawPopupFrame(cr, widget, event);
  } else {
    DrawCustomFrame(cr, widget, event);
  }

  DrawContentShadow(cr);

  cairo_destroy(cr);

  if (UseCustomFrame() && !IsMaximized()) {
    static NineBox custom_frame_border(
        IDR_WINDOW_TOP_LEFT_CORNER,
        IDR_WINDOW_TOP_CENTER,
        IDR_WINDOW_TOP_RIGHT_CORNER,
        IDR_WINDOW_LEFT_SIDE,
        0,
        IDR_WINDOW_RIGHT_SIDE,
        IDR_WINDOW_BOTTOM_LEFT_CORNER,
        IDR_WINDOW_BOTTOM_CENTER,
        IDR_WINDOW_BOTTOM_RIGHT_CORNER);

    custom_frame_border.RenderToWidget(widget);
  }

  return FALSE;  // Allow subwidgets to paint.
}

void BrowserWindowGtk::DrawContentShadow(cairo_t* cr) {
  // Draw the shadow above the toolbar. Tabs on the tabstrip will draw over us.
  GtkThemeService* theme_provider = GtkThemeService::GetFrom(
      browser()->profile());
  int left_x, top_y;
  gtk_widget_translate_coordinates(toolbar_->widget(),
      GTK_WIDGET(window_), 0, 0, &left_x,
      &top_y);
  int center_width = window_vbox_->allocation.width;

  CairoCachedSurface* top_center = theme_provider->GetSurfaceNamed(
      IDR_CONTENT_TOP_CENTER, GTK_WIDGET(window_));
  CairoCachedSurface* top_right = theme_provider->GetSurfaceNamed(
      IDR_CONTENT_TOP_RIGHT_CORNER, GTK_WIDGET(window_));
  CairoCachedSurface* top_left = theme_provider->GetSurfaceNamed(
      IDR_CONTENT_TOP_LEFT_CORNER, GTK_WIDGET(window_));

  int center_left_x = left_x;
  if (ShouldDrawContentDropShadow()) {
    // Don't draw over the corners.
    center_left_x += top_left->Width() - kContentShadowThickness;
    center_width -= (top_left->Width() + top_right->Width());
    center_width += 2 * kContentShadowThickness;
  }

  top_center->SetSource(cr, center_left_x, top_y - kContentShadowThickness);
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
  cairo_rectangle(cr, center_left_x, top_y - kContentShadowThickness,
                  center_width, top_center->Height());
  cairo_fill(cr);

  // Only draw the rest of the shadow if the user has the custom frame enabled
  // and the browser is not maximized.
  if (!ShouldDrawContentDropShadow())
    return;

  // The top left corner has a width of 3 pixels. On Windows, the last column
  // of pixels overlap the toolbar. We just crop it off on Linux.  The top
  // corners extend to the base of the toolbar (one pixel above the dividing
  // line).
  int right_x = center_left_x + center_width;
  top_left->SetSource(
      cr, left_x - kContentShadowThickness, top_y - kContentShadowThickness);
  // The toolbar is shorter in location bar only mode so clip the image to the
  // height of the toolbar + the amount of shadow above the toolbar.
  cairo_rectangle(cr,
      left_x - kContentShadowThickness,
      top_y - kContentShadowThickness,
      top_left->Width(),
      top_left->Height());
  cairo_fill(cr);

  // Likewise, we crop off the left column of pixels for the top right corner.
  top_right->SetSource(cr, right_x, top_y - kContentShadowThickness);
  cairo_rectangle(cr,
      right_x,
      top_y - kContentShadowThickness,
      top_right->Width(),
      top_right->Height());
  cairo_fill(cr);

  // Fill in the sides.  As above, we only draw 2 of the 3 columns on Linux.
  int bottom_y;
  gtk_widget_translate_coordinates(window_vbox_,
      GTK_WIDGET(window_),
      0, window_vbox_->allocation.height,
      NULL, &bottom_y);
  // |side_y| is where to start drawing the side shadows.  The top corners draw
  // the sides down to the bottom of the toolbar.
  int side_y = top_y - kContentShadowThickness + top_right->Height();
  // |side_height| is how many pixels to draw for the side borders.  We do one
  // pixel before the bottom of the web contents because that extra pixel is
  // drawn by the bottom corners.
  int side_height = bottom_y - side_y - 1;
  if (side_height > 0) {
    CairoCachedSurface* left = theme_provider->GetSurfaceNamed(
        IDR_CONTENT_LEFT_SIDE, GTK_WIDGET(window_));
    left->SetSource(cr, left_x - kContentShadowThickness, side_y);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
    cairo_rectangle(cr,
        left_x - kContentShadowThickness,
        side_y,
        kContentShadowThickness,
        side_height);
    cairo_fill(cr);

    CairoCachedSurface* right = theme_provider->GetSurfaceNamed(
        IDR_CONTENT_RIGHT_SIDE, GTK_WIDGET(window_));
    int right_side_x =
        right_x + top_right->Width() - kContentShadowThickness - 1;
    right->SetSource(cr, right_side_x, side_y);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
    cairo_rectangle(cr,
        right_side_x,
        side_y,
        kContentShadowThickness,
        side_height);
    cairo_fill(cr);
  }

  // Draw the bottom corners.  The bottom corners also draw the bottom row of
  // pixels of the side shadows.
  CairoCachedSurface* bottom_left = theme_provider->GetSurfaceNamed(
      IDR_CONTENT_BOTTOM_LEFT_CORNER, GTK_WIDGET(window_));
  bottom_left->SetSource(cr, left_x - kContentShadowThickness, bottom_y - 1);
  cairo_paint(cr);

  CairoCachedSurface* bottom_right = theme_provider->GetSurfaceNamed(
      IDR_CONTENT_BOTTOM_RIGHT_CORNER, GTK_WIDGET(window_));
  bottom_right->SetSource(cr, right_x - 1, bottom_y - 1);
  cairo_paint(cr);

  // Finally, draw the bottom row. Since we don't overlap the contents, we clip
  // the top row of pixels.
  CairoCachedSurface* bottom = theme_provider->GetSurfaceNamed(
      IDR_CONTENT_BOTTOM_CENTER, GTK_WIDGET(window_));
  bottom->SetSource(cr, left_x + 1, bottom_y - 1);
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
  cairo_rectangle(cr,
      left_x + 1,
      bottom_y,
      window_vbox_->allocation.width - 2,
      kContentShadowThickness);
  cairo_fill(cr);
}

void BrowserWindowGtk::DrawPopupFrame(cairo_t* cr,
                                      GtkWidget* widget,
                                      GdkEventExpose* event) {
  GtkThemeService* theme_provider = GtkThemeService::GetFrom(
      browser()->profile());

  // Like DrawCustomFrame(), except that we use the unthemed resources to draw
  // the background. We do this because we can't rely on sane images in the
  // theme that we can draw text on. (We tried using the tab background, but
  // that has inverse saturation from what the user usually expects).
  int image_name = GetThemeFrameResource();
  CairoCachedSurface* surface = theme_provider->GetUnthemedSurfaceNamed(
      image_name, widget);
  surface->SetSource(cr, 0, GetVerticalOffset());
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REFLECT);
  cairo_rectangle(cr, event->area.x, event->area.y,
                  event->area.width, event->area.height);
  cairo_fill(cr);
}

void BrowserWindowGtk::DrawCustomFrame(cairo_t* cr,
                                       GtkWidget* widget,
                                       GdkEventExpose* event) {
  GtkThemeService* theme_provider = GtkThemeService::GetFrom(
      browser()->profile());

  int image_name = GetThemeFrameResource();

  CairoCachedSurface* surface = theme_provider->GetSurfaceNamed(
      image_name, widget);
  if (event->area.y < surface->Height()) {
    surface->SetSource(cr, 0, GetVerticalOffset());

    // The frame background isn't tiled vertically.
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
    cairo_rectangle(cr, event->area.x, event->area.y,
                    event->area.width, surface->Height() - event->area.y);
    cairo_fill(cr);
  }

  if (theme_provider->HasCustomImage(IDR_THEME_FRAME_OVERLAY) &&
      !browser()->profile()->IsOffTheRecord()) {
    CairoCachedSurface* theme_overlay = theme_provider->GetSurfaceNamed(
        IsActive() ? IDR_THEME_FRAME_OVERLAY
        : IDR_THEME_FRAME_OVERLAY_INACTIVE, widget);
    theme_overlay->SetSource(cr, 0, GetVerticalOffset());
    cairo_paint(cr);
  }
}

int BrowserWindowGtk::GetVerticalOffset() {
  return (IsMaximized() || (!UseCustomFrame())) ?
      -kCustomFrameBackgroundVerticalOffset : 0;
}

int BrowserWindowGtk::GetThemeFrameResource() {
  bool incognito = browser()->profile()->IsOffTheRecord();
  int image_name;
  if (IsActive()) {
    image_name = incognito ? IDR_THEME_FRAME_INCOGNITO : IDR_THEME_FRAME;
  } else {
    image_name = incognito ? IDR_THEME_FRAME_INCOGNITO_INACTIVE :
                 IDR_THEME_FRAME_INACTIVE;
  }

  return image_name;
}

void BrowserWindowGtk::Show() {
  // The Browser associated with this browser window must become the active
  // browser at the time Show() is called. This is the natural behaviour under
  // Windows, but gtk_widget_show won't show the widget (and therefore won't
  // call OnFocusIn()) until we return to the runloop. Therefore any calls to
  // BrowserList::GetLastActive() (for example, in bookmark_util), will return
  // the previous browser instead if we don't explicitly set it here.
  BrowserList::SetLastActive(browser());

  gtk_window_present(window_);
  if (maximize_after_show_) {
    gtk_window_maximize(window_);
    maximize_after_show_ = false;
  }

  // If we have sized the window by setting a size request for the render
  // area, then undo it so that the render view can later adjust its own
  // size.
  gtk_widget_set_size_request(contents_container_->widget(), -1, -1);
}

void BrowserWindowGtk::ShowInactive() {
  gtk_window_set_focus_on_map(window_, false);
  gtk_widget_show(GTK_WIDGET(window_));
}

void BrowserWindowGtk::SetBoundsImpl(const gfx::Rect& bounds,
                                     bool exterior,
                                     bool move) {
  gint x = static_cast<gint>(bounds.x());
  gint y = static_cast<gint>(bounds.y());
  gint width = static_cast<gint>(bounds.width());
  gint height = static_cast<gint>(bounds.height());

  if (move)
    gtk_window_move(window_, x, y);

  if (exterior) {
    SetWindowSize(window_, gfx::Size(width, height));
  } else {
    gtk_widget_set_size_request(contents_container_->widget(),
                                width, height);
  }
}

void BrowserWindowGtk::SetBounds(const gfx::Rect& bounds) {
  if (IsFullscreen())
    SetFullscreen(false);
  SetBoundsImpl(bounds, true, true);
}

void BrowserWindowGtk::Close() {
  // We're already closing.  Do nothing.
  if (!window_)
    return;

  if (!CanClose())
    return;

  // We're going to destroy the window, make sure the tab strip isn't running
  // any animations which may still reference GtkWidgets.
  tabstrip_->StopAnimation();

  SaveWindowPosition();

  if (accel_group_) {
    // Disconnecting the keys we connected to our accelerator group frees the
    // closures allocated in ConnectAccelerators.
    AcceleratorsGtk* accelerators = AcceleratorsGtk::GetInstance();
    for (AcceleratorsGtk::const_iterator iter = accelerators->begin();
         iter != accelerators->end(); ++iter) {
      gtk_accel_group_disconnect_key(accel_group_,
          iter->second.GetGdkKeyCode(),
          static_cast<GdkModifierType>(iter->second.modifiers()));
    }
    gtk_window_remove_accel_group(window_, accel_group_);
    g_object_unref(accel_group_);
    accel_group_ = NULL;
  }

  // Cancel any pending callback from the window configure debounce timer.
  window_configure_debounce_timer_.Stop();

  // Likewise for the loading animation.
  loading_animation_timer_.Stop();

  GtkWidget* window = GTK_WIDGET(window_);
  // To help catch bugs in any event handlers that might get fired during the
  // destruction, set window_ to NULL before any handlers will run.
  window_ = NULL;
  titlebar_->set_window(NULL);
  gtk_widget_destroy(window);
}

void BrowserWindowGtk::Activate() {
  gtk_window_present(window_);
}

void BrowserWindowGtk::Deactivate() {
  gdk_window_lower(GTK_WIDGET(window_)->window);
}

bool BrowserWindowGtk::IsActive() const {
  return is_active_;
}

void BrowserWindowGtk::FlashFrame() {
  // May not be respected by all window managers.
  gtk_window_set_urgency_hint(window_, TRUE);
}

gfx::NativeWindow BrowserWindowGtk::GetNativeHandle() {
  return window_;
}

BrowserWindowTesting* BrowserWindowGtk::GetBrowserWindowTesting() {
  NOTIMPLEMENTED();
  return NULL;
}

StatusBubble* BrowserWindowGtk::GetStatusBubble() {
  return status_bubble_.get();
}

void BrowserWindowGtk::ToolbarSizeChanged(bool is_animating) {
  // On Windows, this is used for a performance optimization.
  // http://code.google.com/p/chromium/issues/detail?id=12291
}

void BrowserWindowGtk::UpdateTitleBar() {
  string16 title = browser_->GetWindowTitleForCurrentTab();
  gtk_window_set_title(window_, UTF16ToUTF8(title).c_str());
  if (ShouldShowWindowIcon())
    titlebar_->UpdateTitleAndIcon();
}

void BrowserWindowGtk::ShelfVisibilityChanged() {
  MaybeShowBookmarkBar(false);
}

void BrowserWindowGtk::UpdateDevTools() {
  UpdateDevToolsForContents(
      browser_->GetSelectedTabContents());
}

void BrowserWindowGtk::UpdateLoadingAnimations(bool should_animate) {
  if (should_animate) {
    if (!loading_animation_timer_.IsRunning()) {
      // Loads are happening, and the timer isn't running, so start it.
      loading_animation_timer_.Start(
          base::TimeDelta::FromMilliseconds(kLoadingAnimationFrameTimeMs), this,
          &BrowserWindowGtk::LoadingAnimationCallback);
    }
  } else {
    if (loading_animation_timer_.IsRunning()) {
      loading_animation_timer_.Stop();
      // Loads are now complete, update the state if a task was scheduled.
      LoadingAnimationCallback();
    }
  }
}

void BrowserWindowGtk::LoadingAnimationCallback() {
  if (browser_->is_type_tabbed()) {
    // Loading animations are shown in the tab for tabbed windows.  We check the
    // browser type instead of calling IsTabStripVisible() because the latter
    // will return false for fullscreen windows, but we still need to update
    // their animations (so that when they come out of fullscreen mode they'll
    // be correct).
    tabstrip_->UpdateLoadingAnimations();
  } else if (ShouldShowWindowIcon()) {
    // ... or in the window icon area for popups and app windows.
    TabContents* tab_contents = browser_->GetSelectedTabContents();
    // GetSelectedTabContents can return NULL for example under Purify when
    // the animations are running slowly and this function is called on
    // a timer through LoadingAnimationCallback.
    titlebar_->UpdateThrobber(tab_contents);
  }
}

void BrowserWindowGtk::SetStarredState(bool is_starred) {
  toolbar_->GetLocationBarView()->SetStarred(is_starred);
}

gfx::Rect BrowserWindowGtk::GetRestoredBounds() const {
  return restored_bounds_;
}

gfx::Rect BrowserWindowGtk::GetBounds() const {
  return bounds_;
}

bool BrowserWindowGtk::IsMaximized() const {
  return (state_ & GDK_WINDOW_STATE_MAXIMIZED);
}

bool BrowserWindowGtk::ShouldDrawContentDropShadow() {
  return !IsMaximized() && UseCustomFrame();
}

void BrowserWindowGtk::SetFullscreen(bool fullscreen) {
  // gtk_window_(un)fullscreen asks the window manager to toggle the EWMH
  // for fullscreen windows.  Not all window managers support this.
  if (fullscreen) {
    gtk_window_fullscreen(window_);
  } else {
    // Work around a bug where if we try to unfullscreen, metacity immediately
    // fullscreens us again.  This is a little flickery and not necessary if
    // there's a gnome-panel, but it's not easy to detect whether there's a
    // panel or not.
    std::string wm_name;
    bool unmaximize_before_unfullscreen = IsMaximized() &&
        ui::GetWindowManagerName(&wm_name) && wm_name == "Metacity";
    if (unmaximize_before_unfullscreen)
      UnMaximize();

    gtk_window_unfullscreen(window_);

    if (unmaximize_before_unfullscreen)
      gtk_window_maximize(window_);
  }
}

bool BrowserWindowGtk::IsFullscreen() const {
  return (state_ & GDK_WINDOW_STATE_FULLSCREEN);
}

bool BrowserWindowGtk::IsFullscreenBubbleVisible() const {
  return fullscreen_exit_bubble_.get() ? true : false;
}

LocationBar* BrowserWindowGtk::GetLocationBar() const {
  return toolbar_->GetLocationBar();
}

void BrowserWindowGtk::SetFocusToLocationBar(bool select_all) {
  if (!IsFullscreen())
    GetLocationBar()->FocusLocation(select_all);
}

void BrowserWindowGtk::UpdateReloadStopState(bool is_loading, bool force) {
  toolbar_->GetReloadButton()->ChangeMode(
      is_loading ? ReloadButtonGtk::MODE_STOP : ReloadButtonGtk::MODE_RELOAD,
      force);
}

void BrowserWindowGtk::UpdateToolbar(TabContentsWrapper* contents,
                                     bool should_restore_state) {
  toolbar_->UpdateTabContents(contents->tab_contents(), should_restore_state);
}

void BrowserWindowGtk::FocusToolbar() {
  NOTIMPLEMENTED();
}

void BrowserWindowGtk::FocusAppMenu() {
  NOTIMPLEMENTED();
}

void BrowserWindowGtk::FocusBookmarksToolbar() {
  NOTIMPLEMENTED();
}

void BrowserWindowGtk::FocusChromeOSStatus() {
  NOTIMPLEMENTED();
}

void BrowserWindowGtk::RotatePaneFocus(bool forwards) {
  NOTIMPLEMENTED();
}

bool BrowserWindowGtk::IsBookmarkBarVisible() const {
  return (browser_->SupportsWindowFeature(Browser::FEATURE_BOOKMARKBAR) &&
          bookmark_bar_.get() &&
          browser_->profile()->GetPrefs()->GetBoolean(
              prefs::kShowBookmarkBar) &&
          browser_->profile()->GetPrefs()->GetBoolean(
              prefs::kEnableBookmarkBar));
}

bool BrowserWindowGtk::IsBookmarkBarAnimating() const {
  if (IsBookmarkBarSupported() && bookmark_bar_->IsAnimating())
    return true;
  return false;
}

bool BrowserWindowGtk::IsTabStripEditable() const {
  return !tabstrip()->IsDragSessionActive() &&
      !tabstrip()->IsActiveDropTarget();
}

bool BrowserWindowGtk::IsToolbarVisible() const {
  return IsToolbarSupported();
}

void BrowserWindowGtk::ConfirmAddSearchProvider(const TemplateURL* template_url,
                                                Profile* profile) {
  new EditSearchEngineDialog(window_, template_url, NULL, profile);
}

void BrowserWindowGtk::ToggleBookmarkBar() {
  bookmark_utils::ToggleWhenVisible(browser_->profile());
}

void BrowserWindowGtk::ShowAboutChromeDialog() {
  ShowAboutDialogForProfile(window_, browser_->profile());
}

void BrowserWindowGtk::ShowUpdateChromeDialog() {
  UpdateRecommendedDialog::Show(window_);
}

void BrowserWindowGtk::ShowTaskManager() {
  TaskManagerGtk::Show(false);
}

void BrowserWindowGtk::ShowBackgroundPages() {
  TaskManagerGtk::Show(true);
}

void BrowserWindowGtk::ShowBookmarkBubble(const GURL& url,
                                          bool already_bookmarked) {
  toolbar_->GetLocationBarView()->ShowStarBubble(url, !already_bookmarked);
}

bool BrowserWindowGtk::IsDownloadShelfVisible() const {
  return download_shelf_.get() && download_shelf_->IsShowing();
}

DownloadShelf* BrowserWindowGtk::GetDownloadShelf() {
  if (!download_shelf_.get())
    download_shelf_.reset(new DownloadShelfGtk(browser_.get(),
                                               render_area_vbox_));
  return download_shelf_.get();
}

void BrowserWindowGtk::ShowRepostFormWarningDialog(TabContents* tab_contents) {
  new RepostFormWarningGtk(GetNativeHandle(), tab_contents);
}

void BrowserWindowGtk::ShowCollectedCookiesDialog(TabContents* tab_contents) {
  // Deletes itself on close.
  new CollectedCookiesGtk(GetNativeHandle(), tab_contents);
}

void BrowserWindowGtk::ShowThemeInstallBubble() {
  ThemeInstallBubbleViewGtk::Show(window_);
}

void BrowserWindowGtk::ShowHTMLDialog(HtmlDialogUIDelegate* delegate,
                                      gfx::NativeWindow parent_window) {
  browser::ShowHtmlDialog(parent_window, browser_->profile(), delegate);
}

void BrowserWindowGtk::UserChangedTheme() {
  SetBackgroundColor();
  gdk_window_invalidate_rect(GTK_WIDGET(window_)->window,
      &GTK_WIDGET(window_)->allocation, TRUE);
  UpdateWindowShape(bounds_.width(), bounds_.height());
}

int BrowserWindowGtk::GetExtraRenderViewHeight() const {
  int sum = infobar_container_->TotalHeightOfAnimatingBars();
  if (IsBookmarkBarSupported() && bookmark_bar_->IsAnimating())
    sum += bookmark_bar_->GetHeight();
  if (download_shelf_.get() && download_shelf_->IsClosing())
    sum += download_shelf_->GetHeight();
  return sum;
}

void BrowserWindowGtk::TabContentsFocused(TabContents* tab_contents) {
  NOTIMPLEMENTED();
}

void BrowserWindowGtk::ShowPageInfo(Profile* profile,
                                    const GURL& url,
                                    const NavigationEntry::SSLStatus& ssl,
                                    bool show_history) {
  browser::ShowPageInfoBubble(window_, profile, url, ssl, show_history);
}

void BrowserWindowGtk::ShowAppMenu() {
  toolbar_->ShowAppMenu();
}

bool BrowserWindowGtk::PreHandleKeyboardEvent(
    const NativeWebKeyboardEvent& event, bool* is_keyboard_shortcut) {
  GdkEventKey* os_event = event.os_event;

  if (!os_event || event.type != WebKit::WebInputEvent::RawKeyDown)
    return false;

  // We first find out the browser command associated to the |event|.
  // Then if the command is a reserved one, and should be processed immediately
  // according to the |event|, the command will be executed immediately.
  // Otherwise we just set |*is_keyboard_shortcut| properly and return false.

  // First check if it's a custom accelerator.
  int id = GetCustomCommandId(os_event);

  // Then check if it's a predefined accelerator bound to the window.
  if (id == -1) {
    // This piece of code is based on the fact that calling
    // gtk_window_activate_key() method against |window_| may only trigger a
    // browser command execution, by matching a global accelerator
    // defined in above |kAcceleratorMap|.
    //
    // Here we need to retrieve the command id (if any) associated to the
    // keyboard event. Instead of looking up the command id in above
    // |kAcceleratorMap| table by ourselves, we block the command execution of
    // the |browser_| object then send the keyboard event to the |window_| by
    // calling gtk_window_activate_key() method, as if we are activating an
    // accelerator key. Then we can retrieve the command id from the
    // |browser_| object.
    //
    // Pros of this approach:
    // 1. We don't need to care about keyboard layout problem, as
    //    gtk_window_activate_key() method handles it for us.
    //
    // Cons:
    // 1. The logic is a little complicated.
    // 2. We should be careful not to introduce any accelerators that trigger
    //    customized code instead of browser commands.
    browser_->SetBlockCommandExecution(true);
    gtk_window_activate_key(window_, os_event);
    // We don't need to care about the WindowOpenDisposition value,
    // because all commands executed in this path use the default value.
    id = browser_->GetLastBlockedCommand(NULL);
    browser_->SetBlockCommandExecution(false);
  }

  if (id == -1)
    return false;

  // Executing the command may cause |this| object to be destroyed.
  if (browser_->IsReservedCommandOrKey(id, event) && !event.match_edit_command)
    return browser_->ExecuteCommandIfEnabled(id);

  // The |event| is a keyboard shortcut.
  DCHECK(is_keyboard_shortcut != NULL);
  *is_keyboard_shortcut = true;

  return false;
}

void BrowserWindowGtk::HandleKeyboardEvent(
    const NativeWebKeyboardEvent& event) {
  GdkEventKey* os_event = event.os_event;

  if (!os_event || event.type != WebKit::WebInputEvent::RawKeyDown)
    return;

  // Handles a key event in following sequence:
  // 1. Our special key accelerators, such as ctrl-tab, etc.
  // 2. Gtk accelerators.
  // This sequence matches the default key press handler of GtkWindow.
  //
  // It's not necessary to care about the keyboard layout, as
  // gtk_window_activate_key() takes care of it automatically.
  int id = GetCustomCommandId(os_event);
  if (id != -1)
    browser_->ExecuteCommandIfEnabled(id);
  else
    gtk_window_activate_key(window_, os_event);
}

void BrowserWindowGtk::ShowCreateWebAppShortcutsDialog(
    TabContentsWrapper* tab_contents) {
  CreateWebApplicationShortcutsDialogGtk::Show(window_, tab_contents);
}

void BrowserWindowGtk::ShowCreateChromeAppShortcutsDialog(
    Profile* profile, const Extension* app) {
  CreateChromeApplicationShortcutsDialogGtk::Show(window_, app);
}

void BrowserWindowGtk::Cut() {
  gtk_util::DoCut(this);
}

void BrowserWindowGtk::Copy() {
  gtk_util::DoCopy(this);
}

void BrowserWindowGtk::Paste() {
  gtk_util::DoPaste(this);
}

void BrowserWindowGtk::ToggleTabStripMode() {
}

void BrowserWindowGtk::ToggleUseCompactNavigationBar() {
}

void BrowserWindowGtk::PrepareForInstant() {
  TabContentsWrapper* contents = contents_container_->tab();
  if (contents)
    FadeForInstant(true);
}

void BrowserWindowGtk::ShowInstant(TabContentsWrapper* preview) {
  contents_container_->SetPreview(preview);
  MaybeShowBookmarkBar(false);

  TabContentsWrapper* contents = contents_container_->tab();
  if (contents)
    CancelInstantFade();
}

void BrowserWindowGtk::HideInstant(bool instant_is_active) {
  contents_container_->PopPreview();
  MaybeShowBookmarkBar(false);

  TabContentsWrapper* contents = contents_container_->tab();
  if (contents) {
    if (instant_is_active)
      FadeForInstant(false);
    else
      CancelInstantFade();
  }
}

gfx::Rect BrowserWindowGtk::GetInstantBounds() {
  return gtk_util::GetWidgetScreenBounds(contents_container_->widget());
}

WindowOpenDisposition BrowserWindowGtk::GetDispositionForPopupBounds(
    const gfx::Rect& bounds) {
  return NEW_POPUP;
}

void BrowserWindowGtk::ConfirmBrowserCloseWithPendingDownloads() {
  new DownloadInProgressDialogGtk(browser());
}

void BrowserWindowGtk::Observe(NotificationType type,
                               const NotificationSource& source,
                               const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::BOOKMARK_BAR_VISIBILITY_PREF_CHANGED:
      MaybeShowBookmarkBar(true);
      break;

    case NotificationType::PREF_CHANGED: {
      std::string* pref_name = Details<std::string>(details).ptr();
      if (*pref_name == prefs::kUseCustomChromeFrame) {
        UpdateCustomFrame();
      } else {
        NOTREACHED() << "Got pref change notification we didn't register for!";
      }
      break;
    }

    default:
      NOTREACHED() << "Got a notification we didn't register for!";
  }
}

void BrowserWindowGtk::TabDetachedAt(TabContentsWrapper* contents, int index) {
  // We use index here rather than comparing |contents| because by this time
  // the model has already removed |contents| from its list, so
  // browser_->GetSelectedTabContents() will return NULL or something else.
  if (index == browser_->tabstrip_model()->active_index()) {
    infobar_container_->ChangeTabContents(NULL);
    UpdateDevToolsForContents(NULL);
  }
  contents_container_->DetachTab(contents);
}

void BrowserWindowGtk::ActiveTabChanged(TabContentsWrapper* old_contents,
                                        TabContentsWrapper* new_contents,
                                        int index,
                                        bool user_gesture) {
  if (old_contents == new_contents)
    return;

  if (old_contents && !old_contents->tab_contents()->is_being_destroyed())
    old_contents->view()->StoreFocus();

  // Update various elements that are interested in knowing the current
  // TabContents.
  infobar_container_->ChangeTabContents(new_contents);
  contents_container_->SetTab(new_contents);
  UpdateDevToolsForContents(new_contents->tab_contents());

  new_contents->tab_contents()->DidBecomeSelected();
  // TODO(estade): after we manage browser activation, add a check to make sure
  // we are the active browser before calling RestoreFocus().
  if (!browser_->tabstrip_model()->closing_all()) {
    new_contents->view()->RestoreFocus();
    if (new_contents->find_tab_helper()->find_ui_active())
      browser_->GetFindBarController()->find_bar()->SetFocusAndSelection();
  }

  // Update all the UI bits.
  UpdateTitleBar();
  UpdateToolbar(new_contents, true);
  MaybeShowBookmarkBar(false);
}

void BrowserWindowGtk::ActiveWindowChanged(GdkWindow* active_window) {
  // Do nothing if we're in the process of closing the browser window.
  if (!window_)
    return;

  bool is_active = (GTK_WIDGET(window_)->window == active_window);
  bool changed = (is_active != is_active_);

  if (is_active && changed) {
    // If there's an app modal dialog (e.g., JS alert), try to redirect
    // the user's attention to the window owning the dialog.
    if (AppModalDialogQueue::GetInstance()->HasActiveDialog()) {
      AppModalDialogQueue::GetInstance()->ActivateModalDialog();
      return;
    }
  }

  is_active_ = is_active;
  if (changed) {
    SetBackgroundColor();
    gdk_window_invalidate_rect(GTK_WIDGET(window_)->window,
                               &GTK_WIDGET(window_)->allocation, TRUE);
    // For some reason, the above two calls cause the window shape to be
    // lost so reset it.
    UpdateWindowShape(bounds_.width(), bounds_.height());
  }
}

void BrowserWindowGtk::FadeForInstant(bool animate) {
  DCHECK(contents_container_->tab());
  RenderWidgetHostView* rwhv =
      contents_container_->tab()->tab_contents()->GetRenderWidgetHostView();
  if (rwhv) {
    SkColor whitish = SkColorSetARGB(192, 255, 255, 255);
    rwhv->SetVisuallyDeemphasized(&whitish, animate);
  }
}

void BrowserWindowGtk::CancelInstantFade() {
  DCHECK(contents_container_->tab());
  RenderWidgetHostView* rwhv =
      contents_container_->tab()->tab_contents()->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->SetVisuallyDeemphasized(NULL, false);
}

void BrowserWindowGtk::MaybeShowBookmarkBar(bool animate) {
  if (!IsBookmarkBarSupported())
    return;

  TabContents* contents = GetDisplayedTabContents();
  bool show_bar = false;

  if (contents) {
    bookmark_bar_->SetProfile(contents->profile());
    bookmark_bar_->SetPageNavigator(contents);
    show_bar = true;
  }

  if (show_bar && contents && !contents->ShouldShowBookmarkBar()) {
    PrefService* prefs = contents->profile()->GetPrefs();
    show_bar = prefs->GetBoolean(prefs::kShowBookmarkBar) &&
               prefs->GetBoolean(prefs::kEnableBookmarkBar) &&
               !IsFullscreen();
  }

  if (show_bar) {
    bookmark_bar_->Show(animate);
  } else if (IsFullscreen()) {
    bookmark_bar_->EnterFullscreen();
  } else {
    bookmark_bar_->Hide(animate);
  }
}

void BrowserWindowGtk::UpdateDevToolsForContents(TabContents* contents) {
  TabContentsWrapper* old_devtools = devtools_container_->tab();
  TabContentsWrapper* devtools_contents = contents ?
      DevToolsWindow::GetDevToolsContents(contents) : NULL;
  if (old_devtools == devtools_contents)
    return;

  if (old_devtools)
    devtools_container_->DetachTab(old_devtools);

  devtools_container_->SetTab(devtools_contents);
  if (devtools_contents) {
    // TabContentsViewGtk::WasShown is not called when tab contents is shown by
    // anything other than user selecting a Tab.
    // See TabContentsViewViews::OnWindowPosChanged for reference on how it
    // should be implemented.
    devtools_contents->tab_contents()->ShowContents();
  }

  bool should_show = old_devtools == NULL && devtools_contents != NULL;
  bool should_hide = old_devtools != NULL && devtools_contents == NULL;
  if (should_show) {
    gtk_widget_show(devtools_container_->widget());
  } else if (should_hide) {
    // Store split offset when hiding devtools window only.
    gint divider_offset = gtk_paned_get_position(GTK_PANED(contents_split_));
    browser_->profile()->GetPrefs()->
        SetInteger(prefs::kDevToolsSplitLocation, divider_offset);
    gtk_widget_hide(devtools_container_->widget());
  }
}

void BrowserWindowGtk::DestroyBrowser() {
  browser_.reset();
}

gboolean BrowserWindowGtk::OnConfigure(GtkWidget* widget,
                                       GdkEventConfigure* event) {
  gfx::Rect bounds(event->x, event->y, event->width, event->height);

  // When the window moves, we'll get multiple configure-event signals. We can
  // also get events when the bounds haven't changed, but the window's stacking
  // has, which we aren't interested in. http://crbug.com/70125
  if (bounds == bounds_)
    return FALSE;

  GetLocationBar()->location_entry()->ClosePopup();

  TabContents* tab_contents = GetDisplayedTabContents();
  if (tab_contents) {
    RenderViewHost* rvh = tab_contents->render_view_host();
    rvh->Send(new ViewMsg_MoveOrResizeStarted(rvh->routing_id()));
  }

  if (bounds_.size() != bounds.size())
    OnSizeChanged(bounds.width(), bounds.height());

  // We update |bounds_| but not |restored_bounds_| here.  The latter needs
  // to be updated conditionally when the window is non-maximized and non-
  // fullscreen, but whether those state updates have been processed yet is
  // window-manager specific.  We update |restored_bounds_| in the debounced
  // handler below, after the window state has been updated.
  bounds_ = bounds;

  // The GdkEventConfigure* we get here doesn't have quite the right
  // coordinates though (they're relative to the drawable window area, rather
  // than any window manager decorations, if enabled), so we need to call
  // gtk_window_get_position() to get the right values.  (Otherwise session
  // restore, if enabled, will restore windows to incorrect positions.)  That's
  // a round trip to the X server though, so we set a debounce timer and only
  // call it (in OnDebouncedBoundsChanged() below) after we haven't seen a
  // reconfigure event in a short while.
  // We don't use Reset() because the timer may not yet be running.
  // (In that case Stop() is a no-op.)
  if (!debounce_timer_disabled_) {
    window_configure_debounce_timer_.Stop();
    window_configure_debounce_timer_.Start(base::TimeDelta::FromMilliseconds(
        kDebounceTimeoutMilliseconds), this,
        &BrowserWindowGtk::OnDebouncedBoundsChanged);
  }

  return FALSE;
}

void BrowserWindowGtk::OnDebouncedBoundsChanged() {
  gint x, y;
  gtk_window_get_position(window_, &x, &y);
  gfx::Point origin(x, y);
  bounds_.set_origin(origin);
  if (!IsFullscreen() && !IsMaximized())
    restored_bounds_ = bounds_;
  SaveWindowPosition();
}

gboolean BrowserWindowGtk::OnWindowState(GtkWidget* sender,
                                         GdkEventWindowState* event) {
  state_ = event->new_window_state;

  if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
    bool is_fullscreen = state_ & GDK_WINDOW_STATE_FULLSCREEN;
    browser_->UpdateCommandsForFullscreenMode(is_fullscreen);
    if (is_fullscreen) {
      UpdateCustomFrame();
      toolbar_->Hide();
      tabstrip_->Hide();
      if (IsBookmarkBarSupported())
        bookmark_bar_->EnterFullscreen();
      bool is_kiosk =
          CommandLine::ForCurrentProcess()->HasSwitch(switches::kKioskMode);
      if (!is_kiosk) {
        fullscreen_exit_bubble_.reset(new FullscreenExitBubbleGtk(
            GTK_FLOATING_CONTAINER(render_area_floating_container_)));
      }
      gtk_widget_hide(toolbar_border_);
    } else {
      fullscreen_exit_bubble_.reset();
      UpdateCustomFrame();
      ShowSupportedWindowFeatures();
    }
  }

  titlebar_->UpdateCustomFrame(UseCustomFrame() && !IsFullscreen());
  UpdateWindowShape(bounds_.width(), bounds_.height());
  SaveWindowPosition();
  return FALSE;
}

// Callback for the delete event.  This event is fired when the user tries to
// close the window (e.g., clicking on the X in the window manager title bar).
gboolean BrowserWindowGtk::OnMainWindowDeleteEvent(GtkWidget* widget,
                                                   GdkEvent* event) {
  Close();

  // Return true to prevent the gtk window from being destroyed.  Close will
  // destroy it for us.
  return TRUE;
}

void BrowserWindowGtk::OnMainWindowDestroy(GtkWidget* widget) {
  // BUG 8712. When we gtk_widget_destroy() in Close(), this will emit the
  // signal right away, and we will be here (while Close() is still in the
  // call stack).  In order to not reenter Close(), and to also follow the
  // expectations of BrowserList, we should run the BrowserWindowGtk destructor
  // not now, but after the run loop goes back to process messages.  Otherwise
  // we will remove ourself from BrowserList while it's being iterated.
  // Additionally, now that we know the window is gone, we need to make sure to
  // set window_ to NULL, otherwise we will try to close the window again when
  // we call Close() in the destructor.
  //
  // We don't want to use DeleteSoon() here since it won't work on a nested pump
  // (like in UI tests).
  MessageLoop::current()->PostTask(FROM_HERE,
                                   new DeleteTask<BrowserWindowGtk>(this));
}

void BrowserWindowGtk::UnMaximize() {
  gtk_window_unmaximize(window_);

  // It can happen that you end up with a window whose restore size is the same
  // as the size of the screen, so unmaximizing it merely remaximizes it due to
  // the same WM feature that SetWindowSize() works around.  We try to detect
  // this and resize the window to work around the issue.
  if (bounds_.size() == restored_bounds_.size())
    gtk_window_resize(window_, bounds_.width(), bounds_.height() - 1);
}

bool BrowserWindowGtk::CanClose() const {
  // You cannot close a frame for which there is an active originating drag
  // session.
  if (tabstrip_->IsDragSessionActive())
    return false;

  // Give beforeunload handlers the chance to cancel the close before we hide
  // the window below.
  if (!browser_->ShouldCloseWindow())
    return false;

  if (!browser_->tabstrip_model()->empty()) {
    // Tab strip isn't empty.  Hide the window (so it appears to have closed
    // immediately) and close all the tabs, allowing the renderers to shut
    // down. When the tab strip is empty we'll be called back again.
    gtk_widget_hide(GTK_WIDGET(window_));
    browser_->OnWindowClosing();
    return false;
  }

  // Empty TabStripModel, it's now safe to allow the Window to be closed.
  NotificationService::current()->Notify(
      NotificationType::WINDOW_CLOSED,
      Source<GtkWindow>(window_),
      NotificationService::NoDetails());
  return true;
}

bool BrowserWindowGtk::ShouldShowWindowIcon() const {
  return browser_->SupportsWindowFeature(Browser::FEATURE_TITLEBAR);
}

void BrowserWindowGtk::DisableDebounceTimerForTests(bool is_disabled) {
  debounce_timer_disabled_ = is_disabled;
  if (is_disabled)
    window_configure_debounce_timer_.Stop();
}

void BrowserWindowGtk::AddFindBar(FindBarGtk* findbar) {
  gtk_floating_container_add_floating(
      GTK_FLOATING_CONTAINER(render_area_floating_container_),
      findbar->widget());
}

void BrowserWindowGtk::ResetCustomFrameCursor() {
  if (!frame_cursor_)
    return;

  frame_cursor_ = NULL;
  gdk_window_set_cursor(GTK_WIDGET(window_)->window, NULL);
}

// static
BrowserWindowGtk* BrowserWindowGtk::GetBrowserWindowForNativeWindow(
    gfx::NativeWindow window) {
  if (window) {
    return static_cast<BrowserWindowGtk*>(
        g_object_get_qdata(G_OBJECT(window), GetBrowserWindowQuarkKey()));
  }

  return NULL;
}

// static
GtkWindow* BrowserWindowGtk::GetBrowserWindowForXID(XID xid) {
  std::map<XID, GtkWindow*>::iterator iter =
      BrowserWindowGtk::xid_map_.find(xid);
  return (iter != BrowserWindowGtk::xid_map_.end()) ? iter->second : NULL;
}

// static
void BrowserWindowGtk::RegisterUserPrefs(PrefService* prefs) {
  bool custom_frame_default = false;
  // Avoid checking the window manager if we're not connected to an X server (as
  // is the case in Valgrind tests).
  if (ui::XDisplayExists() &&
      !prefs->HasPrefPath(prefs::kUseCustomChromeFrame)) {
    custom_frame_default = GetCustomFramePrefDefault();
  }
  prefs->RegisterBooleanPref(prefs::kUseCustomChromeFrame,
                             custom_frame_default,
                             PrefService::SYNCABLE_PREF);
}

void BrowserWindowGtk::BookmarkBarIsFloating(bool is_floating) {
  bookmark_bar_is_floating_ = is_floating;
  toolbar_->UpdateForBookmarkBarVisibility(is_floating);

  // This can be NULL during initialization of the bookmark bar.
  if (bookmark_bar_.get())
    PlaceBookmarkBar(is_floating);
}

TabContents* BrowserWindowGtk::GetDisplayedTabContents() {
  return contents_container_->GetVisibleTabContents();
}

void BrowserWindowGtk::QueueToolbarRedraw() {
  gtk_widget_queue_draw(toolbar_->widget());
}

void BrowserWindowGtk::SetGeometryHints() {
  // If we call gtk_window_maximize followed by gtk_window_present, compiz gets
  // confused and maximizes the window, but doesn't set the
  // GDK_WINDOW_STATE_MAXIMIZED bit.  So instead, we keep track of whether to
  // maximize and call it after gtk_window_present.
  maximize_after_show_ = browser_->GetSavedMaximizedState();

  gfx::Rect bounds = browser_->GetSavedWindowBounds();
  // We don't blindly call SetBounds here: that sets a forced position
  // on the window and we intentionally *don't* do that for normal
  // windows.  Most programs do not restore their window position on
  // Linux, instead letting the window manager choose a position.
  //
  // However, in cases like dropping a tab where the bounds are
  // specifically set, we do want to position explicitly.  We also
  // force the position as part of session restore, as applications
  // that restore other, similar state (for instance GIMP, audacity,
  // pidgin, dia, and gkrellm) do tend to restore their positions.
  //
  // For popup windows, we assume that if x == y == 0, the opening page
  // did not specify a position.  Let the WM position the popup instead.
  bool is_popup_or_panel = browser_->is_type_popup() ||
                           browser_->is_type_panel();
  bool popup_without_position = is_popup_or_panel &&
      bounds.x() == 0 && bounds.y() == 0;
  bool move = browser_->bounds_overridden() && !popup_without_position;
  SetBoundsImpl(bounds, !is_popup_or_panel, move);
}

void BrowserWindowGtk::ConnectHandlersToSignals() {
  g_signal_connect(window_, "delete-event",
                   G_CALLBACK(OnMainWindowDeleteEventThunk), this);
  g_signal_connect(window_, "destroy",
                   G_CALLBACK(OnMainWindowDestroyThunk), this);
  g_signal_connect(window_, "configure-event",
                   G_CALLBACK(OnConfigureThunk), this);
  g_signal_connect(window_, "window-state-event",
                   G_CALLBACK(OnWindowStateThunk), this);
  g_signal_connect(window_, "map",
                   G_CALLBACK(MainWindowMapped), NULL);
  g_signal_connect(window_, "unmap",
                   G_CALLBACK(MainWindowUnMapped), NULL);
  g_signal_connect(window_, "key-press-event",
                   G_CALLBACK(OnKeyPressThunk), this);
  g_signal_connect(window_, "motion-notify-event",
                   G_CALLBACK(OnMouseMoveEventThunk), this);
  g_signal_connect(window_, "button-press-event",
                   G_CALLBACK(OnButtonPressEventThunk), this);
  g_signal_connect(window_, "focus-in-event",
                   G_CALLBACK(OnFocusInThunk), this);
  g_signal_connect(window_, "focus-out-event",
                   G_CALLBACK(OnFocusOutThunk), this);
}

void BrowserWindowGtk::InitWidgets() {
  ConnectHandlersToSignals();
  bounds_ = restored_bounds_ = GetInitialWindowBounds(window_);

  // This vbox encompasses all of the widgets within the browser.  This is
  // everything except the custom frame border.
  window_vbox_ = gtk_vbox_new(FALSE, 0);
  gtk_widget_show(window_vbox_);

  // We hold an always hidden GtkMenuBar inside our browser window simply to
  // fool the Unity desktop, which will mirror the contents of the first
  // GtkMenuBar it sees into the global menu bar. (It doesn't seem to check the
  // visibility of the GtkMenuBar, so we can just permanently hide it.)
  global_menu_bar_.reset(new GlobalMenuBar(browser_.get()));
  gtk_container_add(GTK_CONTAINER(window_vbox_), global_menu_bar_->widget());

  // The window container draws the custom browser frame.
  window_container_ = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  gtk_widget_set_name(window_container_, "chrome-custom-frame-border");
  gtk_widget_set_app_paintable(window_container_, TRUE);
  gtk_widget_set_double_buffered(window_container_, FALSE);
  gtk_widget_set_redraw_on_allocate(window_container_, TRUE);
  g_signal_connect(window_container_, "expose-event",
                   G_CALLBACK(OnCustomFrameExposeThunk), this);
  gtk_container_add(GTK_CONTAINER(window_container_), window_vbox_);

  tabstrip_.reset(new TabStripGtk(browser_->tabstrip_model(), this));
  tabstrip_->Init();

  // Build the titlebar (tabstrip + header space + min/max/close buttons).
  titlebar_.reset(new BrowserTitlebar(this, window_));

  // Insert the tabstrip into the window.
  gtk_box_pack_start(GTK_BOX(window_vbox_), titlebar_->widget(), FALSE, FALSE,
                     0);

  toolbar_.reset(new BrowserToolbarGtk(browser_.get(), this));
  toolbar_->Init(browser_->profile(), window_);
  gtk_box_pack_start(GTK_BOX(window_vbox_), toolbar_->widget(),
                     FALSE, FALSE, 0);
  g_signal_connect_after(toolbar_->widget(), "expose-event",
                         G_CALLBACK(OnExposeDrawInfobarBitsThunk), this);
  // This vbox surrounds the render area: find bar, info bars and render view.
  // The reason is that this area as a whole needs to be grouped in its own
  // GdkWindow hierarchy so that animations originating inside it (infobar,
  // download shelf, find bar) are all clipped to that area. This is why
  // |render_area_vbox_| is packed in |render_area_event_box_|.
  render_area_vbox_ = gtk_vbox_new(FALSE, 0);
  gtk_widget_set_name(render_area_vbox_, "chrome-render-area-vbox");
  render_area_floating_container_ = gtk_floating_container_new();
  gtk_container_add(GTK_CONTAINER(render_area_floating_container_),
                    render_area_vbox_);

  GtkWidget* location_icon = toolbar_->GetLocationBarView()->
      location_icon_widget();
  g_signal_connect(location_icon, "size-allocate",
                   G_CALLBACK(OnLocationIconSizeAllocateThunk), this);
  g_signal_connect_after(location_icon, "expose-event",
                         G_CALLBACK(OnExposeDrawInfobarBitsThunk), this);

  toolbar_border_ = gtk_event_box_new();
  gtk_box_pack_start(GTK_BOX(render_area_vbox_),
                     toolbar_border_, FALSE, FALSE, 0);
  gtk_widget_set_size_request(toolbar_border_, -1, 1);
  gtk_widget_set_no_show_all(toolbar_border_, TRUE);
  g_signal_connect_after(toolbar_border_, "expose-event",
                         G_CALLBACK(OnExposeDrawInfobarBitsThunk), this);

  if (IsToolbarSupported())
    gtk_widget_show(toolbar_border_);

  infobar_container_.reset(new InfoBarContainerGtk(browser_->profile()));
  gtk_box_pack_start(GTK_BOX(render_area_vbox_),
                     infobar_container_->widget(),
                     FALSE, FALSE, 0);

  status_bubble_.reset(new StatusBubbleGtk(browser_->profile()));

  contents_container_.reset(new TabContentsContainerGtk(status_bubble_.get()));
  devtools_container_.reset(new TabContentsContainerGtk(NULL));
  ViewIDUtil::SetID(devtools_container_->widget(), VIEW_ID_DEV_TOOLS_DOCKED);
  contents_split_ = gtk_vpaned_new();
  gtk_paned_pack1(GTK_PANED(contents_split_), contents_container_->widget(),
                  TRUE, TRUE);
  gtk_paned_pack2(GTK_PANED(contents_split_), devtools_container_->widget(),
                  FALSE, TRUE);
  gtk_box_pack_end(GTK_BOX(render_area_vbox_), contents_split_, TRUE, TRUE, 0);
  // Restore split offset.
  int split_offset = browser_->profile()->GetPrefs()->
      GetInteger(prefs::kDevToolsSplitLocation);
  if (split_offset != -1) {
    if (split_offset < kMinDevToolsHeight)
      split_offset = kMinDevToolsHeight;
    gtk_paned_set_position(GTK_PANED(contents_split_), split_offset);
  } else {
    gtk_widget_set_size_request(devtools_container_->widget(), -1,
                                kDefaultDevToolsHeight);
  }
  gtk_widget_show_all(render_area_floating_container_);
  gtk_widget_hide(devtools_container_->widget());
  render_area_event_box_ = gtk_event_box_new();
  // Set a white background so during startup the user sees white in the
  // content area before we get a TabContents in place.
  gtk_widget_modify_bg(render_area_event_box_, GTK_STATE_NORMAL,
                       &gtk_util::kGdkWhite);
  gtk_container_add(GTK_CONTAINER(render_area_event_box_),
                    render_area_floating_container_);
  gtk_widget_show(render_area_event_box_);
  gtk_box_pack_end(GTK_BOX(window_vbox_), render_area_event_box_,
                   TRUE, TRUE, 0);

  if (IsBookmarkBarSupported()) {
    bookmark_bar_.reset(new BookmarkBarGtk(this,
                                           browser_->profile(),
                                           browser_.get(),
                                           tabstrip_.get()));
    PlaceBookmarkBar(false);
    gtk_widget_show(bookmark_bar_->widget());

    g_signal_connect_after(bookmark_bar_->widget(), "expose-event",
                           G_CALLBACK(OnBookmarkBarExposeThunk), this);
    g_signal_connect(bookmark_bar_->widget(), "size-allocate",
                     G_CALLBACK(OnBookmarkBarSizeAllocateThunk), this);
  }

  // We have to realize the window before we try to apply a window shape mask.
  gtk_widget_realize(GTK_WIDGET(window_));
  state_ = gdk_window_get_state(GTK_WIDGET(window_)->window);
  // Note that calling this the first time is necessary to get the
  // proper control layout.
  UpdateCustomFrame();

  // We have to call this after the first window is created, but after that only
  // when the theme changes.
  static bool default_icon_set = false;
  if (!default_icon_set) {
    gtk_util::SetDefaultWindowIcon(window_);
    default_icon_set = true;
  }

  gtk_container_add(GTK_CONTAINER(window_), window_container_);
  gtk_widget_show(window_container_);
  browser_->tabstrip_model()->AddObserver(this);
}

void BrowserWindowGtk::SetBackgroundColor() {
  Profile* profile = browser()->profile();
  GtkThemeService* theme_provider = GtkThemeService::GetFrom(profile);
  int frame_color_id;
  if (UsingCustomPopupFrame()) {
    frame_color_id = ThemeService::COLOR_TOOLBAR;
  } else if (IsActive()) {
    frame_color_id = browser()->profile()->IsOffTheRecord()
       ? ThemeService::COLOR_FRAME_INCOGNITO
       : ThemeService::COLOR_FRAME;
  } else {
    frame_color_id = browser()->profile()->IsOffTheRecord()
       ? ThemeService::COLOR_FRAME_INCOGNITO_INACTIVE
       : ThemeService::COLOR_FRAME_INACTIVE;
  }

  SkColor frame_color = theme_provider->GetColor(frame_color_id);

  // Paint the frame color on the left, right and bottom.
  GdkColor frame_color_gdk = gfx::SkColorToGdkColor(frame_color);
  gtk_widget_modify_bg(GTK_WIDGET(window_), GTK_STATE_NORMAL,
                       &frame_color_gdk);

  // Set the color of the dev tools divider.
  gtk_widget_modify_bg(contents_split_, GTK_STATE_NORMAL, &frame_color_gdk);

  // When the cursor is over the divider, GTK+ normally lightens the background
  // color by 1.3 (see LIGHTNESS_MULT in gtkstyle.c).  Since we're setting the
  // color, override the prelight also.
  color_utils::HSL hsl = { -1, 0.5, 0.65 };
  SkColor frame_prelight_color = color_utils::HSLShift(frame_color, hsl);
  GdkColor frame_prelight_color_gdk =
      gfx::SkColorToGdkColor(frame_prelight_color);
  gtk_widget_modify_bg(contents_split_, GTK_STATE_PRELIGHT,
      &frame_prelight_color_gdk);

  GdkColor border_color = theme_provider->GetBorderColor();
  gtk_widget_modify_bg(toolbar_border_, GTK_STATE_NORMAL, &border_color);
}

void BrowserWindowGtk::OnSizeChanged(int width, int height) {
  UpdateWindowShape(width, height);
}

void BrowserWindowGtk::UpdateWindowShape(int width, int height) {
  if (UseCustomFrame() && !IsFullscreen() && !IsMaximized()) {
    // Make the corners rounded.  We set a mask that includes most of the
    // window except for a few pixels in each corner.
    GdkRectangle top_top_rect = { 3, 0, width - 6, 1 };
    GdkRectangle top_mid_rect = { 1, 1, width - 2, 2 };
    GdkRectangle mid_rect = { 0, 3, width, height - 6 };
    // The bottom two rects are mirror images of the top two rects.
    GdkRectangle bot_mid_rect = top_mid_rect;
    bot_mid_rect.y = height - 3;
    GdkRectangle bot_bot_rect = top_top_rect;
    bot_bot_rect.y = height - 1;
    GdkRegion* mask = gdk_region_rectangle(&top_top_rect);
    gdk_region_union_with_rect(mask, &top_mid_rect);
    gdk_region_union_with_rect(mask, &mid_rect);
    gdk_region_union_with_rect(mask, &bot_mid_rect);
    gdk_region_union_with_rect(mask, &bot_bot_rect);
    gdk_window_shape_combine_region(GTK_WIDGET(window_)->window, mask, 0, 0);
    gdk_region_destroy(mask);
    gtk_alignment_set_padding(GTK_ALIGNMENT(window_container_), 1,
        kFrameBorderThickness, kFrameBorderThickness, kFrameBorderThickness);
  } else {
    // XFCE disables the system decorations if there's an xshape set.
    if (UseCustomFrame()) {
      // Disable rounded corners.  Simply passing in a NULL region doesn't
      // seem to work on KWin, so manually set the shape to the whole window.
      GdkRectangle rect = { 0, 0, width, height };
      GdkRegion* mask = gdk_region_rectangle(&rect);
      gdk_window_shape_combine_region(GTK_WIDGET(window_)->window, mask, 0, 0);
      gdk_region_destroy(mask);
    } else {
      gdk_window_shape_combine_region(GTK_WIDGET(window_)->window, NULL, 0, 0);
    }
    gtk_alignment_set_padding(GTK_ALIGNMENT(window_container_), 0, 0, 0, 0);
  }
}

void BrowserWindowGtk::ConnectAccelerators() {
  accel_group_ = gtk_accel_group_new();
  gtk_window_add_accel_group(window_, accel_group_);

  AcceleratorsGtk* accelerators = AcceleratorsGtk::GetInstance();
  for (AcceleratorsGtk::const_iterator iter = accelerators->begin();
       iter != accelerators->end(); ++iter) {
    gtk_accel_group_connect(
        accel_group_,
        iter->second.GetGdkKeyCode(),
        static_cast<GdkModifierType>(iter->second.modifiers()),
        GtkAccelFlags(0),
        g_cclosure_new(G_CALLBACK(OnGtkAccelerator),
                       GINT_TO_POINTER(iter->first), NULL));
  }
}

void BrowserWindowGtk::UpdateCustomFrame() {
  gtk_window_set_decorated(window_, !UseCustomFrame());
  titlebar_->UpdateCustomFrame(UseCustomFrame() && !IsFullscreen());
  UpdateWindowShape(bounds_.width(), bounds_.height());
}

void BrowserWindowGtk::SaveWindowPosition() {
  // Browser::SaveWindowPlacement is used for session restore.
  if (browser_->ShouldSaveWindowPlacement())
    browser_->SaveWindowPlacement(restored_bounds_, IsMaximized());

  // We also need to save the placement for startup.
  // This is a web of calls between views and delegates on Windows, but the
  // crux of the logic follows.  See also cocoa/browser_window_controller.mm.
  if (!browser_->profile()->GetPrefs())
    return;

  std::string window_name = browser_->GetWindowPlacementKey();
  DictionaryPrefUpdate update(browser_->profile()->GetPrefs(),
                              window_name.c_str());
  DictionaryValue* window_preferences = update.Get();
  // Note that we store left/top for consistency with Windows, but that we
  // *don't* obey them; we only use them for computing width/height.  See
  // comments in SetGeometryHints().
  window_preferences->SetInteger("left", restored_bounds_.x());
  window_preferences->SetInteger("top", restored_bounds_.y());
  window_preferences->SetInteger("right", restored_bounds_.right());
  window_preferences->SetInteger("bottom", restored_bounds_.bottom());
  window_preferences->SetBoolean("maximized", IsMaximized());

  scoped_ptr<WindowSizer::MonitorInfoProvider> monitor_info_provider(
      WindowSizer::CreateDefaultMonitorInfoProvider());
  gfx::Rect work_area(
      monitor_info_provider->GetMonitorWorkAreaMatching(restored_bounds_));
  window_preferences->SetInteger("work_area_left", work_area.x());
  window_preferences->SetInteger("work_area_top", work_area.y());
  window_preferences->SetInteger("work_area_right", work_area.right());
  window_preferences->SetInteger("work_area_bottom", work_area.bottom());
}

void BrowserWindowGtk::SetInfoBarShowing(InfoBar* bar, bool animate) {
  infobar_arrow_model_.ShowArrowFor(bar, animate);
}

void BrowserWindowGtk::PaintStateChanged() {
  InvalidateInfoBarBits();
}

void BrowserWindowGtk::InvalidateInfoBarBits() {
  gtk_widget_queue_draw(toolbar_border_);
  gtk_widget_queue_draw(toolbar_->widget());
  if (bookmark_bar_.get() && !bookmark_bar_is_floating_)
    gtk_widget_queue_draw(bookmark_bar_->widget());
}

int BrowserWindowGtk::GetXPositionOfLocationIcon(GtkWidget* relative_to) {
  GtkWidget* location_icon = toolbar_->GetLocationBarView()->
      location_icon_widget();
  int x = 0;
  gtk_widget_translate_coordinates(
      location_icon, relative_to,
      (location_icon->allocation.width + 1) / 2,
      0, &x, NULL);

  if (GTK_WIDGET_NO_WINDOW(relative_to))
    x += relative_to->allocation.x;

  return x;
}

void BrowserWindowGtk::OnLocationIconSizeAllocate(GtkWidget* sender,
                                                  GtkAllocation* allocation) {
  // The position of the arrow may have changed, so we'll have to redraw it.
  InvalidateInfoBarBits();
}

gboolean BrowserWindowGtk::OnExposeDrawInfobarBits(GtkWidget* sender,
                                                   GdkEventExpose* expose) {
  if (!infobar_arrow_model_.NeedToDrawInfoBarArrow())
    return FALSE;

  int x = GetXPositionOfLocationIcon(sender);

  gfx::Rect toolbar_border(toolbar_border_->allocation);
  int y = 0;
  gtk_widget_translate_coordinates(toolbar_border_, sender,
                                   0, toolbar_border.bottom(),
                                   NULL, &y);
  if (GTK_WIDGET_NO_WINDOW(sender))
    y += sender->allocation.y;

  // x, y is the bottom middle of the arrow. Now we need to create the bounding
  // rectangle.
  gfx::Size arrow_size = GetInfobarArrowSize();
  gfx::Rect bounds(
      gfx::Point(x - arrow_size.width() / 2.0, y - arrow_size.height()),
      arrow_size);

  Profile* profile = browser()->profile();
  infobar_arrow_model_.Paint(
      sender, expose, bounds,
      GtkThemeService::GetFrom(profile)->GetBorderColor());
  return FALSE;
}

gfx::Size BrowserWindowGtk::GetInfobarArrowSize() {
  static const size_t kDefaultWidth =
      2 * InfoBarArrowModel::kDefaultArrowSize;
  static const size_t kDefaultHeight = InfoBarArrowModel::kDefaultArrowSize;
  static const size_t kMaxWidth = 30;
  static const size_t kMaxHeight = 24;

  double progress = bookmark_bar_.get() && !bookmark_bar_is_floating_ ?
      bookmark_bar_->animation()->GetCurrentValue() : 0.0;
  size_t width = kDefaultWidth + (kMaxWidth - kDefaultWidth) * progress;
  size_t height = kDefaultHeight + (kMaxHeight - kDefaultHeight) * progress;

  return gfx::Size(width, height);
}

gboolean BrowserWindowGtk::OnBookmarkBarExpose(GtkWidget* sender,
                                               GdkEventExpose* expose) {
  if (!infobar_arrow_model_.NeedToDrawInfoBarArrow())
    return FALSE;

  if (bookmark_bar_is_floating_)
    return FALSE;

  return OnExposeDrawInfobarBits(sender, expose);
}

void BrowserWindowGtk::OnBookmarkBarSizeAllocate(GtkWidget* sender,
                                                 GtkAllocation* allocation) {
  // The size of the bookmark bar affects how the infobar arrow is drawn on
  // the toolbar.
  if (infobar_arrow_model_.NeedToDrawInfoBarArrow())
    InvalidateInfoBarBits();
}

// static
gboolean BrowserWindowGtk::OnGtkAccelerator(GtkAccelGroup* accel_group,
                                            GObject* acceleratable,
                                            guint keyval,
                                            GdkModifierType modifier,
                                            void* user_data) {
  int command_id = GPOINTER_TO_INT(user_data);
  BrowserWindowGtk* browser_window =
      GetBrowserWindowForNativeWindow(GTK_WINDOW(acceleratable));
  DCHECK(browser_window != NULL);
  return browser_window->browser()->ExecuteCommandIfEnabled(command_id);
}

// Let the focused widget have first crack at the key event so we don't
// override their accelerators.
gboolean BrowserWindowGtk::OnKeyPress(GtkWidget* widget, GdkEventKey* event) {
  // If a widget besides the native view is focused, we have to try to handle
  // the custom accelerators before letting it handle them.
  TabContents* current_tab_contents =
      browser()->GetSelectedTabContents();
  // The current tab might not have a render view if it crashed.
  if (!current_tab_contents || !current_tab_contents->GetContentNativeView() ||
      !gtk_widget_is_focus(current_tab_contents->GetContentNativeView())) {
    int command_id = GetCustomCommandId(event);
    if (command_id == -1)
      command_id = GetPreHandleCommandId(event);

    if (command_id != -1 && browser_->ExecuteCommandIfEnabled(command_id))
      return TRUE;

    // Propagate the key event to child widget first, so we don't override their
    // accelerators.
    if (!gtk_window_propagate_key_event(GTK_WINDOW(widget), event)) {
      if (!gtk_window_activate_key(GTK_WINDOW(widget), event)) {
        gtk_bindings_activate_event(GTK_OBJECT(widget), event);
      }
    }
  } else {
    bool rv = gtk_window_propagate_key_event(GTK_WINDOW(widget), event);
    DCHECK(rv);
  }

  // Prevents the default handler from handling this event.
  return TRUE;
}

gboolean BrowserWindowGtk::OnMouseMoveEvent(GtkWidget* widget,
                                            GdkEventMotion* event) {
  // This method is used to update the mouse cursor when over the edge of the
  // custom frame.  If the custom frame is off or we're over some other widget,
  // do nothing.
  if (!UseCustomFrame() || event->window != widget->window) {
    // Reset the cursor.
    if (frame_cursor_) {
      frame_cursor_ = NULL;
      gdk_window_set_cursor(GTK_WIDGET(window_)->window, NULL);
    }
    return FALSE;
  }

  // Update the cursor if we're on the custom frame border.
  GdkWindowEdge edge;
  bool has_hit_edge = GetWindowEdge(static_cast<int>(event->x),
                                    static_cast<int>(event->y), &edge);
  GdkCursorType new_cursor = GDK_LAST_CURSOR;
  if (has_hit_edge)
    new_cursor = GdkWindowEdgeToGdkCursorType(edge);

  GdkCursorType last_cursor = GDK_LAST_CURSOR;
  if (frame_cursor_)
    last_cursor = frame_cursor_->type;

  if (last_cursor != new_cursor) {
    if (has_hit_edge) {
      frame_cursor_ = gfx::GetCursor(new_cursor);
    } else {
      frame_cursor_ = NULL;
    }
    gdk_window_set_cursor(GTK_WIDGET(window_)->window, frame_cursor_);
  }
  return FALSE;
}

gboolean BrowserWindowGtk::OnButtonPressEvent(GtkWidget* widget,
                                              GdkEventButton* event) {
  // Handle back/forward.
  if (event->type == GDK_BUTTON_PRESS) {
    if (event->button == 8) {
      browser_->GoBack(CURRENT_TAB);
      return TRUE;
    } else if (event->button == 9) {
      browser_->GoForward(CURRENT_TAB);
      return TRUE;
    }
  }

  // Handle left, middle and right clicks.  In particular, we care about clicks
  // in the custom frame border and clicks in the titlebar.

  // Make the button press coordinate relative to the browser window.
  int win_x, win_y;
  gdk_window_get_origin(GTK_WIDGET(window_)->window, &win_x, &win_y);

  GdkWindowEdge edge;
  gfx::Point point(static_cast<int>(event->x_root - win_x),
                   static_cast<int>(event->y_root - win_y));
  bool has_hit_edge = GetWindowEdge(point.x(), point.y(), &edge);

  // Ignore clicks that are in/below the browser toolbar.
  GtkWidget* toolbar = toolbar_->widget();
  if (!GTK_WIDGET_VISIBLE(toolbar)) {
    // If the toolbar is not showing, use the location of web contents as the
    // boundary of where to ignore clicks.
    toolbar = render_area_vbox_;
  }
  gint toolbar_y;
  gtk_widget_get_pointer(toolbar, NULL, &toolbar_y);
  bool has_hit_titlebar = !IsFullscreen() && (toolbar_y < 0)
                          && !has_hit_edge;
  if (event->button == 1) {
    if (GDK_BUTTON_PRESS == event->type) {
      guint32 last_click_time = last_click_time_;
      gfx::Point last_click_position = last_click_position_;
      last_click_time_ = event->time;
      last_click_position_ = gfx::Point(static_cast<int>(event->x),
                                        static_cast<int>(event->y));

      // Raise the window after a click on either the titlebar or the border to
      // match the behavior of most window managers, unless that behavior has
      // been suppressed.
      if ((has_hit_titlebar || has_hit_edge) && !suppress_window_raise_)
        gdk_window_raise(GTK_WIDGET(window_)->window);

      if (has_hit_titlebar) {
        return HandleTitleBarLeftMousePress(
            event, last_click_time, last_click_position);
      } else if (has_hit_edge) {
        gtk_window_begin_resize_drag(window_, edge, event->button,
                                     static_cast<gint>(event->x_root),
                                     static_cast<gint>(event->y_root),
                                     event->time);
        return TRUE;
      }
    } else if (GDK_2BUTTON_PRESS == event->type) {
      if (has_hit_titlebar) {
        // Maximize/restore on double click.
        if (IsMaximized()) {
          UnMaximize();
        } else {
          gtk_window_maximize(window_);
        }
        return TRUE;
      }
    }
  } else if (event->button == 2) {
    if (has_hit_titlebar || has_hit_edge) {
      gdk_window_lower(GTK_WIDGET(window_)->window);
    }
    return TRUE;
  } else if (event->button == 3) {
    if (has_hit_titlebar) {
      titlebar_->ShowContextMenu(event);
      return TRUE;
    }
  }

  return FALSE;  // Continue to propagate the event.
}

bool BrowserWindowGtk::HandleTitleBarLeftMousePress(
    GdkEventButton* event,
    guint32 last_click_time,
    gfx::Point last_click_position) {
  // We want to start a move when the user single clicks, but not start a
  // move when the user double clicks.  However, a double click sends the
  // following GDK events: GDK_BUTTON_PRESS, GDK_BUTTON_RELEASE,
  // GDK_BUTTON_PRESS, GDK_2BUTTON_PRESS, GDK_BUTTON_RELEASE.  If we
  // start a gtk_window_begin_move_drag on the second GDK_BUTTON_PRESS,
  // the call to gtk_window_maximize fails.  To work around this, we
  // keep track of the last click and if it's going to be a double click,
  // we don't call gtk_window_begin_move_drag.
  static GtkSettings* settings = gtk_settings_get_default();
  gint double_click_time = 250;
  gint double_click_distance = 5;
  g_object_get(G_OBJECT(settings),
               "gtk-double-click-time", &double_click_time,
               "gtk-double-click-distance", &double_click_distance,
               NULL);

  guint32 click_time = event->time - last_click_time;
  int click_move_x = abs(event->x - last_click_position.x());
  int click_move_y = abs(event->y - last_click_position.y());

  if (click_time > static_cast<guint32>(double_click_time) ||
      click_move_x > double_click_distance ||
      click_move_y > double_click_distance) {
    // Ignore drag requests if the window is the size of the screen.
    // We do this to avoid triggering fullscreen mode in metacity
    // (without the --no-force-fullscreen flag) and in compiz (with
    // Legacy Fullscreen Mode enabled).
    if (!BoundsMatchMonitorSize()) {
      gtk_window_begin_move_drag(window_, event->button,
                                 static_cast<gint>(event->x_root),
                                 static_cast<gint>(event->y_root),
                                 event->time);
    }
    return TRUE;
  }
  return FALSE;
}

// static
void BrowserWindowGtk::MainWindowMapped(GtkWidget* widget) {
  // Map the X Window ID of the window to our window.
  XID xid = ui::GetX11WindowFromGtkWidget(widget);
  BrowserWindowGtk::xid_map_.insert(
      std::pair<XID, GtkWindow*>(xid, GTK_WINDOW(widget)));
}

// static
void BrowserWindowGtk::MainWindowUnMapped(GtkWidget* widget) {
  // Unmap the X Window ID.
  XID xid = ui::GetX11WindowFromGtkWidget(widget);
  BrowserWindowGtk::xid_map_.erase(xid);
}

gboolean BrowserWindowGtk::OnFocusIn(GtkWidget* widget,
                                     GdkEventFocus* event) {
  BrowserList::SetLastActive(browser_.get());
  return FALSE;
}

gboolean BrowserWindowGtk::OnFocusOut(GtkWidget* widget,
                                      GdkEventFocus* event) {
  return FALSE;
}

void BrowserWindowGtk::ShowSupportedWindowFeatures() {
  if (IsTabStripSupported())
    tabstrip_->Show();

  if (IsToolbarSupported()) {
    toolbar_->Show();
    gtk_widget_show(toolbar_border_);
    gdk_window_lower(toolbar_border_->window);
  }

  if (IsBookmarkBarSupported())
    MaybeShowBookmarkBar(false);
}

void BrowserWindowGtk::HideUnsupportedWindowFeatures() {
  if (!IsTabStripSupported())
    tabstrip_->Hide();

  if (!IsToolbarSupported())
    toolbar_->Hide();

  // If the bookmark bar shelf is unsupported, then we never create it.
}

bool BrowserWindowGtk::IsTabStripSupported() const {
  return browser_->SupportsWindowFeature(Browser::FEATURE_TABSTRIP);
}

bool BrowserWindowGtk::IsToolbarSupported() const {
  return browser_->SupportsWindowFeature(Browser::FEATURE_TOOLBAR) ||
         browser_->SupportsWindowFeature(Browser::FEATURE_LOCATIONBAR);
}

bool BrowserWindowGtk::IsBookmarkBarSupported() const {
  return browser_->SupportsWindowFeature(Browser::FEATURE_BOOKMARKBAR);
}

bool BrowserWindowGtk::UsingCustomPopupFrame() const {
  GtkThemeService* theme_provider = GtkThemeService::GetFrom(
      browser()->profile());
  return !theme_provider->UsingNativeTheme() &&
         (browser()->is_type_popup() || browser()->is_type_panel());
}

bool BrowserWindowGtk::GetWindowEdge(int x, int y, GdkWindowEdge* edge) {
  if (!UseCustomFrame())
    return false;

  if (IsMaximized() || IsFullscreen())
    return false;

  if (x < kFrameBorderThickness) {
    // Left edge.
    if (y < kResizeAreaCornerSize - kTopResizeAdjust) {
      *edge = GDK_WINDOW_EDGE_NORTH_WEST;
    } else if (y < bounds_.height() - kResizeAreaCornerSize) {
      *edge = GDK_WINDOW_EDGE_WEST;
    } else {
      *edge = GDK_WINDOW_EDGE_SOUTH_WEST;
    }
    return true;
  } else if (x < bounds_.width() - kFrameBorderThickness) {
    if (y < kFrameBorderThickness - kTopResizeAdjust) {
      // Top edge.
      if (x < kResizeAreaCornerSize) {
        *edge = GDK_WINDOW_EDGE_NORTH_WEST;
      } else if (x < bounds_.width() - kResizeAreaCornerSize) {
        *edge = GDK_WINDOW_EDGE_NORTH;
      } else {
        *edge = GDK_WINDOW_EDGE_NORTH_EAST;
      }
    } else if (y < bounds_.height() - kFrameBorderThickness) {
      // Ignore the middle content area.
      return false;
    } else {
      // Bottom edge.
      if (x < kResizeAreaCornerSize) {
        *edge = GDK_WINDOW_EDGE_SOUTH_WEST;
      } else if (x < bounds_.width() - kResizeAreaCornerSize) {
        *edge = GDK_WINDOW_EDGE_SOUTH;
      } else {
        *edge = GDK_WINDOW_EDGE_SOUTH_EAST;
      }
    }
    return true;
  } else {
    // Right edge.
    if (y < kResizeAreaCornerSize - kTopResizeAdjust) {
      *edge = GDK_WINDOW_EDGE_NORTH_EAST;
    } else if (y < bounds_.height() - kResizeAreaCornerSize) {
      *edge = GDK_WINDOW_EDGE_EAST;
    } else {
      *edge = GDK_WINDOW_EDGE_SOUTH_EAST;
    }
    return true;
  }

  NOTREACHED();
  return false;
}

bool BrowserWindowGtk::UseCustomFrame() {
  // We don't use the custom frame for app mode windows or app window popups.
  return use_custom_frame_pref_.GetValue() && !browser_->is_app();
}

bool BrowserWindowGtk::BoundsMatchMonitorSize() {
  // A screen can be composed of multiple monitors.
  GdkScreen* screen = gtk_window_get_screen(window_);
  gint monitor_num = gdk_screen_get_monitor_at_window(screen,
      GTK_WIDGET(window_)->window);

  GdkRectangle monitor_size;
  gdk_screen_get_monitor_geometry(screen, monitor_num, &monitor_size);
  return bounds_.size() == gfx::Size(monitor_size.width, monitor_size.height);
}

void BrowserWindowGtk::PlaceBookmarkBar(bool is_floating) {
  GtkWidget* parent = gtk_widget_get_parent(bookmark_bar_->widget());
  if (parent)
    gtk_container_remove(GTK_CONTAINER(parent), bookmark_bar_->widget());

  if (!is_floating) {
    // Place the bookmark bar at the end of |window_vbox_|; this happens after
    // we have placed the render area at the end of |window_vbox_| so we will
    // be above the render area.
    gtk_box_pack_end(GTK_BOX(window_vbox_), bookmark_bar_->widget(),
                     FALSE, FALSE, 0);
  } else {
    // Place the bookmark bar at the end of the render area; this happens after
    // the tab contents container has been placed there so we will be
    // above the webpage (in terms of y).
    gtk_box_pack_end(GTK_BOX(render_area_vbox_), bookmark_bar_->widget(),
                     FALSE, FALSE, 0);
  }
}

// static
bool BrowserWindowGtk::GetCustomFramePrefDefault() {
  std::string wm_name;
  if (!ui::GetWindowManagerName(&wm_name))
    return false;

  // Ideally, we'd use the custom frame by default and just fall back on using
  // system decorations for the few (?) tiling window managers where the custom
  // frame doesn't make sense (e.g. awesome, ion3, ratpoison, xmonad, etc.) or
  // other WMs where it has issues (e.g. Fluxbox -- see issue 19130).  The EWMH
  // _NET_SUPPORTING_WM property makes it easy to look up a name for the current
  // WM, but at least some of the WMs in the latter group don't set it.
  // Instead, we default to using system decorations for all WMs and
  // special-case the ones where the custom frame should be used.  These names
  // are taken from the WMs' source code.
  return (wm_name == "Blackbox" ||
          wm_name == "compiz" ||
          wm_name == "Compiz" ||
          wm_name == "e16" ||  // Enlightenment DR16
          wm_name == "Metacity" ||
          wm_name == "Mutter" ||
          wm_name == "Openbox" ||
          wm_name == "Xfwm4");
}
