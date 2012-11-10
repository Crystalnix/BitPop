// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/spellchecker/spellcheck_provider.h"

#include "base/command_line.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/spellcheck_messages.h"
#include "chrome/common/spellcheck_result.h"
#include "chrome/renderer/chrome_content_renderer_client.h"
#include "chrome/renderer/spellchecker/spellcheck.h"
#include "content/public/renderer/render_view.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebTextCheckingCompletion.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebTextCheckingResult.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebTextCheckingType.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebVector.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"

using WebKit::WebFrame;
using WebKit::WebString;
using WebKit::WebTextCheckingCompletion;
using WebKit::WebTextCheckingResult;
using WebKit::WebTextCheckingType;
using WebKit::WebVector;

COMPILE_ASSERT(int(WebKit::WebTextCheckingTypeSpelling) ==
               int(SpellCheckResult::SPELLING), mismatching_enums);
COMPILE_ASSERT(int(WebKit::WebTextCheckingTypeGrammar) ==
               int(SpellCheckResult::GRAMMAR), mismatching_enums);
COMPILE_ASSERT(int(WebKit::WebTextCheckingTypeLink) ==
               int(SpellCheckResult::LINK), mismatching_enums);
COMPILE_ASSERT(int(WebKit::WebTextCheckingTypeQuote) ==
               int(SpellCheckResult::QUOTE), mismatching_enums);
COMPILE_ASSERT(int(WebKit::WebTextCheckingTypeDash) ==
               int(SpellCheckResult::DASH), mismatching_enums);
COMPILE_ASSERT(int(WebKit::WebTextCheckingTypeReplacement) ==
               int(SpellCheckResult::REPLACEMENT), mismatching_enums);
COMPILE_ASSERT(int(WebKit::WebTextCheckingTypeCorrection) ==
               int(SpellCheckResult::CORRECTION), mismatching_enums);
COMPILE_ASSERT(int(WebKit::WebTextCheckingTypeShowCorrectionPanel) ==
               int(SpellCheckResult::SHOWCORRECTIONPANEL), mismatching_enums);

namespace {

// Converts a vector of SpellCheckResult objects (used by Chrome) to a vector of
// WebTextCheckingResult objects (used by WebKit).
void CreateTextCheckingResults(
    int offset,
    const std::vector<SpellCheckResult>& spellcheck_results,
    WebKit::WebVector<WebKit::WebTextCheckingResult>* textcheck_results) {
  size_t result_size = spellcheck_results.size();
  WebKit::WebVector<WebKit::WebTextCheckingResult> list(result_size);
  for (size_t i = 0; i < result_size; ++i) {
    list[i] = WebTextCheckingResult(
        static_cast<WebTextCheckingType>(spellcheck_results[i].type),
        spellcheck_results[i].location + offset,
        spellcheck_results[i].length,
        spellcheck_results[i].replacement);
  }
  textcheck_results->swap(list);
}

}  // namespace

SpellCheckProvider::SpellCheckProvider(
    content::RenderView* render_view,
    chrome::ChromeContentRendererClient* renderer_client)
    : content::RenderViewObserver(render_view),
#if defined(OS_MACOSX)
      has_document_tag_(false),
#endif
      document_tag_(0),
      spelling_panel_visible_(false),
      chrome_content_renderer_client_(renderer_client) {
  if (render_view)  // NULL in unit tests.
    render_view->GetWebView()->setSpellCheckClient(this);
}

SpellCheckProvider::~SpellCheckProvider() {
#if defined(OS_MACOSX)
  // Tell the spellchecker that the document is closed.
  if (has_document_tag_) {
    Send(new SpellCheckHostMsg_DocumentWithTagClosed(
        routing_id(), document_tag_));
  }
#endif
}

