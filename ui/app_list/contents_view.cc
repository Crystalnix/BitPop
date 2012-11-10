// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/app_list/contents_view.h"

#include <algorithm>

#include "ui/app_list/app_list_view.h"
#include "ui/app_list/apps_grid_view.h"
#include "ui/app_list/page_switcher.h"
#include "ui/app_list/pagination_model.h"
#include "ui/app_list/search_result_list_view.h"
#include "ui/views/animation/bounds_animator.h"
#include "ui/views/view_model.h"
#include "ui/views/view_model_utils.h"

namespace app_list {

namespace {

const int kPreferredIconDimension = 48;
const int kPreferredCols = 4;
const int kPreferredRows = 4;

// Indexes of interesting views in ViewModel of ContentsView.
const int kIndexAppsGrid = 0;
const int kIndexPageSwitcher = 1;
const int kIndexSearchResults = 2;

const int kMinMouseWheelToSwitchPage = 20;
const int kMinScrollToSwitchPage = 20;
const int kMinHorizVelocityToSwitchPage = 1100;

// Helpers to get certain child view from |model|.
AppsGridView* GetAppsGridView(views::ViewModel* model) {
  return static_cast<AppsGridView*>(model->view_at(kIndexAppsGrid));
}

PageSwitcher* GetPageSwitcherView(views::ViewModel* model) {
  return static_cast<PageSwitcher*>(model->view_at(kIndexPageSwitcher));
}

SearchResultListView* GetSearchResultListView(views::ViewModel* model) {
  return static_cast<SearchResultListView*>(
      model->view_at(kIndexSearchResults));
}

}  // namespace

ContentsView::ContentsView(AppListView* app_list_view,
                           PaginationModel* pagination_model)
    : show_state_(SHOW_APPS),
      pagination_model_(pagination_model),
      view_model_(new views::ViewModel),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          bounds_animator_(new views::BoundsAnimator(this))) {
  AppsGridView* apps_grid_view = new AppsGridView(app_list_view,
                                                  pagination_model);
  apps_grid_view->SetLayout(kPreferredIconDimension,
                            kPreferredCols,
                            kPreferredRows);
  AddChildView(apps_grid_view);
  view_model_->Add(apps_grid_view, kIndexAppsGrid);

  PageSwitcher* page_switcher_view = new PageSwitcher(pagination_model);
  AddChildView(page_switcher_view);
  view_model_->Add(page_switcher_view, kIndexPageSwitcher);

  SearchResultListView* search_results_view = new SearchResultListView(
      app_list_view);
  AddChildView(search_results_view);
  view_model_->Add(search_results_view, kIndexSearchResults);
}

ContentsView::~ContentsView() {
}

void ContentsView::SetModel(AppListModel* model) {
  if (model) {
    GetAppsGridView(view_model_.get())->SetModel(model->apps());
    GetSearchResultListView(view_model_.get())->SetResults(model->results());
  } else {
    GetAppsGridView(view_model_.get())->SetModel(NULL);
    GetSearchResultListView(view_model_.get())->SetResults(NULL);
  }
}

void ContentsView::SetShowState(ShowState show_state) {
  if (show_state_ == show_state)
    return;

  show_state_ = show_state;
  ShowStateChanged();
}

void ContentsView::ShowStateChanged() {
  if (show_state_ == SHOW_SEARCH_RESULTS) {
    // TODO(xiyuan): Highlight default match instead of the first.
    SearchResultListView* results_view =
        GetSearchResultListView(view_model_.get());
    if (results_view->visible())
      results_view->SetSelectedIndex(0);
  }

  AnimateToIdealBounds();
}

void ContentsView::CalculateIdealBounds() {
  gfx::Rect rect(GetContentsBounds());
  if (rect.IsEmpty())
    return;

  const int x = rect.x();
  const int width = rect.width();

  // AppsGridView and PageSwitcher uses a vertical box layout.
  int y = rect.y();
  const int grid_height =
      GetAppsGridView(view_model_.get())->GetPreferredSize().height();
  gfx::Rect grid_frame(gfx::Point(x, y), gfx::Size(width, grid_height));
  grid_frame = rect.Intersect(grid_frame);

  y = grid_frame.bottom();
  const int page_switcher_height = rect.bottom() - y;
  gfx::Rect page_switcher_frame(gfx::Point(x, y),
                                gfx::Size(width, page_switcher_height));
  page_switcher_frame = rect.Intersect(page_switcher_frame);

  // SearchResultListView occupies the whole space when visible.
  gfx::Rect results_frame(rect);

  // Offsets apps grid, page switcher and result list based on |show_state_|.
  // SearchResultListView is on top of apps grid + page switcher. Visible view
  // is left in visible area and invisible ones is put out of the visible area.
  int contents_area_height = rect.height();
  switch (show_state_) {
    case SHOW_APPS:
      results_frame.Offset(0, -contents_area_height);
      break;
    case SHOW_SEARCH_RESULTS:
      grid_frame.Offset(0, contents_area_height);
      page_switcher_frame.Offset(0, contents_area_height);
      break;
    default:
      NOTREACHED() << "Unknown show_state_ " << show_state_;
      break;
  }

  view_model_->set_ideal_bounds(kIndexAppsGrid, grid_frame);
  view_model_->set_ideal_bounds(kIndexPageSwitcher, page_switcher_frame);
  view_model_->set_ideal_bounds(kIndexSearchResults, results_frame);
}

