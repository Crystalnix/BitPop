// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/bubble/bubble_gtk.h"

#include <gdk/gdkkeysyms.h>

#include "base/logging.h"
#include "chrome/browser/ui/gtk/bubble/bubble_accelerators_gtk.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_source.h"
#include "ui/base/gtk/gtk_compat.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/gtk/gtk_windowing.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/path.h"
#include "ui/gfx/rect.h"

namespace {

// The height of the arrow, and the width will be about twice the height.
const int kArrowSize = 8;

// Number of pixels to the middle of the arrow from the close edge of the
// window.
const int kArrowX = 18;

// Number of pixels between the tip of the arrow and the region we're
// pointing to.
const int kArrowToContentPadding = -4;

// We draw flat diagonal corners, each corner is an NxN square.
const int kCornerSize = 3;

// Margins around the content.
const int kTopMargin = kArrowSize + kCornerSize - 1;
const int kBottomMargin = kCornerSize - 1;
const int kLeftMargin = kCornerSize - 1;
const int kRightMargin = kCornerSize - 1;

const GdkColor kBackgroundColor = GDK_COLOR_RGB(0xff, 0xff, 0xff);
const GdkColor kFrameColor = GDK_COLOR_RGB(0x63, 0x63, 0x63);

// Helper functions that encapsulate arrow locations.
bool HasArrow(BubbleGtk::ArrowLocationGtk location) {
  return location != BubbleGtk::ARROW_LOCATION_NONE &&
      location != BubbleGtk::ARROW_LOCATION_FLOAT;
}

bool IsArrowLeft(BubbleGtk::ArrowLocationGtk location) {
  return location == BubbleGtk::ARROW_LOCATION_TOP_LEFT ||
      location == BubbleGtk::ARROW_LOCATION_BOTTOM_LEFT;
}

bool IsArrowMiddle(BubbleGtk::ArrowLocationGtk location) {
  return location == BubbleGtk::ARROW_LOCATION_TOP_MIDDLE ||
      location == BubbleGtk::ARROW_LOCATION_BOTTOM_MIDDLE;
}

bool IsArrowRight(BubbleGtk::ArrowLocationGtk location) {
  return location == BubbleGtk::ARROW_LOCATION_TOP_RIGHT ||
      location == BubbleGtk::ARROW_LOCATION_BOTTOM_RIGHT;
}

bool IsArrowTop(BubbleGtk::ArrowLocationGtk location) {
  return location == BubbleGtk::ARROW_LOCATION_TOP_LEFT ||
      location == BubbleGtk::ARROW_LOCATION_TOP_MIDDLE ||
      location == BubbleGtk::ARROW_LOCATION_TOP_RIGHT;
}

bool IsArrowBottom(BubbleGtk::ArrowLocationGtk location) {
  return location == BubbleGtk::ARROW_LOCATION_BOTTOM_LEFT ||
      location == BubbleGtk::ARROW_LOCATION_BOTTOM_MIDDLE ||
      location == BubbleGtk::ARROW_LOCATION_BOTTOM_RIGHT;
}

}  // namespace

// static
BubbleGtk* BubbleGtk::Show(GtkWidget* anchor_widget,
                           const gfx::Rect* rect,
                           GtkWidget* content,
                           ArrowLocationGtk arrow_location,
                           int attribute_flags,
                           GtkThemeService* provider,
                           BubbleDelegateGtk* delegate) {
  BubbleGtk* bubble = new BubbleGtk(provider, attribute_flags);
  bubble->Init(anchor_widget, rect, content, arrow_location, attribute_flags);
  bubble->set_delegate(delegate);
  return bubble;
}

