// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines utility functions for X11 (Linux only). This code has been
// ported from XCB since we can't use XCB on Ubuntu while its 32-bit support
// remains woefully incomplete.

#include "ui/base/x/x11_util.h"

#include <sys/ipc.h>
#include <sys/shm.h>

#include <list>
#include <map>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/threading/thread.h"
#include "ui/base/keycodes/keyboard_code_conversion_x.h"
#include "ui/base/x/x11_util_internal.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/size.h"

#if defined(OS_FREEBSD)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(TOOLKIT_USES_GTK)
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include "ui/base/gtk/gtk_compat.h"
#include "ui/base/gtk/gdk_x_compat.h"
#else
// TODO(sad): Use the new way of handling X errors when
// http://codereview.chromium.org/7889040/ lands.
#define gdk_error_trap_push()
#define gdk_error_trap_pop() false
#define gdk_flush()
#endif

namespace ui {

namespace {

// Used to cache the XRenderPictFormat for a visual/display pair.
struct CachedPictFormat {
  bool equals(Display* display, Visual* visual) const {
    return display == this->display && visual == this->visual;
  }

  Display* display;
  Visual* visual;
  XRenderPictFormat* format;
};

typedef std::list<CachedPictFormat> CachedPictFormats;

// Returns the cache of pict formats.
CachedPictFormats* get_cached_pict_formats() {
  static CachedPictFormats* formats = NULL;
  if (!formats)
    formats = new CachedPictFormats();
  return formats;
}

// Maximum number of CachedPictFormats we keep around.
const size_t kMaxCacheSize = 5;

int DefaultX11ErrorHandler(Display* d, XErrorEvent* e) {
  if (MessageLoop::current()) {
    MessageLoop::current()->PostTask(
         FROM_HERE,
         base::Bind(&LogErrorEventDescription, d, *e));
  } else {
    LOG(ERROR)
        << "X Error detected: "
        << "serial " << e->serial << ", "
        << "error_code " << static_cast<int>(e->error_code) << ", "
        << "request_code " << static_cast<int>(e->request_code) << ", "
        << "minor_code " << static_cast<int>(e->minor_code);
  }
  return 0;
}

int DefaultX11IOErrorHandler(Display* d) {
  // If there's an IO error it likely means the X server has gone away
  LOG(ERROR) << "X IO Error detected";
  _exit(1);
}

// Note: The caller should free the resulting value data.
bool GetProperty(XID window, const std::string& property_name, long max_length,
                 Atom* type, int* format, unsigned long* num_items,
                 unsigned char** property) {
  Atom property_atom = GetAtom(property_name.c_str());
  unsigned long remaining_bytes = 0;
  return XGetWindowProperty(GetXDisplay(),
                            window,
                            property_atom,
                            0,          // offset into property data to read
                            max_length, // max length to get
                            False,      // deleted
                            AnyPropertyType,
                            type,
                            format,
                            num_items,
                            &remaining_bytes,
                            property);
}

// Converts ui::EventType to XKeyEvent state.
unsigned int XKeyEventState(int flags) {
  return
      ((flags & ui::EF_SHIFT_DOWN) ? ShiftMask : 0) |
      ((flags & ui::EF_CONTROL_DOWN) ? ControlMask : 0) |
      ((flags & ui::EF_ALT_DOWN) ? Mod1Mask : 0) |
      ((flags & ui::EF_CAPS_LOCK_DOWN) ? LockMask : 0);
}

// Converts EventType to XKeyEvent type.
int XKeyEventType(ui::EventType type) {
  switch (type) {
    case ui::ET_KEY_PRESSED:
      return KeyPress;
    case ui::ET_KEY_RELEASED:
      return KeyRelease;
    default:
      return 0;
  }
}

// Converts KeyboardCode to XKeyEvent keycode.
unsigned int XKeyEventKeyCode(ui::KeyboardCode key_code,
                              int flags,
                              Display* display) {
  const int keysym = XKeysymForWindowsKeyCode(key_code,
                                              flags & ui::EF_SHIFT_DOWN);
  // Tests assume the keycode for XK_less is equal to the one of XK_comma,
  // but XKeysymToKeycode returns 94 for XK_less while it returns 59 for
  // XK_comma. Here we convert the value for XK_less to the value for XK_comma.
  return (keysym == XK_less) ? 59 : XKeysymToKeycode(display, keysym);
}

// A process wide singleton that manages the usage of X cursors.
class XCursorCache {
 public:
   XCursorCache() {}
  ~XCursorCache() {
    Clear();
  }

