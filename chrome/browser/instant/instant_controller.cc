// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/instant/instant_controller.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "build/build_config.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/instant/instant_controller_delegate.h"
#include "chrome/browser/instant/instant_loader.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/blocked_content/blocked_content_tab_helper.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"

#if defined(TOOLKIT_VIEWS)
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#endif

InstantController::InstantController(InstantControllerDelegate* delegate,
                                     Mode mode)
    : delegate_(delegate),
      is_displayable_(false),
      is_out_of_date_(true),
      commit_on_pointer_release_(false),
      last_transition_type_(content::PAGE_TRANSITION_LINK),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)),
      mode_(mode) {
  DCHECK(mode_ == INSTANT || mode_ == SUGGEST || mode_ == HIDDEN ||
         mode_ == SILENT);
}

InstantController::~InstantController() {
}

// static
void InstantController::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterBooleanPref(prefs::kInstantConfirmDialogShown,
                             false,
                             PrefService::SYNCABLE_PREF);
  prefs->RegisterBooleanPref(prefs::kInstantEnabled,
                             false,
                             PrefService::SYNCABLE_PREF);

  // TODO(jamescook): Move this to search controller.
  prefs->RegisterDoublePref(prefs::kInstantAnimationScaleFactor,
                            1.0,
                            PrefService::UNSYNCABLE_PREF);
}

// static
void InstantController::RecordMetrics(Profile* profile) {
  UMA_HISTOGRAM_ENUMERATION("Instant.Status", IsEnabled(profile), 2);
}

// static
bool InstantController::IsEnabled(Profile* profile) {
  const PrefService* prefs = profile->GetPrefs();
  return prefs && prefs->GetBoolean(prefs::kInstantEnabled);
}

// static
void InstantController::Enable(Profile* profile) {
  PrefService* prefs = profile->GetPrefs();
  if (!prefs)
    return;

  prefs->SetBoolean(prefs::kInstantEnabled, true);
  prefs->SetBoolean(prefs::kInstantConfirmDialogShown, true);
  UMA_HISTOGRAM_ENUMERATION("Instant.Preference", 1, 2);
}

// static
void InstantController::Disable(Profile* profile) {
  PrefService* prefs = profile->GetPrefs();
  if (!prefs)
    return;

  prefs->SetBoolean(prefs::kInstantEnabled, false);
  UMA_HISTOGRAM_ENUMERATION("Instant.Preference", 0, 2);
}

bool InstantController::Update(const AutocompleteMatch& match,
                               const string16& user_text,
                               bool verbatim,
                               string16* suggested_text) {
  suggested_text->clear();

  is_out_of_date_ = false;
  commit_on_pointer_release_ = false;
  last_transition_type_ = match.transition;
  last_url_ = match.destination_url;
  last_user_text_ = user_text;

  TabContents* tab_contents = delegate_->GetInstantHostTabContents();
  if (!tab_contents) {
    Hide();
    return false;
  }

  Profile* profile = tab_contents->profile();
  const TemplateURL* template_url = match.GetTemplateURL(profile);
  const TemplateURL* default_t_url =
      TemplateURLServiceFactory::GetForProfile(profile)
                                 ->GetDefaultSearchProvider();
  if (!IsValidInstantTemplateURL(template_url) || !default_t_url ||
      template_url->id() != default_t_url->id()) {
    Hide();
    return false;
  }

  if (!loader_.get() || loader_->template_url_id() != template_url->id())
    loader_.reset(new InstantLoader(this, template_url->id(), std::string()));

  if (mode_ == SILENT) {
    // For the SILENT mode, we process |user_text| at commit time.
    loader_->MaybeLoadInstantURL(tab_contents, template_url);
    return true;
  }

  UpdateLoader(tab_contents, template_url, match.destination_url,
               match.transition, user_text, verbatim, suggested_text);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_INSTANT_CONTROLLER_UPDATED,
      content::Source<InstantController>(this),
      content::NotificationService::NoDetails());
  return true;
}

