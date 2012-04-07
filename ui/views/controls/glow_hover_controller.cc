// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/glow_hover_controller.h"

#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/skbitmap_operations.h"
#include "ui/views/view.h"

namespace views {

// Amount to scale the opacity.
static const double kOpacityScale = 0.5;

// How long the hover state takes.
static const int kHoverDurationMs = 400;

GlowHoverController::GlowHoverController(views::View* view)
    : view_(view),
      ALLOW_THIS_IN_INITIALIZER_LIST(animation_(this)) {
  animation_.set_delegate(this);
  animation_.SetSlideDuration(kHoverDurationMs);
}

GlowHoverController::~GlowHoverController() {
}

void GlowHoverController::SetAnimationContainer(
    ui::AnimationContainer* container) {
  animation_.SetContainer(container);
}

void GlowHoverController::SetLocation(const gfx::Point& location) {
  location_ = location;
  if (ShouldDraw())
    view_->SchedulePaint();
}

void GlowHoverController::Show() {
  animation_.SetTweenType(ui::Tween::EASE_OUT);
  animation_.Show();
}

void GlowHoverController::Hide() {
  animation_.SetTweenType(ui::Tween::EASE_IN);
  animation_.Hide();
}

void GlowHoverController::HideImmediately() {
  if (ShouldDraw())
    view_->SchedulePaint();
  animation_.Reset();
}

double GlowHoverController::GetAnimationValue() const {
  return animation_.GetCurrentValue();
}

bool GlowHoverController::ShouldDraw() const {
  return animation_.IsShowing() || animation_.is_animating();
}

void GlowHoverController::Draw(gfx::Canvas* canvas,
                               const SkBitmap& mask_image) const {
  if (!ShouldDraw())
    return;

  // Draw a radial gradient to hover_canvas.
  gfx::CanvasSkia hover_canvas(
      gfx::Size(mask_image.width(), mask_image.height()), false);

  // Draw a radial gradient to hover_canvas.
  int radius = view_->width() / 3;

  SkPaint paint;
  paint.setStyle(SkPaint::kFill_Style);
  paint.setFlags(SkPaint::kAntiAlias_Flag);
  SkPoint loc = { SkIntToScalar(location_.x()), SkIntToScalar(location_.y()) };
  SkColor colors[2];
  int hover_alpha =
      static_cast<int>(255 * kOpacityScale * animation_.GetCurrentValue());
  colors[0] = SkColorSetARGB(hover_alpha, 255, 255, 255);
  colors[1] = SkColorSetARGB(0, 255, 255, 255);
  SkShader* shader = SkGradientShader::CreateRadial(
      loc,
      SkIntToScalar(radius),
      colors,
      NULL,
      2,
      SkShader::kClamp_TileMode);
  // Shader can end up null when radius = 0.
  // If so, this results in default full tab glow behavior.
  if (shader) {
    paint.setShader(shader);
    shader->unref();
    hover_canvas.DrawRect(gfx::Rect(location_.x() - radius,
                                    location_.y() - radius,
                                    radius * 2, radius * 2), paint);
  }
  SkBitmap result = SkBitmapOperations::CreateMaskedBitmap(
      hover_canvas.ExtractBitmap(), mask_image);
  canvas->DrawBitmapInt(result, (view_->width() - mask_image.width()) / 2,
                        (view_->height() - mask_image.height()) / 2);
}

void GlowHoverController::AnimationEnded(const ui::Animation* animation) {
  view_->SchedulePaint();
}

void GlowHoverController::AnimationProgressed(const ui::Animation* animation) {
  view_->SchedulePaint();
}

}  // namespace views
