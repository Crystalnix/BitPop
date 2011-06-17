// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/tab_contents_drag_source.h"

#include <string>

#include "base/file_util.h"
#include "base/mime_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/download/drag_download_file.h"
#include "chrome/browser/download/drag_download_util.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/render_view_host_delegate.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/tab_contents_view.h"
#include "net/base/file_stream.h"
#include "net/base/net_util.h"
#include "ui/base/dragdrop/gtk_dnd_util.h"
#include "ui/gfx/gtk_util.h"
#include "webkit/glue/webdropdata.h"

using WebKit::WebDragOperation;
using WebKit::WebDragOperationsMask;
using WebKit::WebDragOperationNone;

TabContentsDragSource::TabContentsDragSource(
    TabContentsView* tab_contents_view)
    : tab_contents_view_(tab_contents_view),
      drag_pixbuf_(NULL),
      drag_failed_(false),
      drag_widget_(gtk_invisible_new()),
      drag_context_(NULL),
      drag_icon_(gtk_window_new(GTK_WINDOW_POPUP)) {
  signals_.Connect(drag_widget_, "drag-failed",
                   G_CALLBACK(OnDragFailedThunk), this);
  signals_.Connect(drag_widget_, "drag-begin",
                   G_CALLBACK(OnDragBeginThunk),
                   this);
  signals_.Connect(drag_widget_, "drag-end",
                   G_CALLBACK(OnDragEndThunk), this);
  signals_.Connect(drag_widget_, "drag-data-get",
                   G_CALLBACK(OnDragDataGetThunk), this);

  signals_.Connect(drag_icon_, "expose-event",
                   G_CALLBACK(OnDragIconExposeThunk), this);
}

TabContentsDragSource::~TabContentsDragSource() {
  // Break the current drag, if any.
  if (drop_data_.get()) {
    gtk_grab_add(drag_widget_);
    gtk_grab_remove(drag_widget_);
    MessageLoopForUI::current()->RemoveObserver(this);
    drop_data_.reset();
  }

  gtk_widget_destroy(drag_widget_);
  gtk_widget_destroy(drag_icon_);
}

TabContents* TabContentsDragSource::tab_contents() const {
  return tab_contents_view_->tab_contents();
}

void TabContentsDragSource::StartDragging(const WebDropData& drop_data,
                                          WebDragOperationsMask allowed_ops,
                                          GdkEventButton* last_mouse_down,
                                          const SkBitmap& image,
                                          const gfx::Point& image_offset) {
  // Guard against re-starting before previous drag completed.
  if (drag_context_) {
    NOTREACHED();
    tab_contents()->SystemDragEnded();
    return;
  }

  int targets_mask = 0;

  if (!drop_data.plain_text.empty())
    targets_mask |= ui::TEXT_PLAIN;
  if (drop_data.url.is_valid()) {
    targets_mask |= ui::TEXT_URI_LIST;
    targets_mask |= ui::CHROME_NAMED_URL;
    targets_mask |= ui::NETSCAPE_URL;
  }
  if (!drop_data.text_html.empty())
    targets_mask |= ui::TEXT_HTML;
  if (!drop_data.file_contents.empty())
    targets_mask |= ui::CHROME_WEBDROP_FILE_CONTENTS;
  if (!drop_data.download_metadata.empty() &&
      drag_download_util::ParseDownloadMetadata(drop_data.download_metadata,
                                                &wide_download_mime_type_,
                                                &download_file_name_,
                                                &download_url_)) {
    targets_mask |= ui::DIRECT_SAVE_FILE;
  }

  // NOTE: Begin a drag even if no targets present. Otherwise, things like
  // draggable list elements will not work.

  drop_data_.reset(new WebDropData(drop_data));

  // The image we get from WebKit makes heavy use of alpha-shading. This looks
  // bad on non-compositing WMs. Fall back to the default drag icon.
  if (!image.isNull() && gtk_util::IsScreenComposited())
    drag_pixbuf_ = gfx::GdkPixbufFromSkBitmap(&image);
  image_offset_ = image_offset;

  GtkTargetList* list = ui::GetTargetListFromCodeMask(targets_mask);
  if (targets_mask & ui::CHROME_WEBDROP_FILE_CONTENTS) {
    drag_file_mime_type_ = gdk_atom_intern(
        mime_util::GetDataMimeType(drop_data.file_contents).c_str(), FALSE);
    gtk_target_list_add(list, drag_file_mime_type_,
                        0, ui::CHROME_WEBDROP_FILE_CONTENTS);
  }

  drag_failed_ = false;
  // If we don't pass an event, GDK won't know what event time to start grabbing
  // mouse events. Technically it's the mouse motion event and not the mouse
  // down event that causes the drag, but there's no reliable way to know
  // *which* motion event initiated the drag, so this will have to do.
  // TODO(estade): This can sometimes be very far off, e.g. if the user clicks
  // and holds and doesn't start dragging for a long time. I doubt it matters
  // much, but we should probably look into the possibility of getting the
  // initiating event from webkit.
  drag_context_ = gtk_drag_begin(drag_widget_, list,
      gtk_util::WebDragOpToGdkDragAction(allowed_ops),
      1,  // Drags are always initiated by the left button.
      reinterpret_cast<GdkEvent*>(last_mouse_down));
  // The drag adds a ref; let it own the list.
  gtk_target_list_unref(list);

  // Sometimes the drag fails to start; |context| will be NULL and we won't
  // get a drag-end signal.
  if (!drag_context_) {
    drag_failed_ = true;
    drop_data_.reset();
    tab_contents()->SystemDragEnded();
    return;
  }

  MessageLoopForUI::current()->AddObserver(this);
}

