// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/safe_browsing/phishing_classifier_delegate.h"

#include <set>

#include "base/callback.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/memory/scoped_callback_factory.h"
#include "chrome/common/safe_browsing/csd.pb.h"
#include "chrome/common/safe_browsing/safebrowsing_messages.h"
#include "chrome/renderer/safe_browsing/feature_extractor_clock.h"
#include "chrome/renderer/safe_browsing/phishing_classifier.h"
#include "chrome/renderer/safe_browsing/scorer.h"
#include "content/renderer/navigation_state.h"
#include "content/renderer/render_thread.h"
#include "content/renderer/render_view.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebURL.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"

namespace safe_browsing {


static GURL StripRef(const GURL& url) {
  GURL::Replacements replacements;
  replacements.ClearRef();
  return url.ReplaceComponents(replacements);
}

typedef std::set<PhishingClassifierDelegate*> PhishingClassifierDelegates;
static base::LazyInstance<PhishingClassifierDelegates>
    g_delegates(base::LINKER_INITIALIZED);

static base::LazyInstance<scoped_ptr<const safe_browsing::Scorer> >
    g_phishing_scorer(base::LINKER_INITIALIZED);

class ScorerCallback {
 public:
  static Scorer::CreationCallback* CreateCallback() {
    ScorerCallback* scorer_callback = new ScorerCallback();
    return scorer_callback->callback_factory_->NewCallback(
        &ScorerCallback::PhishingScorerCreated);
  }

 private:
  ScorerCallback() {
    callback_factory_.reset(
        new base::ScopedCallbackFactory<ScorerCallback>(this));
  }

  // Callback to be run once the phishing Scorer has been created.
  void PhishingScorerCreated(safe_browsing::Scorer* scorer) {
    if (!scorer) {
      DLOG(ERROR) << "Unable to create a PhishingScorer - corrupt model?";
      return;
    }

    g_phishing_scorer.Get().reset(scorer);

    PhishingClassifierDelegates::iterator i;
    for (i = g_delegates.Get().begin(); i != g_delegates.Get().end(); ++i)
      (*i)->SetPhishingScorer(scorer);

    delete this;
  }

  scoped_ptr<base::ScopedCallbackFactory<ScorerCallback> > callback_factory_;
};

// static
PhishingClassifierFilter* PhishingClassifierFilter::Create() {
  // Private constructor and public static Create() method to facilitate
  // stubbing out this class for binary-size reduction purposes.
  return new PhishingClassifierFilter();
}

PhishingClassifierFilter::PhishingClassifierFilter()
    : RenderProcessObserver() {}

PhishingClassifierFilter::~PhishingClassifierFilter() {}

bool PhishingClassifierFilter::OnControlMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PhishingClassifierFilter, message)
    IPC_MESSAGE_HANDLER(SafeBrowsingMsg_SetPhishingModel, OnSetPhishingModel)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PhishingClassifierFilter::OnSetPhishingModel(
    IPC::PlatformFileForTransit model_file) {
  safe_browsing::Scorer::CreateFromFile(
      IPC::PlatformFileForTransitToPlatformFile(model_file),
      RenderThread::current()->GetFileThreadMessageLoopProxy(),
      ScorerCallback::CreateCallback());
}

// static
PhishingClassifierDelegate* PhishingClassifierDelegate::Create(
    RenderView* render_view, PhishingClassifier* classifier) {
  // Private constructor and public static Create() method to facilitate
  // stubbing out this class for binary-size reduction purposes.
  return new PhishingClassifierDelegate(render_view, classifier);
}

PhishingClassifierDelegate::PhishingClassifierDelegate(
    RenderView* render_view,
    PhishingClassifier* classifier)
    : RenderViewObserver(render_view),
      last_main_frame_transition_(PageTransition::LINK),
      have_page_text_(false),
      is_classifying_(false) {
  g_delegates.Get().insert(this);
  if (!classifier) {
    classifier = new PhishingClassifier(render_view,
                                        new FeatureExtractorClock());
  }

  classifier_.reset(classifier);

  if (g_phishing_scorer.Get().get())
    SetPhishingScorer(g_phishing_scorer.Get().get());
}

PhishingClassifierDelegate::~PhishingClassifierDelegate() {
  CancelPendingClassification(SHUTDOWN);
  g_delegates.Get().erase(this);
}

void PhishingClassifierDelegate::SetPhishingScorer(
    const safe_browsing::Scorer* scorer) {
  if (!render_view()->webview())
    return;  // RenderView is tearing down.

  classifier_->set_phishing_scorer(scorer);
  // Start classifying the current page if all conditions are met.
  // See MaybeStartClassification() for details.
  MaybeStartClassification();
}


void PhishingClassifierDelegate::OnStartPhishingDetection(const GURL& url) {
  last_url_received_from_browser_ = StripRef(url);
  // Start classifying the current page if all conditions are met.
  // See MaybeStartClassification() for details.
  MaybeStartClassification();
}

