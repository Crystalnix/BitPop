// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/aeropeek_manager.h"

#include <dwmapi.h>
#include <shobjidl.h>

#include "base/command_line.h"
#include "base/scoped_native_library.h"
#include "base/synchronization/waitable_event.h"
#include "base/win/scoped_comptr.h"
#include "base/win/scoped_gdi_object.h"
#include "base/win/scoped_hdc.h"
#include "base/win/windows_version.h"
#include "chrome/browser/app_icon_win.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/favicon/favicon_tab_helper.h"
#include "chrome/browser/sessions/restore_tab_helper.h"
#include "chrome/browser/tab_contents/thumbnail_generator.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/installer/util/browser_distribution.h"
#include "content/browser/renderer_host/backing_store.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_view.h"
#include "skia/ext/image_operations.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/win/shell.h"
#include "ui/base/win/window_impl.h"
#include "ui/gfx/gdi_util.h"
#include "ui/gfx/icon_util.h"
#include "ui/views/widget/native_widget_win.h"

#pragma comment(lib, "dwmapi.lib")

using content::BrowserThread;
using content::WebContents;

namespace {

// Sends a thumbnail bitmap to Windows. Windows assumes this function is called
// when a WM_DWMSENDICONICTHUMBNAIL message sent to a place-holder window. We
// can use DwmInvalidateIconicBitmap() to force Windows to send the message.
HRESULT CallDwmSetIconicThumbnail(HWND window, HBITMAP bitmap, DWORD flags) {
  FilePath dwmapi_path(base::GetNativeLibraryName(L"dwmapi"));
  base::ScopedNativeLibrary dwmapi(dwmapi_path);

  typedef HRESULT (STDAPICALLTYPE *DwmSetIconicThumbnailProc)(
      HWND, HBITMAP, DWORD);
  DwmSetIconicThumbnailProc dwm_set_iconic_thumbnail =
      static_cast<DwmSetIconicThumbnailProc>(
      dwmapi.GetFunctionPointer("DwmSetIconicThumbnail"));

  if (!dwm_set_iconic_thumbnail)
    return E_FAIL;

  return dwm_set_iconic_thumbnail(window, bitmap, flags);
}

// Sends a preview bitmap to Windows. Windows assumes this function is called
// when a WM_DWMSENDICONICLIVEPREVIEWBITMAP message sent to a place-holder
// window.
HRESULT CallDwmSetIconicLivePreviewBitmap(HWND window,
                                          HBITMAP bitmap,
                                          POINT* client,
                                          DWORD flags) {
  FilePath dwmapi_path(base::GetNativeLibraryName(L"dwmapi"));
  base::ScopedNativeLibrary dwmapi(dwmapi_path);

  typedef HRESULT (STDAPICALLTYPE *DwmSetIconicLivePreviewBitmapProc)(
      HWND, HBITMAP, POINT*, DWORD);
  DwmSetIconicLivePreviewBitmapProc dwm_set_live_preview_bitmap =
      static_cast<DwmSetIconicLivePreviewBitmapProc>(
      dwmapi.GetFunctionPointer("DwmSetIconicLivePreviewBitmap"));

  if (!dwm_set_live_preview_bitmap)
    return E_FAIL;

  return dwm_set_live_preview_bitmap(window, bitmap, client, flags);
}

// Invalidates the thumbnail image of the specified place-holder window. (See
// the comments in CallDwmSetIconicThumbnai()).
HRESULT CallDwmInvalidateIconicBitmaps(HWND window) {
  FilePath dwmapi_path(base::GetNativeLibraryName(L"dwmapi"));
  base::ScopedNativeLibrary dwmapi(dwmapi_path);

  typedef HRESULT (STDAPICALLTYPE *DwmInvalidateIconicBitmapsProc)(HWND);
  DwmInvalidateIconicBitmapsProc dwm_invalidate_iconic_bitmaps =
      static_cast<DwmInvalidateIconicBitmapsProc>(
      dwmapi.GetFunctionPointer("DwmInvalidateIconicBitmaps"));

  if (!dwm_invalidate_iconic_bitmaps)
    return E_FAIL;

  return dwm_invalidate_iconic_bitmaps(window);
}

}  // namespace