BubbleGtk::BubbleGtk(GtkThemeService* provider, int attribute_flags)
    : delegate_(NULL),
      window_(NULL),
      theme_service_(provider),
      accel_group_(gtk_accel_group_new()),
      toplevel_window_(NULL),
      anchor_widget_(NULL),
      mask_region_(NULL),
      preferred_arrow_location_(ARROW_LOCATION_TOP_LEFT),
      current_arrow_location_(ARROW_LOCATION_TOP_LEFT),
      match_system_theme_(attribute_flags & MATCH_SYSTEM_THEME),
      grab_input_(true),
      closed_by_escape_(false) {
}

BubbleGtk::~BubbleGtk() {
  // Notify the delegate that we're about to close.  This gives the chance
  // to save state / etc from the hosted widget before it's destroyed.
  if (delegate_)
    delegate_->BubbleClosing(this, closed_by_escape_);

  g_object_unref(accel_group_);
  if (mask_region_)
    gdk_region_destroy(mask_region_);
}

void BubbleGtk::Init(GtkWidget* anchor_widget,
                     const gfx::Rect* rect,
                     GtkWidget* content,
                     ArrowLocationGtk arrow_location,
                     int attribute_flags) {
  // If there is a current grab widget (menu, other bubble, etc.), hide it.
  GtkWidget* current_grab_widget = gtk_grab_get_current();
  if (current_grab_widget)
    gtk_widget_hide(current_grab_widget);

  DCHECK(!window_);
  anchor_widget_ = anchor_widget;
  toplevel_window_ = gtk_widget_get_toplevel(anchor_widget_);
  DCHECK(gtk_widget_is_toplevel(toplevel_window_));
  rect_ = rect ? *rect : gtk_util::WidgetBounds(anchor_widget);
  preferred_arrow_location_ = arrow_location;

  grab_input_ = attribute_flags & GRAB_INPUT;
  // Using a TOPLEVEL window may cause placement issues with certain WMs but it
  // is necessary to be able to focus the window.
  window_ = gtk_window_new(attribute_flags & POPUP_WINDOW ?
                           GTK_WINDOW_POPUP : GTK_WINDOW_TOPLEVEL);

  gtk_widget_set_app_paintable(window_, TRUE);
  // Resizing is handled by the program, not user.
  gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);

  // Attach all of the accelerators to the bubble.
  for (BubbleAcceleratorsGtk::const_iterator i(BubbleAcceleratorsGtk::begin());
       i != BubbleAcceleratorsGtk::end(); ++i) {
    gtk_accel_group_connect(accel_group_,
                            i->keyval,
                            i->modifier_type,
                            GtkAccelFlags(0),
                            g_cclosure_new(G_CALLBACK(&OnGtkAcceleratorThunk),
                                           this,
                                           NULL));
  }

  gtk_window_add_accel_group(GTK_WINDOW(window_), accel_group_);

  GtkWidget* alignment = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  gtk_alignment_set_padding(GTK_ALIGNMENT(alignment),
                            kTopMargin, kBottomMargin,
                            kLeftMargin, kRightMargin);

  gtk_container_add(GTK_CONTAINER(alignment), content);
  gtk_container_add(GTK_CONTAINER(window_), alignment);

  // GtkWidget only exposes the bitmap mask interface.  Use GDK to more
  // efficently mask a GdkRegion.  Make sure the window is realized during
  // OnSizeAllocate, so the mask can be applied to the GdkWindow.
  gtk_widget_realize(window_);

  UpdateArrowLocation(true);  // Force move and reshape.
  StackWindow();

  gtk_widget_add_events(window_, GDK_BUTTON_PRESS_MASK);

  signals_.Connect(window_, "expose-event", G_CALLBACK(OnExposeThunk), this);
  signals_.Connect(window_, "size-allocate", G_CALLBACK(OnSizeAllocateThunk),
                   this);
  signals_.Connect(window_, "button-press-event",
                   G_CALLBACK(OnButtonPressThunk), this);
  signals_.Connect(window_, "destroy", G_CALLBACK(OnDestroyThunk), this);
  signals_.Connect(window_, "hide", G_CALLBACK(OnHideThunk), this);

  // If the toplevel window is being used as the anchor, then the signals below
  // are enough to keep us positioned correctly.
  if (anchor_widget_ != toplevel_window_) {
    signals_.Connect(anchor_widget_, "size-allocate",
                     G_CALLBACK(OnAnchorAllocateThunk), this);
    signals_.Connect(anchor_widget_, "destroy",
                     G_CALLBACK(gtk_widget_destroyed), &anchor_widget_);
  }

  signals_.Connect(toplevel_window_, "configure-event",
                   G_CALLBACK(OnToplevelConfigureThunk), this);
  signals_.Connect(toplevel_window_, "unmap-event",
                   G_CALLBACK(OnToplevelUnmapThunk), this);
  // Set |toplevel_window_| to NULL if it gets destroyed.
  signals_.Connect(toplevel_window_, "destroy",
                   G_CALLBACK(gtk_widget_destroyed), &toplevel_window_);

  gtk_widget_show_all(window_);

  if (grab_input_)
    gtk_grab_add(window_);
  GrabPointerAndKeyboard();

  registrar_.Add(this, chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                 content::Source<ThemeService>(theme_service_));
  theme_service_->InitThemesFor(this);
}