void SpellCheckProvider::RequestTextChecking(
    const WebString& text,
    int document_tag,
    WebTextCheckingCompletion* completion) {
#if defined(OS_MACOSX)
  // Text check (unified request for grammar and spell check) is only
  // available for browser process, so we ask the system spellchecker
  // over IPC or return an empty result if the checker is not
  // available.
  Send(new SpellCheckHostMsg_RequestTextCheck(
      routing_id(),
      text_check_completions_.Add(completion),
      document_tag,
      text));
#else
  if (text.isEmpty() || !HasWordCharacters(text, 0)) {
    completion->didCancelCheckingText();
    return;
  }
  // Cancel this spellcheck request if the cached text is a substring of the
  // given text and the given text is the middle of a possible word.
  // TODO(hbono): Move this cache code to a new function and add its unit test.
  string16 request(text);
  size_t text_length = request.length();
  size_t last_length = last_request_.length();
  if (text_length >= last_length &&
      !request.compare(0, last_length, last_request_)) {
    if (text_length == last_length || !HasWordCharacters(text, last_length)) {
      completion->didCancelCheckingText();
      return;
    }
    int code = 0;
    int length = static_cast<int>(text_length);
    U16_PREV(text.data(), 0, length, code);
    UErrorCode error = U_ZERO_ERROR;
    if (uscript_getScript(code, &error) != USCRIPT_COMMON) {
      completion->didCancelCheckingText();
      return;
    }
  }
  // Create a subset of the cached results and return it if the given text is a
  // substring of the cached text.
  if (text_length < last_length &&
      !last_request_.compare(0, text_length, request)) {
    size_t result_size = 0;
    for (size_t i = 0; i < last_results_.size(); ++i) {
      size_t start = last_results_[i].location;
      size_t end = start + last_results_[i].length;
      if (start <= text_length && end <= text_length)
        ++result_size;
    }
    if (result_size > 0) {
      WebKit::WebVector<WebKit::WebTextCheckingResult> results(result_size);
      for (size_t i = 0; i < result_size; ++i) {
        results[i].type = last_results_[i].type;
        results[i].location = last_results_[i].location;
        results[i].length = last_results_[i].length;
        results[i].replacement = last_results_[i].replacement;
      }
      completion->didFinishCheckingText(results);
      return;
    }
  }
  // Send this text to a browser. A browser checks the user profile and send
  // this text to the Spelling service only if a user enables this feature.
  last_request_.clear();
  last_results_.assign(WebKit::WebVector<WebKit::WebTextCheckingResult>());
  Send(new SpellCheckHostMsg_CallSpellingService(
      routing_id(),
      text_check_completions_.Add(completion),
      0,
      request));
#endif  // !OS_MACOSX
}

bool SpellCheckProvider::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(SpellCheckProvider, message)
#if !defined(OS_MACOSX)
    IPC_MESSAGE_HANDLER(SpellCheckMsg_RespondSpellingService,
                        OnRespondSpellingService)
#endif
#if defined(OS_MACOSX)
    IPC_MESSAGE_HANDLER(SpellCheckMsg_AdvanceToNextMisspelling,
                        OnAdvanceToNextMisspelling)
    IPC_MESSAGE_HANDLER(SpellCheckMsg_RespondTextCheck, OnRespondTextCheck)
    IPC_MESSAGE_HANDLER(SpellCheckMsg_ToggleSpellPanel, OnToggleSpellPanel)
#endif
    IPC_MESSAGE_HANDLER(SpellCheckMsg_ToggleSpellCheck, OnToggleSpellCheck)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void SpellCheckProvider::FocusedNodeChanged(const WebKit::WebNode& unused) {
#if defined(OS_MACOSX)
  bool enabled = false;
  WebKit::WebNode node = render_view()->GetFocusedNode();
  if (!node.isNull())
    enabled = render_view()->IsEditableNode(node);

  bool checked = false;
  if (enabled && render_view()->GetWebView()) {
    WebFrame* frame = render_view()->GetWebView()->focusedFrame();
    if (frame->isContinuousSpellCheckingEnabled())
      checked = true;
  }

  Send(new SpellCheckHostMsg_ToggleSpellCheck(routing_id(), enabled, checked));
#endif  // OS_MACOSX
}

void SpellCheckProvider::spellCheck(
    const WebString& text,
    int& offset,
    int& length,
    WebVector<WebString>* optional_suggestions) {
  EnsureDocumentTag();

  string16 word(text);
  // Will be NULL during unit tests.
  if (chrome_content_renderer_client_) {
    std::vector<string16> suggestions;
    chrome_content_renderer_client_->spellcheck()->SpellCheckWord(
        word.c_str(), word.size(), document_tag_,
        &offset, &length, optional_suggestions ? & suggestions : NULL);
    if (optional_suggestions)
      *optional_suggestions = suggestions;
    if (!optional_suggestions) {
      // If optional_suggestions is not requested, the API is called
      // for marking.  So we use this for counting markable words.
      Send(new SpellCheckHostMsg_NotifyChecked(routing_id(), word, 0 < length));
    }
  }
}

void SpellCheckProvider::checkTextOfParagraph(
    const WebKit::WebString& text,
    WebKit::WebTextCheckingTypeMask mask,
    WebKit::WebVector<WebKit::WebTextCheckingResult>* results) {
#if !defined(OS_MACOSX)
  // Since Mac has its own spell checker, this method will not be used on Mac.

  if (!results)
    return;

  if (!(mask & WebKit::WebTextCheckingTypeSpelling))
    return;

  EnsureDocumentTag();

  // Will be NULL during unit tets.
  if (!chrome_content_renderer_client_)
    return;

  chrome_content_renderer_client_->spellcheck()->SpellCheckParagraph(
      string16(text),
      results);
#endif
}

void SpellCheckProvider::requestCheckingOfText(
    const WebString& text,
    WebTextCheckingCompletion* completion) {
  RequestTextChecking(text, document_tag_, completion);
}

