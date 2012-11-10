// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_LOCATION_BAR_LOCATION_BAR_VIEW_MAC_H_
#define CHROME_BROWSER_UI_COCOA_LOCATION_BAR_LOCATION_BAR_VIEW_MAC_H_

#include <string>

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/command_observer.h"
#include "chrome/browser/extensions/image_loading_tracker.h"
#include "chrome/browser/prefs/pref_member.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/cocoa/omnibox/omnibox_view_mac.h"
#include "chrome/browser/ui/omnibox/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_controller.h"
#include "chrome/browser/ui/toolbar/toolbar_model.h"
#include "chrome/common/content_settings_types.h"

@class AutocompleteTextField;
class ChromeToMobileDecoration;
class CommandUpdater;
class ContentSettingDecoration;
class EVBubbleDecoration;
class KeywordHintDecoration;
class LocationBarDecoration;
class LocationIconDecoration;
class PageActionDecoration;
class PlusDecoration;
class Profile;
class SelectedKeywordDecoration;
class SkBitmap;
class StarDecoration;
class ToolbarModel;

// A C++ bridge class that represents the location bar UI element to
// the portable code.  Wires up an OmniboxViewMac instance to
// the location bar text field, which handles most of the work.

class LocationBarViewMac : public LocationBar,
                           public LocationBarTesting,
                           public OmniboxEditController,
                           public content::NotificationObserver,
                           public CommandObserver {
 public:
  LocationBarViewMac(AutocompleteTextField* field,
                     CommandUpdater* command_updater,
                     ToolbarModel* toolbar_model,
                     Profile* profile,
                     Browser* browser);
  virtual ~LocationBarViewMac();

  // Overridden from LocationBar:
  virtual void ShowFirstRunBubble() OVERRIDE;
  virtual void SetSuggestedText(const string16& text,
                                InstantCompleteBehavior behavior) OVERRIDE;
  virtual string16 GetInputString() const OVERRIDE;
  virtual WindowOpenDisposition GetWindowOpenDisposition() const OVERRIDE;
  virtual content::PageTransition GetPageTransition() const OVERRIDE;
  virtual void AcceptInput() OVERRIDE;
  virtual void FocusLocation(bool select_all) OVERRIDE;
  virtual void FocusSearch() OVERRIDE;
  virtual void UpdateContentSettingsIcons() OVERRIDE;
  virtual void UpdatePageActions() OVERRIDE;
  virtual void InvalidatePageActions() OVERRIDE;
  virtual void SaveStateToContents(content::WebContents* contents) OVERRIDE;
  virtual void Revert() OVERRIDE;
  virtual const OmniboxView* GetLocationEntry() const OVERRIDE;
  virtual OmniboxView* GetLocationEntry() OVERRIDE;
  virtual LocationBarTesting* GetLocationBarForTesting() OVERRIDE;

  // Overridden from LocationBarTesting:
  virtual int PageActionCount() OVERRIDE;
  virtual int PageActionVisibleCount() OVERRIDE;
  virtual ExtensionAction* GetPageAction(size_t index) OVERRIDE;
  virtual ExtensionAction* GetVisiblePageAction(size_t index) OVERRIDE;
  virtual void TestPageActionPressed(size_t index) OVERRIDE;

  // Set/Get the editable state of the field.
  void SetEditable(bool editable);
  bool IsEditable();

  // Set the starred state of the bookmark star.
  void SetStarred(bool starred);

  // Set ChromeToMobileDecoration's lit state (to update the icon).
  void SetChromeToMobileDecorationLit(bool lit);

  // Get the point in window coordinates on the star for the bookmark bubble to
  // aim at.
  NSPoint GetBookmarkBubblePoint() const;

  // Get the point in window coordinates on the Chrome To Mobile icon for
  // anchoring its bubble.
  NSPoint GetChromeToMobileBubblePoint() const;

  // Get the point in window coordinates in the security icon at which the page
  // info bubble aims.
  NSPoint GetPageInfoBubblePoint() const;

  // Updates the location bar.  Resets the bar's permanent text and
  // security style, and if |should_restore_state| is true, restores
  // saved state from the tab (for tab switching).
  void Update(const content::WebContents* tab, bool should_restore_state);

  // Layout the various decorations which live in the field.
  void Layout();

  // Re-draws |decoration| if it's already being displayed.
  void RedrawDecoration(LocationBarDecoration* decoration);

  // Returns the current WebContents.
  content::WebContents* GetWebContents() const;

  // Sets preview_enabled_ for the PageActionImageView associated with this
  // |page_action|. If |preview_enabled|, the location bar will display the
  // PageAction icon even if it has not been activated by the extension.
  // This is used by the ExtensionInstalledBubble to preview what the icon
  // will look like for the user upon installation of the extension.
  void SetPreviewEnabledPageAction(ExtensionAction* page_action,
                                   bool preview_enabled);

  // Return |page_action|'s info-bubble point in window coordinates.
  // This function should always be called with a visible page action.
  // If |page_action| is not a page action or not visible, NOTREACHED()
  // is called and this function returns |NSZeroPoint|.
  NSPoint GetPageActionBubblePoint(ExtensionAction* page_action);

  // Get the blocked-popup content setting's frame in window
  // coordinates.  Used by the blocked-popup animation.  Returns
  // |NSZeroRect| if the relevant content setting decoration is not
  // visible.
  NSRect GetBlockedPopupRect() const;

  // OmniboxEditController:
  virtual void OnAutocompleteAccept(
      const GURL& url,
      WindowOpenDisposition disposition,
      content::PageTransition transition,
      const GURL& alternate_nav_url) OVERRIDE;
  virtual void OnChanged() OVERRIDE;
  virtual void OnSelectionBoundsChanged() OVERRIDE;
  virtual void OnInputInProgress(bool in_progress) OVERRIDE;
  virtual void OnKillFocus() OVERRIDE;
  virtual void OnSetFocus() OVERRIDE;
  virtual SkBitmap GetFavicon() const OVERRIDE;
  virtual string16 GetTitle() const OVERRIDE;
  virtual InstantController* GetInstant() OVERRIDE;
  virtual TabContents* GetTabContents() const OVERRIDE;

  NSImage* GetKeywordImage(const string16& keyword);

  AutocompleteTextField* GetAutocompleteTextField() { return field_; }


  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // CommandObserver:
  virtual void EnabledStateChangedForCommand(int id, bool enabled) OVERRIDE;

 private:
  // Posts |notification| to the default notification center.
  void PostNotification(NSString* notification);

  // Return the decoration for |page_action|.
  PageActionDecoration* GetPageActionDecoration(ExtensionAction* page_action);

  // Clear the page-action decorations.
  void DeletePageActionDecorations();

  // Re-generate the page-action decorations from the profile's
  // extension service.
  void RefreshPageActionDecorations();

  // Updates visibility of the content settings icons based on the current
  // tab contents state.
  bool RefreshContentSettingsDecorations();

  void ShowFirstRunBubbleInternal();

  // Checks if the bookmark star should be enabled or not.
  bool IsStarEnabled();

  // Update the Chrome To Mobile page action visibility and command state.
  void UpdateChromeToMobileEnabled();

  scoped_ptr<OmniboxViewMac> omnibox_view_;

  CommandUpdater* command_updater_;  // Weak, owned by Browser.

  AutocompleteTextField* field_;  // owned by tab controller

  // When we get an OnAutocompleteAccept notification from the autocomplete
  // edit, we save the input string so we can give it back to the browser on
  // the LocationBar interface via GetInputString().
  string16 location_input_;

  // The user's desired disposition for how their input should be opened.
  WindowOpenDisposition disposition_;

  // A decoration that shows an icon to the left of the address.
  scoped_ptr<LocationIconDecoration> location_icon_decoration_;

  // A decoration that shows the keyword-search bubble on the left.
  scoped_ptr<SelectedKeywordDecoration> selected_keyword_decoration_;

  // A decoration that shows a lock icon and ev-cert label in a bubble
  // on the left.
  scoped_ptr<EVBubbleDecoration> ev_bubble_decoration_;

  // Action "plus" button right of bookmark star.
  scoped_ptr<PlusDecoration> plus_decoration_;

  // Bookmark star right of page actions.
  scoped_ptr<StarDecoration> star_decoration_;

  // Chrome To Mobile page action icon.
  scoped_ptr<ChromeToMobileDecoration> chrome_to_mobile_decoration_;

  // The installed page actions.
  std::vector<ExtensionAction*> page_actions_;

  // Decorations for the installed Page Actions.
  ScopedVector<PageActionDecoration> page_action_decorations_;

  // The content blocked decorations.
  ScopedVector<ContentSettingDecoration> content_setting_decorations_;

  // Keyword hint decoration displayed on the right-hand side.
  scoped_ptr<KeywordHintDecoration> keyword_hint_decoration_;

  Profile* profile_;

  Browser* browser_;

  ToolbarModel* toolbar_model_;  // Weak, owned by Browser.

  // The transition type to use for the navigation.
  content::PageTransition transition_;

  // Used to register for notifications received by NotificationObserver.
  content::NotificationRegistrar registrar_;

  // Used to schedule a task for the first run info bubble.
  base::WeakPtrFactory<LocationBarViewMac> weak_ptr_factory_;

  // Used to change the visibility of the star decoration.
  BooleanPrefMember edit_bookmarks_enabled_;

  DISALLOW_COPY_AND_ASSIGN(LocationBarViewMac);
};

#endif  // CHROME_BROWSER_UI_COCOA_LOCATION_BAR_LOCATION_BAR_VIEW_MAC_H_