void PhishingClassifierDelegate::DidCommitProvisionalLoad(
    WebKit::WebFrame* frame, bool is_new_navigation) {
  // A new page is starting to load, so cancel classificaiton.
  //
  // TODO(bryner): We shouldn't need to cancel classification if the navigation
  // is within the same page.  However, if we let classification continue in
  // this case, we need to properly deal with the fact that PageCaptured will
  // be called again for the in-page navigation.  We need to be sure not to
  // swap out the page text while the term feature extractor is still running.
  NavigationState* state = NavigationState::FromDataSource(
      frame->dataSource());
  CancelPendingClassification(state->was_within_same_page() ?
                              NAVIGATE_WITHIN_PAGE : NAVIGATE_AWAY);
  if (frame == render_view()->webview()->mainFrame()) {
    last_main_frame_transition_ = state->transition_type();
  }
}

void PhishingClassifierDelegate::PageCaptured(string16* page_text,
                                              bool preliminary_capture) {
  if (preliminary_capture) {
    return;
  }
  // Make sure there's no classification in progress.  We don't want to swap
  // out the page text string from underneath the term feature extractor.
  //
  // Note: Currently, if the url hasn't changed, we won't restart
  // classification in this case.  We may want to adjust this.
  CancelPendingClassification(PAGE_RECAPTURED);
  last_finished_load_url_ = GetToplevelUrl();
  classifier_page_text_.swap(*page_text);
  have_page_text_ = true;
  MaybeStartClassification();
}

void PhishingClassifierDelegate::CancelPendingClassification(
    CancelClassificationReason reason) {
  if (is_classifying_) {
    UMA_HISTOGRAM_ENUMERATION("SBClientPhishing.CancelClassificationReason",
                              reason,
                              CANCEL_CLASSIFICATION_MAX);
    is_classifying_ = false;
  }
  if (classifier_->is_ready()) {
    classifier_->CancelPendingClassification();
  }
  classifier_page_text_.clear();
  have_page_text_ = false;
}

bool PhishingClassifierDelegate::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PhishingClassifierDelegate, message)
    IPC_MESSAGE_HANDLER(SafeBrowsingMsg_StartPhishingDetection,
                        OnStartPhishingDetection)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PhishingClassifierDelegate::ClassificationDone(
    const ClientPhishingRequest& verdict) {
  // We no longer need the page text.
  classifier_page_text_.clear();
  VLOG(2) << "Phishy verdict = " << verdict.is_phishing()
          << " score = " << verdict.client_score();
  if (!verdict.is_phishing()) {
    return;
  }
  DCHECK(last_url_sent_to_classifier_.spec() == verdict.url());
  Send(new SafeBrowsingHostMsg_DetectedPhishingSite(
      routing_id(), verdict.SerializeAsString()));
}

GURL PhishingClassifierDelegate::GetToplevelUrl() {
  return render_view()->webview()->mainFrame()->url();
}

void PhishingClassifierDelegate::MaybeStartClassification() {
  // We can begin phishing classification when the following conditions are
  // met:
  //  1. A Scorer has been created
  //  2. The browser has sent a StartPhishingDetection message for the current
  //     toplevel URL.
  //  3. The page has finished loading and the page text has been extracted.
  //  4. The load is a new navigation (not a session history navigation).
  //  5. The toplevel URL has not already been classified.
  //
  // Note that if we determine that this particular navigation should not be
  // classified at all (as opposed to deferring it until we get an IPC or the
  // load completes), we discard the page text since it won't be needed.
  if (!classifier_->is_ready()) {
    VLOG(2) << "Not starting classification, no Scorer created.";
    // Keep classifier_page_text_, in case a Scorer is set later.
    return;
  }

  if (last_main_frame_transition_ & PageTransition::FORWARD_BACK) {
    // Skip loads from session history navigation.  However, update the
    // last URL sent to the classifier, so that we'll properly detect
    // in-page navigations.
    VLOG(2) << "Not starting classification for back/forward navigation";
    last_url_sent_to_classifier_ = last_finished_load_url_;
    classifier_page_text_.clear();  // we won't need this.
    have_page_text_ = false;
    return;
  }

  GURL stripped_last_load_url(StripRef(last_finished_load_url_));
  if (stripped_last_load_url == StripRef(last_url_sent_to_classifier_)) {
    // We've already classified this toplevel URL, so this was likely an
    // in-page navigation or a subframe navigation.  The browser should not
    // send a StartPhishingDetection IPC in this case.
    VLOG(2) << "Toplevel URL is unchanged, not starting classification.";
    classifier_page_text_.clear();  // we won't need this.
    have_page_text_ = false;
    return;
  }

  if (!have_page_text_) {
    VLOG(2) << "Not starting classification, there is no page text ready.";
    return;
  }

  if (last_url_received_from_browser_ != stripped_last_load_url) {
    // The browser has not yet confirmed that this URL should be classified,
    // so defer classification for now.  Note: the ref does not affect
    // any of the browser's preclassification checks, so we don't require it
    // to match.
    VLOG(2) << "Not starting classification, last url from browser is "
            << last_url_received_from_browser_ << ", last finished load is "
            << last_finished_load_url_;
    // Keep classifier_page_text_, in case the browser notifies us later that
    // we should classify the URL.
    return;
  }

  VLOG(2) << "Starting classification for " << last_finished_load_url_;
  last_url_sent_to_classifier_ = last_finished_load_url_;
  is_classifying_ = true;
  classifier_->BeginClassification(
      &classifier_page_text_,
      NewCallback(this, &PhishingClassifierDelegate::ClassificationDone));
}

}  // namespace safe_browsing
