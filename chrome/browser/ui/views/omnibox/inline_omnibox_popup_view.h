// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_OMNIBOX_INLINE_OMNIBOX_POPUP_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_OMNIBOX_INLINE_OMNIBOX_POPUP_VIEW_H_

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_model.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_result_view_model.h"
#include "ui/base/animation/animation_delegate.h"
#include "ui/base/animation/slide_animation.h"
#include "ui/gfx/font.h"
#include "ui/views/view.h"
#include "webkit/glue/window_open_disposition.h"

struct AutocompleteMatch;
class OmniboxEditModel;
class OmniboxResultView;
class Profile;

namespace views {
class BubbleBorder;
}

// A view representing the contents of the omnibox popup.
class InlineOmniboxPopupView : public views::View,
                               public OmniboxResultViewModel,
                               public OmniboxPopupView,
                               public ui::AnimationDelegate {
 public:
  InlineOmniboxPopupView(const gfx::Font& font,
                         OmniboxView* omnibox_view,
                         OmniboxEditModel* edit_model,
                         views::View* location_bar);

  // Call immediately after construction.
  void Init();

  // Returns the bounds the popup should be shown at. This is the display bounds
  // and includes offsets for the dropshadow which this view's border renders.
  gfx::Rect GetPopupBounds() const;

  virtual void LayoutChildren();

  // Overridden from AutocompletePopupView:
  virtual bool IsOpen() const OVERRIDE;
  virtual void InvalidateLine(size_t line) OVERRIDE;
  virtual void UpdatePopupAppearance() OVERRIDE;
  virtual gfx::Rect GetTargetBounds() OVERRIDE;
  virtual void PaintUpdatesNow() OVERRIDE;
  virtual void OnDragCanceled() OVERRIDE;

  // Overridden from OmniboxResultViewModel:
  virtual bool IsSelectedIndex(size_t index) const OVERRIDE;
  virtual bool IsHoveredIndex(size_t index) const OVERRIDE;
  virtual const SkBitmap* GetIconIfExtensionMatch(size_t index) const OVERRIDE;

  // Overridden from ui::AnimationDelegate:
  virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;

  // Overridden from views::View:
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual views::View* GetEventHandlerForPoint(
      const gfx::Point& point) OVERRIDE;
  virtual bool OnMousePressed(const views::MouseEvent& event) OVERRIDE;
  virtual bool OnMouseDragged(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseReleased(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseCaptureLost() OVERRIDE;
  virtual void OnMouseMoved(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseEntered(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseExited(const views::MouseEvent& event) OVERRIDE;
  virtual ui::GestureStatus OnGestureEvent(
      const views::GestureEvent& event) OVERRIDE;

 protected:
  virtual ~InlineOmniboxPopupView();

  // Calculates the height needed to show all the results in the model.
  virtual int CalculatePopupHeight();
  virtual OmniboxResultView* CreateResultView(
      OmniboxResultViewModel* model,
      int model_index,
      const gfx::Font& font,
      const gfx::Font& bold_font);

  scoped_ptr<OmniboxPopupModel> model_;

 private:
  class AutocompletePopupWidget;

  // Returns true if the model has a match at the specified index.
  bool HasMatchAt(size_t index) const;

  // Returns the match at the specified index within the popup model.
  const AutocompleteMatch& GetMatchAtIndex(size_t index) const;

  // Fill a path for the contents' roundrect. |bounding_rect| is the rect that
  // bounds the path.
  void MakeContentsPath(gfx::Path* path, const gfx::Rect& bounding_rect);

  // Called when the line at the specified index should be opened with the
  // provided disposition.
  void OpenIndex(size_t index, WindowOpenDisposition disposition);

  // Find the index of the match under the given |point|, specified in window
  // coordinates. Returns AutocompletePopupModel::kNoMatch if there isn't a
  // match at the specified point.
  size_t GetIndexForPoint(const gfx::Point& point);

  // Processes a located event (e.g. mouse/gesture) and sets the selection/hover
  // state of a line in the list.
  void UpdateLineEvent(const views::LocatedEvent& event,
                       bool should_set_selected_line);

  // Opens an entry from the list depending on the event and the selected
  // disposition.
  void OpenSelectedLine(const views::LocatedEvent& event,
                        WindowOpenDisposition disposition);

  // Returns the target bounds given the specified content height.
  // TODO(sky): convert to size.
  gfx::Rect CalculateTargetBounds(int h);

  // The edit view that invokes us.
  OmniboxView* omnibox_view_;

  Profile* profile_;

  // An object that the popup positions itself against.
  views::View* location_bar_;

  // The font that we should use for result rows. This is based on the font used
  // by the edit that created us.
  gfx::Font result_font_;

  // The font used for portions that match the input.
  gfx::Font result_bold_font_;

  // If the user cancels a dragging action (i.e. by pressing ESC), we don't have
  // a convenient way to release mouse capture. Instead we use this flag to
  // simply ignore all remaining drag events, and the eventual mouse release
  // event. Since OnDragCanceled() can be called when we're not dragging, this
  // flag is reset to false on a mouse pressed event, to make sure we don't
  // erroneously ignore the next drag.
  bool ignore_mouse_drag_;

  // The popup sizes vertically using an animation when the popup is getting
  // shorter (not larger, that makes it look "slow").
  ui::SlideAnimation size_animation_;
  // TODO(sky): convert to sizes.
  gfx::Rect start_bounds_;
  gfx::Rect target_bounds_;

  DISALLOW_COPY_AND_ASSIGN(InlineOmniboxPopupView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_OMNIBOX_INLINE_OMNIBOX_POPUP_VIEW_H_