// NOTE: This seems a bit overcomplicated, but it requires a bunch of careful
// fudging to get the pixels rasterized exactly where we want them, the arrow to
// have a 1 pixel point, etc.
// TODO(deanm): Windows draws with Skia and uses some PNG images for the
// corners.  This is a lot more work, but they get anti-aliasing.
// static
std::vector<GdkPoint> BubbleGtk::MakeFramePolygonPoints(
    ArrowLocationGtk arrow_location,
    int width,
    int height,
    FrameType type) {
  using gtk_util::MakeBidiGdkPoint;
  std::vector<GdkPoint> points;

  int top_arrow_size = IsArrowTop(arrow_location) ? kArrowSize : 0;
  int bottom_arrow_size = IsArrowBottom(arrow_location) ? kArrowSize : 0;
  bool on_left = IsArrowLeft(arrow_location);

  // If we're stroking the frame, we need to offset some of our points by 1
  // pixel.  We do this when we draw horizontal lines that are on the bottom or
  // when we draw vertical lines that are closer to the end (where "end" is the
  // right side for ARROW_LOCATION_TOP_LEFT).
  int y_off = (type == FRAME_MASK) ? 0 : -1;
  // We use this one for arrows located on the left.
  int x_off_l = on_left ? y_off : 0;
  // We use this one for RTL.
  int x_off_r = !on_left ? -y_off : 0;

  // Top left corner.
  points.push_back(MakeBidiGdkPoint(
      x_off_r, top_arrow_size + kCornerSize - 1, width, on_left));
  points.push_back(MakeBidiGdkPoint(
      kCornerSize + x_off_r - 1, top_arrow_size, width, on_left));

  // The top arrow.
  if (top_arrow_size) {
    int arrow_x = arrow_location == ARROW_LOCATION_TOP_MIDDLE ?
        width / 2 : kArrowX;
    points.push_back(MakeBidiGdkPoint(
        arrow_x - top_arrow_size + x_off_r, top_arrow_size, width, on_left));
    points.push_back(MakeBidiGdkPoint(
        arrow_x + x_off_r, 0, width, on_left));
    points.push_back(MakeBidiGdkPoint(
        arrow_x + 1 + x_off_l, 0, width, on_left));
    points.push_back(MakeBidiGdkPoint(
        arrow_x + top_arrow_size + 1 + x_off_l, top_arrow_size,
        width, on_left));
  }

  // Top right corner.
  points.push_back(MakeBidiGdkPoint(
      width - kCornerSize + 1 + x_off_l, top_arrow_size, width, on_left));
  points.push_back(MakeBidiGdkPoint(
      width + x_off_l, top_arrow_size + kCornerSize - 1, width, on_left));

  // Bottom right corner.
  points.push_back(MakeBidiGdkPoint(
      width + x_off_l, height - bottom_arrow_size - kCornerSize,
      width, on_left));
  points.push_back(MakeBidiGdkPoint(
      width - kCornerSize + x_off_r, height - bottom_arrow_size + y_off,
      width, on_left));

  // The bottom arrow.
  if (bottom_arrow_size) {
    int arrow_x = arrow_location == ARROW_LOCATION_BOTTOM_MIDDLE ?
        width / 2 : kArrowX;
    points.push_back(MakeBidiGdkPoint(
        arrow_x + bottom_arrow_size + 1 + x_off_l,
        height - bottom_arrow_size + y_off,
        width,
        on_left));
    points.push_back(MakeBidiGdkPoint(
        arrow_x + 1 + x_off_l, height + y_off, width, on_left));
    points.push_back(MakeBidiGdkPoint(
        arrow_x + x_off_r, height + y_off, width, on_left));
    points.push_back(MakeBidiGdkPoint(
        arrow_x - bottom_arrow_size + x_off_r,
        height - bottom_arrow_size + y_off,
        width,
        on_left));
  }

  // Bottom left corner.
  points.push_back(MakeBidiGdkPoint(
      kCornerSize + x_off_l, height -bottom_arrow_size + y_off,
      width, on_left));
  points.push_back(MakeBidiGdkPoint(
      x_off_r, height - bottom_arrow_size - kCornerSize, width, on_left));

  return points;
}

