// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/utf_string_conversions.h"
#include "content/renderer/media/media_stream_extra_data.h"
#include "content/renderer/media/mock_media_stream_dependency_factory.h"
#include "content/renderer/media/mock_web_peer_connection_00_handler_client.h"
#include "content/renderer/media/mock_peer_connection_impl.h"
#include "content/renderer/media/peer_connection_handler_jsep.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/libjingle/source/talk/app/webrtc/peerconnectioninterface.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebICECandidateDescriptor.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebICEOptions.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebMediaHints.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebMediaStreamDescriptor.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebMediaStreamSource.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebSessionDescriptionDescriptor.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebString.h"

class PeerConnectionHandlerJsepUnderTest : public PeerConnectionHandlerJsep {
 public:
  PeerConnectionHandlerJsepUnderTest(
      WebKit::WebPeerConnection00HandlerClient* client,
      MediaStreamDependencyFactory* dependency_factory)
      : PeerConnectionHandlerJsep(client, dependency_factory) {
  }

  webrtc::MockPeerConnectionImpl* native_peer_connection() {
    return static_cast<webrtc::MockPeerConnectionImpl*>(
        native_peer_connection_.get());
  }
};

class PeerConnectionHandlerJsepTest : public ::testing::Test {
 public:
  PeerConnectionHandlerJsepTest() : mock_peer_connection_(NULL) {
  }

  void SetUp() {
    mock_client_.reset(new WebKit::MockWebPeerConnection00HandlerClient());
    mock_dependency_factory_.reset(
        new MockMediaStreamDependencyFactory(NULL));
    mock_dependency_factory_->CreatePeerConnectionFactory(NULL,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          NULL);
    pc_handler_.reset(
        new PeerConnectionHandlerJsepUnderTest(mock_client_.get(),
                                               mock_dependency_factory_.get()));

    WebKit::WebString server_config(
        WebKit::WebString::fromUTF8("STUN stun.l.google.com:19302"));
    WebKit::WebString username;
    pc_handler_->initialize(server_config, username);

    mock_peer_connection_ = pc_handler_->native_peer_connection();
    ASSERT_TRUE(mock_peer_connection_);
  }

  // Creates a WebKit local MediaStream.
  WebKit::WebMediaStreamDescriptor CreateLocalMediaStream(
      const std::string& stream_label) {
    std::string video_track_label("video-label");
    std::string audio_track_label("audio-label");

    talk_base::scoped_refptr<webrtc::LocalMediaStreamInterface> native_stream(
        mock_dependency_factory_->CreateLocalMediaStream(stream_label));
    talk_base::scoped_refptr<webrtc::LocalAudioTrackInterface> audio_track(
        mock_dependency_factory_->CreateLocalAudioTrack(audio_track_label,
                                                        NULL));
    native_stream->AddTrack(audio_track);
    talk_base::scoped_refptr<webrtc::LocalVideoTrackInterface> video_track(
        mock_dependency_factory_->CreateLocalVideoTrack(video_track_label, 0));
    native_stream->AddTrack(video_track);

    WebKit::WebVector<WebKit::WebMediaStreamSource> audio_sources(
        static_cast<size_t>(1));
    audio_sources[0].initialize(WebKit::WebString::fromUTF8(video_track_label),
                                WebKit::WebMediaStreamSource::TypeAudio,
                                WebKit::WebString::fromUTF8("audio_track"));
    WebKit::WebVector<WebKit::WebMediaStreamSource> video_sources(
        static_cast<size_t>(1));
    video_sources[0].initialize(WebKit::WebString::fromUTF8(video_track_label),
                                WebKit::WebMediaStreamSource::TypeVideo,
                                WebKit::WebString::fromUTF8("video_track"));
    WebKit::WebMediaStreamDescriptor local_stream;
    local_stream.initialize(UTF8ToUTF16(stream_label), audio_sources,
                            video_sources);
    local_stream.setExtraData(new MediaStreamExtraData(native_stream));
    return local_stream;
  }

  // Creates a remote MediaStream and adds it to the mocked native
  // peer connection.
  talk_base::scoped_refptr<webrtc::MediaStreamInterface>
  AddRemoteMockMediaStream(const std::string& stream_label,
                           const std::string& video_track_label,
                           const std::string& audio_track_label) {
    // We use a local stream as a remote since for testing purposes we really
    // only need the MediaStreamInterface.
    talk_base::scoped_refptr<webrtc::LocalMediaStreamInterface> stream(
        mock_dependency_factory_->CreateLocalMediaStream(stream_label));
    if (!video_track_label.empty()) {
      talk_base::scoped_refptr<webrtc::LocalVideoTrackInterface> video_track(
          mock_dependency_factory_->CreateLocalVideoTrack(video_track_label,
                                                          0));
      stream->AddTrack(video_track);
    }
    if (!audio_track_label.empty()) {
      talk_base::scoped_refptr<webrtc::LocalAudioTrackInterface> audio_track(
          mock_dependency_factory_->CreateLocalAudioTrack(audio_track_label,
                                                          NULL));
      stream->AddTrack(audio_track);
    }
    mock_peer_connection_->AddRemoteStream(stream);
    return stream;
  }

