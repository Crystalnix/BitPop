// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "content/browser/browser_thread.h"
#include "content/browser/speech/speech_recognizer.h"
#include "content/common/test_url_fetcher_factory.h"
#include "media/audio/test_audio_input_controller_factory.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_request_status.h"
#include "testing/gtest/include/gtest/gtest.h"

using media::AudioInputController;
using media::TestAudioInputController;
using media::TestAudioInputControllerFactory;

namespace speech_input {

class SpeechRecognizerTest : public SpeechRecognizerDelegate,
                             public testing::Test {
 public:
  SpeechRecognizerTest()
      : io_thread_(BrowserThread::IO, &message_loop_),
        ALLOW_THIS_IN_INITIALIZER_LIST(
            recognizer_(new SpeechRecognizer(this, 1, std::string(),
                                             std::string(), std::string(),
                                             std::string()))),
        recording_complete_(false),
        recognition_complete_(false),
        result_received_(false),
        audio_received_(false),
        error_(SpeechRecognizer::RECOGNIZER_NO_ERROR),
        volume_(-1.0f) {
    int audio_packet_length_bytes =
        (SpeechRecognizer::kAudioSampleRate *
         SpeechRecognizer::kAudioPacketIntervalMs *
         ChannelLayoutToChannelCount(SpeechRecognizer::kChannelLayout) *
         SpeechRecognizer::kNumBitsPerAudioSample) / (8 * 1000);
    audio_packet_.resize(audio_packet_length_bytes);
  }

  // SpeechRecognizer::Delegate methods.
  virtual void SetRecognitionResult(int caller_id,
                                    bool error,
                                    const SpeechInputResultArray& result) {
    result_received_ = true;
  }

  virtual void DidCompleteRecording(int caller_id) {
    recording_complete_ = true;
  }

  virtual void DidCompleteRecognition(int caller_id) {
    recognition_complete_ = true;
  }

  virtual void DidCompleteEnvironmentEstimation(int caller_id) {
  }

  virtual void DidStartReceivingAudio(int caller_id) {
    audio_received_ = true;
  }

  virtual void OnRecognizerError(int caller_id,
                                 SpeechRecognizer::ErrorCode error) {
    error_ = error;
  }

  virtual void SetInputVolume(int caller_id, float volume, float noise_volume) {
    volume_ = volume;
    noise_volume_ = noise_volume;
  }

  // testing::Test methods.
  virtual void SetUp() {
    URLFetcher::set_factory(&url_fetcher_factory_);
    AudioInputController::set_factory(&audio_input_controller_factory_);
  }

  virtual void TearDown() {
    URLFetcher::set_factory(NULL);
    AudioInputController::set_factory(NULL);
  }

  void FillPacketWithTestWaveform() {
    // Fill the input with a simple pattern, a 125Hz sawtooth waveform.
    for (size_t i = 0; i < audio_packet_.size(); ++i)
      audio_packet_[i] = static_cast<uint8>(i);
  }

  void FillPacketWithNoise() {
    int value = 0;
    int factor = 175;
    for (size_t i = 0; i < audio_packet_.size(); ++i) {
      value += factor;
      audio_packet_[i] = value % 100;
    }
  }

 protected:
  MessageLoopForIO message_loop_;
  BrowserThread io_thread_;
  scoped_refptr<SpeechRecognizer> recognizer_;
  bool recording_complete_;
  bool recognition_complete_;
  bool result_received_;
  bool audio_received_;
  SpeechRecognizer::ErrorCode error_;
  TestURLFetcherFactory url_fetcher_factory_;
  TestAudioInputControllerFactory audio_input_controller_factory_;
  std::vector<uint8> audio_packet_;
  float volume_;
  float noise_volume_;
};

TEST_F(SpeechRecognizerTest, StopNoData) {
  // Check for callbacks when stopping record before any audio gets recorded.
  EXPECT_TRUE(recognizer_->StartRecording());
  recognizer_->CancelRecognition();
  EXPECT_FALSE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_FALSE(audio_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_NO_ERROR, error_);
}

TEST_F(SpeechRecognizerTest, CancelNoData) {
  // Check for callbacks when canceling recognition before any audio gets
  // recorded.
  EXPECT_TRUE(recognizer_->StartRecording());
  recognizer_->StopRecording();
  EXPECT_TRUE(recording_complete_);
  EXPECT_TRUE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_FALSE(audio_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_NO_ERROR, error_);
}

TEST_F(SpeechRecognizerTest, StopWithData) {
  // Start recording, give some data and then stop. This should wait for the
  // network callback to arrive before completion.
  EXPECT_TRUE(recognizer_->StartRecording());
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);

