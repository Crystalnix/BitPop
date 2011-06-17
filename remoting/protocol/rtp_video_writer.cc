// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/rtp_video_writer.h"

#include "base/task.h"
#include "net/base/io_buffer.h"
#include "remoting/base/compound_buffer.h"
#include "remoting/proto/video.pb.h"
#include "remoting/protocol/rtp_writer.h"
#include "remoting/protocol/session.h"

namespace remoting {
namespace protocol {

namespace {
const int kMtu = 1200;
}  // namespace

RtpVideoWriter::RtpVideoWriter() { }

RtpVideoWriter::~RtpVideoWriter() { }

void RtpVideoWriter::Init(protocol::Session* session) {
  rtp_writer_.Init(session->video_rtp_channel());
}

void RtpVideoWriter::ProcessVideoPacket(const VideoPacket* packet, Task* done) {
  CHECK(packet->format().encoding() == VideoPacketFormat::ENCODING_VP8)
      << "Only VP8 is supported in RTP.";

  CompoundBuffer payload;
  // TODO(sergeyu): This copy would not be necessary CompoundBuffer was used
  // inside of VideoPacket.
  payload.AppendCopyOf(packet->data().data(), packet->data().size());

  Vp8Descriptor vp8_desriptor;
  // TODO(sergeyu): Add a flag in VideoPacket that indicates whether this is a
  // key frame or not.
  vp8_desriptor.non_reference_frame = false;
  vp8_desriptor.picture_id = kuint32max;

  int position = 0;
  while (position < payload.total_bytes()) {
    int size = std::min(kMtu, payload.total_bytes() - position);

    // Frame beginning flag is set only for the first packet in the first
    // partition.
    vp8_desriptor.frame_beginning =
        (packet->flags() & VideoPacket::FIRST_PACKET) != 0 && position == 0;

    // Marker bit is set only for the last packet in the last partition.
    bool marker = (position + size) == payload.total_bytes() &&
        (packet->flags() & VideoPacket::LAST_PACKET) != 0;

    // Set fragmentation flag appropriately.
    if (position == 0) {
      if (size == payload.total_bytes()) {
        vp8_desriptor.fragmentation_info = Vp8Descriptor::NOT_FRAGMENTED;
      } else {
        vp8_desriptor.fragmentation_info = Vp8Descriptor::FIRST_FRAGMENT;
      }
    } else {
      if (position + size == payload.total_bytes()) {
        vp8_desriptor.fragmentation_info = Vp8Descriptor::LAST_FRAGMENT;
      } else {
        vp8_desriptor.fragmentation_info = Vp8Descriptor::MIDDLE_FRAGMENT;
      }
    }

    // Create CompoundBuffer for the chunk.
    CompoundBuffer chunk;
    chunk.CopyFrom(payload, position, position + size);

    // And send it.
    rtp_writer_.SendPacket(packet->timestamp(), marker, vp8_desriptor, chunk);

    position += size;
  }
  DCHECK_EQ(position, payload.total_bytes());

  done->Run();
  delete done;
}

int RtpVideoWriter::GetPendingPackets() {
  return rtp_writer_.GetPendingPackets();
}

}  // namespace protocol
}  // namespace remoting
