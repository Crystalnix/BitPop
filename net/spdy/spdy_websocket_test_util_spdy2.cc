// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_websocket_test_util_spdy2.h"

#include "net/spdy/buffered_spdy_framer.h"
#include "net/spdy/spdy_http_utils.h"
#include "net/spdy/spdy_test_util_spdy2.h"

static const int kDefaultAssociatedStreamId = 0;
static const bool kDefaultCompressed = false;
static const char* const kDefaultDataPointer = NULL;
static const uint32 kDefaultDataLength = 0;
static const char** const kDefaultExtraHeaders = NULL;
static const int kDefaultExtraHeaderCount = 0;

namespace net {

namespace test_spdy2 {

SpdyFrame* ConstructSpdyWebSocketHandshakeRequestFrame(
    const char* const headers[],
    int header_count,
    SpdyStreamId stream_id,
    RequestPriority request_priority) {

  // SPDY SYN_STREAM control frame header.
  const SpdyHeaderInfo kSynStreamHeader = {
    SYN_STREAM,
    stream_id,
    kDefaultAssociatedStreamId,
    ConvertRequestPriorityToSpdyPriority(request_priority, 2),
    CONTROL_FLAG_NONE,
    kDefaultCompressed,
    INVALID,
    kDefaultDataPointer,
    kDefaultDataLength,
    DATA_FLAG_NONE
  };

  // Construct SPDY SYN_STREAM control frame.
  return ConstructSpdyPacket(
      kSynStreamHeader,
      kDefaultExtraHeaders,
      kDefaultExtraHeaderCount,
      headers,
      header_count);
}

SpdyFrame* ConstructSpdyWebSocketHandshakeResponseFrame(
    const char* const headers[],
    int header_count,
    SpdyStreamId stream_id,
    RequestPriority request_priority) {

  // SPDY SYN_REPLY control frame header.
  const SpdyHeaderInfo kSynReplyHeader = {
    SYN_REPLY,
    stream_id,
    kDefaultAssociatedStreamId,
    ConvertRequestPriorityToSpdyPriority(request_priority, 2),
    CONTROL_FLAG_NONE,
    kDefaultCompressed,
    INVALID,
    kDefaultDataPointer,
    kDefaultDataLength,
    DATA_FLAG_NONE
  };

  // Construct SPDY SYN_REPLY control frame.
  return ConstructSpdyPacket(
      kSynReplyHeader,
      kDefaultExtraHeaders,
      kDefaultExtraHeaderCount,
      headers,
      header_count);
}

SpdyFrame* ConstructSpdyWebSocketDataFrame(
    const char* data,
    int len,
    SpdyStreamId stream_id,
    bool fin) {

  // Construct SPDY data frame.
  BufferedSpdyFramer framer(2);
  return framer.CreateDataFrame(
      stream_id,
      data,
      len,
      fin ? DATA_FLAG_FIN : DATA_FLAG_NONE);
}

}  // namespace test_spdy2

}  // namespace net