WebString SpellCheckProvider::autoCorrectWord(const WebString& word) {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kExperimentalSpellcheckerFeatures)) {
    EnsureDocumentTag();
    // Will be NULL during unit tests.
    if (chrome_content_renderer_client_) {
      return chrome_content_renderer_client_->spellcheck()->
          GetAutoCorrectionWord(word, document_tag_);
    }
  }
  return string16();
}

void SpellCheckProvider::showSpellingUI(bool show) {
#if defined(OS_MACOSX)
  Send(new SpellCheckHostMsg_ShowSpellingPanel(routing_id(), show));
#endif
}

bool SpellCheckProvider::isShowingSpellingUI() {
  return spelling_panel_visible_;
}

void SpellCheckProvider::updateSpellingUIWithMisspelledWord(
    const WebString& word) {
#if defined(OS_MACOSX)
  Send(new SpellCheckHostMsg_UpdateSpellingPanelWithMisspelledWord(routing_id(),
                                                                   word));
#endif
}

#if !defined(OS_MACOSX)
void SpellCheckProvider::OnRespondSpellingService(
    int identifier,
    int offset,
    bool succeeded,
    const string16& line,
    const std::vector<SpellCheckResult>& results) {
  WebTextCheckingCompletion* completion =
      text_check_completions_.Lookup(identifier);
  if (!completion)
    return;
  text_check_completions_.Remove(identifier);

  // If |succeeded| is false, we use local spellcheck as a fallback.
  if (!succeeded) {
    // |chrome_content_renderer_client| may be NULL in unit tests.
    if (chrome_content_renderer_client_) {
      chrome_content_renderer_client_->spellcheck()->RequestTextChecking(
          line, offset, completion);
      return;
    }
  }

  // Double-check the returned spellchecking results with our spellchecker to
  // visualize the differences between ours and the on-line spellchecker.
  WebKit::WebVector<WebKit::WebTextCheckingResult> textcheck_results;
  if (chrome_content_renderer_client_) {
    chrome_content_renderer_client_->spellcheck()->CreateTextCheckingResults(
        offset, line, results, &textcheck_results);
  } else {
    CreateTextCheckingResults(offset, results, &textcheck_results);
  }
  completion->didFinishCheckingText(textcheck_results);

  // Cache the request and the converted results.
  last_request_ = line;
  last_results_.swap(textcheck_results);
}

bool SpellCheckProvider::HasWordCharacters(
    const WebKit::WebString& text,
    int index) const {
  const char16* data = text.data();
  int length = text.length();
  while (index < length) {
    uint32 code = 0;
    U16_NEXT(data, index, length, code);
    UErrorCode error = U_ZERO_ERROR;
    if (uscript_getScript(code, &error) != USCRIPT_COMMON)
      return true;
  }
  return false;
}
#endif

#if defined(OS_MACOSX)
void SpellCheckProvider::OnAdvanceToNextMisspelling() {
  if (!render_view()->GetWebView())
    return;
  render_view()->GetWebView()->focusedFrame()->executeCommand(
      WebString::fromUTF8("AdvanceToNextMisspelling"));
}

void SpellCheckProvider::OnRespondTextCheck(
    int identifier,
    int tag,
    const std::vector<SpellCheckResult>& results) {
  WebTextCheckingCompletion* completion =
      text_check_completions_.Lookup(identifier);
  if (!completion)
    return;
  text_check_completions_.Remove(identifier);
  WebKit::WebVector<WebKit::WebTextCheckingResult> textcheck_results;
  CreateTextCheckingResults(0, results, &textcheck_results);
  completion->didFinishCheckingText(textcheck_results);
}

void SpellCheckProvider::OnToggleSpellPanel(bool is_currently_visible) {
  if (!render_view()->GetWebView())
    return;
  // We need to tell the webView whether the spelling panel is visible or not so
  // that it won't need to make ipc calls later.
  spelling_panel_visible_ = is_currently_visible;
  render_view()->GetWebView()->focusedFrame()->executeCommand(
      WebString::fromUTF8("ToggleSpellPanel"));
}
#endif

void SpellCheckProvider::OnToggleSpellCheck() {
  if (!render_view()->GetWebView())
    return;

  WebFrame* frame = render_view()->GetWebView()->focusedFrame();
  frame->enableContinuousSpellChecking(
      !frame->isContinuousSpellCheckingEnabled());
}

void SpellCheckProvider::EnsureDocumentTag() {
  // TODO(darin): There's actually no reason for this to be here.  We should
  // have the browser side manage the document tag.
#if defined(OS_MACOSX)
  if (!has_document_tag_) {
    // Make the call to get the tag.
    Send(new SpellCheckHostMsg_GetDocumentTag(routing_id(), &document_tag_));
    has_document_tag_ = true;
  }
#endif
}
