// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/mock_media_stream_dependency_factory.h"
#include "content/renderer/media/mock_peer_connection_impl.h"

#include <vector>

#include "base/logging.h"

namespace webrtc {

class MockStreamCollection : public StreamCollectionInterface {
 public:
  virtual size_t count() OVERRIDE {
    return streams_.size();
  }
  virtual MediaStreamInterface* at(size_t index) OVERRIDE {
    return streams_[index];
  }
  virtual MediaStreamInterface* find(const std::string& label) OVERRIDE {
    for (size_t i = 0; i < streams_.size(); ++i) {
      if (streams_[i]->label() == label)
        return streams_[i];
    }
    return NULL;
  }
  void AddStream(MediaStreamInterface* stream) {
    streams_.push_back(stream);
  }

 protected:
  virtual ~MockStreamCollection() {}

 private:
  std::vector<talk_base::scoped_refptr<MediaStreamInterface> > streams_;
};

const char MockPeerConnectionImpl::kDummyOffer[] = "dummy offer";

MockPeerConnectionImpl::MockPeerConnectionImpl(
    MockMediaStreamDependencyFactory* factory)
    : dependency_factory_(factory),
      local_streams_(new talk_base::RefCountedObject<MockStreamCollection>),
      remote_streams_(new talk_base::RefCountedObject<MockStreamCollection>),
      hint_audio_(false),
      hint_video_(false),
      action_(kAnswer),
      ice_options_(kOnlyRelay),
      ready_state_(kNew) {
}

MockPeerConnectionImpl::~MockPeerConnectionImpl() {}

talk_base::scoped_refptr<StreamCollectionInterface>
MockPeerConnectionImpl::local_streams() {
  return local_streams_;
}

talk_base::scoped_refptr<StreamCollectionInterface>
MockPeerConnectionImpl::remote_streams() {
  return remote_streams_;
}

void MockPeerConnectionImpl::AddStream(LocalMediaStreamInterface* stream) {
  DCHECK(stream_label_.empty());
  stream_label_ = stream->label();
  local_streams_->AddStream(stream);
}

bool MockPeerConnectionImpl::AddStream(
    MediaStreamInterface* local_stream,
    const MediaConstraintsInterface* constraints) {
  DCHECK(stream_label_.empty());
  stream_label_ = local_stream->label();
  local_streams_->AddStream(local_stream);
  return true;
}

void MockPeerConnectionImpl::RemoveStream(
    MediaStreamInterface* local_stream) {
  DCHECK_EQ(stream_label_, local_stream->label());
  stream_label_.clear();
}

MockPeerConnectionImpl::ReadyState MockPeerConnectionImpl::ready_state() {
  return ready_state_;
}

bool MockPeerConnectionImpl::StartIce(IceOptions options) {
  ice_options_ = options;
  return true;
}

webrtc::SessionDescriptionInterface* MockPeerConnectionImpl::CreateOffer(
    const webrtc::MediaHints& hints) {
  hint_audio_ = hints.has_audio();
  hint_video_ = hints.has_video();
  return dependency_factory_->CreateSessionDescription(kDummyOffer);
}

webrtc::SessionDescriptionInterface* MockPeerConnectionImpl::CreateAnswer(
    const webrtc::MediaHints& hints,
    const webrtc::SessionDescriptionInterface* offer) {
  hint_audio_ = hints.has_audio();
  hint_video_ = hints.has_video();
  offer->ToString(&description_sdp_);
  return dependency_factory_->CreateSessionDescription(description_sdp_);
}

bool MockPeerConnectionImpl::SetLocalDescription(
    Action action,
    webrtc::SessionDescriptionInterface* desc) {
  action_ = action;
  local_desc_.reset(desc);
  return desc->ToString(&description_sdp_);
}

bool MockPeerConnectionImpl::SetRemoteDescription(
    Action action,
    webrtc::SessionDescriptionInterface* desc) {
  action_ = action;
  remote_desc_.reset(desc);
  return desc->ToString(&description_sdp_);
}

bool MockPeerConnectionImpl::ProcessIceMessage(
    const webrtc::IceCandidateInterface* ice_candidate) {
  sdp_mid_ = ice_candidate->sdp_mid();
  sdp_mline_index_ = ice_candidate->sdp_mline_index();
  return ice_candidate->ToString(&ice_sdp_);
}

const webrtc::SessionDescriptionInterface*
MockPeerConnectionImpl::local_description() const {
  return local_desc_.get();
}

const webrtc::SessionDescriptionInterface*
MockPeerConnectionImpl::remote_description() const {
  return remote_desc_.get();
}

void MockPeerConnectionImpl::AddRemoteStream(MediaStreamInterface* stream) {
  remote_streams_->AddStream(stream);
}

void MockPeerConnectionImpl::CreateOffer(
    CreateSessionDescriptionObserver* observer,
    const MediaConstraintsInterface* constraints) {
  NOTIMPLEMENTED();
}

void MockPeerConnectionImpl::CreateAnswer(
    CreateSessionDescriptionObserver* observer,
    const MediaConstraintsInterface* constraints) {
  NOTIMPLEMENTED();
}

void MockPeerConnectionImpl::SetLocalDescription(
    SetSessionDescriptionObserver* observer,
    SessionDescriptionInterface* desc) {
  NOTIMPLEMENTED();
}

void MockPeerConnectionImpl::SetRemoteDescription(
    SetSessionDescriptionObserver* observer,
    SessionDescriptionInterface* desc) {
  NOTIMPLEMENTED();
}

bool MockPeerConnectionImpl::UpdateIce(
    const IceServers& configuration,
    const MediaConstraintsInterface* constraints) {
  NOTIMPLEMENTED();
  return false;
}

bool MockPeerConnectionImpl::AddIceCandidate(
    const IceCandidateInterface* candidate) {
  NOTIMPLEMENTED();
  return false;
}

PeerConnectionInterface::IceState MockPeerConnectionImpl::ice_state() {
  NOTIMPLEMENTED();
  return kIceNew;
}

}  // namespace webrtc
