// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/string_number_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/utf_string_conversions.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/speech/input_tag_speech_dispatcher_host.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/speech_recognition_manager.h"
#include "content/public/browser/speech_recognition_session_config.h"
#include "content/public/browser/speech_recognition_session_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/speech_recognition_error.h"
#include "content/public/common/speech_recognition_result.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/test_utils.h"
#include "content/shell/shell.h"
#include "content/test/content_browser_test.h"
#include "content/test/content_browser_test_utils.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputEvent.h"

namespace content {

const char kTestResult[] = "Pictures of the moon";

class FakeSpeechRecognitionManager : public SpeechRecognitionManager {
 public:
  FakeSpeechRecognitionManager()
      : session_id_(0),
        listener_(NULL),
        did_cancel_all_(false),
        should_send_fake_response_(true),
        recognition_started_event_(false, false) {
  }

  std::string grammar() {
    return grammar_;
  }

  bool did_cancel_all() {
    return did_cancel_all_;
  }

  void set_should_send_fake_response(bool send) {
    should_send_fake_response_ = send;
  }

  bool should_send_fake_response() {
    return should_send_fake_response_;
  }

  base::WaitableEvent& recognition_started_event() {
    return recognition_started_event_;
  }

  // SpeechRecognitionManager methods.
  virtual int CreateSession(
      const SpeechRecognitionSessionConfig& config) OVERRIDE {
    VLOG(1) << "FAKE CreateSession invoked.";
    EXPECT_EQ(0, session_id_);
    EXPECT_EQ(NULL, listener_);
    listener_ = config.event_listener;
    if (config.grammars.size() > 0)
      grammar_ = config.grammars[0].url;
    session_ctx_ = config.initial_context;
    session_config_ = config;
    session_id_ = 1;
    return session_id_;
  }

  virtual void StartSession(int session_id) OVERRIDE {
    VLOG(1) << "FAKE StartSession invoked.";
    EXPECT_EQ(session_id, session_id_);
    EXPECT_TRUE(listener_ != NULL);

    if (should_send_fake_response_) {
      // Give the fake result in a short while.
      MessageLoop::current()->PostTask(FROM_HERE, base::Bind(
          &FakeSpeechRecognitionManager::SetFakeRecognitionResult,
          // This class does not need to be refcounted (typically done by
          // PostTask) since it will outlive the test and gets released only
          // when the test shuts down. Disabling refcounting here saves a bit
          // of unnecessary code and the factory method can return a plain
          // pointer below as required by the real code.
          base::Unretained(this)));
    }
    recognition_started_event_.Signal();
  }

  virtual void AbortSession(int session_id) OVERRIDE {
    VLOG(1) << "FAKE AbortSession invoked.";
    EXPECT_EQ(session_id_, session_id);
    session_id_ = 0;
    listener_ = NULL;
  }

  virtual void StopAudioCaptureForSession(int session_id) OVERRIDE {
    VLOG(1) << "StopRecording invoked.";
    EXPECT_EQ(session_id_, session_id);
    // Nothing to do here since we aren't really recording.
  }

  virtual void AbortAllSessionsForListener(
      SpeechRecognitionEventListener* listener) OVERRIDE {
    VLOG(1) << "CancelAllRequestsWithDelegate invoked.";
    // listener_ is set to NULL if a fake result was received (see below), so
    // check that listener_ matches the incoming parameter only when there is
    // no fake result sent.
    EXPECT_TRUE(should_send_fake_response_ || listener_ == listener);
    did_cancel_all_ = true;
  }

  virtual void AbortAllSessionsForRenderView(int render_process_id,
                                             int render_view_id) OVERRIDE {
    NOTREACHED();
  }
  virtual bool HasAudioInputDevices() OVERRIDE { return true; }
  virtual bool IsCapturingAudio() OVERRIDE { return true; }
  virtual string16 GetAudioInputDeviceModel() OVERRIDE { return string16(); }
  virtual void ShowAudioInputSettings() OVERRIDE {}

  virtual int GetSession(int render_process_id,
                         int render_view_id,
                         int request_id) const OVERRIDE {
    return session_ctx_.render_process_id == render_process_id &&
           session_ctx_.render_view_id == render_view_id &&
           session_ctx_.request_id == request_id;
  }

  virtual const SpeechRecognitionSessionConfig& GetSessionConfig(
      int session_id) const OVERRIDE {
    EXPECT_EQ(session_id, session_id_);
    return session_config_;
  }

  virtual SpeechRecognitionSessionContext GetSessionContext(
      int session_id) const OVERRIDE {
    EXPECT_EQ(session_id, session_id_);
    return session_ctx_;
  }

 private:
  void SetFakeRecognitionResult() {
    if (session_id_) {  // Do a check in case we were cancelled..
      VLOG(1) << "Setting fake recognition result.";
      listener_->OnAudioEnd(session_id_);
      SpeechRecognitionResult result;
      result.hypotheses.push_back(SpeechRecognitionHypothesis(
          ASCIIToUTF16(kTestResult), 1.0));
      SpeechRecognitionResults results;
      results.push_back(result);
      listener_->OnRecognitionResults(session_id_, results);
      listener_->OnRecognitionEnd(session_id_);
      session_id_ = 0;
      listener_ = NULL;
      VLOG(1) << "Finished setting fake recognition result.";
    }
  }

