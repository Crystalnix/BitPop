// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/border.h"

#include "base/logging.h"
#include "ui/gfx/canvas.h"
#include "ui/views/painter.h"

namespace views {

namespace {

// A simple border with a fixed thickness and single color.
class SolidBorder : public Border {
 public:
  SolidBorder(int thickness, SkColor color);

  virtual void Paint(const View& view, gfx::Canvas* canvas) const;
  virtual void GetInsets(gfx::Insets* insets) const;

 private:
  int thickness_;
  SkColor color_;
  gfx::Insets insets_;

  DISALLOW_COPY_AND_ASSIGN(SolidBorder);
};

SolidBorder::SolidBorder(int thickness, SkColor color)
    : thickness_(thickness),
      color_(color),
      insets_(thickness, thickness, thickness, thickness) {
}

void SolidBorder::Paint(const View& view, gfx::Canvas* canvas) const {
  // Top border.
  canvas->FillRect(color_, gfx::Rect(0, 0, view.width(), insets_.top()));
  // Left border.
  canvas->FillRect(color_, gfx::Rect(0, 0, insets_.left(), view.height()));
  // Bottom border.
  canvas->FillRect(color_, gfx::Rect(0, view.height() - insets_.bottom(),
                                     view.width(), insets_.bottom()));
  // Right border.
  canvas->FillRect(color_, gfx::Rect(view.width() - insets_.right(), 0,
                                     insets_.right(), view.height()));
}

void SolidBorder::GetInsets(gfx::Insets* insets) const {
  DCHECK(insets);
  insets->Set(insets_.top(), insets_.left(), insets_.bottom(), insets_.right());
}

class EmptyBorder : public Border {
 public:
  EmptyBorder(int top, int left, int bottom, int right)
      : top_(top), left_(left), bottom_(bottom), right_(right) {}

  virtual void Paint(const View& view, gfx::Canvas* canvas) const {}

  virtual void GetInsets(gfx::Insets* insets) const {
    DCHECK(insets);
    insets->Set(top_, left_, bottom_, right_);
  }

 private:
  int top_;
  int left_;
  int bottom_;
  int right_;

  DISALLOW_COPY_AND_ASSIGN(EmptyBorder);
};

class BorderPainter : public Border {
 public:
  BorderPainter(Painter* painter)
      : painter_(painter) {
    DCHECK(painter);
  }

  virtual ~BorderPainter() {
    delete painter_;
  }

  void Paint(const View& view, gfx::Canvas* canvas) const {
    Painter::PaintPainterAt(0, 0, view.width(), view.height(), canvas,
                            painter_);
  }

  virtual void GetInsets(gfx::Insets* insets) const {
    DCHECK(insets);
    insets->Set(0, 0, 0, 0);
  }

 private:
  Painter* painter_;

  DISALLOW_COPY_AND_ASSIGN(BorderPainter);
};

} // namespace

Border::Border() {
}

Border::~Border() {
}

// static
Border* Border::CreateSolidBorder(int thickness, SkColor color) {
  return new SolidBorder(thickness, color);
}

// static
Border* Border::CreateEmptyBorder(int top, int left, int bottom, int right) {
  return new EmptyBorder(top, left, bottom, right);
}

//static
Border* Border::CreateBorderPainter(Painter* painter) {
  return new BorderPainter(painter);
}

}  // namespace views