namespace {

// These callbacks indirectly access the specified tab through the
// AeroPeekWindowDelegate interface to prevent these tasks from accessing the
// deleted tabs.

// A callback that registers a thumbnail window as a child of the specified
// browser application.
void RegisterThumbnailCallback(HWND frame_window, HWND window, bool active) {
  // Set the App ID of the browser for this place-holder window to tell
  // that this window is a child of the browser application, i.e. to tell
  // that this thumbnail window should be displayed when we hover the
  // browser icon in the taskbar.
  // TODO(mattm): This should use ShellIntegration::GetChromiumAppId to work
  // properly with multiple profiles.
  ui::win::SetAppIdForWindow(
      BrowserDistribution::GetDistribution()->GetBrowserAppId(), window);

  // Register this place-holder window to the taskbar as a child of
  // the browser window and add it to the end of its tab list.
  // Correctly, this registration should be called after this browser window
  // receives a registered window message "TaskbarButtonCreated", which
  // means that Windows creates a taskbar button for this window in its
  // taskbar. But it seems to be OK to register it without checking the
  // message.
  // TODO(hbono): we need to check this registered message?
  base::win::ScopedComPtr<ITaskbarList3> taskbar;
  if (FAILED(taskbar.CreateInstance(CLSID_TaskbarList, NULL,
                                    CLSCTX_INPROC_SERVER)) ||
      FAILED(taskbar->HrInit()) ||
      FAILED(taskbar->RegisterTab(window, frame_window)) ||
      FAILED(taskbar->SetTabOrder(window, NULL)))
    return;
  if (active)
    taskbar->SetTabActive(window, frame_window, 0);
}

// Calculates the thumbnail size sent to Windows so we can preserve the pixel
// aspect-ratio of the source bitmap. Since Windows returns an error when we
// send an image bigger than the given size, we decrease either the thumbnail
// width or the thumbnail height so we can fit the longer edge of the source
// window.
void GetThumbnailSize(const gfx::Size& aeropeek_size, int width, int height,
                      gfx::Size* output) {
  float thumbnail_width = static_cast<float>(aeropeek_size.width());
  float thumbnail_height = static_cast<float>(aeropeek_size.height());
  float source_width = static_cast<float>(width);
  float source_height = static_cast<float>(height);
  DCHECK(source_width && source_height);

  float ratio_width = thumbnail_width / source_width;
  float ratio_height = thumbnail_height / source_height;
  if (ratio_width > ratio_height) {
    thumbnail_width = source_width * ratio_height;
  } else {
    thumbnail_height = source_height * ratio_width;
  }

  output->set_width(static_cast<int>(thumbnail_width));
  output->set_height(static_cast<int>(thumbnail_height));
}

// Returns a pixel of the specified bitmap. If this bitmap is a dummy bitmap,
// this function returns an opaque white pixel instead.
int GetPixel(const SkBitmap& bitmap, int x, int y) {
  const int* tab_pixels = reinterpret_cast<const int*>(bitmap.getPixels());
  if (!tab_pixels)
    return 0xFFFFFFFF;
  return tab_pixels[y * bitmap.width() + x];
}

// A callback which creates a thumbnail image used by AeroPeek and sends it to
// Windows.
void SendThumbnailCallback(
    HWND aeropeek_window, const gfx::Rect& content_bounds,
    const gfx::Size& aeropeek_size, const SkBitmap& tab_bitmap,
    base::WaitableEvent* ready) {
  // Calculate the size of the aeropeek thumbnail and resize the tab bitmap
  // to the size. When the given bitmap is an empty bitmap, we create a dummy
  // bitmap from the content-area rectangle to create a DIB. (We don't need to
  // allocate pixels for this case since we don't use them.)
  gfx::Size thumbnail_size;
  SkBitmap thumbnail_bitmap;

  if (tab_bitmap.isNull() || tab_bitmap.empty()) {
    GetThumbnailSize(
        aeropeek_size, content_bounds.width(), content_bounds.height(),
        &thumbnail_size);

    thumbnail_bitmap.setConfig(SkBitmap::kARGB_8888_Config,
                               thumbnail_size.width(), thumbnail_size.height());
  } else {
    GetThumbnailSize(aeropeek_size, tab_bitmap.width(), tab_bitmap.height(),
                     &thumbnail_size);

    thumbnail_bitmap = skia::ImageOperations::Resize(
        tab_bitmap, skia::ImageOperations::RESIZE_LANCZOS3,
        thumbnail_size.width(), thumbnail_size.height());
  }

  // Create a DIB, copy the resized image, and send the DIB to Windows.
  // We can delete this DIB after sending it to Windows since Windows creates
  // a copy of the DIB and use it.
  base::win::ScopedCreateDC hdc(CreateCompatibleDC(NULL));
  if (!hdc.Get()) {
    LOG(ERROR) << "cannot create a memory DC: " << GetLastError();
    return;
  }

  BITMAPINFOHEADER header;
  gfx::CreateBitmapHeader(thumbnail_size.width(), thumbnail_size.height(),
                          &header);

  void* bitmap_data = NULL;
  base::win::ScopedBitmap bitmap(
      CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&header),
                       DIB_RGB_COLORS, &bitmap_data, NULL, 0));

  if (!bitmap.Get() || !bitmap_data) {
    LOG(ERROR) << "cannot create a bitmap: " << GetLastError();
    return;
  }

  SkAutoLockPixels lock(thumbnail_bitmap);
  int* content_pixels = reinterpret_cast<int*>(bitmap_data);
  for (int y = 0; y < thumbnail_size.height(); ++y) {
    for (int x = 0; x < thumbnail_size.width(); ++x) {
      content_pixels[y * thumbnail_size.width() + x] =
          GetPixel(thumbnail_bitmap, x, y);
    }
  }

  HRESULT result = CallDwmSetIconicThumbnail(aeropeek_window, bitmap, 0);
  if (FAILED(result))
    LOG(ERROR) << "cannot set a tab thumbnail: " << result;

  ready->Signal();
}

