// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_MOCK_MEDIA_STREAM_DEPENDENCY_FACTORY_H_
#define CONTENT_RENDERER_MEDIA_MOCK_MEDIA_STREAM_DEPENDENCY_FACTORY_H_

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "content/renderer/media/media_stream_dependency_factory.h"

namespace webrtc {

class MockLocalVideoTrack : public LocalVideoTrackInterface {
 public:
  explicit MockLocalVideoTrack(std::string label)
    : enabled_(false),
      label_(label),
      renderer_(NULL) {
  }
  virtual cricket::VideoCapturer* GetVideoCapture() OVERRIDE;
  virtual void SetRenderer(VideoRendererWrapperInterface* renderer) OVERRIDE;
  virtual VideoRendererWrapperInterface* GetRenderer() OVERRIDE;
  virtual std::string kind() const OVERRIDE;
  virtual std::string label() const OVERRIDE;
  virtual bool enabled() const OVERRIDE;
  virtual TrackState state() const OVERRIDE;
  virtual bool set_enabled(bool enable) OVERRIDE;
  virtual bool set_state(TrackState new_state) OVERRIDE;
  virtual void RegisterObserver(ObserverInterface* observer) OVERRIDE;
  virtual void UnregisterObserver(ObserverInterface* observer) OVERRIDE;

  VideoRendererWrapperInterface* renderer() const { return renderer_; }

 protected:
  virtual ~MockLocalVideoTrack() {}

 private:
  bool enabled_;
  std::string label_;
  VideoRendererWrapperInterface* renderer_;
};

} //  namespace webrtc

// A mock factory for creating different objects for MediaStreamImpl.
class MockMediaStreamDependencyFactory : public MediaStreamDependencyFactory {
 public:
  MockMediaStreamDependencyFactory();
  virtual ~MockMediaStreamDependencyFactory();

  virtual bool CreatePeerConnectionFactory(
      talk_base::Thread* worker_thread,
      talk_base::Thread* signaling_thread,
      content::P2PSocketDispatcher* socket_dispatcher,
      talk_base::NetworkManager* network_manager,
      talk_base::PacketSocketFactory* socket_factory) OVERRIDE;
  virtual void ReleasePeerConnectionFactory() OVERRIDE;
  virtual bool PeerConnectionFactoryCreated() OVERRIDE;
  virtual talk_base::scoped_refptr<webrtc::PeerConnectionInterface>
      CreatePeerConnection(
          const std::string& config,
          webrtc::PeerConnectionObserver* observer) OVERRIDE;
  virtual talk_base::scoped_refptr<webrtc::LocalMediaStreamInterface>
      CreateLocalMediaStream(const std::string& label) OVERRIDE;
  virtual talk_base::scoped_refptr<webrtc::LocalVideoTrackInterface>
      CreateLocalVideoTrack(
          const std::string& label,
          cricket::VideoCapturer* video_device) OVERRIDE;
  virtual talk_base::scoped_refptr<webrtc::LocalAudioTrackInterface>
      CreateLocalAudioTrack(
          const std::string& label,
          webrtc::AudioDeviceModule* audio_device) OVERRIDE;

 private:
  bool mock_pc_factory_created_;

  DISALLOW_COPY_AND_ASSIGN(MockMediaStreamDependencyFactory);
};

#endif  // CONTENT_RENDERER_MEDIA_MOCK_MEDIA_STREAM_DEPENDENCY_FACTORY_H_