BubbleGtk::ArrowLocationGtk BubbleGtk::GetArrowLocation(
    ArrowLocationGtk preferred_location,
    int arrow_x,
    int arrow_y,
    int width,
    int height) {
  int screen_width = gdk_screen_get_width(gdk_screen_get_default());
  int screen_height = gdk_screen_get_height(gdk_screen_get_default());

  // Choose whether we should show this bubble above the specified location or
  // below it.
  bool wants_top = IsArrowTop(preferred_location) ||
      preferred_location == ARROW_LOCATION_NONE;
  bool top_is_onscreen = (arrow_y + height < screen_height);
  bool bottom_is_onscreen = (arrow_y - height >= 0);

  ArrowLocationGtk arrow_location_none;
  ArrowLocationGtk arrow_location_left;
  ArrowLocationGtk arrow_location_middle;
  ArrowLocationGtk arrow_location_right;
  if (top_is_onscreen && (wants_top || !bottom_is_onscreen)) {
    arrow_location_none = ARROW_LOCATION_NONE;
    arrow_location_left = ARROW_LOCATION_TOP_LEFT;
    arrow_location_middle = ARROW_LOCATION_TOP_MIDDLE;
    arrow_location_right =ARROW_LOCATION_TOP_RIGHT;
  } else {
    arrow_location_none = ARROW_LOCATION_FLOAT;
    arrow_location_left = ARROW_LOCATION_BOTTOM_LEFT;
    arrow_location_middle = ARROW_LOCATION_BOTTOM_MIDDLE;
    arrow_location_right =ARROW_LOCATION_BOTTOM_RIGHT;
  }

  if (!HasArrow(preferred_location))
    return arrow_location_none;

  if (IsArrowMiddle(preferred_location))
    return arrow_location_middle;

  bool wants_left = IsArrowLeft(preferred_location);
  bool left_is_onscreen = (arrow_x - kArrowX + width < screen_width);
  bool right_is_onscreen = (arrow_x + kArrowX - width >= 0);

  // Use the requested location if it fits onscreen, use whatever fits
  // otherwise, and use the requested location if neither fits.
  if (left_is_onscreen && (wants_left || !right_is_onscreen))
    return arrow_location_left;
  if (right_is_onscreen && (!wants_left || !left_is_onscreen))
    return arrow_location_right;
  return (wants_left ? arrow_location_left : arrow_location_right);
}

