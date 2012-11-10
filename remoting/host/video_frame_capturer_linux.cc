// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/video_frame_capturer.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>

#include <set>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "remoting/base/capture_data.h"
#include "remoting/host/differ.h"
#include "remoting/host/video_frame_capturer_helper.h"
#include "remoting/host/x_server_pixel_buffer.h"
#include "remoting/proto/control.pb.h"

namespace remoting {

namespace {

static const int kBytesPerPixel = 4;

// Default to false, since many systems have broken XDamage support - see
// http://crbug.com/73423.
static bool g_should_use_x_damage = false;

static bool ShouldUseXDamage() {
  return g_should_use_x_damage;
}

// A class representing a full-frame pixel buffer
class VideoFrameBuffer {
 public:
  VideoFrameBuffer()
      : size_(SkISize::Make(0, 0)),
        bytes_per_row_(0),
        needs_update_(true) {
  }

  void Update(Display* display, Window root_window) {
    if (needs_update_) {
      needs_update_ = false;
      XWindowAttributes root_attr;
      XGetWindowAttributes(display, root_window, &root_attr);
      if (root_attr.width != size_.width() ||
          root_attr.height != size_.height()) {
        size_.set(root_attr.width, root_attr.height);
        bytes_per_row_ = size_.width() * kBytesPerPixel;
        size_t buffer_size = size_.width() * size_.height() * kBytesPerPixel;
        ptr_.reset(new uint8[buffer_size]);
      }
    }
  }

  SkISize size() const { return size_; }
  int bytes_per_row() const { return bytes_per_row_; }
  uint8* ptr() const { return ptr_.get(); }

  void set_needs_update() { needs_update_ = true; }

 private:
  SkISize size_;
  int bytes_per_row_;
  scoped_array<uint8> ptr_;
  bool needs_update_;

  DISALLOW_COPY_AND_ASSIGN(VideoFrameBuffer);
};

// A class to perform video frame capturing for Linux.
class VideoFrameCapturerLinux : public VideoFrameCapturer {
 public:
  VideoFrameCapturerLinux();
  virtual ~VideoFrameCapturerLinux();

  bool Init();  // TODO(ajwong): Do we really want this to be synchronous?

  // Capturer interface.
  virtual void Start(const CursorShapeChangedCallback& callback) OVERRIDE;
  virtual void Stop() OVERRIDE;
  virtual media::VideoFrame::Format pixel_format() const OVERRIDE;
  virtual void InvalidateRegion(const SkRegion& invalid_region) OVERRIDE;
  virtual void CaptureInvalidRegion(
      const CaptureCompletedCallback& callback) OVERRIDE;
  virtual const SkISize& size_most_recent() const OVERRIDE;

 private:
  void InitXDamage();

  // Read and handle all currently-pending XEvents.
  // In the DAMAGE case, process the XDamage events and store the resulting
  // damage rectangles in the VideoFrameCapturerHelper.
  // In all cases, call ScreenConfigurationChanged() in response to any
  // ConfigNotify events.
  void ProcessPendingXEvents();

  // Capture screen pixels, and return the data in a new CaptureData object,
  // to be freed by the caller.
  // In the DAMAGE case, the VideoFrameCapturerHelper already holds the list of
  // invalid rectangles from ProcessPendingXEvents().
  // In the non-DAMAGE case, this captures the whole screen, then calculates
  // some invalid rectangles that include any differences between this and the
  // previous capture.
  CaptureData* CaptureFrame();

  // Capture the cursor image and call the CursorShapeChangedCallback if it
  // has been set (using SetCursorShapeChangedCallback).
  void CaptureCursor();

  // Called when the screen configuration is changed.
  void ScreenConfigurationChanged();

  // Synchronize the current buffer with |last_buffer_|, by copying pixels from
  // the area of |last_invalid_rects|.
  // Note this only works on the assumption that kNumBuffers == 2, as
  // |last_invalid_rects| holds the differences from the previous buffer and
  // the one prior to that (which will then be the current buffer).
  void SynchronizeFrame();

  void DeinitXlib();

  // Capture a rectangle from |x_server_pixel_buffer_|, and copy the data into
  // |capture_data|.
  void CaptureRect(const SkIRect& rect, CaptureData* capture_data);

  // We expose two forms of blitting to handle variations in the pixel format.
  // In FastBlit, the operation is effectively a memcpy.
  void FastBlit(uint8* image, const SkIRect& rect, CaptureData* capture_data);
  void SlowBlit(uint8* image, const SkIRect& rect, CaptureData* capture_data);