int GetTabPixel(const SkBitmap& tab_bitmap, int x, int y) {
  // Return the opaque while pixel to prevent old foreground tab from being
  // shown when we cannot get the specified pixel.
  const int* tab_pixels = reinterpret_cast<int*>(tab_bitmap.getPixels());
  if (!tab_pixels || x >= tab_bitmap.width() || y >= tab_bitmap.height())
    return 0xFFFFFFFF;

  // DWM uses alpha values to distinguish opaque colors and transparent ones.
  // Set the alpha value of this source pixel to prevent the original window
  // from being shown through.
  return 0xFF000000 | tab_pixels[y * tab_bitmap.width() + x];
}

// A task which creates a preview image used by AeroPeek and sends it to
// Windows.
// This task becomes more complicated than SendThumbnailTask because this task
// calculates the rectangle of the user-perceived content area (infobars +
// content area) so Windows can paste the preview image on it.
// This task is used if an AeroPeek window receives a
// WM_DWMSENDICONICLIVEPREVIEWBITMAP message.
void SendLivePreviewCallback(
    HWND aeropeek_window, const gfx::Rect& content_bounds,
    const SkBitmap& tab_bitmap) {
  // Create a DIB for the user-perceived content area of the tab, copy the
  // tab image into the DIB, and send it to Windows.
  // We don't need to paste this tab image onto the frame image since Windows
  // automatically pastes it for us.
  base::win::ScopedCreateDC hdc(CreateCompatibleDC(NULL));
  if (!hdc.Get()) {
    LOG(ERROR) << "cannot create a memory DC: " << GetLastError();
    return;
  }

  BITMAPINFOHEADER header;
  gfx::CreateBitmapHeader(content_bounds.width(), content_bounds.height(),
                          &header);

  void* bitmap_data = NULL;
  base::win::ScopedBitmap bitmap(
      CreateDIBSection(hdc.Get(), reinterpret_cast<BITMAPINFO*>(&header),
                       DIB_RGB_COLORS, &bitmap_data, NULL, 0));
  if (!bitmap.Get() || !bitmap_data) {
    LOG(ERROR) << "cannot create a bitmap: " << GetLastError();
    return;
  }

  // Copy the tab image onto the DIB.
  SkAutoLockPixels lock(tab_bitmap);
  int* content_pixels = reinterpret_cast<int*>(bitmap_data);
  for (int y = 0; y < content_bounds.height(); ++y) {
    for (int x = 0; x < content_bounds.width(); ++x)
      content_pixels[y * content_bounds.width() + x] =
          GetTabPixel(tab_bitmap, x, y);
  }

  // Send the preview image to Windows.
  // We can set its offset to the top left corner of the user-perceived
  // content area so Windows can paste this bitmap onto the correct
  // position.
  POINT content_offset = {content_bounds.x(), content_bounds.y()};
  HRESULT result = CallDwmSetIconicLivePreviewBitmap(
      aeropeek_window, bitmap, &content_offset, 0);
  if (FAILED(result))
    LOG(ERROR) << "cannot send a content image: " << result;
}

}  // namespace

// A class which implements a place-holder window used by AeroPeek.
// The major work of this class are:
// * Updating the status of Tab Thumbnails;
// * Receiving messages from Windows, and;
// * Translating received messages for TabStrip.
// This class is used by the AeroPeekManager class, which is a proxy
// between TabStrip and Windows 7.
class AeroPeekWindow : public ui::WindowImpl {
 public:
  AeroPeekWindow(HWND frame_window,
                 AeroPeekWindowDelegate* delegate,
                 int tab_id,
                 bool tab_active,
                 const std::wstring& title,
                 const SkBitmap& favicon_bitmap);
  ~AeroPeekWindow();

  // Activates or deactivates this window.
  // This window uses this information not only for highlighting the selected
  // tab when Windows shows the thumbnail list, but also for preventing us
  // from rendering AeroPeek images for deactivated windows so often.
  void Activate();
  void Deactivate();

  // Updates the image of this window.
  // When the AeroPeekManager class calls this function, this window starts
  // a task which updates its thumbnail image.
  // NOTE: to prevent sending lots of tasks that update the thumbnail images
  // and hurt the system performance, we post a task only when |is_loading| is
  // false for non-active tabs. (On the other hand, we always post an update
  // task for an active tab as IE8 does.)
  void Update(bool is_loading);

  // Destroys this window.
  // This function removes this window from the thumbnail list and deletes
  // all the resources attached to this window, i.e. this object is not valid
  // any longer after calling this function.
  void Destroy();