bool BubbleGtk::UpdateArrowLocation(bool force_move_and_reshape) {
  if (!toplevel_window_ || !anchor_widget_)
    return false;

  gint toplevel_x = 0, toplevel_y = 0;
  gdk_window_get_position(gtk_widget_get_window(toplevel_window_),
                          &toplevel_x, &toplevel_y);
  int offset_x, offset_y;
  gtk_widget_translate_coordinates(anchor_widget_, toplevel_window_,
                                   rect_.x(), rect_.y(), &offset_x, &offset_y);

  ArrowLocationGtk old_location = current_arrow_location_;
  GtkAllocation allocation;
  gtk_widget_get_allocation(window_, &allocation);
  current_arrow_location_ = GetArrowLocation(
      preferred_arrow_location_,
      toplevel_x + offset_x + (rect_.width() / 2),  // arrow_x
      toplevel_y + offset_y,
      allocation.width,
      allocation.height);

  if (force_move_and_reshape || current_arrow_location_ != old_location) {
    UpdateWindowShape();
    MoveWindow();
    // We need to redraw the entire window to repaint its border.
    gtk_widget_queue_draw(window_);
    return true;
  }
  return false;
}

void BubbleGtk::UpdateWindowShape() {
  if (mask_region_) {
    gdk_region_destroy(mask_region_);
    mask_region_ = NULL;
  }
  GtkAllocation allocation;
  gtk_widget_get_allocation(window_, &allocation);
  std::vector<GdkPoint> points = MakeFramePolygonPoints(
      current_arrow_location_, allocation.width, allocation.height,
      FRAME_MASK);
  mask_region_ = gdk_region_polygon(&points[0],
                                    points.size(),
                                    GDK_EVEN_ODD_RULE);

  GdkWindow* gdk_window = gtk_widget_get_window(window_);
  gdk_window_shape_combine_region(gdk_window, NULL, 0, 0);
  gdk_window_shape_combine_region(gdk_window, mask_region_, 0, 0);
}

void BubbleGtk::MoveWindow() {
  if (!toplevel_window_ || !anchor_widget_)
    return;

  gint toplevel_x = 0, toplevel_y = 0;
  gdk_window_get_position(gtk_widget_get_window(toplevel_window_),
                          &toplevel_x, &toplevel_y);

  int offset_x, offset_y;
  gtk_widget_translate_coordinates(anchor_widget_, toplevel_window_,
                                   rect_.x(), rect_.y(), &offset_x, &offset_y);

  GtkAllocation allocation;
  gtk_widget_get_allocation(window_, &allocation);

  gint screen_x = 0;
  if (!HasArrow(current_arrow_location_) ||
      IsArrowMiddle(current_arrow_location_)) {
    screen_x =
        toplevel_x + offset_x + (rect_.width() / 2) - allocation.width / 2;
  } else if (IsArrowLeft(current_arrow_location_)) {
    screen_x = toplevel_x + offset_x + (rect_.width() / 2) - kArrowX;
  } else if (IsArrowRight(current_arrow_location_)) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(window_, &allocation);
    screen_x = toplevel_x + offset_x + (rect_.width() / 2) -
               allocation.width + kArrowX;
  } else {
    NOTREACHED();
  }

  gint screen_y = toplevel_y + offset_y + rect_.height();
  if (IsArrowTop(current_arrow_location_) ||
      current_arrow_location_ == ARROW_LOCATION_NONE) {
    screen_y += kArrowToContentPadding;
  } else {
    screen_y -= allocation.height + kArrowToContentPadding;
  }

  gtk_window_move(GTK_WINDOW(window_), screen_x, screen_y);
}

void BubbleGtk::StackWindow() {
  // Stack our window directly above the toplevel window.
  if (toplevel_window_)
    ui::StackPopupWindow(window_, toplevel_window_);
}

