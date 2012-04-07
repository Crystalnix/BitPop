// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_BASE_DECODER_ROW_BASED_H_
#define REMOTING_BASE_DECODER_ROW_BASED_H_

#include "remoting/base/decoder.h"

namespace remoting {

class Decompressor;

class DecoderRowBased : public Decoder {
 public:
  virtual ~DecoderRowBased();

  static DecoderRowBased* CreateZlibDecoder();
  static DecoderRowBased* CreateVerbatimDecoder();

  // Decoder implementation.
  virtual bool IsReadyForData() OVERRIDE;
  virtual void Initialize(scoped_refptr<media::VideoFrame> frame) OVERRIDE;
  virtual DecodeResult DecodePacket(const VideoPacket* packet) OVERRIDE;
  virtual void GetUpdatedRegion(SkRegion* region) OVERRIDE;
  virtual void Reset() OVERRIDE;
  virtual VideoPacketFormat::Encoding Encoding() OVERRIDE;

 private:
  enum State {
    kUninitialized,
    kReady,
    kProcessing,
    kPartitionDone,
    kDone,
    kError,
  };

  DecoderRowBased(Decompressor* decompressor,
                  VideoPacketFormat::Encoding encoding);

  // Helper method. Called from DecodePacket to updated state of the decoder.
  void UpdateStateForPacket(const VideoPacket* packet);

  // The internal state of the decoder.
  State state_;

  // Keeps track of the updating rect.
  SkIRect clip_;

  // The video frame to write to.
  scoped_refptr<media::VideoFrame> frame_;

  // The compression for the input byte stream.
  scoped_ptr<Decompressor> decompressor_;

  // The encoding of the incoming stream.
  VideoPacketFormat::Encoding encoding_;

  // The position in the row that we are updating.
  int row_pos_;

  // The current row in the rect that we are updaing.
  int row_y_;

  SkRegion updated_region_;

  DISALLOW_COPY_AND_ASSIGN(DecoderRowBased);
};

}  // namespace remoting

#endif  // REMOTING_BASE_DECODER_ROW_BASED_H_