void TabContentsDragSource::WillProcessEvent(GdkEvent* event) {
  // No-op.
}

void TabContentsDragSource::DidProcessEvent(GdkEvent* event) {
  if (event->type != GDK_MOTION_NOTIFY)
    return;

  GdkEventMotion* event_motion = reinterpret_cast<GdkEventMotion*>(event);
  gfx::Point client = gtk_util::ClientPoint(GetContentNativeView());

  if (tab_contents()->render_view_host()) {
    tab_contents()->render_view_host()->DragSourceMovedTo(
        client.x(), client.y(),
        static_cast<int>(event_motion->x_root),
        static_cast<int>(event_motion->y_root));
  }
}

void TabContentsDragSource::OnDragDataGet(GtkWidget* sender,
    GdkDragContext* context, GtkSelectionData* selection_data,
    guint target_type, guint time) {
  const int kBitsPerByte = 8;

  switch (target_type) {
    case ui::TEXT_PLAIN: {
      std::string utf8_text = UTF16ToUTF8(drop_data_->plain_text);
      gtk_selection_data_set_text(selection_data, utf8_text.c_str(),
                                  utf8_text.length());
      break;
    }

    case ui::TEXT_HTML: {
      // TODO(estade): change relative links to be absolute using
      // |html_base_url|.
      std::string utf8_text = UTF16ToUTF8(drop_data_->text_html);
      gtk_selection_data_set(selection_data,
                             ui::GetAtomForTarget(ui::TEXT_HTML),
                             kBitsPerByte,
                             reinterpret_cast<const guchar*>(utf8_text.c_str()),
                             utf8_text.length());
      break;
    }

    case ui::TEXT_URI_LIST:
    case ui::CHROME_NAMED_URL:
    case ui::NETSCAPE_URL: {
      ui::WriteURLWithName(selection_data, drop_data_->url,
                           drop_data_->url_title, target_type);
      break;
    }

    case ui::CHROME_WEBDROP_FILE_CONTENTS: {
      gtk_selection_data_set(
          selection_data,
          drag_file_mime_type_, kBitsPerByte,
          reinterpret_cast<const guchar*>(drop_data_->file_contents.data()),
          drop_data_->file_contents.length());
      break;
    }

    case ui::DIRECT_SAVE_FILE: {
      char status_code = 'E';

      // Retrieves the full file path (in file URL format) provided by the
      // drop target by reading from the source window's XdndDirectSave0
      // property.
      gint file_url_len = 0;
      guchar* file_url_value = NULL;
      if (gdk_property_get(context->source_window,
                           ui::GetAtomForTarget(ui::DIRECT_SAVE_FILE),
                           ui::GetAtomForTarget(ui::TEXT_PLAIN_NO_CHARSET),
                           0,
                           1024,
                           FALSE,
                           NULL,
                           NULL,
                           &file_url_len,
                           &file_url_value) &&
          file_url_value) {
        // Convert from the file url to the file path.
        GURL file_url(std::string(reinterpret_cast<char*>(file_url_value),
                                  file_url_len));
        g_free(file_url_value);
        FilePath file_path;
        if (net::FileURLToFilePath(file_url, &file_path)) {
          // Open the file as a stream.
          net::FileStream* file_stream =
              drag_download_util::CreateFileStreamForDrop(&file_path);
          if (file_stream) {
              // Start downloading the file to the stream.
              TabContents* tab_contents = tab_contents_view_->tab_contents();
              scoped_refptr<DragDownloadFile> drag_file_downloader =
                  new DragDownloadFile(file_path,
                                       linked_ptr<net::FileStream>(file_stream),
                                       download_url_,
                                       tab_contents->GetURL(),
                                       tab_contents->encoding(),
                                       tab_contents);
              drag_file_downloader->Start(
                  new drag_download_util::PromiseFileFinalizer(
                      drag_file_downloader));

              // Set the status code to success.
              status_code = 'S';
          }
        }

        // Return the status code to the file manager.
        gtk_selection_data_set(selection_data,
                               selection_data->target,
                               8,
                               reinterpret_cast<guchar*>(&status_code),
                               1);
      }
      break;
    }

    default:
      NOTREACHED();
  }
}

