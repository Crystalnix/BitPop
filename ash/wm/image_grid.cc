// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/image_grid.h"

#include <algorithm>

#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/transform.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkXfermode.h"

using std::max;
using std::min;

namespace ash {
namespace internal {

gfx::Rect ImageGrid::TestAPI::GetTransformedLayerBounds(
    const ui::Layer& layer) {
  gfx::Rect bounds = layer.bounds();
  layer.transform().TransformRect(&bounds);
  return bounds;
}

ImageGrid::ImageGrid()
    : top_image_height_(0),
      bottom_image_height_(0),
      left_image_width_(0),
      right_image_width_(0),
      top_row_height_(0),
      bottom_row_height_(0),
      left_column_width_(0),
      right_column_width_(0) {
}

ImageGrid::~ImageGrid() {
}

void ImageGrid::Init(const gfx::Image* top_left_image,
                     const gfx::Image* top_image,
                     const gfx::Image* top_right_image,
                     const gfx::Image* left_image,
                     const gfx::Image* center_image,
                     const gfx::Image* right_image,
                     const gfx::Image* bottom_left_image,
                     const gfx::Image* bottom_image,
                     const gfx::Image* bottom_right_image) {
  layer_.reset(new ui::Layer(ui::Layer::LAYER_NOT_DRAWN));

  InitImage(top_left_image, &top_left_layer_, &top_left_painter_);
  InitImage(top_image, &top_layer_, &top_painter_);
  InitImage(top_right_image, &top_right_layer_, &top_right_painter_);
  InitImage(left_image, &left_layer_, &left_painter_);
  InitImage(center_image, &center_layer_, &center_painter_);
  InitImage(right_image, &right_layer_, &right_painter_);
  InitImage(bottom_left_image, &bottom_left_layer_, &bottom_left_painter_);
  InitImage(bottom_image, &bottom_layer_, &bottom_painter_);
  InitImage(bottom_right_image, &bottom_right_layer_, &bottom_right_painter_);

  top_image_height_ = GetImageSize(top_image).height();
  bottom_image_height_ = GetImageSize(bottom_image).height();
  left_image_width_ = GetImageSize(left_image).width();
  right_image_width_ = GetImageSize(right_image).width();

  top_row_height_ = max(GetImageSize(top_left_image).height(),
                        max(GetImageSize(top_image).height(),
                            GetImageSize(top_right_image).height()));
  bottom_row_height_ = max(GetImageSize(bottom_left_image).height(),
                           max(GetImageSize(bottom_image).height(),
                               GetImageSize(bottom_right_image).height()));
  left_column_width_ = max(GetImageSize(top_left_image).width(),
                           max(GetImageSize(left_image).width(),
                               GetImageSize(bottom_left_image).width()));
  right_column_width_ = max(GetImageSize(top_right_image).width(),
                            max(GetImageSize(right_image).width(),
                                GetImageSize(bottom_right_image).width()));
}

void ImageGrid::SetSize(const gfx::Size& size) {
  if (size_ == size)
    return;

  size_ = size;

  gfx::Rect updated_bounds = layer_->bounds();
  updated_bounds.set_size(size);
  layer_->SetBounds(updated_bounds);

  float center_width = size.width() - left_column_width_ - right_column_width_;
  float center_height = size.height() - top_row_height_ - bottom_row_height_;

  if (top_layer_.get()) {
    if (center_width > 0) {
      ui::Transform transform;
      transform.SetScaleX(center_width / top_layer_->bounds().width());
      transform.ConcatTranslate(left_column_width_, 0);
      top_layer_->SetTransform(transform);
    }
    top_layer_->SetVisible(center_width > 0);
  }
  if (bottom_layer_.get()) {
    if (center_width > 0) {
      ui::Transform transform;
      transform.SetScaleX(center_width / bottom_layer_->bounds().width());
      transform.ConcatTranslate(
          left_column_width_, size.height() - bottom_layer_->bounds().height());
      bottom_layer_->SetTransform(transform);
    }
    bottom_layer_->SetVisible(center_width > 0);
  }
  if (left_layer_.get()) {
    if (center_height > 0) {
      ui::Transform transform;
      transform.SetScaleY(center_height / left_layer_->bounds().height());
      transform.ConcatTranslate(0, top_row_height_);
      left_layer_->SetTransform(transform);
    }
    left_layer_->SetVisible(center_height > 0);
  }
  if (right_layer_.get()) {
    if (center_height > 0) {
      ui::Transform transform;
      transform.SetScaleY(center_height / right_layer_->bounds().height());
      transform.ConcatTranslate(
          size.width() - right_layer_->bounds().width(), top_row_height_);
      right_layer_->SetTransform(transform);
    }
    right_layer_->SetVisible(center_height > 0);
  }

  // Calculate the available amount of space for corner images on all sides of
  // the grid.  If the images don't fit, we need to clip them.
  const int left = min(left_column_width_, size_.width() / 2);
  const int right = min(right_column_width_, size_.width() - left);
  const int top = min(top_row_height_, size_.height() / 2);
  const int bottom = min(bottom_row_height_, size_.height() - top);

  if (top_left_layer_.get()) {
    // No transformation needed; it should be at (0, 0) and unscaled.
    top_left_painter_->SetClipRect(
        LayerExceedsSize(top_left_layer_.get(), gfx::Size(left, top)) ?
            gfx::Rect(gfx::Rect(0, 0, left, top)) :
            gfx::Rect(),
        top_left_layer_.get());
  }
  if (top_right_layer_.get()) {
    ui::Transform transform;
    transform.SetTranslateX(size.width() - top_right_layer_->bounds().width());
    top_right_layer_->SetTransform(transform);
    top_right_painter_->SetClipRect(
        LayerExceedsSize(top_right_layer_.get(), gfx::Size(right, top)) ?
            gfx::Rect(top_right_layer_->bounds().width() - right, 0,
                      right, top) :
            gfx::Rect(),
        top_right_layer_.get());
  }
  if (bottom_left_layer_.get()) {
    ui::Transform transform;
    transform.SetTranslateY(
        size.height() - bottom_left_layer_->bounds().height());
    bottom_left_layer_->SetTransform(transform);
    bottom_left_painter_->SetClipRect(
        LayerExceedsSize(bottom_left_layer_.get(), gfx::Size(left, bottom)) ?
            gfx::Rect(0, bottom_left_layer_->bounds().height() - bottom,
                      left, bottom) :
            gfx::Rect(),
        bottom_left_layer_.get());
  }
  if (bottom_right_layer_.get()) {
    ui::Transform transform;
    transform.SetTranslate(
        size.width() - bottom_right_layer_->bounds().width(),
        size.height() - bottom_right_layer_->bounds().height());
    bottom_right_layer_->SetTransform(transform);
    bottom_right_painter_->SetClipRect(
        LayerExceedsSize(bottom_right_layer_.get(), gfx::Size(right, bottom)) ?
            gfx::Rect(bottom_right_layer_->bounds().width() - right,
                      bottom_right_layer_->bounds().height() - bottom,
                      right, bottom) :
            gfx::Rect(),
        bottom_right_layer_.get());
  }

  if (center_layer_.get()) {
    if (center_width > 0 && center_height > 0) {
      ui::Transform transform;
      transform.SetScale(center_width / center_layer_->bounds().width(),
                         center_height / center_layer_->bounds().height());
      transform.ConcatTranslate(left_column_width_, top_row_height_);
      center_layer_->SetTransform(transform);
    }
    center_layer_->SetVisible(center_width > 0 && center_height > 0);
  }
}

void ImageGrid::ImagePainter::SetClipRect(const gfx::Rect& clip_rect,
                                          ui::Layer* layer) {
  if (clip_rect != clip_rect_) {
    clip_rect_ = clip_rect;
    layer->ScheduleDraw();
  }
}

void ImageGrid::ImagePainter::OnPaintLayer(gfx::Canvas* canvas) {
  if (!clip_rect_.IsEmpty())
    canvas->ClipRect(clip_rect_);
  canvas->DrawBitmapInt(*(image_->ToSkBitmap()), 0, 0);
}

// static
gfx::Size ImageGrid::GetImageSize(const gfx::Image* image) {
  return image ?
      gfx::Size(image->ToSkBitmap()->width(), image->ToSkBitmap()->height()) :
      gfx::Size();
}

// static
bool ImageGrid::LayerExceedsSize(const ui::Layer* layer,
                                 const gfx::Size& size) {
  return layer->bounds().width() > size.width() ||
      layer->bounds().height() > size.height();
}

void ImageGrid::InitImage(const gfx::Image* image,
                          scoped_ptr<ui::Layer>* layer_ptr,
                          scoped_ptr<ImagePainter>* painter_ptr) {
  if (!image)
    return;

  layer_ptr->reset(new ui::Layer(ui::Layer::LAYER_TEXTURED));

  const gfx::Size size = GetImageSize(image);
  layer_ptr->get()->SetBounds(gfx::Rect(0, 0, size.width(), size.height()));

  painter_ptr->reset(new ImagePainter(image));
  layer_ptr->get()->set_delegate(painter_ptr->get());
  layer_ptr->get()->SetFillsBoundsOpaquely(false);
  layer_ptr->get()->SetVisible(true);
  layer_->Add(layer_ptr->get());
}

}  // namespace internal
}  // namespace ash