  // X11 graphics context.
  Display* display_;
  GC gc_;
  Window root_window_;

  // XFixes.
  bool has_xfixes_;
  int xfixes_event_base_;
  int xfixes_error_base_;

  // XDamage information.
  bool use_damage_;
  Damage damage_handle_;
  int damage_event_base_;
  int damage_error_base_;
  XserverRegion damage_region_;

  // Access to the X Server's pixel buffer.
  XServerPixelBuffer x_server_pixel_buffer_;

  // A thread-safe list of invalid rectangles, and the size of the most
  // recently captured screen.
  VideoFrameCapturerHelper helper_;

  // Callback notified whenever the cursor shape is changed.
  CursorShapeChangedCallback cursor_shape_changed_callback_;

  // Capture state.
  static const int kNumBuffers = 2;
  VideoFrameBuffer buffers_[kNumBuffers];
  int current_buffer_;

  // Format of pixels returned in buffer.
  media::VideoFrame::Format pixel_format_;

  // Invalid region from the previous capture. This is used to synchronize the
  // current with the last buffer used.
  SkRegion last_invalid_region_;

  // Last capture buffer used.
  uint8* last_buffer_;

  // |Differ| for use when polling for changes.
  scoped_ptr<Differ> differ_;

  DISALLOW_COPY_AND_ASSIGN(VideoFrameCapturerLinux);
};

VideoFrameCapturerLinux::VideoFrameCapturerLinux()
    : display_(NULL),
      gc_(NULL),
      root_window_(BadValue),
      has_xfixes_(false),
      xfixes_event_base_(-1),
      xfixes_error_base_(-1),
      use_damage_(false),
      damage_handle_(0),
      damage_event_base_(-1),
      damage_error_base_(-1),
      damage_region_(0),
      current_buffer_(0),
      pixel_format_(media::VideoFrame::RGB32),
      last_buffer_(NULL) {
  helper_.SetLogGridSize(4);
}

VideoFrameCapturerLinux::~VideoFrameCapturerLinux() {
  DeinitXlib();
}

bool VideoFrameCapturerLinux::Init() {
  // TODO(ajwong): We should specify the display string we are attaching to
  // in the constructor.
  display_ = XOpenDisplay(NULL);
  if (!display_) {
    LOG(ERROR) << "Unable to open display";
    return false;
  }

  x_server_pixel_buffer_.Init(display_);

  root_window_ = RootWindow(display_, DefaultScreen(display_));
  if (root_window_ == BadValue) {
    LOG(ERROR) << "Unable to get the root window";
    DeinitXlib();
    return false;
  }

  gc_ = XCreateGC(display_, root_window_, 0, NULL);
  if (gc_ == NULL) {
    LOG(ERROR) << "Unable to get graphics context";
    DeinitXlib();
    return false;
  }

  // Check for XFixes extension. This is required for cursor shape
  // notifications, and for our use of XDamage.
  if (XFixesQueryExtension(display_, &xfixes_event_base_,
                           &xfixes_error_base_)) {
    has_xfixes_ = true;
  } else {
    LOG(INFO) << "X server does not support XFixes.";
  }

  if (ShouldUseXDamage()) {
    InitXDamage();
  }

  // Register for changes to the dimensions of the root window.
  XSelectInput(display_, root_window_, StructureNotifyMask);

  if (has_xfixes_) {
    // Register for changes to the cursor shape.
    XFixesSelectCursorInput(display_, root_window_,
                            XFixesDisplayCursorNotifyMask);
  }

  return true;
}

void VideoFrameCapturerLinux::InitXDamage() {
  // Our use of XDamage requires XFixes.
  if (!has_xfixes_) {
    return;
  }

  // Check for XDamage extension.
  if (!XDamageQueryExtension(display_, &damage_event_base_,
                             &damage_error_base_)) {
    LOG(INFO) << "X server does not support XDamage.";
    return;
  }

  // TODO(lambroslambrou): Disable DAMAGE in situations where it is known
  // to fail, such as when Desktop Effects are enabled, with graphics
  // drivers (nVidia, ATI) that fail to report DAMAGE notifications
  // properly.

  // Request notifications every time the screen becomes damaged.
  damage_handle_ = XDamageCreate(display_, root_window_,
                                 XDamageReportNonEmpty);
  if (!damage_handle_) {
    LOG(ERROR) << "Unable to initialize XDamage.";
    return;
  }

  // Create an XFixes server-side region to collate damage into.
  damage_region_ = XFixesCreateRegion(display_, 0, 0);
  if (!damage_region_) {
    XDamageDestroy(display_, damage_handle_);
    LOG(ERROR) << "Unable to create XFixes region.";
    return;
  }

  use_damage_ = true;
  LOG(INFO) << "Using XDamage extension.";
}

void VideoFrameCapturerLinux::Start(
    const CursorShapeChangedCallback& callback) {
  cursor_shape_changed_callback_ = callback;
}

void VideoFrameCapturerLinux::Stop() {
}

media::VideoFrame::Format VideoFrameCapturerLinux::pixel_format() const {
  return pixel_format_;
}

void VideoFrameCapturerLinux::InvalidateRegion(const SkRegion& invalid_region) {
  helper_.InvalidateRegion(invalid_region);
}

void VideoFrameCapturerLinux::CaptureInvalidRegion(
    const CaptureCompletedCallback& callback) {
  // Process XEvents for XDamage and cursor shape tracking.
  ProcessPendingXEvents();

  // Resize the current buffer if there was a recent change of
  // screen-resolution.
  VideoFrameBuffer &current = buffers_[current_buffer_];
  current.Update(display_, root_window_);
  // Also refresh the Differ helper used by CaptureFrame(), if needed.
  if (!use_damage_ && !last_buffer_) {
    differ_.reset(new Differ(current.size().width(), current.size().height(),
                             kBytesPerPixel, current.bytes_per_row()));
  }

  scoped_refptr<CaptureData> capture_data(CaptureFrame());

  current_buffer_ = (current_buffer_ + 1) % kNumBuffers;

  callback.Run(capture_data);
}

void VideoFrameCapturerLinux::ProcessPendingXEvents() {
  // Find the number of events that are outstanding "now."  We don't just loop
  // on XPending because we want to guarantee this terminates.
  int events_to_process = XPending(display_);
  XEvent e;

  for (int i = 0; i < events_to_process; i++) {
    XNextEvent(display_, &e);
    if (use_damage_ && (e.type == damage_event_base_ + XDamageNotify)) {
      XDamageNotifyEvent* event = reinterpret_cast<XDamageNotifyEvent*>(&e);
      DCHECK(event->level == XDamageReportNonEmpty);
    } else if (e.type == ConfigureNotify) {
      ScreenConfigurationChanged();
    } else if (has_xfixes_ &&
               e.type == xfixes_event_base_ + XFixesCursorNotify) {
      XFixesCursorNotifyEvent* cne;
      cne = reinterpret_cast<XFixesCursorNotifyEvent*>(&e);
      if (cne->subtype == XFixesDisplayCursorNotify) {
        CaptureCursor();
      }
    } else {
      LOG(WARNING) << "Got unknown event type: " << e.type;
    }
  }
}

void VideoFrameCapturerLinux::CaptureCursor() {
  DCHECK(has_xfixes_);
  if (cursor_shape_changed_callback_.is_null())
    return;

  XFixesCursorImage* img = XFixesGetCursorImage(display_);
  if (!img) {
    return;
  }

  int width = img->width;
  int height = img->height;
  int total_bytes = width * height * kBytesPerPixel;

  scoped_ptr<protocol::CursorShapeInfo> cursor_proto(
      new protocol::CursorShapeInfo());
  cursor_proto->set_width(width);
  cursor_proto->set_height(height);
  cursor_proto->set_hotspot_x(img->xhot);
  cursor_proto->set_hotspot_y(img->yhot);

  cursor_proto->mutable_data()->resize(total_bytes);
  uint8* proto_data = const_cast<uint8*>(reinterpret_cast<const uint8*>(
      cursor_proto->mutable_data()->data()));

  // Xlib stores 32-bit data in longs, even if longs are 64-bits long.
  unsigned long* src = img->pixels;
  uint32* dst = reinterpret_cast<uint32*>(proto_data);
  uint32* dst_end = dst + (width * height);
  while (dst < dst_end) {
    *dst++ = static_cast<uint32>(*src++);
  }
  XFree(img);

  cursor_shape_changed_callback_.Run(cursor_proto.Pass());
}

CaptureData* VideoFrameCapturerLinux::CaptureFrame() {
  VideoFrameBuffer& buffer = buffers_[current_buffer_];
  DataPlanes planes;
  planes.data[0] = buffer.ptr();
  planes.strides[0] = buffer.bytes_per_row();

  CaptureData* capture_data = new CaptureData(planes, buffer.size(),
                                              media::VideoFrame::RGB32);

  // Pass the screen size to the helper, so it can clip the invalid region if it
  // expands that region to a grid.
  helper_.set_size_most_recent(capture_data->size());

  // In the DAMAGE case, ensure the frame is up-to-date with the previous frame
  // if any.  If there isn't a previous frame, that means a screen-resolution
  // change occurred, and |invalid_rects| will be updated to include the whole
  // screen.
  if (use_damage_ && last_buffer_)
    SynchronizeFrame();

  SkRegion invalid_region;

  x_server_pixel_buffer_.Synchronize();
  if (use_damage_ && last_buffer_) {
    // Atomically fetch and clear the damage region.
    XDamageSubtract(display_, damage_handle_, None, damage_region_);
    int nRects = 0;
    XRectangle bounds;
    XRectangle* rects = XFixesFetchRegionAndBounds(display_, damage_region_,
                                                   &nRects, &bounds);
    for (int i=0; i<nRects; ++i) {
      invalid_region.op(SkIRect::MakeXYWH(rects[i].x, rects[i].y,
                                          rects[i].width, rects[i].height),
                        SkRegion::kUnion_Op);
    }
    XFree(rects);
    helper_.InvalidateRegion(invalid_region);

    // Capture the damaged portions of the desktop.
    helper_.SwapInvalidRegion(&invalid_region);
    for (SkRegion::Iterator it(invalid_region); !it.done(); it.next()) {
      CaptureRect(it.rect(), capture_data);
    }
  } else {
    // Doing full-screen polling, or this is the first capture after a
    // screen-resolution change.  In either case, need a full-screen capture.
    SkIRect screen_rect = SkIRect::MakeWH(buffer.size().width(),
                                          buffer.size().height());
    CaptureRect(screen_rect, capture_data);

    if (last_buffer_) {
      // Full-screen polling, so calculate the invalid rects here, based on the
      // changed pixels between current and previous buffers.
      DCHECK(differ_ != NULL);
      differ_->CalcDirtyRegion(last_buffer_, buffer.ptr(), &invalid_region);
    } else {
      // No previous buffer, so always invalidate the whole screen, whether
      // or not DAMAGE is being used.  DAMAGE doesn't necessarily send a
      // full-screen notification after a screen-resolution change, so
      // this is done here.
      invalid_region.op(screen_rect, SkRegion::kUnion_Op);
    }
  }

  capture_data->mutable_dirty_region() = invalid_region;
  last_invalid_region_ = invalid_region;
  last_buffer_ = buffer.ptr();
  return capture_data;
}

void VideoFrameCapturerLinux::ScreenConfigurationChanged() {
  last_buffer_ = NULL;
  for (int i = 0; i < kNumBuffers; ++i) {
    buffers_[i].set_needs_update();
  }
  helper_.ClearInvalidRegion();
  x_server_pixel_buffer_.Init(display_);
}

void VideoFrameCapturerLinux::SynchronizeFrame() {
  // Synchronize the current buffer with the previous one since we do not
  // capture the entire desktop. Note that encoder may be reading from the
  // previous buffer at this time so thread access complaints are false
  // positives.

  // TODO(hclam): We can reduce the amount of copying here by subtracting
  // |capturer_helper_|s region from |last_invalid_region_|.
  // http://crbug.com/92354
  DCHECK(last_buffer_);
  VideoFrameBuffer& buffer = buffers_[current_buffer_];
  for (SkRegion::Iterator it(last_invalid_region_); !it.done(); it.next()) {
    const SkIRect& r = it.rect();
    int offset = r.fTop * buffer.bytes_per_row() + r.fLeft * kBytesPerPixel;
    for (int i = 0; i < r.height(); ++i) {
      memcpy(buffer.ptr() + offset, last_buffer_ + offset,
             r.width() * kBytesPerPixel);
      offset += buffer.size().width() * kBytesPerPixel;
    }
  }
}

void VideoFrameCapturerLinux::DeinitXlib() {
  if (gc_) {
    XFreeGC(display_, gc_);
    gc_ = NULL;
  }

  x_server_pixel_buffer_.Release();

  if (display_) {
    if (damage_handle_)
      XDamageDestroy(display_, damage_handle_);
    if (damage_region_)
      XFixesDestroyRegion(display_, damage_region_);
    XCloseDisplay(display_);
    display_ = NULL;
    damage_handle_ = 0;
    damage_region_ = 0;
  }
}

void VideoFrameCapturerLinux::CaptureRect(const SkIRect& rect,
                                          CaptureData* capture_data) {
  uint8* image = x_server_pixel_buffer_.CaptureRect(rect);
  int depth = x_server_pixel_buffer_.GetDepth();
  int bpp = x_server_pixel_buffer_.GetBitsPerPixel();
  bool is_rgb = x_server_pixel_buffer_.IsRgb();
  if ((depth == 24 || depth == 32) && bpp == 32 && is_rgb) {
    DVLOG(3) << "Fast blitting";
    FastBlit(image, rect, capture_data);
  } else {
    DVLOG(3) << "Slow blitting";
    SlowBlit(image, rect, capture_data);
  }
}

void VideoFrameCapturerLinux::FastBlit(uint8* image, const SkIRect& rect,
                                       CaptureData* capture_data) {
  uint8* src_pos = image;
  int src_stride = x_server_pixel_buffer_.GetStride();
  int dst_x = rect.fLeft, dst_y = rect.fTop;

  DataPlanes planes = capture_data->data_planes();
  uint8* dst_buffer = planes.data[0];

  const int dst_stride = planes.strides[0];

  uint8* dst_pos = dst_buffer + dst_stride * dst_y;
  dst_pos += dst_x * kBytesPerPixel;

  int height = rect.height(), row_bytes = rect.width() * kBytesPerPixel;
  for (int y = 0; y < height; ++y) {
    memcpy(dst_pos, src_pos, row_bytes);
    src_pos += src_stride;
    dst_pos += dst_stride;
  }
}

void VideoFrameCapturerLinux::SlowBlit(uint8* image, const SkIRect& rect,
                                       CaptureData* capture_data) {
  DataPlanes planes = capture_data->data_planes();
  uint8* dst_buffer = planes.data[0];
  const int dst_stride = planes.strides[0];
  int src_stride = x_server_pixel_buffer_.GetStride();
  int dst_x = rect.fLeft, dst_y = rect.fTop;
  int width = rect.width(), height = rect.height();

  unsigned int red_mask = x_server_pixel_buffer_.GetRedMask();
  unsigned int blue_mask = x_server_pixel_buffer_.GetBlueMask();
  unsigned int green_mask = x_server_pixel_buffer_.GetGreenMask();
  unsigned int red_shift = x_server_pixel_buffer_.GetRedShift();
  unsigned int blue_shift = x_server_pixel_buffer_.GetBlueShift();
  unsigned int green_shift = x_server_pixel_buffer_.GetGreenShift();

  unsigned int max_red = red_mask >> red_shift;
  unsigned int max_blue = blue_mask >> blue_shift;
  unsigned int max_green = green_mask >> green_shift;

  unsigned int bits_per_pixel = x_server_pixel_buffer_.GetBitsPerPixel();

  uint8* dst_pos = dst_buffer + dst_stride * dst_y;
  uint8* src_pos = image;
  dst_pos += dst_x * kBytesPerPixel;
  // TODO(hclam): Optimize, perhaps using MMX code or by converting to
  // YUV directly
  for (int y = 0; y < height; y++) {
    uint32_t* dst_pos_32 = reinterpret_cast<uint32_t*>(dst_pos);
    uint32_t* src_pos_32 = reinterpret_cast<uint32_t*>(src_pos);
    uint16_t* src_pos_16 = reinterpret_cast<uint16_t*>(src_pos);
    for (int x = 0; x < width; x++) {
      // Dereference through an appropriately-aligned pointer.
      uint32_t pixel;
      if (bits_per_pixel == 32)
        pixel = src_pos_32[x];
      else if (bits_per_pixel == 16)
        pixel = src_pos_16[x];
      else
        pixel = src_pos[x];
      uint32_t r = (((pixel & red_mask) >> red_shift) * 255) / max_red;
      uint32_t b = (((pixel & blue_mask) >> blue_shift) * 255) / max_blue;
      uint32_t g = (((pixel & green_mask) >> green_shift) * 255) / max_green;
      // Write as 32-bit RGB.
      dst_pos_32[x] = r << 16 | g << 8 | b;
    }
    dst_pos += dst_stride;
    src_pos += src_stride;
  }
}

const SkISize& VideoFrameCapturerLinux::size_most_recent() const {
  return helper_.size_most_recent();
}

}  // namespace

// static
VideoFrameCapturer* VideoFrameCapturer::Create() {
  VideoFrameCapturerLinux* capturer = new VideoFrameCapturerLinux();
  if (!capturer->Init()) {
    delete capturer;
    capturer = NULL;
  }
  return capturer;
}

// static
void VideoFrameCapturer::EnableXDamage(bool enable) {
  g_should_use_x_damage = enable;
}

}  // namespace remoting