  // Try sending 5 chunks of mock audio data and verify that each of them
  // resulted immediately in a packet sent out via the network. This verifies
  // that we are streaming out encoded data as chunks without waiting for the
  // full recording to complete.
  const size_t kNumChunks = 5;
  for (size_t i = 0; i < kNumChunks; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
    MessageLoop::current()->RunAllPending();
    TestURLFetcher* fetcher = url_fetcher_factory_.GetFetcherByID(0);
    ASSERT_TRUE(fetcher);
    EXPECT_EQ(i + 1, fetcher->upload_chunks().size());
  }

  recognizer_->StopRecording();
  EXPECT_TRUE(audio_received_);
  EXPECT_TRUE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_NO_ERROR, error_);

  // Issue the network callback to complete the process.
  TestURLFetcher* fetcher = url_fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher);
  net::URLRequestStatus status;
  status.set_status(net::URLRequestStatus::SUCCESS);
  fetcher->delegate()->OnURLFetchComplete(
      fetcher, fetcher->original_url(), status, 200, net::ResponseCookies(),
      "{\"hypotheses\":[{\"utterance\":\"123\"}]}");
  EXPECT_TRUE(recognition_complete_);
  EXPECT_TRUE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_NO_ERROR, error_);
}

TEST_F(SpeechRecognizerTest, CancelWithData) {
  // Start recording, give some data and then cancel. This should create
  // a network request but give no callbacks.
  EXPECT_TRUE(recognizer_->StartRecording());
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  recognizer_->CancelRecognition();
  ASSERT_TRUE(url_fetcher_factory_.GetFetcherByID(0));
  EXPECT_TRUE(audio_received_);
  EXPECT_FALSE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_NO_ERROR, error_);
}

TEST_F(SpeechRecognizerTest, ConnectionError) {
  // Start recording, give some data and then stop. Issue the network callback
  // with a connection error and verify that the recognizer bubbles the error up
  EXPECT_TRUE(recognizer_->StartRecording());
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  TestURLFetcher* fetcher = url_fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher);

  recognizer_->StopRecording();
  EXPECT_TRUE(audio_received_);
  EXPECT_TRUE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_NO_ERROR, error_);

  // Issue the network callback to complete the process.
  net::URLRequestStatus status;
  status.set_status(net::URLRequestStatus::FAILED);
  status.set_os_error(net::ERR_CONNECTION_REFUSED);
  fetcher->delegate()->OnURLFetchComplete(
      fetcher, fetcher->original_url(), status, 0, net::ResponseCookies(), "");
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_ERROR_NETWORK, error_);
}

TEST_F(SpeechRecognizerTest, ServerError) {
  // Start recording, give some data and then stop. Issue the network callback
  // with a 500 error and verify that the recognizer bubbles the error up
  EXPECT_TRUE(recognizer_->StartRecording());
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  TestURLFetcher* fetcher = url_fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher);

  recognizer_->StopRecording();
  EXPECT_TRUE(audio_received_);
  EXPECT_TRUE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_NO_ERROR, error_);

  // Issue the network callback to complete the process.
  net::URLRequestStatus status;
  status.set_status(net::URLRequestStatus::SUCCESS);
  fetcher->delegate()->OnURLFetchComplete(fetcher, fetcher->original_url(),
                                          status, 500, net::ResponseCookies(),
                                          "Internal Server Error");
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_ERROR_NETWORK, error_);
}

