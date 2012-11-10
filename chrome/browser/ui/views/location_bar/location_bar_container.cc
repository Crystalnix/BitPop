// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/location_bar_container.h"

#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/webui/instant_ui.h"
#include "ui/views/background.h"
#include "ui/views/layout/fill_layout.h"

namespace {

// Duration of the animations used by this class (AnimateTo()).
const int kAnimationDuration = 180;

}

LocationBarContainer::LocationBarContainer(views::View* parent,
                                           bool instant_extended_api_enabled)
    : animator_(parent),
      view_parent_(NULL),
      location_bar_view_(NULL),
      native_view_host_(NULL),
      in_toolbar_(true) {
  parent->AddChildView(this);
  animator_.set_tween_type(ui::Tween::EASE_IN_OUT);
  PlatformInit();
  if (instant_extended_api_enabled) {
    view_parent_->set_background(
        views::Background::CreateSolidBackground(GetBackgroundColor()));
  }
  SetLayoutManager(new views::FillLayout);
}

LocationBarContainer::~LocationBarContainer() {
}

void LocationBarContainer::SetLocationBarView(LocationBarView* view) {
  DCHECK(!location_bar_view_ && view);
  location_bar_view_ = view;
  view_parent_->AddChildView(location_bar_view_);
  DCHECK_EQ(1, view_parent_->child_count());  // Only support one child.
}

void LocationBarContainer::AnimateTo(const gfx::Rect& bounds) {
  // Animation duration can change during session.
  animator_.SetAnimationDuration(GetAnimationDuration());
  animator_.AnimateViewTo(this, bounds);
}

bool LocationBarContainer::IsAnimating() const {
  return animator_.IsAnimating();
}

gfx::Rect LocationBarContainer::GetTargetBounds() {
  return animator_.GetTargetBounds(this);
}

std::string LocationBarContainer::GetClassName() const {
  return "browser/ui/views/location_bar/LocationBarContainer";
}

gfx::Size LocationBarContainer::GetPreferredSize() {
  return location_bar_view_->GetPreferredSize();
}

bool LocationBarContainer::SkipDefaultKeyEventProcessing(
    const views::KeyEvent& event) {
  return location_bar_view_->SkipDefaultKeyEventProcessing(event);
}

void LocationBarContainer::GetAccessibleState(
    ui::AccessibleViewState* state) {
  location_bar_view_->GetAccessibleState(state);
}

void LocationBarContainer::OnBoundsAnimatorDone(
    views::BoundsAnimator* animator) {
  SetInToolbar(true);
}

// static
int LocationBarContainer::GetAnimationDuration() {
  return kAnimationDuration * InstantUI::GetSlowAnimationScaleFactor();
}
