// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/base/encoder_row_based.h"

#include "base/logging.h"
#include "remoting/base/capture_data.h"
#include "remoting/base/compressor_verbatim.h"
#include "remoting/base/compressor_zlib.h"
#include "remoting/base/util.h"
#include "remoting/proto/video.pb.h"

namespace remoting {

static const int kPacketSize = 1024 * 1024;

EncoderRowBased* EncoderRowBased::CreateZlibEncoder() {
  return new EncoderRowBased(new CompressorZlib(),
                             VideoPacketFormat::ENCODING_ZLIB);
}

EncoderRowBased* EncoderRowBased::CreateZlibEncoder(int packet_size) {
  return new EncoderRowBased(new CompressorZlib(),
                             VideoPacketFormat::ENCODING_ZLIB,
                             packet_size);
}

EncoderRowBased* EncoderRowBased::CreateVerbatimEncoder() {
  return new EncoderRowBased(new CompressorVerbatim(),
                             VideoPacketFormat::ENCODING_VERBATIM);
}

EncoderRowBased* EncoderRowBased::CreateVerbatimEncoder(int packet_size) {
  return new EncoderRowBased(new CompressorVerbatim(),
                             VideoPacketFormat::ENCODING_VERBATIM,
                             packet_size);
}

EncoderRowBased::EncoderRowBased(Compressor* compressor,
                                 VideoPacketFormat::Encoding encoding)
    : encoding_(encoding),
      compressor_(compressor),
      screen_size_(SkISize::Make(0,0)),
      packet_size_(kPacketSize) {
}

EncoderRowBased::EncoderRowBased(Compressor* compressor,
                                 VideoPacketFormat::Encoding encoding,
                                 int packet_size)
    : encoding_(encoding),
      compressor_(compressor),
      screen_size_(SkISize::Make(0,0)),
      packet_size_(packet_size) {
}

EncoderRowBased::~EncoderRowBased() {}

void EncoderRowBased::Encode(
    scoped_refptr<CaptureData> capture_data,
    bool key_frame,
    const DataAvailableCallback& data_available_callback) {
  CHECK(capture_data->pixel_format() == media::VideoFrame::RGB32)
      << "RowBased Encoder only works with RGB32. Got "
      << capture_data->pixel_format();
  capture_data_ = capture_data;
  callback_ = data_available_callback;

  const SkRegion& region = capture_data->dirty_region();
  SkRegion::Iterator iter(region);
  while (!iter.done()) {
    SkIRect rect = iter.rect();
    iter.next();
    EncodeRect(rect, iter.done());
  }

  capture_data_ = NULL;
  callback_.Reset();
}

void EncoderRowBased::EncodeRect(const SkIRect& rect, bool last) {
  CHECK(capture_data_->data_planes().data[0]);
  CHECK_EQ(capture_data_->pixel_format(), media::VideoFrame::RGB32);
  const int strides = capture_data_->data_planes().strides[0];
  const int bytes_per_pixel = 4;
  const int row_size = bytes_per_pixel * rect.width();

  compressor_->Reset();

  scoped_ptr<VideoPacket> packet(new VideoPacket());
  PrepareUpdateStart(rect, packet.get());
  const uint8* in = capture_data_->data_planes().data[0] +
      rect.fTop * strides + rect.fLeft * bytes_per_pixel;
  // TODO(hclam): Fill in the sequence number.
  uint8* out = GetOutputBuffer(packet.get(), packet_size_);
  int filled = 0;
  int row_pos = 0;  // Position in the current row in bytes.
  int row_y = 0;  // Current row.
  bool compress_again = true;
  while (compress_again) {
    // Prepare a message for sending out.
    if (!packet.get()) {
      packet.reset(new VideoPacket());
      out = GetOutputBuffer(packet.get(), packet_size_);
      filled = 0;
    }

    Compressor::CompressorFlush flush = Compressor::CompressorNoFlush;
    if (row_y == rect.height() - 1) {
      flush = Compressor::CompressorFinish;
    }

    int consumed = 0;
    int written = 0;
    compress_again = compressor_->Process(in + row_pos, row_size - row_pos,
                                          out + filled, packet_size_ - filled,
                                          flush, &consumed, &written);
    row_pos += consumed;
    filled += written;

    // We have reached the end of stream.
    if (!compress_again) {
      packet->set_flags(packet->flags() | VideoPacket::LAST_PACKET);
      packet->set_capture_time_ms(capture_data_->capture_time_ms());
      packet->set_client_sequence_number(
          capture_data_->client_sequence_number());
      SkIPoint dpi(capture_data_->dpi());
      if (dpi.x())
        packet->mutable_format()->set_x_dpi(dpi.x());
      if (dpi.y())
        packet->mutable_format()->set_y_dpi(dpi.y());
      if (last)
        packet->set_flags(packet->flags() | VideoPacket::LAST_PARTITION);
      DCHECK(row_pos == row_size);
      DCHECK(row_y == rect.height() - 1);
    }

    // If we have filled the message or we have reached the end of stream.
    if (filled == packet_size_ || !compress_again) {
      packet->mutable_data()->resize(filled);
      callback_.Run(packet.Pass());
    }

    // Reached the end of input row and we're not at the last row.
    if (row_pos == row_size && row_y < rect.height() - 1) {
      row_pos = 0;
      in += strides;
      ++row_y;
    }
  }
}

void EncoderRowBased::PrepareUpdateStart(const SkIRect& rect,
                                         VideoPacket* packet) {
  packet->set_flags(packet->flags() | VideoPacket::FIRST_PACKET);

  VideoPacketFormat* format = packet->mutable_format();
  format->set_x(rect.fLeft);
  format->set_y(rect.fTop);
  format->set_width(rect.width());
  format->set_height(rect.height());
  format->set_encoding(encoding_);
  if (capture_data_->size() != screen_size_) {
    screen_size_ = capture_data_->size();
    format->set_screen_width(screen_size_.width());
    format->set_screen_height(screen_size_.height());
  }
}

uint8* EncoderRowBased::GetOutputBuffer(VideoPacket* packet, size_t size) {
  packet->mutable_data()->resize(size);
  // TODO(ajwong): Is there a better way to do this at all???
  return const_cast<uint8*>(reinterpret_cast<const uint8*>(
      packet->mutable_data()->data()));
}

}  // namespace remoting