gboolean TabContentsDragSource::OnDragFailed(GtkWidget* sender,
                                             GdkDragContext* context,
                                             GtkDragResult result) {
  drag_failed_ = true;

  gfx::Point root = gtk_util::ScreenPoint(GetContentNativeView());
  gfx::Point client = gtk_util::ClientPoint(GetContentNativeView());

  if (tab_contents()->render_view_host()) {
    tab_contents()->render_view_host()->DragSourceEndedAt(
        client.x(), client.y(), root.x(), root.y(),
        WebDragOperationNone);
  }

  // Let the native failure animation run.
  return FALSE;
}

void TabContentsDragSource::OnDragBegin(GtkWidget* sender,
                                        GdkDragContext* drag_context) {
  if (!download_url_.is_empty()) {
    // Generate the file name based on both mime type and proposed file name.
    std::string download_mime_type = UTF16ToUTF8(wide_download_mime_type_);
    std::string content_disposition("attachment; filename=");
    content_disposition += download_file_name_.value();
    FilePath generated_download_file_name;
    download_util::GenerateFileName(download_url_,
                                    content_disposition,
                                    std::string(),
                                    download_mime_type,
                                    &generated_download_file_name);

    // Pass the file name to the drop target by setting the source window's
    // XdndDirectSave0 property.
    gdk_property_change(drag_context->source_window,
                        ui::GetAtomForTarget(ui::DIRECT_SAVE_FILE),
                        ui::GetAtomForTarget(ui::TEXT_PLAIN_NO_CHARSET),
                        8,
                        GDK_PROP_MODE_REPLACE,
                        reinterpret_cast<const guchar*>(
                            generated_download_file_name.value().c_str()),
                        generated_download_file_name.value().length());
  }

  if (drag_pixbuf_) {
    gtk_widget_set_size_request(drag_icon_,
                                gdk_pixbuf_get_width(drag_pixbuf_),
                                gdk_pixbuf_get_height(drag_pixbuf_));

    // We only need to do this once.
    if (!GTK_WIDGET_REALIZED(drag_icon_)) {
      GdkScreen* screen = gtk_widget_get_screen(drag_icon_);
      GdkColormap* rgba = gdk_screen_get_rgba_colormap(screen);
      if (rgba)
        gtk_widget_set_colormap(drag_icon_, rgba);
    }

    gtk_drag_set_icon_widget(drag_context, drag_icon_,
                             image_offset_.x(), image_offset_.y());
  }
}

void TabContentsDragSource::OnDragEnd(GtkWidget* sender,
                                      GdkDragContext* drag_context) {
  if (drag_pixbuf_) {
    g_object_unref(drag_pixbuf_);
    drag_pixbuf_ = NULL;
  }

  MessageLoopForUI::current()->RemoveObserver(this);

  if (!download_url_.is_empty()) {
    gdk_property_delete(drag_context->source_window,
                        ui::GetAtomForTarget(ui::DIRECT_SAVE_FILE));
  }

  if (!drag_failed_) {
    gfx::Point root = gtk_util::ScreenPoint(GetContentNativeView());
    gfx::Point client = gtk_util::ClientPoint(GetContentNativeView());

    if (tab_contents()->render_view_host()) {
      tab_contents()->render_view_host()->DragSourceEndedAt(
          client.x(), client.y(), root.x(), root.y(),
          gtk_util::GdkDragActionToWebDragOp(drag_context->action));
    }
  }

  tab_contents()->SystemDragEnded();

  drop_data_.reset();
  drag_context_ = NULL;
}

gfx::NativeView TabContentsDragSource::GetContentNativeView() const {
  return tab_contents_view_->GetContentNativeView();
}

gboolean TabContentsDragSource::OnDragIconExpose(GtkWidget* sender,
                                                 GdkEventExpose* event) {
  cairo_t* cr = gdk_cairo_create(event->window);
  gdk_cairo_rectangle(cr, &event->area);
  cairo_clip(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  gdk_cairo_set_source_pixbuf(cr, drag_pixbuf_, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);

  return TRUE;
}