  Cursor GetCursor(int cursor_shape) {
    // Lookup cursor by attempting to insert a null value, which avoids
    // a second pass through the map after a cache miss.
    std::pair<std::map<int, Cursor>::iterator, bool> it = cache_.insert(
        std::make_pair(cursor_shape, 0));
    if (it.second) {
      Display* display = base::MessagePumpForUI::GetDefaultXDisplay();
      it.first->second = XCreateFontCursor(display, cursor_shape);
    }
    return it.first->second;
  }

  void Clear() {
    Display* display = base::MessagePumpForUI::GetDefaultXDisplay();
    for (std::map<int, Cursor>::iterator it =
        cache_.begin(); it != cache_.end(); ++it) {
      XFreeCursor(display, it->second);
    }
    cache_.clear();
  }

 private:
  // Maps X11 font cursor shapes to Cursor IDs.
  std::map<int, Cursor> cache_;

  DISALLOW_COPY_AND_ASSIGN(XCursorCache);
};

// A singleton object that remembers remappings of mouse buttons.
class XButtonMap {
 public:
  static XButtonMap* GetInstance() {
    return Singleton<XButtonMap>::get();
  }

  void UpdateMapping() {
    count_ = XGetPointerMapping(ui::GetXDisplay(), map_, arraysize(map_));
  }

  int GetMappedButton(int button) {
    return button > 0 && button <= count_ ? map_[button - 1] : button;
  }

 private:
  friend struct DefaultSingletonTraits<XButtonMap>;

  XButtonMap() {
    UpdateMapping();
  }

  ~XButtonMap() {}

  unsigned char map_[256];
  int count_;