  scoped_ptr<WebKit::MockWebPeerConnection00HandlerClient> mock_client_;
  scoped_ptr<MockMediaStreamDependencyFactory> mock_dependency_factory_;
  scoped_ptr<PeerConnectionHandlerJsepUnderTest> pc_handler_;

  // Weak reference to the mocked native peer connection implementation.
  webrtc::MockPeerConnectionImpl* mock_peer_connection_;
};

TEST_F(PeerConnectionHandlerJsepTest, Basic) {
  // Create offer.
  WebKit::WebMediaHints hints;
  hints.initialize(true, true);
  WebKit::WebSessionDescriptionDescriptor offer =
      pc_handler_->createOffer(hints);
  EXPECT_FALSE(offer.isNull());
  EXPECT_EQ(std::string(mock_peer_connection_->kDummyOffer),
            UTF16ToUTF8(offer.initialSDP()));
  EXPECT_EQ(hints.audio(), mock_peer_connection_->hint_audio());
  EXPECT_EQ(hints.video(), mock_peer_connection_->hint_video());

  // Create answer.
  WebKit::WebString offer_string = "offer";
  hints.reset();
  hints.initialize(false, false);
  WebKit::WebSessionDescriptionDescriptor answer =
      pc_handler_->createAnswer(offer_string, hints);
  EXPECT_FALSE(answer.isNull());
  EXPECT_EQ(UTF16ToUTF8(offer_string), UTF16ToUTF8(answer.initialSDP()));
  EXPECT_EQ(UTF16ToUTF8(offer_string),
            mock_peer_connection_->description_sdp());
  EXPECT_EQ(hints.audio(), mock_peer_connection_->hint_audio());
  EXPECT_EQ(hints.video(), mock_peer_connection_->hint_video());

  // Set local description.
  PeerConnectionHandlerJsep::Action action =
      PeerConnectionHandlerJsep::ActionSDPOffer;
  WebKit::WebSessionDescriptionDescriptor description;
  WebKit::WebString sdp = "test sdp";
  description.initialize(sdp);
  EXPECT_TRUE(pc_handler_->setLocalDescription(action, description));
  EXPECT_EQ(webrtc::PeerConnectionInterface::kOffer,
            mock_peer_connection_->action());
  EXPECT_EQ(UTF16ToUTF8(sdp), mock_peer_connection_->description_sdp());

  // Get local description.
  description.reset();
  description = pc_handler_->localDescription();
  EXPECT_FALSE(description.isNull());
  EXPECT_EQ(UTF16ToUTF8(sdp), UTF16ToUTF8(description.initialSDP()));

  // Set remote description.
  sdp = "test sdp 2";
  description.reset();
  description.initialize(sdp);

  // PrAnswer
  action = PeerConnectionHandlerJsep::ActionSDPPRanswer;
  EXPECT_TRUE(pc_handler_->setRemoteDescription(action, description));
  EXPECT_EQ(webrtc::PeerConnectionInterface::kPrAnswer,
            mock_peer_connection_->action());
  EXPECT_EQ(UTF16ToUTF8(sdp), mock_peer_connection_->description_sdp());
  // Get remote description.
  description.reset();
  description = pc_handler_->remoteDescription();
  EXPECT_FALSE(description.isNull());
  EXPECT_EQ(UTF16ToUTF8(sdp), UTF16ToUTF8(description.initialSDP()));

  // Answer
  action = PeerConnectionHandlerJsep::ActionSDPAnswer;
  EXPECT_TRUE(pc_handler_->setRemoteDescription(action, description));
  EXPECT_EQ(webrtc::PeerConnectionInterface::kAnswer,
            mock_peer_connection_->action());
  EXPECT_EQ(UTF16ToUTF8(sdp), mock_peer_connection_->description_sdp());

  // Get remote description.
  description.reset();
  description = pc_handler_->remoteDescription();
  EXPECT_FALSE(description.isNull());
  EXPECT_EQ(UTF16ToUTF8(sdp), UTF16ToUTF8(description.initialSDP()));

  // Start ICE.
  WebKit::WebICEOptions options;
  options.initialize(WebKit::WebICEOptions::CandidateTypeAll);
  EXPECT_TRUE(pc_handler_->startIce(options));
  EXPECT_EQ(webrtc::PeerConnectionInterface::kUseAll,
            mock_peer_connection_->ice_options());

  // Process ICE message.
  WebKit::WebICECandidateDescriptor candidate;
  WebKit::WebString label = "0";
  sdp = "test sdp";
  candidate.initialize(label, sdp);
  EXPECT_TRUE(pc_handler_->processIceMessage(candidate));
  EXPECT_EQ(0, mock_peer_connection_->sdp_mline_index());
  EXPECT_TRUE(mock_peer_connection_->sdp_mid().empty());
  EXPECT_EQ(UTF16ToUTF8(sdp), mock_peer_connection_->ice_sdp());

  // Add stream.
  std::string stream_label = "local_stream";
  WebKit::WebMediaStreamDescriptor local_stream(
      CreateLocalMediaStream(stream_label));

  pc_handler_->addStream(local_stream);
  EXPECT_EQ(stream_label, mock_peer_connection_->stream_label());

  // On add stream. ( Remote stream received)
  std::string remote_stream_label("remote_stream");
  talk_base::scoped_refptr<webrtc::MediaStreamInterface> remote_stream(
      AddRemoteMockMediaStream(remote_stream_label, "video", "audio"));
  pc_handler_->OnAddStream(remote_stream);
  EXPECT_EQ(remote_stream_label, mock_client_->stream_label());

  // Remove stream.
  WebKit::WebVector<WebKit::WebMediaStreamDescriptor> empty_streams(
      static_cast<size_t>(0));
  pc_handler_->removeStream(local_stream);
  EXPECT_EQ("", mock_peer_connection_->stream_label());

  // On remove stream.
  pc_handler_->OnRemoveStream(remote_stream);
  EXPECT_TRUE(mock_client_->stream_label().empty());

  // Add stream again.
  pc_handler_->addStream(local_stream);
  EXPECT_EQ(stream_label, mock_peer_connection_->stream_label());

  // On state change.
  mock_peer_connection_->SetReadyState(
      webrtc::PeerConnectionInterface::kActive);
  webrtc::PeerConnectionObserver::StateType state =
      webrtc::PeerConnectionObserver::kReadyState;
  pc_handler_->OnStateChange(state);
  EXPECT_EQ(WebKit::WebPeerConnection00HandlerClient::ReadyStateActive,
            mock_client_->ready_state());

  // On ICE candidate.
  std::string candidate_label = "0";
  std::string candidate_sdp = "test sdp";
  int sdp_mline_index = 0;
  scoped_ptr<webrtc::IceCandidateInterface> native_candidate(
      mock_dependency_factory_->CreateIceCandidate(candidate_label,
                                                   sdp_mline_index,
                                                   candidate_sdp));
  pc_handler_->OnIceCandidate(native_candidate.get());
  EXPECT_EQ(candidate_label, mock_client_->candidate_label());
  EXPECT_EQ(candidate_sdp, mock_client_->candidate_sdp());
  EXPECT_TRUE(mock_client_->more_to_follow());

  // On ICE complete.
  pc_handler_->OnIceComplete();
  EXPECT_TRUE(mock_client_->candidate_label().empty());
  EXPECT_TRUE(mock_client_->candidate_sdp().empty());
  EXPECT_FALSE(mock_client_->more_to_follow());

  // Stop.
  pc_handler_->stop();
  EXPECT_FALSE(pc_handler_->native_peer_connection());

  // PC handler is expected to be deleted when stop calls
  // MediaStreamImpl::ClosePeerConnection. We own and delete it here instead of
  // in the mock.
  pc_handler_.reset();
}

// Test that the glue code can receive multiple media streams and can set a
// video renderer on each media stream.
TEST_F(PeerConnectionHandlerJsepTest, ReceiveMultipleRemoteStreams) {
  std::string stream_label_1 = "remote_stream_1";
  std::string video_track_label_1 = "remote_video_track_1";
  std::string audio_track_label_1 = "remote_audio_track_1";
  talk_base::scoped_refptr<webrtc::MediaStreamInterface> stream_1(
      AddRemoteMockMediaStream(stream_label_1, video_track_label_1,
                               audio_track_label_1));
  std::string stream_label_2 = "remote_stream_2";
  std::string video_track_label_2 = "remote_video_track_2";
  std::string audio_track_label_2 = "remote_audio_track_2";
  talk_base::scoped_refptr<webrtc::MediaStreamInterface> stream_2(
      AddRemoteMockMediaStream(stream_label_2, video_track_label_2,
                               audio_track_label_2));

  pc_handler_->OnAddStream(stream_1);
  EXPECT_EQ(stream_label_1, mock_client_->stream_label());

  pc_handler_->OnAddStream(stream_2);
  EXPECT_EQ(stream_label_2, mock_client_->stream_label());
}
