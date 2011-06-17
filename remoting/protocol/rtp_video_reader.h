// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_RTP_VIDEO_READER_H_
#define REMOTING_PROTOCOL_RTP_VIDEO_READER_H_

#include "base/time.h"
#include "remoting/protocol/rtcp_writer.h"
#include "remoting/protocol/rtp_reader.h"
#include "remoting/protocol/video_reader.h"

namespace remoting {
namespace protocol {

class Session;

class RtpVideoReader : public VideoReader {
 public:
  RtpVideoReader();
  virtual ~RtpVideoReader();

  // VideoReader interface.
  virtual void Init(protocol::Session* session, VideoStub* video_stub);

 private:
  friend class RtpVideoReaderTest;

  // Following struct is used to store pending packets in |packets_queue_|.
  // Each entry may be in three different states:
  //  |received| == false, |packet| == NULL - packet with the corresponding
  //    sequence number hasn't been received.
  //  |received| == true, |packet| != NULL - packet with the corresponding
  //    sequence number has been received, but hasn't been processed, still
  //    waiting for other fragments.
  //  |received| == true, |packet| == NULL - packet with the corresponding
  //    sequence number has been received and processed. Ignore any additional
  //    packet with the same sequence number.
  struct PacketsQueueEntry {
    PacketsQueueEntry();
    bool received;
    const RtpPacket* packet;
  };

  typedef std::deque<PacketsQueueEntry> PacketsQueue;

  void OnRtpPacket(const RtpPacket* rtp_packet);
  void CheckFullPacket(PacketsQueue::iterator pos);
  void RebuildVideoPacket(PacketsQueue::iterator from,
                          PacketsQueue::iterator to);
  void ResetQueue();

  // Helper method that sends RTCP receiver reports if enough time has
  // passed since the last report. It is called from
  // OnRtpPacket(). Interval between reports is defined by
  // |kReceiverReportsIntervalMs|.
  void SendReceiverReportIf();

  RtpReader rtp_reader_;
  RtcpWriter rtcp_writer_;

  PacketsQueue packets_queue_;
  uint32 last_sequence_number_;

  base::Time last_receiver_report_;

  // The stub that processes all received packets.
  VideoStub* video_stub_;

  DISALLOW_COPY_AND_ASSIGN(RtpVideoReader);
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_RTP_VIDEO_READER_H_
