// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_DESKTOP_ENVIRONMENT_H_
#define REMOTING_HOST_DESKTOP_ENVIRONMENT_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "remoting/host/event_executor.h"

namespace remoting {

class AudioCapturer;
class ChromotingHostContext;
class VideoFrameCapturer;

namespace protocol {
class ClipboardStub;
class HostEventStub;
}

class DesktopEnvironment {
 public:
  // Creates a DesktopEnvironment used in a host plugin.
  static scoped_ptr<DesktopEnvironment> Create(
      ChromotingHostContext* context);

  // Creates a DesktopEnvironment used in a service process.
  static scoped_ptr<DesktopEnvironment> CreateForService(
      ChromotingHostContext* context);

  static scoped_ptr<DesktopEnvironment> CreateFake(
      ChromotingHostContext* context,
      scoped_ptr<VideoFrameCapturer> capturer,
      scoped_ptr<EventExecutor> event_executor,
      scoped_ptr<AudioCapturer> audio_capturer);

  virtual ~DesktopEnvironment();

  VideoFrameCapturer* capturer() const { return capturer_.get(); }
  EventExecutor* event_executor() const { return event_executor_.get(); }
  AudioCapturer* audio_capturer() const { return audio_capturer_.get(); }
  void OnSessionStarted(scoped_ptr<protocol::ClipboardStub> client_clipboard);
  void OnSessionFinished();

 private:
  DesktopEnvironment(ChromotingHostContext* context,
                     scoped_ptr<VideoFrameCapturer> capturer,
                     scoped_ptr<EventExecutor> event_executor,
                     scoped_ptr<AudioCapturer> audio_capturer);

  // Host context used to make sure operations are run on the correct thread.
  // This is owned by the ChromotingHost.
  ChromotingHostContext* context_;

  // Used to capture video to deliver to clients.
  scoped_ptr<VideoFrameCapturer> capturer_;

  // Used to capture audio to deliver to clients.
  scoped_ptr<AudioCapturer> audio_capturer_;

  // Executes input and clipboard events received from the client.
  scoped_ptr<EventExecutor> event_executor_;

  DISALLOW_COPY_AND_ASSIGN(DesktopEnvironment);
};

}  // namespace remoting

#endif  // REMOTING_HOST_DESKTOP_ENVIRONMENT_H_
