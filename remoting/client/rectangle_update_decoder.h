// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_RECTANGLE_UPDATE_DECODER_H
#define REMOTING_CLIENT_RECTANGLE_UPDATE_DECODER_H

#include "base/memory/scoped_ptr.h"
#include "base/task.h"
#include "media/base/video_frame.h"
#include "remoting/base/decoder.h"
#include "ui/gfx/size.h"

class MessageLoop;

namespace remoting {

class FrameConsumer;
class VideoPacketFormat;
class VideoPacket;

namespace protocol {
class SessionConfig;
}  // namespace protocol

// TODO(ajwong): Re-examine this API, especially with regards to how error
// conditions on each step are reported.  Should they be CHECKs? Logs? Other?
// TODO(sergeyu): Rename this class.
class RectangleUpdateDecoder :
    public base::RefCountedThreadSafe<RectangleUpdateDecoder> {
 public:
  RectangleUpdateDecoder(MessageLoop* message_loop,
                         FrameConsumer* consumer);

  // Initializes decoder with the infromation from the protocol config.
  void Initialize(const protocol::SessionConfig* config);

  // Decodes the contents of |packet| calling OnPartialFrameOutput() in the
  // regsitered as data is avaialable. DecodePacket may keep a reference to
  // |packet| so the |packet| must remain alive and valid until |done| is
  // executed.
  //
  // TODO(ajwong): Should packet be a const pointer to make the lifetime
  // more clear?
  void DecodePacket(const VideoPacket* packet, Task* done);

 private:
   friend class base::RefCountedThreadSafe<RectangleUpdateDecoder>;
  ~RectangleUpdateDecoder();

  void AllocateFrame(const VideoPacket* packet, Task* done);
  void ProcessPacketData(const VideoPacket* packet, Task* done);

  // Pointers to infrastructure objects.  Not owned.
  MessageLoop* message_loop_;
  FrameConsumer* consumer_;

  gfx::Size initial_screen_size_;

  scoped_ptr<Decoder> decoder_;

  // The video frame that the decoder writes to.
  scoped_refptr<media::VideoFrame> frame_;
  bool frame_is_new_;
};

}  // namespace remoting

DISABLE_RUNNABLE_METHOD_REFCOUNT(remoting::RectangleUpdateDecoder);

#endif  // REMOTING_CLIENT_RECTANGLE_UPDATE_DECODER_H