void BubbleGtk::Observe(int type,
                        const content::NotificationSource& source,
                        const content::NotificationDetails& details) {
  DCHECK_EQ(type, chrome::NOTIFICATION_BROWSER_THEME_CHANGED);
  if (theme_service_->UsingNativeTheme() && match_system_theme_) {
    gtk_widget_modify_bg(window_, GTK_STATE_NORMAL, NULL);
  } else {
    // Set the background color, so we don't need to paint it manually.
    gtk_widget_modify_bg(window_, GTK_STATE_NORMAL, &kBackgroundColor);
  }
}

void BubbleGtk::HandlePointerAndKeyboardUngrabbedByContent() {
  if (grab_input_)
    GrabPointerAndKeyboard();
}

void BubbleGtk::StopGrabbingInput() {
  if (!grab_input_)
    return;
  grab_input_ = false;
  gtk_grab_remove(window_);
}

void BubbleGtk::Close() {
  // We don't need to ungrab the pointer or keyboard here; the X server will
  // automatically do that when we destroy our window.
  DCHECK(window_);
  gtk_widget_destroy(window_);
  // |this| has been deleted, see OnDestroy.
}

void BubbleGtk::GrabPointerAndKeyboard() {
  GdkWindow* gdk_window = gtk_widget_get_window(window_);

  // Install X pointer and keyboard grabs to make sure that we have the focus
  // and get all mouse and keyboard events until we're closed. As a hack, grab
  // the pointer even if |grab_input_| is false to prevent a weird error
  // rendering the bubble's frame. See
  // https://code.google.com/p/chromium/issues/detail?id=130820.
  GdkGrabStatus pointer_grab_status =
      gdk_pointer_grab(gdk_window,
                       TRUE,                   // owner_events
                       GDK_BUTTON_PRESS_MASK,  // event_mask
                       NULL,                   // confine_to
                       NULL,                   // cursor
                       GDK_CURRENT_TIME);
  if (pointer_grab_status != GDK_GRAB_SUCCESS) {
    // This will fail if someone else already has the pointer grabbed, but
    // there's not really anything we can do about that.
    DLOG(ERROR) << "Unable to grab pointer (status="
                << pointer_grab_status << ")";
  }

  // Only grab the keyboard input if |grab_input_| is true.
  if (grab_input_) {
    GdkGrabStatus keyboard_grab_status =
        gdk_keyboard_grab(gdk_window,
                          FALSE,  // owner_events
                          GDK_CURRENT_TIME);
    if (keyboard_grab_status != GDK_GRAB_SUCCESS) {
      DLOG(ERROR) << "Unable to grab keyboard (status="
                  << keyboard_grab_status << ")";
    }
  }
}

gboolean BubbleGtk::OnGtkAccelerator(GtkAccelGroup* group,
                                     GObject* acceleratable,
                                     guint keyval,
                                     GdkModifierType modifier) {
  GdkEventKey msg;
  GdkKeymapKey* keys;
  gint n_keys;

  switch (keyval) {
    case GDK_Escape:
      // Close on Esc and trap the accelerator
      closed_by_escape_ = true;
      Close();
      return TRUE;
    case GDK_w:
      // Close on C-w and forward the accelerator
      if (modifier & GDK_CONTROL_MASK) {
        gdk_keymap_get_entries_for_keyval(NULL,
                                          keyval,
                                          &keys,
                                          &n_keys);
        if (n_keys) {
          // Forward the accelerator to root window the bubble is anchored
          // to for further processing
          msg.type = GDK_KEY_PRESS;
          msg.window = gtk_widget_get_window(toplevel_window_);
          msg.send_event = TRUE;
          msg.time = GDK_CURRENT_TIME;
          msg.state = modifier | GDK_MOD2_MASK;
          msg.keyval = keyval;
          // length and string are deprecated and thus zeroed out
          msg.length = 0;
          msg.string = NULL;
          msg.hardware_keycode = keys[0].keycode;
          msg.group = keys[0].group;
          msg.is_modifier = 0;

          g_free(keys);

          gtk_main_do_event(reinterpret_cast<GdkEvent*>(&msg));
        } else {
          // This means that there isn't a h/w code for the keyval in the
          // current keymap, which is weird but possible if the keymap just
          // changed. This isn't a critical error, but might be indicative
          // of something off if it happens regularly.
          DLOG(WARNING) << "Found no keys for value " << keyval;
        }
        Close();
      }
      break;
    default:
      return FALSE;
  }

  return TRUE;
}