  DISALLOW_COPY_AND_ASSIGN(XButtonMap);
};

}  // namespace

bool XDisplayExists() {
  return (GetXDisplay() != NULL);
}

Display* GetXDisplay() {
  return base::MessagePumpForUI::GetDefaultXDisplay();
}

static SharedMemorySupport DoQuerySharedMemorySupport(Display* dpy) {
  int dummy;
  Bool pixmaps_supported;
  // Query the server's support for XSHM.
  if (!XShmQueryVersion(dpy, &dummy, &dummy, &pixmaps_supported))
    return SHARED_MEMORY_NONE;

#if defined(OS_FREEBSD)
  // On FreeBSD we can't access the shared memory after it was marked for
  // deletion, unless this behaviour is explicitly enabled by the user.
  // In case it's not enabled disable shared memory support.
  int allow_removed;
  size_t length = sizeof(allow_removed);

  if ((sysctlbyname("kern.ipc.shm_allow_removed", &allow_removed, &length,
      NULL, 0) < 0) || allow_removed < 1) {
    return SHARED_MEMORY_NONE;
  }
#endif

  // Next we probe to see if shared memory will really work
  int shmkey = shmget(IPC_PRIVATE, 1, 0666);
  if (shmkey == -1)
    return SHARED_MEMORY_NONE;
  void* address = shmat(shmkey, NULL, 0);
  // Mark the shared memory region for deletion
  shmctl(shmkey, IPC_RMID, NULL);

  XShmSegmentInfo shminfo;
  memset(&shminfo, 0, sizeof(shminfo));
  shminfo.shmid = shmkey;

  gdk_error_trap_push();
  bool result = XShmAttach(dpy, &shminfo);
  XSync(dpy, False);
  if (gdk_error_trap_pop())
    result = false;
  shmdt(address);
  if (!result)
    return SHARED_MEMORY_NONE;

  XShmDetach(dpy, &shminfo);
  return pixmaps_supported ? SHARED_MEMORY_PIXMAP : SHARED_MEMORY_PUTIMAGE;
}

SharedMemorySupport QuerySharedMemorySupport(Display* dpy) {
  static SharedMemorySupport shared_memory_support = SHARED_MEMORY_NONE;
  static bool shared_memory_support_cached = false;

  if (shared_memory_support_cached)
    return shared_memory_support;

  shared_memory_support = DoQuerySharedMemorySupport(dpy);
  shared_memory_support_cached = true;

  return shared_memory_support;
}

bool QueryRenderSupport(Display* dpy) {
  static bool render_supported = false;
  static bool render_supported_cached = false;

  if (render_supported_cached)
    return render_supported;

  // We don't care about the version of Xrender since all the features which
  // we use are included in every version.
  int dummy;
  render_supported = XRenderQueryExtension(dpy, &dummy, &dummy);
  render_supported_cached = true;

  return render_supported;
}

int GetDefaultScreen(Display* display) {
  return XDefaultScreen(display);
}

Cursor GetXCursor(int cursor_shape) {
  CR_DEFINE_STATIC_LOCAL(XCursorCache, cache, ());

  if (cursor_shape == kCursorClearXCursorCache) {
    cache.Clear();
    return 0;
  }

  return cache.GetCursor(cursor_shape);
}

XID GetX11RootWindow() {
  return DefaultRootWindow(GetXDisplay());
}

bool GetCurrentDesktop(int* desktop) {
  return GetIntProperty(GetX11RootWindow(), "_NET_CURRENT_DESKTOP", desktop);
}

#if defined(TOOLKIT_USES_GTK)
XID GetX11WindowFromGtkWidget(GtkWidget* widget) {
  return GDK_WINDOW_XID(gtk_widget_get_window(widget));
}

XID GetX11WindowFromGdkWindow(GdkWindow* window) {
  return GDK_WINDOW_XID(window);
}

GtkWindow* GetGtkWindowFromX11Window(XID xid) {
  GdkWindow* gdk_window =
      gdk_x11_window_lookup_for_display(gdk_display_get_default(), xid);
  if (!gdk_window)
    return NULL;
  GtkWindow* gtk_window = NULL;
  gdk_window_get_user_data(gdk_window,
                           reinterpret_cast<gpointer*>(&gtk_window));
  if (!gtk_window)
    return NULL;
  return gtk_window;
}

void* GetVisualFromGtkWidget(GtkWidget* widget) {
  return GDK_VISUAL_XVISUAL(gtk_widget_get_visual(widget));
}
#endif  // defined(TOOLKIT_USES_GTK)

int BitsPerPixelForPixmapDepth(Display* dpy, int depth) {
  int count;
  XPixmapFormatValues* formats = XListPixmapFormats(dpy, &count);
  if (!formats)
    return -1;

  int bits_per_pixel = -1;
  for (int i = 0; i < count; ++i) {
    if (formats[i].depth == depth) {
      bits_per_pixel = formats[i].bits_per_pixel;
      break;
    }
  }

  XFree(formats);
  return bits_per_pixel;
}

bool IsWindowVisible(XID window) {
  XWindowAttributes win_attributes;
  if (!XGetWindowAttributes(GetXDisplay(), window, &win_attributes))
    return false;
  if (win_attributes.map_state != IsViewable)
    return false;
  // Some compositing window managers (notably kwin) do not actually unmap
  // windows on desktop switch, so we also must check the current desktop.
  int window_desktop, current_desktop;
  return (!GetWindowDesktop(window, &window_desktop) ||
          !GetCurrentDesktop(&current_desktop) ||
          window_desktop == kAllDesktops ||
          window_desktop == current_desktop);
}

bool GetWindowRect(XID window, gfx::Rect* rect) {
  Window root, child;
  int x, y;
  unsigned int width, height;
  unsigned int border_width, depth;

  if (!XGetGeometry(GetXDisplay(), window, &root, &x, &y,
                    &width, &height, &border_width, &depth))
    return false;

  if (!XTranslateCoordinates(GetXDisplay(), window, root,
                             0, 0, &x, &y, &child))
    return false;

  *rect = gfx::Rect(x, y, width, height);
  return true;
}

bool PropertyExists(XID window, const std::string& property_name) {
  Atom type = None;
  int format = 0;  // size in bits of each item in 'property'
  unsigned long num_items = 0;
  unsigned char* property = NULL;

  int result = GetProperty(window, property_name, 1,
                           &type, &format, &num_items, &property);
  if (result != Success)
    return false;

  XFree(property);
  return num_items > 0;
}

bool GetIntProperty(XID window, const std::string& property_name, int* value) {
  Atom type = None;
  int format = 0;  // size in bits of each item in 'property'
  unsigned long num_items = 0;
  unsigned char* property = NULL;

  int result = GetProperty(window, property_name, 1,
                           &type, &format, &num_items, &property);
  if (result != Success)
    return false;

  if (format != 32 || num_items != 1) {
    XFree(property);
    return false;
  }

  *value = static_cast<int>(*(reinterpret_cast<long*>(property)));
  XFree(property);
  return true;
}

bool GetIntArrayProperty(XID window,
                         const std::string& property_name,
                         std::vector<int>* value) {
  Atom type = None;
  int format = 0;  // size in bits of each item in 'property'
  unsigned long num_items = 0;
  unsigned char* properties = NULL;

  int result = GetProperty(window, property_name,
                           (~0L), // (all of them)
                           &type, &format, &num_items, &properties);
  if (result != Success)
    return false;

  if (format != 32) {
    XFree(properties);
    return false;
  }

  long* int_properties = reinterpret_cast<long*>(properties);
  value->clear();
  for (unsigned long i = 0; i < num_items; ++i) {
    value->push_back(static_cast<int>(int_properties[i]));
  }
  XFree(properties);
  return true;
}

bool GetAtomArrayProperty(XID window,
                          const std::string& property_name,
                          std::vector<Atom>* value) {
  Atom type = None;
  int format = 0;  // size in bits of each item in 'property'
  unsigned long num_items = 0;
  unsigned char* properties = NULL;

  int result = GetProperty(window, property_name,
                           (~0L), // (all of them)
                           &type, &format, &num_items, &properties);
  if (result != Success)
    return false;

  if (type != XA_ATOM) {
    XFree(properties);
    return false;
  }

  Atom* atom_properties = reinterpret_cast<Atom*>(properties);
  value->clear();
  value->insert(value->begin(), atom_properties, atom_properties + num_items);
  XFree(properties);
  return true;
}

bool GetStringProperty(
    XID window, const std::string& property_name, std::string* value) {
  Atom type = None;
  int format = 0;  // size in bits of each item in 'property'
  unsigned long num_items = 0;
  unsigned char* property = NULL;

  int result = GetProperty(window, property_name, 1024,
                           &type, &format, &num_items, &property);
  if (result != Success)
    return false;

  if (format != 8) {
    XFree(property);
    return false;
  }

  value->assign(reinterpret_cast<char*>(property), num_items);
  XFree(property);
  return true;
}

bool SetIntProperty(XID window,
                    const std::string& name,
                    const std::string& type,
                    int value) {
  std::vector<int> values(1, value);
  return SetIntArrayProperty(window, name, type, values);
}

bool SetIntArrayProperty(XID window,
                         const std::string& name,
                         const std::string& type,
                         const std::vector<int>& value) {
  DCHECK(!value.empty());
  Atom name_atom = GetAtom(name.c_str());
  Atom type_atom = GetAtom(type.c_str());

  // XChangeProperty() expects values of type 32 to be longs.
  scoped_array<long> data(new long[value.size()]);
  for (size_t i = 0; i < value.size(); ++i)
    data[i] = value[i];

  gdk_error_trap_push();
  XChangeProperty(ui::GetXDisplay(),
                  window,
                  name_atom,
                  type_atom,
                  32,  // size in bits of items in 'value'
                  PropModeReplace,
                  reinterpret_cast<const unsigned char*>(data.get()),
                  value.size());  // num items
  XSync(ui::GetXDisplay(), False);
  return gdk_error_trap_pop() == 0;
}

Atom GetAtom(const char* name) {
#if defined(TOOLKIT_USES_GTK)
  return gdk_x11_get_xatom_by_name_for_display(
      gdk_display_get_default(), name);
#else
  // TODO(derat): Cache atoms to avoid round-trips to the server.
  return XInternAtom(GetXDisplay(), name, false);
#endif
}

XID GetParentWindow(XID window) {
  XID root = None;
  XID parent = None;
  XID* children = NULL;
  unsigned int num_children = 0;
  XQueryTree(GetXDisplay(), window, &root, &parent, &children, &num_children);
  if (children)
    XFree(children);
  return parent;
}

XID GetHighestAncestorWindow(XID window, XID root) {
  while (true) {
    XID parent = GetParentWindow(window);
    if (parent == None)
      return None;
    if (parent == root)
      return window;
    window = parent;
  }
}

bool GetWindowDesktop(XID window, int* desktop) {
  return GetIntProperty(window, "_NET_WM_DESKTOP", desktop);
}

// Returns true if |window| is a named window.
bool IsWindowNamed(XID window) {
  XTextProperty prop;
  if (!XGetWMName(GetXDisplay(), window, &prop) || !prop.value)
    return false;

  XFree(prop.value);
  return true;
}

bool EnumerateChildren(EnumerateWindowsDelegate* delegate, XID window,
                       const int max_depth, int depth) {
  if (depth > max_depth)
    return false;

  XID root, parent, *children;
  unsigned int num_children;
  int status = XQueryTree(GetXDisplay(), window, &root, &parent, &children,
                          &num_children);
  if (status == 0)
    return false;

  std::vector<XID> windows;
  for (int i = static_cast<int>(num_children) - 1; i >= 0; i--)
    windows.push_back(children[i]);

  XFree(children);

  // XQueryTree returns the children of |window| in bottom-to-top order, so
  // reverse-iterate the list to check the windows from top-to-bottom.
  std::vector<XID>::iterator iter;
  for (iter = windows.begin(); iter != windows.end(); iter++) {
    if (IsWindowNamed(*iter) && delegate->ShouldStopIterating(*iter))
      return true;
  }

  // If we're at this point, we didn't find the window we're looking for at the
  // current level, so we need to recurse to the next level.  We use a second
  // loop because the recursion and call to XQueryTree are expensive and is only
  // needed for a small number of cases.
  if (++depth <= max_depth) {
    for (iter = windows.begin(); iter != windows.end(); iter++) {
      if (EnumerateChildren(delegate, *iter, max_depth, depth))
        return true;
    }
  }

  return false;
}

bool EnumerateAllWindows(EnumerateWindowsDelegate* delegate, int max_depth) {
  XID root = GetX11RootWindow();
  return EnumerateChildren(delegate, root, max_depth, 0);
}

bool GetXWindowStack(Window window, std::vector<XID>* windows) {
  windows->clear();

  Atom type;
  int format;
  unsigned long count;
  unsigned char *data = NULL;
  if (GetProperty(window,
                  "_NET_CLIENT_LIST_STACKING",
                  ~0L,
                  &type,
                  &format,
                  &count,
                  &data) != Success) {
    return false;
  }

  bool result = false;
  if (type == XA_WINDOW && format == 32 && data && count > 0) {
    result = true;
    XID* stack = reinterpret_cast<XID*>(data);
    for (long i = static_cast<long>(count) - 1; i >= 0; i--)
      windows->push_back(stack[i]);
  }

  if (data)
    XFree(data);

  return result;
}

void RestackWindow(XID window, XID sibling, bool above) {
  XWindowChanges changes;
  changes.sibling = sibling;
  changes.stack_mode = above ? Above : Below;
  XConfigureWindow(GetXDisplay(), window, CWSibling | CWStackMode, &changes);
}

XSharedMemoryId AttachSharedMemory(Display* display, int shared_memory_key) {
  DCHECK(QuerySharedMemorySupport(display));

  XShmSegmentInfo shminfo;
  memset(&shminfo, 0, sizeof(shminfo));
  shminfo.shmid = shared_memory_key;

  // This function is only called if QuerySharedMemorySupport returned true. In
  // which case we've already succeeded in having the X server attach to one of
  // our shared memory segments.
  if (!XShmAttach(display, &shminfo))
    NOTREACHED();

  return shminfo.shmseg;
}

void DetachSharedMemory(Display* display, XSharedMemoryId shmseg) {
  DCHECK(QuerySharedMemorySupport(display));

  XShmSegmentInfo shminfo;
  memset(&shminfo, 0, sizeof(shminfo));
  shminfo.shmseg = shmseg;

  if (!XShmDetach(display, &shminfo))
    NOTREACHED();
}

XID CreatePictureFromSkiaPixmap(Display* display, XID pixmap) {
  XID picture = XRenderCreatePicture(
      display, pixmap, GetRenderARGB32Format(display), 0, NULL);

  return picture;
}

void PutARGBImage(Display* display,
                  void* visual, int depth,
                  XID pixmap, void* pixmap_gc,
                  const uint8* data,
                  int width, int height) {
  PutARGBImage(display,
               visual, depth,
               pixmap, pixmap_gc,
               data, width, height,
               0, 0, // src_x, src_y
               0, 0, // dst_x, dst_y
               width, height);
}

void PutARGBImage(Display* display,
                  void* visual, int depth,
                  XID pixmap, void* pixmap_gc,
                  const uint8* data,
                  int data_width, int data_height,
                  int src_x, int src_y,
                  int dst_x, int dst_y,
                  int copy_width, int copy_height) {
  // TODO(scherkus): potential performance impact... consider passing in as a
  // parameter.
  int pixmap_bpp = BitsPerPixelForPixmapDepth(display, depth);

  XImage image;
  memset(&image, 0, sizeof(image));

  image.width = data_width;
  image.height = data_height;
  image.format = ZPixmap;
  image.byte_order = LSBFirst;
  image.bitmap_unit = 8;
  image.bitmap_bit_order = LSBFirst;
  image.depth = depth;
  image.bits_per_pixel = pixmap_bpp;
  image.bytes_per_line = data_width * pixmap_bpp / 8;

  if (pixmap_bpp == 32) {
    image.red_mask = 0xff0000;
    image.green_mask = 0xff00;
    image.blue_mask = 0xff;

    // If the X server depth is already 32-bits and the color masks match,
    // then our job is easy.
    Visual* vis = static_cast<Visual*>(visual);
    if (image.red_mask == vis->red_mask &&
        image.green_mask == vis->green_mask &&
        image.blue_mask == vis->blue_mask) {
      image.data = const_cast<char*>(reinterpret_cast<const char*>(data));
      XPutImage(display, pixmap, static_cast<GC>(pixmap_gc), &image,
                src_x, src_y, dst_x, dst_y,
                copy_width, copy_height);
    } else {
      // Otherwise, we need to shuffle the colors around. Assume red and blue
      // need to be swapped.
      //
      // It's possible to use some fancy SSE tricks here, but since this is the
      // slow path anyway, we do it slowly.

      uint8_t* bitmap32 =
          static_cast<uint8_t*>(malloc(4 * data_width * data_height));
      if (!bitmap32)
        return;
      uint8_t* const orig_bitmap32 = bitmap32;
      const uint32_t* bitmap_in = reinterpret_cast<const uint32_t*>(data);
      for (int y = 0; y < data_height; ++y) {
        for (int x = 0; x < data_width; ++x) {
          const uint32_t pixel = *(bitmap_in++);
          bitmap32[0] = (pixel >> 16) & 0xff;  // Red
          bitmap32[1] = (pixel >> 8) & 0xff;   // Green
          bitmap32[2] = pixel & 0xff;          // Blue
          bitmap32[3] = (pixel >> 24) & 0xff;  // Alpha
          bitmap32 += 4;
        }
      }
      image.data = reinterpret_cast<char*>(orig_bitmap32);
      XPutImage(display, pixmap, static_cast<GC>(pixmap_gc), &image,
                src_x, src_y, dst_x, dst_y,
                copy_width, copy_height);
      free(orig_bitmap32);
    }
  } else if (pixmap_bpp == 16) {
    // Some folks have VNC setups which still use 16-bit visuals and VNC
    // doesn't include Xrender.

    uint16_t* bitmap16 =
        static_cast<uint16_t*>(malloc(2 * data_width * data_height));
    if (!bitmap16)
      return;
    uint16_t* const orig_bitmap16 = bitmap16;
    const uint32_t* bitmap_in = reinterpret_cast<const uint32_t*>(data);
    for (int y = 0; y < data_height; ++y) {
      for (int x = 0; x < data_width; ++x) {
        const uint32_t pixel = *(bitmap_in++);
        uint16_t out_pixel = ((pixel >> 8) & 0xf800) |
                             ((pixel >> 5) & 0x07e0) |
                             ((pixel >> 3) & 0x001f);
        *(bitmap16++) = out_pixel;
      }
    }

    image.data = reinterpret_cast<char*>(orig_bitmap16);
    image.red_mask = 0xf800;
    image.green_mask = 0x07e0;
    image.blue_mask = 0x001f;

    XPutImage(display, pixmap, static_cast<GC>(pixmap_gc), &image,
              src_x, src_y, dst_x, dst_y,
              copy_width, copy_height);
    free(orig_bitmap16);
  } else {
    LOG(FATAL) << "Sorry, we don't support your visual depth without "
                  "Xrender support (depth:" << depth
               << " bpp:" << pixmap_bpp << ")";
  }
}

void FreePicture(Display* display, XID picture) {
  XRenderFreePicture(display, picture);
}

void FreePixmap(Display* display, XID pixmap) {
  XFreePixmap(display, pixmap);
}

bool GetWindowManagerName(std::string* wm_name) {
  DCHECK(wm_name);
  int wm_window = 0;
  if (!GetIntProperty(GetX11RootWindow(),
                      "_NET_SUPPORTING_WM_CHECK",
                      &wm_window)) {
    return false;
  }

  // It's possible that a window manager started earlier in this X session left
  // a stale _NET_SUPPORTING_WM_CHECK property when it was replaced by a
  // non-EWMH window manager, so we trap errors in the following requests to
  // avoid crashes (issue 23860).

  // EWMH requires the supporting-WM window to also have a
  // _NET_SUPPORTING_WM_CHECK property pointing to itself (to avoid a stale
  // property referencing an ID that's been recycled for another window), so we
  // check that too.
  gdk_error_trap_push();
  int wm_window_property = 0;
  bool result = GetIntProperty(
      wm_window, "_NET_SUPPORTING_WM_CHECK", &wm_window_property);
  gdk_flush();
  bool got_error = gdk_error_trap_pop();
  if (got_error || !result || wm_window_property != wm_window)
    return false;

  gdk_error_trap_push();
  result = GetStringProperty(
      static_cast<XID>(wm_window), "_NET_WM_NAME", wm_name);
  gdk_flush();
  got_error = gdk_error_trap_pop();
  return !got_error && result;
}

WindowManagerName GuessWindowManager() {
  std::string name;
  if (GetWindowManagerName(&name)) {
    // These names are taken from the WMs' source code.
    if (name == "Compiz" || name == "compiz")
      return WM_COMPIZ;
    if (name == "KWin")
      return WM_KWIN;
    if (name == "Metacity")
      return WM_METACITY;
    if (name == "Mutter")
      return WM_MUTTER;
    if (name == "Xfwm4")
      return WM_XFWM4;
    if (name == "chromeos-wm")
      return WM_CHROME_OS;
    if (name == "Blackbox")
      return WM_BLACKBOX;
    if (name == "e16")
      return WM_ENLIGHTENMENT;
    if (StartsWithASCII(name, "IceWM", true))
      return WM_ICE_WM;
    if (name == "Openbox")
      return WM_OPENBOX;
  }
  return WM_UNKNOWN;
}

bool ChangeWindowDesktop(XID window, XID destination) {
  int desktop;
  if (!GetWindowDesktop(destination, &desktop))
    return false;

  // If |window| is sticky, use the current desktop.
  if (desktop == kAllDesktops &&
      !GetCurrentDesktop(&desktop))
    return false;

  XEvent event;
  event.xclient.type = ClientMessage;
  event.xclient.window = window;
  event.xclient.message_type = GetAtom("_NET_WM_DESKTOP");
  event.xclient.format = 32;
  event.xclient.data.l[0] = desktop;
  event.xclient.data.l[1] = 1;  // source indication

  int result = XSendEvent(GetXDisplay(), GetX11RootWindow(), False,
                          SubstructureNotifyMask, &event);
  return result == Success;
}

void SetDefaultX11ErrorHandlers() {
  SetX11ErrorHandlers(NULL, NULL);
}

bool IsX11WindowFullScreen(XID window) {
  // First check if _NET_WM_STATE property contains _NET_WM_STATE_FULLSCREEN.
  static Atom atom = GetAtom("_NET_WM_STATE_FULLSCREEN");

  std::vector<Atom> atom_properties;
  if (GetAtomArrayProperty(window,
                           "_NET_WM_STATE",
                           &atom_properties) &&
      std::find(atom_properties.begin(), atom_properties.end(), atom)
          != atom_properties.end())
    return true;

#if defined(TOOLKIT_USES_GTK)
  // As the last resort, check if the window size is as large as the main
  // screen.
  GdkRectangle monitor_rect;
  gdk_screen_get_monitor_geometry(gdk_screen_get_default(), 0, &monitor_rect);

  gfx::Rect window_rect;
  if (!ui::GetWindowRect(window, &window_rect))
    return false;

  return monitor_rect.x == window_rect.x() &&
         monitor_rect.y == window_rect.y() &&
         monitor_rect.width == window_rect.width() &&
         monitor_rect.height == window_rect.height();
#else
  NOTIMPLEMENTED();
  return false;
#endif
}

bool IsMotionEvent(XEvent* event) {
  int type = event->type;
  if (type == GenericEvent)
    type = event->xgeneric.evtype;
  return type == MotionNotify;
}

int GetMappedButton(int button) {
  return XButtonMap::GetInstance()->GetMappedButton(button);
}

void UpdateButtonMap() {
  XButtonMap::GetInstance()->UpdateMapping();
}

void InitXKeyEventForTesting(EventType type,
                             KeyboardCode key_code,
                             int flags,
                             XEvent* event) {
  CHECK(event);
  Display* display = GetXDisplay();
  XKeyEvent key_event;
  key_event.type = XKeyEventType(type);
  CHECK_NE(0, key_event.type);
  key_event.serial = 0;
  key_event.send_event = 0;
  key_event.display = display;
  key_event.time = 0;
  key_event.window = 0;
  key_event.root = 0;
  key_event.subwindow = 0;
  key_event.x = 0;
  key_event.y = 0;
  key_event.x_root = 0;
  key_event.y_root = 0;
  key_event.state = XKeyEventState(flags);
  key_event.keycode = XKeyEventKeyCode(key_code, flags, display);
  key_event.same_screen = 1;
  event->type = key_event.type;
  event->xkey = key_event;
}

// ----------------------------------------------------------------------------
// These functions are declared in x11_util_internal.h because they require
// XLib.h to be included, and it conflicts with many other headers.
XRenderPictFormat* GetRenderARGB32Format(Display* dpy) {
  static XRenderPictFormat* pictformat = NULL;
  if (pictformat)
    return pictformat;

  // First look for a 32-bit format which ignores the alpha value
  XRenderPictFormat templ;
  templ.depth = 32;
  templ.type = PictTypeDirect;
  templ.direct.red = 16;
  templ.direct.green = 8;
  templ.direct.blue = 0;
  templ.direct.redMask = 0xff;
  templ.direct.greenMask = 0xff;
  templ.direct.blueMask = 0xff;
  templ.direct.alphaMask = 0;

  static const unsigned long kMask =
    PictFormatType | PictFormatDepth |
    PictFormatRed | PictFormatRedMask |
    PictFormatGreen | PictFormatGreenMask |
    PictFormatBlue | PictFormatBlueMask |
    PictFormatAlphaMask;

  pictformat = XRenderFindFormat(dpy, kMask, &templ, 0 /* first result */);

  if (!pictformat) {
    // Not all X servers support xRGB32 formats. However, the XRENDER spec says
    // that they must support an ARGB32 format, so we can always return that.
    pictformat = XRenderFindStandardFormat(dpy, PictStandardARGB32);
    CHECK(pictformat) << "XRENDER ARGB32 not supported.";
  }

  return pictformat;
}

XRenderPictFormat* GetRenderVisualFormat(Display* dpy, Visual* visual) {
  DCHECK(QueryRenderSupport(dpy));

  CachedPictFormats* formats = get_cached_pict_formats();

  for (CachedPictFormats::const_iterator i = formats->begin();
       i != formats->end(); ++i) {
    if (i->equals(dpy, visual))
      return i->format;
  }

  // Not cached, look up the value.
  XRenderPictFormat* pictformat = XRenderFindVisualFormat(dpy, visual);
  CHECK(pictformat) << "XRENDER does not support default visual";

  // And store it in the cache.
  CachedPictFormat cached_value;
  cached_value.visual = visual;
  cached_value.display = dpy;
  cached_value.format = pictformat;
  formats->push_front(cached_value);

  if (formats->size() == kMaxCacheSize) {
    formats->pop_back();
    // We should really only have at most 2 display/visual combinations:
    // one for normal browser windows, and possibly another for an argb window
    // created to display a menu.
    //
    // If we get here it's not fatal, we just need to make sure we aren't
    // always blowing away the cache. If we are, then we should figure out why
    // and make it bigger.
    NOTREACHED();
  }

  return pictformat;
}

void SetX11ErrorHandlers(XErrorHandler error_handler,
                         XIOErrorHandler io_error_handler) {
  XSetErrorHandler(error_handler ? error_handler : DefaultX11ErrorHandler);
  XSetIOErrorHandler(
      io_error_handler ? io_error_handler : DefaultX11IOErrorHandler);
}

void LogErrorEventDescription(Display* dpy,
                              const XErrorEvent& error_event) {
  char error_str[256];
  char request_str[256];

  XGetErrorText(dpy, error_event.error_code, error_str, sizeof(error_str));

  strncpy(request_str, "Unknown", sizeof(request_str));
  if (error_event.request_code < 128) {
    std::string num = base::UintToString(error_event.request_code);
    XGetErrorDatabaseText(
        dpy, "XRequest", num.c_str(), "Unknown", request_str,
        sizeof(request_str));
  } else {
    int num_ext;
    char** ext_list = XListExtensions(dpy, &num_ext);

    for (int i = 0; i < num_ext; i++) {
      int ext_code, first_event, first_error;
      XQueryExtension(dpy, ext_list[i], &ext_code, &first_event, &first_error);
      if (error_event.request_code == ext_code) {
        std::string msg = StringPrintf(
            "%s.%d", ext_list[i], error_event.minor_code);
        XGetErrorDatabaseText(
            dpy, "XRequest", msg.c_str(), "Unknown", request_str,
            sizeof(request_str));
        break;
      }
    }
    XFreeExtensionList(ext_list);
  }

  LOG(ERROR) 
      << "X Error detected: "
      << "serial " << error_event.serial << ", "
      << "error_code " << static_cast<int>(error_event.error_code)
      << " (" << error_str << "), "
      << "request_code " << static_cast<int>(error_event.request_code) << ", "
      << "minor_code " << static_cast<int>(error_event.minor_code)
      << " (" << request_str << ")";
}

// ----------------------------------------------------------------------------
// End of x11_util_internal.h


}  // namespace ui