  // Updates the title of this window.
  // This function just sends a WM_SETTEXT message to update the window title.
  void SetTitle(const std::wstring& title);

  // Updates the icon used for AeroPeek. Unlike SetTitle(), this function just
  // saves a copy of the given bitmap since it takes time to create a Windows
  // icon from this bitmap set it as the window icon. We will create a Windows
  // when Windows sends a WM_GETICON message to retrieve it.
  void SetFavicon(const SkBitmap& favicon);

  // Returns the tab ID associated with this window.
  int tab_id() { return tab_id_; }

  // Message handlers.
  BEGIN_MSG_MAP_EX(TabbedThumbnailWindow)
    MESSAGE_HANDLER_EX(WM_DWMSENDICONICTHUMBNAIL, OnDwmSendIconicThumbnail)
    MESSAGE_HANDLER_EX(WM_DWMSENDICONICLIVEPREVIEWBITMAP,
                       OnDwmSendIconicLivePreviewBitmap)

    MSG_WM_ACTIVATE(OnActivate)
    MSG_WM_CLOSE(OnClose)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_GETICON(OnGetIcon)
  END_MSG_MAP()

 private:
  // Updates the thumbnail image of this window.
  // This function is a wrapper function of CallDwmInvalidateIconicBitmaps()
  // but it invalidates the thumbnail only when |ready_| is signaled to prevent
  // us from posting two or more tasks.
  void UpdateThumbnail();

  // Returns the user-perceived content area.
  gfx::Rect GetContentBounds() const;

  // Message-handler functions.
  // Called when a window has been created.
  LRESULT OnCreate(LPCREATESTRUCT create_struct);

  // Called when this thumbnail window is activated, i.e. a user clicks this
  // thumbnail window.
  void OnActivate(UINT action, BOOL minimized, HWND window);

  // Called when this thumbnail window is closed, i.e. a user clicks the close
  // button of this thumbnail window.
  void OnClose();

  // Called when Windows needs a thumbnail image for this thumbnail window.
  // Windows can send a WM_DWMSENDICONICTHUMBNAIL message anytime when it
  // needs the thumbnail bitmap for this place-holder window (e.g. when we
  // register this place-holder window to Windows, etc.)
  // When this window receives a WM_DWMSENDICONICTHUMBNAIL message, it HAS TO
  // create a thumbnail bitmap and send it to Windows through a
  // DwmSendIconicThumbnail() call. (Windows shows a "page-loading" animation
  // while it waits for a thumbnail bitmap.)
  LRESULT OnDwmSendIconicThumbnail(UINT message,
                                   WPARAM wparam,
                                   LPARAM lparam);

  // Called when Windows needs a preview image for this thumbnail window.
  // Same as above, Windows can send a WM_DWMSENDICONICLIVEPREVIEWBITMAP
  // message anytime when it needs a preview bitmap and we have to create and
  // send the bitmap when it needs it.
  LRESULT OnDwmSendIconicLivePreviewBitmap(UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam);

  // Called when Windows needs an icon for this thumbnail window.
  // Windows sends a WM_GETICON message with ICON_SMALL when it needs an
  // AeroPeek icon. we handle WM_GETICON messages by ourselves so we can create
  // a custom icon from a favicon only when Windows need it.
  HICON OnGetIcon(UINT index);

 private:
  // An application window which owns this tab.
  // We show this thumbnail image of this window when a user hovers a mouse
  // cursor onto the taskbar icon of this application window.
  HWND frame_window_;

  // An interface which dispatches events received from Window.
  // This window notifies events received from Windows to TabStrip through
  // this interface.
  // We should not directly access TabContents members since Windows may send
  // AeroPeek events to a tab closed by Chrome.
  // To prevent such race condition, we get access to TabContents through
  // AeroPeekManager.
  AeroPeekWindowDelegate* delegate_;

  // A tab ID associated with this window.
  int tab_id_;

  // A flag that represents whether or not this tab is active.
  // This flag is used for preventing us from updating the thumbnail images
  // when this window is not active.
  bool tab_active_;

  // An event that represents whether or not we can post a task which updates
  // the thumbnail image of this window.
  // We post a task only when this event is signaled.
  base::WaitableEvent ready_to_update_thumbnail_;

  // The title of this tab.
  std::wstring title_;

  // The favicon for this tab.
  SkBitmap favicon_bitmap_;
  base::win::ScopedHICON favicon_;

  // The icon used by the frame window.
  // This icon is used when this tab doesn't have a favicon.
  HICON frame_icon_;

  DISALLOW_COPY_AND_ASSIGN(AeroPeekWindow);
};