void InstantController::SetOmniboxBounds(const gfx::Rect& bounds) {
  if (omnibox_bounds_ == bounds)
    return;

  // Always track the omnibox bounds. That way if Update is later invoked the
  // bounds are in sync.
  omnibox_bounds_ = bounds;

  if (loader_.get() && !is_out_of_date_ && mode_ == INSTANT)
    loader_->SetOmniboxBounds(bounds);
}

void InstantController::DestroyPreviewContents() {
  if (!loader_.get()) {
    // We're not showing anything, nothing to do.
    return;
  }

  if (is_displayable_) {
    is_displayable_ = false;
    delegate_->HideInstant();
  }
  delete ReleasePreviewContents(INSTANT_COMMIT_DESTROY, NULL);
}

void InstantController::Hide() {
  is_out_of_date_ = true;
  commit_on_pointer_release_ = false;
  if (is_displayable_) {
    is_displayable_ = false;
    delegate_->HideInstant();
  }
}

bool InstantController::IsCurrent() const {
  // TODO(mmenke):  See if we can do something more intelligent in the
  //                navigation pending case.
  return is_displayable_ && !loader_->IsNavigationPending() &&
      !loader_->needs_reload();
}

bool InstantController::PrepareForCommit() {
  if (is_out_of_date_ || !loader_.get())
    return false;

  // If we are in the visible (INSTANT) mode, return the status of the preview.
  if (mode_ == INSTANT)
    return IsCurrent();

  TabContents* tab_contents = delegate_->GetInstantHostTabContents();
  if (!tab_contents)
    return false;

  const TemplateURL* template_url =
      TemplateURLServiceFactory::GetForProfile(tab_contents->profile())
                                 ->GetDefaultSearchProvider();
  if (!IsValidInstantTemplateURL(template_url) ||
      loader_->template_url_id() != template_url->id() ||
      loader_->IsNavigationPending() ||
      loader_->is_determining_if_page_supports_instant()) {
    return false;
  }

  // In the SUGGEST and HIDDEN modes, we must have sent an Update() by now, so
  // check if the loader failed to process it.
  if ((mode_ == SUGGEST || mode_ == HIDDEN)
      && (!loader_->ready() || !loader_->http_status_ok())) {
    return false;
  }

  // Ignore the suggested text, as we are about to commit the verbatim query.
  string16 suggested_text;
  UpdateLoader(tab_contents, template_url, last_url_, last_transition_type_,
               last_user_text_, true, &suggested_text);
  return true;
}

TabContents* InstantController::CommitCurrentPreview(InstantCommitType type) {
  DCHECK(loader_.get());
  TabContents* tab_contents = delegate_->GetInstantHostTabContents();
  DCHECK(tab_contents);
  TabContents* preview = ReleasePreviewContents(type, tab_contents);
  preview->web_contents()->GetController().CopyStateFromAndPrune(
      &tab_contents->web_contents()->GetController());
  delegate_->CommitInstant(preview);
  CompleteRelease(preview);
  return preview;
}

bool InstantController::CommitIfCurrent() {
  if (IsCurrent()) {
    CommitCurrentPreview(INSTANT_COMMIT_PRESSED_ENTER);
    return true;
  }
  return false;
}

void InstantController::SetCommitOnPointerRelease() {
  commit_on_pointer_release_ = true;
}

bool InstantController::IsPointerDownFromActivate() {
  DCHECK(loader_.get());
  return loader_->IsPointerDownFromActivate();
}