  int session_id_;
  SpeechRecognitionEventListener* listener_;
  SpeechRecognitionSessionConfig session_config_;
  SpeechRecognitionSessionContext session_ctx_;
  std::string grammar_;
  bool did_cancel_all_;
  bool should_send_fake_response_;
  base::WaitableEvent recognition_started_event_;
};

class SpeechRecognitionBrowserTest : public ContentBrowserTest {
 public:
  // ContentBrowserTest methods
  virtual void SetUpCommandLine(CommandLine* command_line) {
    EXPECT_TRUE(!command_line->HasSwitch(switches::kDisableSpeechInput));
  }

 protected:
  void LoadAndStartSpeechRecognitionTest(const char* filename) {
    // The test page calculates the speech button's coordinate in the page on
    // load & sets that coordinate in the URL fragment. We send mouse down & up
    // events at that coordinate to trigger speech recognition.
    GURL test_url = GetTestUrl("speech", filename);
    NavigateToURL(shell(), test_url);

    WebKit::WebMouseEvent mouse_event;
    mouse_event.type = WebKit::WebInputEvent::MouseDown;
    mouse_event.button = WebKit::WebMouseEvent::ButtonLeft;
    mouse_event.x = 0;
    mouse_event.y = 0;
    mouse_event.clickCount = 1;
    WebContents* web_contents = shell()->web_contents();

    WindowedNotificationObserver observer(
        NOTIFICATION_LOAD_STOP,
        Source<NavigationController>(&web_contents->GetController()));
    web_contents->GetRenderViewHost()->ForwardMouseEvent(mouse_event);
    mouse_event.type = WebKit::WebInputEvent::MouseUp;
    web_contents->GetRenderViewHost()->ForwardMouseEvent(mouse_event);
    fake_speech_recognition_manager_.recognition_started_event().Wait();

    // We should wait for a navigation event, raised by the test page JS code
    // upon the onwebkitspeechchange event, in all cases except when the
    // speech response is inhibited.
    if (fake_speech_recognition_manager_.should_send_fake_response())
      observer.Wait();
  }

  void RunSpeechRecognitionTest(const char* filename) {
    // The fake speech input manager would receive the speech input
    // request and return the test string as recognition result. The test page
    // then sets the URL fragment as 'pass' if it received the expected string.
    LoadAndStartSpeechRecognitionTest(filename);

    EXPECT_EQ("pass", shell()->web_contents()->GetURL().ref());
  }

  // ContentBrowserTest methods.
  virtual void SetUpInProcessBrowserTestFixture() {
    fake_speech_recognition_manager_.set_should_send_fake_response(true);
    speech_recognition_manager_ = &fake_speech_recognition_manager_;

    // Inject the fake manager factory so that the test result is returned to
    // the web page.
    InputTagSpeechDispatcherHost::SetManagerForTests(
        speech_recognition_manager_);
  }

  virtual void TearDownInProcessBrowserTestFixture() {
    speech_recognition_manager_ = NULL;
  }

  FakeSpeechRecognitionManager fake_speech_recognition_manager_;

  // This is used by the static |fakeManager|, and it is a pointer rather than a
  // direct instance per the style guide.
  static SpeechRecognitionManager* speech_recognition_manager_;
};

SpeechRecognitionManager*
    SpeechRecognitionBrowserTest::speech_recognition_manager_ = NULL;

// TODO(satish): Once this flakiness has been fixed, add a second test here to
// check for sending many clicks in succession to the speech button and verify
// that it doesn't cause any crash but works as expected. This should act as the
// test for http://crbug.com/59173
//
// TODO(satish): Similar to above, once this flakiness has been fixed add
// another test here to check that when speech recognition is in progress and
// a renderer crashes, we get a call to
// SpeechRecognitionManager::CancelAllRequestsWithDelegate.
IN_PROC_BROWSER_TEST_F(SpeechRecognitionBrowserTest, TestBasicRecognition) {
  RunSpeechRecognitionTest("basic_recognition.html");
  EXPECT_TRUE(fake_speech_recognition_manager_.grammar().empty());
}

IN_PROC_BROWSER_TEST_F(SpeechRecognitionBrowserTest, GrammarAttribute) {
  RunSpeechRecognitionTest("grammar_attribute.html");
  EXPECT_EQ("http://example.com/grammar.xml",
            fake_speech_recognition_manager_.grammar());
}

// Flaky on Linux, Windows and Mac http://crbug.com/140765.
IN_PROC_BROWSER_TEST_F(SpeechRecognitionBrowserTest, DISABLED_TestCancelAll) {
  // The test checks that the cancel-all callback gets issued when a session
  // is pending, so don't send a fake response.
  // We are not expecting a navigation event being raised from the JS of the
  // test page JavaScript in this case.
  fake_speech_recognition_manager_.set_should_send_fake_response(false);

  LoadAndStartSpeechRecognitionTest("basic_recognition.html");

  // Make the renderer crash. This should trigger
  // InputTagSpeechDispatcherHost to cancel all pending sessions.
  NavigateToURL(shell(), GURL(chrome::kChromeUICrashURL));

  EXPECT_TRUE(fake_speech_recognition_manager_.did_cancel_all());
}

}  // namespace content