AeroPeekWindow::AeroPeekWindow(HWND frame_window,
                               AeroPeekWindowDelegate* delegate,
                               int tab_id,
                               bool tab_active,
                               const std::wstring& title,
                               const SkBitmap& favicon_bitmap)
    : frame_window_(frame_window),
      delegate_(delegate),
      tab_id_(tab_id),
      tab_active_(tab_active),
      ready_to_update_thumbnail_(false, true),
      title_(title),
      favicon_bitmap_(favicon_bitmap),
      frame_icon_(NULL) {
  // Set the class styles and window styles for this thumbnail window.
  // An AeroPeek window should be a tool window. (Otherwise,
  // Windows doesn't send WM_DWMSENDICONICTHUMBNAIL messages.)
  set_initial_class_style(0);
  set_window_style(WS_POPUP | WS_BORDER | WS_SYSMENU | WS_CAPTION);
  set_window_ex_style(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
}

AeroPeekWindow::~AeroPeekWindow() {
}

void AeroPeekWindow::Activate() {
  tab_active_ = true;

  // Create a place-holder window and add it to the tab list if it has not been
  // created yet. (This case happens when we re-attached a detached window.)
  if (!IsWindow(hwnd())) {
    Update(false);
    return;
  }

  // Notify Windows to set the thumbnail focus to this window.
  base::win::ScopedComPtr<ITaskbarList3> taskbar;
  HRESULT result = taskbar.CreateInstance(CLSID_TaskbarList, NULL,
                                          CLSCTX_INPROC_SERVER);
  if (FAILED(result)) {
    LOG(ERROR) << "failed creating an ITaskbarList3 interface.";
    return;
  }

  result = taskbar->HrInit();
  if (FAILED(result)) {
    LOG(ERROR) << "failed initializing an ITaskbarList3 interface.";
    return;
  }

  result = taskbar->ActivateTab(hwnd());
  if (FAILED(result)) {
    LOG(ERROR) << "failed activating a thumbnail window.";
    return;
  }

  // Update the thumbnail image to the up-to-date one.
  UpdateThumbnail();
}

void AeroPeekWindow::Deactivate() {
  tab_active_ = false;
}

void AeroPeekWindow::Update(bool is_loading) {
  // Create a place-holder window used by AeroPeek if it has not been created
  // so Windows can send events used by AeroPeek to this window.
  // Windows automatically sends a WM_DWMSENDICONICTHUMBNAIL message after this
  // window is registered to Windows. So, we don't have to invalidate the
  // thumbnail image of this window now.
  if (!hwnd()) {
    gfx::Rect bounds;
    WindowImpl::Init(frame_window_, bounds);
    return;
  }

  // Invalidate the thumbnail image of this window.
  // When we invalidate the thumbnail image, we HAVE TO handle a succeeding
  // WM_DWMSENDICONICTHUMBNAIL message and update the thumbnail image with a
  // DwmSetIconicThumbnail() call. So, we should not call this function when
  // we don't have enough information to create a thumbnail.
  if (tab_active_ || !is_loading)
    UpdateThumbnail();
}

void AeroPeekWindow::Destroy() {
  if (!IsWindow(hwnd()))
    return;

  // Remove this window from the tab list of Windows.
  base::win::ScopedComPtr<ITaskbarList3> taskbar;
  HRESULT result = taskbar.CreateInstance(CLSID_TaskbarList, NULL,
                                          CLSCTX_INPROC_SERVER);
  if (FAILED(result))
    return;

  result = taskbar->HrInit();
  if (FAILED(result))
    return;

  result = taskbar->UnregisterTab(hwnd());

  // Destroy this window.
  DestroyWindow(hwnd());
}

void AeroPeekWindow::SetTitle(const std::wstring& title) {
  title_ = title;
}

void AeroPeekWindow::SetFavicon(const SkBitmap& favicon) {
  favicon_bitmap_ = favicon;
}

void AeroPeekWindow::UpdateThumbnail() {
  // We post a task to actually create a new thumbnail. So, this function may
  // be called while we are creating a thumbnail. To prevent this window from
  // posting two or more tasks, we don't invalidate the current thumbnail
  // when this event is not signaled.
  if (ready_to_update_thumbnail_.IsSignaled())
    CallDwmInvalidateIconicBitmaps(hwnd());
}

gfx::Rect AeroPeekWindow::GetContentBounds() const {
  RECT content_rect;
  GetClientRect(frame_window_, &content_rect);

  gfx::Insets content_insets;
  delegate_->GetContentInsets(&content_insets);

  gfx::Rect content_bounds(content_rect);
  content_bounds.Inset(content_insets.left(),
                       content_insets.top(),
                       content_insets.right(),
                       content_insets.bottom());
  return content_bounds;
}

// message handlers

void AeroPeekWindow::OnActivate(UINT action,
                                BOOL minimized,
                                HWND window) {
  // Windows sends a WM_ACTIVATE message not only when a user clicks this
  // window (i.e. this window gains the thumbnail focus) but also a user clicks
  // another window (i.e. this window loses the thumbnail focus.)
  // Return when this window loses the thumbnail focus since we don't have to
  // do anything for this case.
  if (action == WA_INACTIVE)
    return;

  // Ask Chrome to activate the tab associated with this thumbnail window.
  // Since TabStripModel calls AeroPeekManager::ActiveTabChanged() when it
  // finishes activating the tab. We will move the tab focus of AeroPeek there.
  if (delegate_)
    delegate_->ActivateTab(tab_id_);
}

LRESULT AeroPeekWindow::OnCreate(LPCREATESTRUCT create_struct) {
  // Initialize the window title now since WindowImpl::Init() always calls
  // CreateWindowEx() with its window name NULL.
  if (!title_.empty()) {
    SendMessage(hwnd(), WM_SETTEXT, 0,
                reinterpret_cast<LPARAM>(title_.c_str()));
  }

  // Window attributes for DwmSetWindowAttribute().
  // These enum values are copied from Windows SDK 7 so we can compile this
  // file with or without it.
  // TODO(hbono): Bug 16903: to be deleted when we use Windows SDK 7.
  enum {
    DWMWA_NCRENDERING_ENABLED = 1,
    DWMWA_NCRENDERING_POLICY,
    DWMWA_TRANSITIONS_FORCEDISABLED,
    DWMWA_ALLOW_NCPAINT,
    DWMWA_CAPTION_BUTTON_BOUNDS,
    DWMWA_NONCLIENT_RTL_LAYOUT,
    DWMWA_FORCE_ICONIC_REPRESENTATION,
    DWMWA_FLIP3D_POLICY,
    DWMWA_EXTENDED_FRAME_BOUNDS,
    DWMWA_HAS_ICONIC_BITMAP,
    DWMWA_DISALLOW_PEEK,
    DWMWA_EXCLUDED_FROM_PEEK,
    DWMWA_LAST
  };

  // Set DWM attributes to tell Windows that this window can provide the
  // bitmaps used by AeroPeek.
  BOOL force_iconic_representation = TRUE;
  DwmSetWindowAttribute(hwnd(),
                        DWMWA_FORCE_ICONIC_REPRESENTATION,
                        &force_iconic_representation,
                        sizeof(force_iconic_representation));

  BOOL has_iconic_bitmap = TRUE;
  DwmSetWindowAttribute(hwnd(),
                        DWMWA_HAS_ICONIC_BITMAP,
                        &has_iconic_bitmap,
                        sizeof(has_iconic_bitmap));

  // Post a task that registers this thumbnail window to Windows because it
  // may take some time. (For example, when we create an ITaskbarList3
  // interface for the first time, Windows loads DLLs and we need to wait for
  // some time.)
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&RegisterThumbnailCallback, frame_window_, hwnd(),
                 tab_active_));

  return 0;
}