#if defined(OS_MACOSX)
void InstantController::OnAutocompleteLostFocus(
    gfx::NativeView view_gaining_focus) {
  // If |IsPointerDownFromActivate()| returns false, the RenderWidgetHostView
  // did not receive a mouseDown event. Therefore, we should destroy the
  // preview. Otherwise, the RWHV was clicked, so we commit the preview.
  if (!IsCurrent() || !IsPointerDownFromActivate())
    DestroyPreviewContents();
  else
    SetCommitOnPointerRelease();
}
#else
void InstantController::OnAutocompleteLostFocus(
    gfx::NativeView view_gaining_focus) {
  if (!IsCurrent()) {
    DestroyPreviewContents();
    return;
  }

  content::RenderWidgetHostView* rwhv =
      GetPreviewContents()->web_contents()->GetRenderWidgetHostView();
  if (!view_gaining_focus || !rwhv) {
    DestroyPreviewContents();
    return;
  }

#if defined(TOOLKIT_VIEWS)
  // For views the top level widget is always focused. If the focus change
  // originated in views determine the child Widget from the view that is being
  // focused.
  views::Widget* widget =
      views::Widget::GetWidgetForNativeView(view_gaining_focus);
  if (widget) {
    views::FocusManager* focus_manager = widget->GetFocusManager();
    if (focus_manager && focus_manager->is_changing_focus() &&
        focus_manager->GetFocusedView() &&
        focus_manager->GetFocusedView()->GetWidget()) {
      view_gaining_focus =
          focus_manager->GetFocusedView()->GetWidget()->GetNativeView();
    }
  }
#endif

  gfx::NativeView tab_view =
      GetPreviewContents()->web_contents()->GetNativeView();
  // Focus is going to the renderer.
  if (rwhv->GetNativeView() == view_gaining_focus ||
      tab_view == view_gaining_focus) {
    if (!IsPointerDownFromActivate()) {
      // If the mouse is not down, focus is not going to the renderer. Someone
      // else moved focus and we shouldn't commit.
      DestroyPreviewContents();
      return;
    }

    // We're showing instant results. As instant results may shift when
    // committing we commit on the mouse up. This way a slow click still works
    // fine.
    SetCommitOnPointerRelease();
    return;
  }

  // Walk up the view hierarchy. If the view gaining focus is a subview of the
  // WebContents view (such as a windowed plugin or http auth dialog), we want
  // to keep the preview contents. Otherwise, focus has gone somewhere else,
  // such as the JS inspector, and we want to cancel the preview.
  gfx::NativeView view_gaining_focus_ancestor = view_gaining_focus;
  while (view_gaining_focus_ancestor &&
         view_gaining_focus_ancestor != tab_view) {
    view_gaining_focus_ancestor =
        platform_util::GetParent(view_gaining_focus_ancestor);
  }

  if (view_gaining_focus_ancestor) {
    CommitCurrentPreview(INSTANT_COMMIT_FOCUS_LOST);
    return;
  }

  DestroyPreviewContents();
}
#endif

void InstantController::OnAutocompleteGotFocus() {
  TabContents* tab_contents = delegate_->GetInstantHostTabContents();
  if (!tab_contents)
    return;

  const TemplateURL* template_url =
      TemplateURLServiceFactory::GetForProfile(tab_contents->profile())
                                 ->GetDefaultSearchProvider();
  if (!IsValidInstantTemplateURL(template_url))
    return;

  if (!loader_.get() || loader_->template_url_id() != template_url->id())
    loader_.reset(new InstantLoader(this, template_url->id(), std::string()));
  loader_->MaybeLoadInstantURL(tab_contents, template_url);
}

TabContents* InstantController::ReleasePreviewContents(
    InstantCommitType type,
    TabContents* current_tab) {
  if (!loader_.get())
    return NULL;

  TabContents* tab = loader_->ReleasePreviewContents(type, current_tab);
  ClearBlacklist();
  is_out_of_date_ = true;
  is_displayable_ = false;
  commit_on_pointer_release_ = false;
  omnibox_bounds_ = gfx::Rect();
  loader_.reset();
  return tab;
}

void InstantController::CompleteRelease(TabContents* tab) {
  tab->blocked_content_tab_helper()->SetAllContentsBlocked(false);
}

TabContents* InstantController::GetPreviewContents() const {
  return loader_.get() ? loader_->preview_contents() : NULL;
}

void InstantController::InstantStatusChanged(InstantLoader* loader) {
  DCHECK(loader_.get());
  UpdateIsDisplayable();
}

void InstantController::SetSuggestedTextFor(
    InstantLoader* loader,
    const string16& text,
    InstantCompleteBehavior behavior) {
  if (is_out_of_date_)
    return;

  if (mode_ == INSTANT || mode_ == SUGGEST)
    delegate_->SetSuggestedText(text, behavior);
}