gboolean BubbleGtk::OnExpose(GtkWidget* widget, GdkEventExpose* expose) {
  // TODO(erg): This whole method will need to be rewritten in cairo.
  GdkDrawable* drawable = GDK_DRAWABLE(gtk_widget_get_window(window_));
  GdkGC* gc = gdk_gc_new(drawable);
  gdk_gc_set_rgb_fg_color(gc, &kFrameColor);

  // Stroke the frame border.
  GtkAllocation allocation;
  gtk_widget_get_allocation(window_, &allocation);
  std::vector<GdkPoint> points = MakeFramePolygonPoints(
      current_arrow_location_, allocation.width, allocation.height,
      FRAME_STROKE);
  gdk_draw_polygon(drawable, gc, FALSE, &points[0], points.size());

  // If |grab_input_| is false, pointer input has been grabbed as a hack in
  // |GrabPointerAndKeyboard()| to ensure that the polygon frame is drawn
  // correctly. Since the intention is not actually to grab the pointer, release
  // it now that the frame is drawn to prevent clicks from being missed. See
  // https://code.google.com/p/chromium/issues/detail?id=130820.
  if (!grab_input_)
    gdk_pointer_ungrab(GDK_CURRENT_TIME);

  g_object_unref(gc);
  return FALSE;  // Propagate so our children paint, etc.
}

// When our size is initially allocated or changed, we need to recompute
// and apply our shape mask region.
void BubbleGtk::OnSizeAllocate(GtkWidget* widget,
                               GtkAllocation* allocation) {
  if (!UpdateArrowLocation(false)) {
    UpdateWindowShape();
    if (current_arrow_location_ != ARROW_LOCATION_TOP_LEFT)
      MoveWindow();
  }
}

gboolean BubbleGtk::OnButtonPress(GtkWidget* widget,
                                  GdkEventButton* event) {
  // If we got a click in our own window, that's okay (we need to additionally
  // check that it falls within our bounds, since we've grabbed the pointer and
  // some events that actually occurred in other windows will be reported with
  // respect to our window).
  GdkWindow* gdk_window = gtk_widget_get_window(window_);
  if (event->window == gdk_window &&
      (mask_region_ && gdk_region_point_in(mask_region_, event->x, event->y))) {
    return FALSE;  // Propagate.
  }

  // Our content widget got a click.
  if (event->window != gdk_window &&
      gdk_window_get_toplevel(event->window) == gdk_window) {
    return FALSE;
  }

  if (grab_input_) {
    // Otherwise we had a click outside of our window, close ourself.
    Close();
    return TRUE;
  }

  return FALSE;
}

gboolean BubbleGtk::OnDestroy(GtkWidget* widget) {
  // We are self deleting, we have a destroy signal setup to catch when we
  // destroy the widget manually, or the window was closed via X.  This will
  // delete the BubbleGtk object.
  delete this;
  return FALSE;  // Propagate.
}

void BubbleGtk::OnHide(GtkWidget* widget) {
  gtk_widget_destroy(widget);
}

gboolean BubbleGtk::OnToplevelConfigure(GtkWidget* widget,
                                        GdkEventConfigure* event) {
  if (!UpdateArrowLocation(false))
    MoveWindow();
  StackWindow();
  return FALSE;
}

gboolean BubbleGtk::OnToplevelUnmap(GtkWidget* widget, GdkEvent* event) {
  Close();
  return FALSE;
}

void BubbleGtk::OnAnchorAllocate(GtkWidget* widget,
                                 GtkAllocation* allocation) {
  if (!UpdateArrowLocation(false))
    MoveWindow();
}