void AeroPeekWindow::OnClose() {
  // Unregister this window from the tab list of Windows and destroy this
  // window.
  // The resources attached to this object will be deleted when TabStrip calls
  // AeroPeekManager::TabClosingAt(). (Please read the comment in TabClosingAt()
  // for its details.)
  Destroy();

  // Ask AeroPeekManager to close the tab associated with this thumbnail
  // window.
  if (delegate_)
    delegate_->CloseTab(tab_id_);
}

LRESULT AeroPeekWindow::OnDwmSendIconicThumbnail(UINT message,
                                                 WPARAM wparam,
                                                 LPARAM lparam) {
  // Update the window title to synchronize the title.
  SendMessage(hwnd(), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(title_.c_str()));

  // Create an I/O task since it takes long time to resize these images and
  // send them to Windows. This task signals |ready_to_update_thumbnail_| in
  // its destructor to notify us when this task has been finished. (We create an
  // I/O task even when the given thumbnail is empty to stop the "loading"
  // animation.)
  DCHECK(delegate_);

  SkBitmap thumbnail;
  delegate_->GetTabThumbnail(tab_id_, &thumbnail);

  gfx::Size aeropeek_size(HIWORD(lparam), LOWORD(lparam));
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SendThumbnailCallback, hwnd(), GetContentBounds(),
                 aeropeek_size, thumbnail, &ready_to_update_thumbnail_));
  return 0;
}

LRESULT AeroPeekWindow::OnDwmSendIconicLivePreviewBitmap(UINT message,
                                                         WPARAM wparam,
                                                         LPARAM lparam) {
  // Same as OnDwmSendIconicThumbnail(), we create an I/O task which creates
  // a preview image used by AeroPeek and send it to Windows. Unlike
  // OnDwmSendIconicThumbnail(), we don't have to use events for preventing this
  // window from sending two or more tasks because Windows doesn't send
  // WM_DWMSENDICONICLIVEPREVIEWBITMAP messages before we send the preview image
  // to Windows.
  DCHECK(delegate_);

  SkBitmap preview;
  delegate_->GetTabPreview(tab_id_, &preview);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SendLivePreviewCallback, hwnd(), GetContentBounds(),
                 preview));

  return 0;
}