gfx::Rect InstantController::GetInstantBounds() {
  return delegate_->GetInstantBounds();
}

bool InstantController::ShouldCommitInstantOnPointerRelease() {
  return commit_on_pointer_release_;
}

void InstantController::CommitInstantLoader(InstantLoader* loader) {
  if (loader_.get() && loader_.get() == loader) {
    CommitCurrentPreview(INSTANT_COMMIT_FOCUS_LOST);
  } else {
    // This can happen if the mouse was down, we swapped out the preview and
    // the mouse was released. Generally this shouldn't happen, but if it does
    // revert.
    DestroyPreviewContents();
  }
}

void InstantController::InstantLoaderDoesntSupportInstant(
    InstantLoader* loader) {
  VLOG(1) << "provider does not support instant";

  // Don't attempt to use instant for this search engine again.
  BlacklistFromInstant();
}

void InstantController::AddToBlacklist(InstantLoader* loader, const GURL& url) {
  // Don't attempt to use instant for this search engine again.
  BlacklistFromInstant();
}

void InstantController::SwappedTabContents(InstantLoader* loader) {
  if (is_displayable_)
    delegate_->ShowInstant(loader->preview_contents());
}

void InstantController::InstantLoaderContentsFocused() {
#if defined(USE_AURA)
  // On aura the omnibox only receives a focus lost if we initiate the focus
  // change. This does that.
  if (mode_ == INSTANT)
    delegate_->InstantPreviewFocused();
#endif
}

void InstantController::UpdateIsDisplayable() {
  bool displayable = !is_out_of_date_ && loader_.get() && loader_->ready() &&
                     loader_->http_status_ok();
  if (displayable == is_displayable_ || mode_ != INSTANT)
    return;

  is_displayable_ = displayable;
  if (!is_displayable_) {
    delegate_->HideInstant();
  } else {
    delegate_->ShowInstant(loader_->preview_contents());
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_INSTANT_CONTROLLER_SHOWN,
        content::Source<InstantController>(this),
        content::NotificationService::NoDetails());
  }
}

void InstantController::UpdateLoader(TabContents* tab_contents,
                                     const TemplateURL* template_url,
                                     const GURL& url,
                                     content::PageTransition transition_type,
                                     const string16& user_text,
                                     bool verbatim,
                                     string16* suggested_text) {
  if (mode_ == INSTANT)
    loader_->SetOmniboxBounds(omnibox_bounds_);
  loader_->Update(tab_contents, template_url, url, transition_type, user_text,
                  verbatim, suggested_text);
  UpdateIsDisplayable();
  // For the HIDDEN and SILENT modes, don't send back suggestions.
  if (mode_ == HIDDEN || mode_ == SILENT)
    suggested_text->clear();
}

// Returns true if |template_url| is a valid TemplateURL for use by instant.
bool InstantController::IsValidInstantTemplateURL(
    const TemplateURL* template_url) {
  return template_url && template_url->id() &&
      template_url->instant_url_ref().SupportsReplacement() &&
      !IsBlacklistedFromInstant(template_url->id());
}

void InstantController::BlacklistFromInstant() {
  if (!loader_.get())
    return;

  DCHECK(loader_->template_url_id());
  blacklisted_ids_.insert(loader_->template_url_id());

  // Because of the state of the stack we can't destroy the loader now.
  ScheduleDestroy(loader_.release());
  UpdateIsDisplayable();
}

bool InstantController::IsBlacklistedFromInstant(TemplateURLID id) {
  return blacklisted_ids_.count(id) > 0;
}

void InstantController::ClearBlacklist() {
  blacklisted_ids_.clear();
}

void InstantController::ScheduleDestroy(InstantLoader* loader) {
  loaders_to_destroy_.push_back(loader);
  if (!weak_factory_.HasWeakPtrs()) {
    MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(&InstantController::DestroyLoaders,
                              weak_factory_.GetWeakPtr()));
  }
}

void InstantController::DestroyLoaders() {
  loaders_to_destroy_.clear();
}