void ContentsView::AnimateToIdealBounds() {
  CalculateIdealBounds();
  for (int i = 0; i < view_model_->view_size(); ++i) {
    bounds_animator_->AnimateViewTo(view_model_->view_at(i),
                                    view_model_->ideal_bounds(i));
  }
}

void ContentsView::ShowSearchResults(bool show) {
  SetShowState(show ? SHOW_SEARCH_RESULTS : SHOW_APPS);
}

gfx::Size ContentsView::GetPreferredSize() {
  const gfx::Size grid_size =
      GetAppsGridView(view_model_.get())->GetPreferredSize();
  const gfx::Size page_switcher_size =
      GetPageSwitcherView(view_model_.get())->GetPreferredSize();
  const gfx::Size results_size =
      GetSearchResultListView(view_model_.get())->GetPreferredSize();

  int width = std::max(
      std::max(grid_size.width(), page_switcher_size.width()),
      results_size.width());
  int height = std::max(grid_size.height() + page_switcher_size.height(),
                        results_size.height());
  return gfx::Size(width, height);
}

void ContentsView::Layout() {
  CalculateIdealBounds();
  views::ViewModelUtils::SetViewBoundsToIdealBounds(*view_model_);
}

ui::GestureStatus ContentsView::OnGestureEvent(
    const views::GestureEvent& event) {
  if (show_state_ != SHOW_APPS)
    return ui::GESTURE_STATUS_UNKNOWN;

  switch (event.type()) {
    case ui::ET_GESTURE_SCROLL_BEGIN:
      pagination_model_->StartScroll();
      return ui::GESTURE_STATUS_CONSUMED;
    case ui::ET_GESTURE_SCROLL_UPDATE:
      // event.details.scroll_x() > 0 means moving contents to right. That is,
      // transitioning to previous page.
      pagination_model_->UpdateScroll(
          event.details().scroll_x() / GetContentsBounds().width());
      return ui::GESTURE_STATUS_CONSUMED;
    case ui::ET_GESTURE_SCROLL_END:
      pagination_model_->EndScroll();
      return ui::GESTURE_STATUS_CONSUMED;
    case ui::ET_SCROLL_FLING_START: {
      if (fabs(event.details().velocity_x()) > kMinHorizVelocityToSwitchPage) {
        pagination_model_->SelectPageRelative(
            event.details().velocity_x() < 0 ? 1 : -1,
            true);
        return ui::GESTURE_STATUS_CONSUMED;
      }
      break;
    }
    default:
      break;
  }

  return ui::GESTURE_STATUS_UNKNOWN;
}

bool ContentsView::OnKeyPressed(const views::KeyEvent& event) {
  switch (show_state_) {
    case SHOW_APPS:
      return GetAppsGridView(view_model_.get())->OnKeyPressed(event);
    case SHOW_SEARCH_RESULTS:
      return GetSearchResultListView(view_model_.get())->OnKeyPressed(event);
    default:
      NOTREACHED() << "Unknown show state " << show_state_;
  }
  return false;
}

bool ContentsView::OnMouseWheel(const views::MouseWheelEvent& event) {
  if (show_state_ != SHOW_APPS)
    return false;

  if (abs(event.offset()) > kMinMouseWheelToSwitchPage) {
    if (!pagination_model_->has_transition())
      pagination_model_->SelectPageRelative(event.offset() > 0 ? -1 : 1, true);
    return true;
  }

  return false;
}

bool ContentsView::OnScrollEvent(const views::ScrollEvent & event) {
  if (show_state_ != SHOW_APPS)
    return false;

  if (abs(event.x_offset()) > kMinScrollToSwitchPage) {
    if (!pagination_model_->has_transition()) {
      pagination_model_->SelectPageRelative(event.x_offset() > 0 ? 1 : -1,
                                            true);
    }
    return true;
  }

  return false;
}

}  // namespace app_list