HICON AeroPeekWindow::OnGetIcon(UINT index) {
  // Return the application icon if this window doesn't have favicons.
  // We save this application icon to avoid calling LoadIcon() twice or more.
  if (favicon_bitmap_.isNull()) {
    if (!frame_icon_) {
      frame_icon_ = GetAppIcon();
    }
    return frame_icon_;
  }

  // Create a Windows icon from SkBitmap and send it to Windows. We set this
  // icon to the ScopedIcon object to delete it in the destructor.
  favicon_.Set(IconUtil::CreateHICONFromSkBitmap(favicon_bitmap_));
  return favicon_.Get();
}

AeroPeekManager::AeroPeekManager(HWND application_window)
    : application_window_(application_window),
      border_left_(0),
      border_top_(0),
      toolbar_top_(0) {
}

AeroPeekManager::~AeroPeekManager() {
  // Delete all AeroPeekWindow objects.
  for (std::list<AeroPeekWindow*>::iterator i = tab_list_.begin();
       i != tab_list_.end(); ++i) {
    AeroPeekWindow* window = *i;
    delete window;
  }
}

void AeroPeekManager::SetContentInsets(const gfx::Insets& insets) {
  content_insets_ = insets;
}

// static
bool AeroPeekManager::Enabled() {
  // We enable our custom AeroPeek only when:
  // * Chrome is running on Windows 7 and Aero is enabled,
  // * Chrome is not launched in application mode, and
  // * Chrome is launched with the "--enable-aero-peek-tabs" option.
  // TODO(hbono): Bug 37957 <http://crbug.com/37957>: find solutions that avoid
  // flooding users with tab thumbnails.
  const CommandLine* command_line = CommandLine::ForCurrentProcess();
  return base::win::GetVersion() >= base::win::VERSION_WIN7 &&
      views::NativeWidgetWin::IsAeroGlassEnabled() &&
      !command_line->HasSwitch(switches::kApp) &&
      command_line->HasSwitch(switches::kEnableAeroPeekTabs);
}

void AeroPeekManager::DeleteAeroPeekWindow(int tab_id) {
  // This function does NOT call AeroPeekWindow::Destroy() before deleting
  // the AeroPeekWindow instance.
  for (std::list<AeroPeekWindow*>::iterator i = tab_list_.begin();
       i != tab_list_.end(); ++i) {
    AeroPeekWindow* window = *i;
    if (window->tab_id() == tab_id) {
      tab_list_.erase(i);
      delete window;
      return;
    }
  }
}

void AeroPeekManager::DeleteAeroPeekWindowForTab(TabContentsWrapper* tab) {
  // Delete the AeroPeekWindow object associated with this tab and all its
  // resources. (AeroPeekWindow::Destory() also removes this tab from the tab
  // list of Windows.)
  AeroPeekWindow* window = GetAeroPeekWindow(GetTabID(tab));
  if (!window)
    return;

  window->Destroy();
  DeleteAeroPeekWindow(GetTabID(tab));
}

AeroPeekWindow* AeroPeekManager::GetAeroPeekWindow(int tab_id) const {
  size_t size = tab_list_.size();
  for (std::list<AeroPeekWindow*>::const_iterator i = tab_list_.begin();
       i != tab_list_.end(); ++i) {
    AeroPeekWindow* window = *i;
    if (window->tab_id() == tab_id)
      return window;
  }
  return NULL;
}

void AeroPeekManager::CreateAeroPeekWindowIfNecessary(TabContentsWrapper* tab,
                                                      bool foreground) {
  if (GetAeroPeekWindow(GetTabID(tab)))
    return;

  AeroPeekWindow* window =
      new AeroPeekWindow(application_window_,
                         this,
                         GetTabID(tab),
                         foreground,
                         tab->web_contents()->GetTitle(),
                         tab->favicon_tab_helper()->GetFavicon());
  tab_list_.push_back(window);
}

WebContents* AeroPeekManager::GetWebContents(int tab_id) const {
  for (TabContentsIterator iterator; !iterator.done(); ++iterator) {
    if (GetTabID(*iterator) == tab_id)
      return (*iterator)->web_contents();
  }
  return NULL;
}

int AeroPeekManager::GetTabID(TabContentsWrapper* contents) const {
  if (!contents)
    return -1;
  return contents->restore_tab_helper()->session_id().id();
}

///////////////////////////////////////////////////////////////////////////////
// AeroPeekManager, TabStripModelObserver implementation:

void AeroPeekManager::TabInsertedAt(TabContentsWrapper* contents,
                                    int index,
                                    bool foreground) {
  if (!contents)
    return;

  CreateAeroPeekWindowIfNecessary(contents, foreground);
}

void AeroPeekManager::TabDetachedAt(TabContentsWrapper* contents, int index) {
  if (!contents)
    return;

  // Chrome will call TabInsertedAt() when this tab is inserted to another
  // TabStrip. We will re-create an AeroPeekWindow object for this tab and
  // re-add it to the tab list there.
  DeleteAeroPeekWindowForTab(contents);
}