TEST_F(SpeechRecognizerTest, AudioControllerErrorNoData) {
  // Check if things tear down properly if AudioInputController threw an error.
  EXPECT_TRUE(recognizer_->StartRecording());
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnError(controller, 0);
  MessageLoop::current()->RunAllPending();
  EXPECT_FALSE(audio_received_);
  EXPECT_FALSE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_ERROR_CAPTURE, error_);
}

TEST_F(SpeechRecognizerTest, AudioControllerErrorWithData) {
  // Check if things tear down properly if AudioInputController threw an error
  // after giving some audio data.
  EXPECT_TRUE(recognizer_->StartRecording());
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  controller->event_handler()->OnError(controller, 0);
  MessageLoop::current()->RunAllPending();
  ASSERT_TRUE(url_fetcher_factory_.GetFetcherByID(0));
  EXPECT_TRUE(audio_received_);
  EXPECT_FALSE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_ERROR_CAPTURE, error_);
}

TEST_F(SpeechRecognizerTest, NoSpeechCallbackIssued) {
  // Start recording and give a lot of packets with audio samples set to zero.
  // This should trigger the no-speech detector and issue a callback.
  EXPECT_TRUE(recognizer_->StartRecording());
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller = audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);

  int num_packets = (SpeechRecognizer::kNoSpeechTimeoutSec * 1000) /
                     SpeechRecognizer::kAudioPacketIntervalMs;
  // The vector is already filled with zero value samples on create.
  for (int i = 0; i < num_packets; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
  }
  MessageLoop::current()->RunAllPending();
  EXPECT_TRUE(audio_received_);
  EXPECT_FALSE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_ERROR_NO_SPEECH, error_);
}

TEST_F(SpeechRecognizerTest, NoSpeechCallbackNotIssued) {
  // Start recording and give a lot of packets with audio samples set to zero
  // and then some more with reasonably loud audio samples. This should be
  // treated as normal speech input and the no-speech detector should not get
  // triggered.
  EXPECT_TRUE(recognizer_->StartRecording());
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller = audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);

  int num_packets = (SpeechRecognizer::kNoSpeechTimeoutSec * 1000) /
                     SpeechRecognizer::kAudioPacketIntervalMs;

  // The vector is already filled with zero value samples on create.
  for (int i = 0; i < num_packets / 2; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
  }

  FillPacketWithTestWaveform();
  for (int i = 0; i < num_packets / 2; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
  }

  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_NO_ERROR, error_);
  EXPECT_TRUE(audio_received_);
  EXPECT_FALSE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  recognizer_->CancelRecognition();
}

TEST_F(SpeechRecognizerTest, SetInputVolumeCallback) {
  // Start recording and give a lot of packets with audio samples set to zero
  // and then some more with reasonably loud audio samples. Check that we don't
  // get the callback during estimation phase, then get zero for the silence
  // samples and proper volume for the loud audio.
  EXPECT_TRUE(recognizer_->StartRecording());
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller = audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);

  // Feed some samples to begin with for the endpointer to do noise estimation.
  int num_packets = SpeechRecognizer::kEndpointerEstimationTimeMs /
                    SpeechRecognizer::kAudioPacketIntervalMs;
  FillPacketWithNoise();
  for (int i = 0; i < num_packets; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
  }
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(-1.0f, volume_);  // No audio volume set yet.

  // The vector is already filled with zero value samples on create.
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  EXPECT_FLOAT_EQ(0.74939233f, volume_);

  FillPacketWithTestWaveform();
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  EXPECT_FLOAT_EQ(0.89926866f, volume_);
  EXPECT_FLOAT_EQ(0.75071919f, noise_volume_);

  EXPECT_EQ(SpeechRecognizer::RECOGNIZER_NO_ERROR, error_);
  EXPECT_FALSE(recording_complete_);
  EXPECT_FALSE(recognition_complete_);
  recognizer_->CancelRecognition();
}

}  // namespace speech_input
