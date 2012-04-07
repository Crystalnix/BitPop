// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_MEDIA_STREAM_IMPL_H_
#define CONTENT_RENDERER_MEDIA_MEDIA_STREAM_IMPL_H_

#include <list>
#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop_proxy.h"
#include "base/threading/non_thread_safe.h"
#include "base/threading/thread.h"
#include "content/common/content_export.h"
#include "content/renderer/media/media_stream_dispatcher_eventhandler.h"
#include "content/renderer/media/rtc_video_decoder.h"
#include "third_party/libjingle/source/talk/app/webrtc/mediastream.h"
#include "third_party/libjingle/source/talk/base/scoped_ref_ptr.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebUserMediaClient.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebUserMediaRequest.h"
#include "webkit/media/media_stream_client.h"

namespace base {
class WaitableEvent;
}

namespace content {
class IpcNetworkManager;
class IpcPacketSocketFactory;
class P2PSocketDispatcher;
}

namespace talk_base {
class Thread;
}

namespace WebKit {
class WebPeerConnectionHandler;
class WebPeerConnectionHandlerClient;
}

class MediaStreamDispatcher;
class MediaStreamDependencyFactory;
class PeerConnectionHandler;
class VideoCaptureImplManager;
class RTCVideoDecoder;

// MediaStreamImpl is a delegate for the Media Stream API messages used by
// WebKit. It ties together WebKit, native PeerConnection in libjingle and
// MediaStreamManager (via MediaStreamDispatcher and MediaStreamDispatcherHost)
// in the browser process. It must be created, called and destroyed on the
// render thread.
class CONTENT_EXPORT MediaStreamImpl
    : NON_EXPORTED_BASE(public WebKit::WebUserMediaClient),
      NON_EXPORTED_BASE(public webkit_media::MediaStreamClient),
      public MediaStreamDispatcherEventHandler,
      NON_EXPORTED_BASE(public base::NonThreadSafe),
      public base::RefCountedThreadSafe<MediaStreamImpl> {
 public:
  MediaStreamImpl(
      MediaStreamDispatcher* media_stream_dispatcher,
      content::P2PSocketDispatcher* p2p_socket_dispatcher,
      VideoCaptureImplManager* vc_manager,
      MediaStreamDependencyFactory* dependency_factory);
  virtual ~MediaStreamImpl();

  virtual WebKit::WebPeerConnectionHandler* CreatePeerConnectionHandler(
      WebKit::WebPeerConnectionHandlerClient* client);
  virtual void ClosePeerConnection();
  virtual webrtc::MediaStreamTrackInterface* GetLocalMediaStreamTrack(
      const std::string& label);

  // WebKit::WebUserMediaClient implementation
  virtual void requestUserMedia(
      const WebKit::WebUserMediaRequest& user_media_request,
      const WebKit::WebVector<WebKit::WebMediaStreamSource>&
          media_stream_source_vector) OVERRIDE;
  virtual void cancelUserMediaRequest(
      const WebKit::WebUserMediaRequest& user_media_request) OVERRIDE;

  // webkit_media::MediaStreamClient implementation.
  virtual scoped_refptr<media::VideoDecoder> GetVideoDecoder(
      const GURL& url,
      media::MessageLoopFactory* message_loop_factory) OVERRIDE;

  // MediaStreamDispatcherEventHandler implementation.
  virtual void OnStreamGenerated(
      int request_id,
      const std::string& label,
      const media_stream::StreamDeviceInfoArray& audio_array,
      const media_stream::StreamDeviceInfoArray& video_array) OVERRIDE;
  virtual void OnStreamGenerationFailed(int request_id) OVERRIDE;
  virtual void OnVideoDeviceFailed(
      const std::string& label,
      int index) OVERRIDE;
  virtual void OnAudioDeviceFailed(
      const std::string& label,
      int index) OVERRIDE;
  virtual void OnDevicesEnumerated(
      int request_id,
      const media_stream::StreamDeviceInfoArray& device_array) OVERRIDE;
  virtual void OnDevicesEnumerationFailed(int request_id) OVERRIDE;
  virtual void OnDeviceOpened(
      int request_id,
      const std::string& label,
      const media_stream::StreamDeviceInfo& device_info) OVERRIDE;
  virtual void OnDeviceOpenFailed(int request_id) OVERRIDE;

 private:
  FRIEND_TEST_ALL_PREFIXES(MediaStreamImplTest, Basic);

  class VideoRendererWrapper : public webrtc::VideoRendererWrapperInterface {
   public:
    VideoRendererWrapper();
    virtual cricket::VideoRenderer* renderer() OVERRIDE {
      return rtc_video_decoder_.get();
    }
    void SetVideoDecoder(RTCVideoDecoder* decoder);

   protected:
    virtual ~VideoRendererWrapper();

   private:
    scoped_refptr<RTCVideoDecoder> rtc_video_decoder_;
  };

  void InitializeWorkerThread(
      talk_base::Thread** thread,
      base::WaitableEvent* event);
  void DeleteIpcNetworkManager();
  bool EnsurePeerConnectionFactory();

  scoped_ptr<MediaStreamDependencyFactory> dependency_factory_;

  // media_stream_dispatcher_ is a weak reference, owned by RenderView. It's
  // valid for the lifetime of RenderView.
  MediaStreamDispatcher* media_stream_dispatcher_;

  // p2p_socket_dispatcher_ is a weak reference, owned by RenderView. It's valid
  // for the lifetime of RenderView.
  content::P2PSocketDispatcher* p2p_socket_dispatcher_;

  // We own network_manager_, must be deleted on the worker thread.
  content::IpcNetworkManager* network_manager_;

  scoped_ptr<content::IpcPacketSocketFactory> socket_factory_;

  scoped_refptr<VideoCaptureImplManager> vc_manager_;

  // peer_connection_handler_ is a weak reference, owned by WebKit. It's valid
  // until stop is called on it (which will call ClosePeerConnection on us).
  // TODO(grunell): Support several PeerConnectionsHandlers.
  PeerConnectionHandler* peer_connection_handler_;

  // We keep a list of the generated local tracks, so that we can add capture
  // devices when generated and also use them for recording.
  typedef talk_base::scoped_refptr<webrtc::MediaStreamTrackInterface>
      MediaStreamTrackPtr;
  typedef std::map<std::string, MediaStreamTrackPtr> MediaStreamTrackPtrMap;
  MediaStreamTrackPtrMap local_tracks_;

  talk_base::scoped_refptr<VideoRendererWrapper> video_renderer_;
  scoped_refptr<base::MessageLoopProxy> message_loop_proxy_;

  // PeerConnection threads. signaling_thread_ is created from the
  // "current" chrome thread.
  talk_base::Thread* signaling_thread_;
  talk_base::Thread* worker_thread_;
  base::Thread chrome_worker_thread_;

  static int next_request_id_;
  typedef std::map<int, WebKit::WebUserMediaRequest> MediaRequestMap;
  MediaRequestMap user_media_requests_;

  DISALLOW_COPY_AND_ASSIGN(MediaStreamImpl);
};

#endif  // CONTENT_RENDERER_MEDIA_MEDIA_STREAM_IMPL_H_