void AeroPeekManager::ActiveTabChanged(TabContentsWrapper* old_contents,
                                       TabContentsWrapper* new_contents,
                                       int index,
                                       bool user_gesture) {
  // Deactivate the old window in the thumbnail list and activate the new one
  // to synchronize the thumbnail list with TabStrip.
  if (old_contents) {
    AeroPeekWindow* old_window = GetAeroPeekWindow(GetTabID(old_contents));
    if (old_window)
      old_window->Deactivate();
  }

  if (new_contents) {
    AeroPeekWindow* new_window = GetAeroPeekWindow(GetTabID(new_contents));
    if (new_window)
      new_window->Activate();
  }
}

void AeroPeekManager::TabReplacedAt(TabStripModel* tab_strip_model,
                                    TabContentsWrapper* old_contents,
                                    TabContentsWrapper* new_contents,
                                    int index) {
  DeleteAeroPeekWindowForTab(old_contents);

  CreateAeroPeekWindowIfNecessary(new_contents,
                                  (index == tab_strip_model->active_index()));
  // We don't need to update the selection as if |new_contents| is selected the
  // TabStripModel will send ActiveTabChanged.
}

void AeroPeekManager::TabMoved(TabContentsWrapper* contents,
                               int from_index,
                               int to_index,
                               bool pinned_state_changed) {
  // TODO(hbono): we need to reorder the thumbnail list of Windows here?
  // (Unfortunately, it is not so trivial to reorder the thumbnail list when
  // we detach/attach tabs.)
}

void AeroPeekManager::TabChangedAt(TabContentsWrapper* contents,
                                   int index,
                                   TabChangeType change_type) {
  if (!contents)
    return;

  // Retrieve the AeroPeekWindow object associated with this tab, update its
  // title, and post a task that update its thumbnail image if necessary.
  AeroPeekWindow* window = GetAeroPeekWindow(GetTabID(contents));
  if (!window)
    return;

  // Update the title, the favicon, and the thumbnail used for AeroPeek.
  // These function don't actually update the icon and the thumbnail until
  // Windows needs them (e.g. when a user hovers a taskbar icon) to avoid
  // hurting the rendering performance. (These functions just save the
  // information needed for handling update requests from Windows.)
  window->SetTitle(contents->web_contents()->GetTitle());
  window->SetFavicon(contents->favicon_tab_helper()->GetFavicon());
  window->Update(contents->web_contents()->IsLoading());
}

///////////////////////////////////////////////////////////////////////////////
// AeroPeekManager, AeroPeekWindowDelegate implementation:

void AeroPeekManager::ActivateTab(int tab_id) {
  // Ask TabStrip to activate this tab.
  // We don't have to update thumbnails now since TabStrip will call
  // ActiveTabChanged() when it actually activates this tab.
  WebContents* contents = GetWebContents(tab_id);
  if (contents && contents->GetDelegate())
    contents->GetDelegate()->ActivateContents(contents);
}

void AeroPeekManager::CloseTab(int tab_id) {
  // Ask TabStrip to close this tab.
  // TabStrip will call TabClosingAt() when it actually closes this tab. We
  // will delete the AeroPeekWindow object attached to this tab there.
  WebContents* contents = GetWebContents(tab_id);
  if (contents && contents->GetDelegate())
    contents->GetDelegate()->CloseContents(contents);
}

void AeroPeekManager::GetContentInsets(gfx::Insets* insets) {
  *insets = content_insets_;
}

bool AeroPeekManager::GetTabThumbnail(int tab_id, SkBitmap* thumbnail) {
  DCHECK(thumbnail);

  // Copy the thumbnail image and the favicon of this tab. We will resize the
  // images and send them to Windows.
  WebContents* contents = GetWebContents(tab_id);
  if (!contents)
    return false;

  ThumbnailGenerator* generator = g_browser_process->GetThumbnailGenerator();
  DCHECK(generator);
  *thumbnail = generator->GetThumbnailForRenderer(
      contents->GetRenderViewHost());

  return true;
}

bool AeroPeekManager::GetTabPreview(int tab_id, SkBitmap* preview) {
  DCHECK(preview);

  // Retrieve the BackingStore associated with the given tab and return its
  // SkPlatformCanvas.
  WebContents* contents = GetWebContents(tab_id);
  if (!contents)
    return false;

  RenderViewHost* render_view_host = contents->GetRenderViewHost();
  if (!render_view_host)
    return false;

  BackingStore* backing_store = render_view_host->GetBackingStore(false);
  if (!backing_store)
    return false;

  // Create a copy of this BackingStore image.
  // This code is just copied from "thumbnail_generator.cc".
  skia::PlatformCanvas canvas;
  if (!backing_store->CopyFromBackingStore(gfx::Rect(backing_store->size()),
                                           &canvas))
    return false;

  const SkBitmap& bitmap = skia::GetTopDevice(canvas)->accessBitmap(false);
  bitmap.copyTo(preview, SkBitmap::kARGB_8888_Config);
  return true;
}
