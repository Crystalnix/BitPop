// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_SPDY_IO_BUFFER_H_
#define NET_SPDY_SPDY_IO_BUFFER_H_

#include "base/memory/ref_counted.h"
#include "net/base/io_buffer.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"

namespace net {

class SpdyStream;

// A class for managing SPDY IO buffers.  These buffers need to be prioritized
// so that the SpdySession sends them in the right order.  Further, they need
// to track the SpdyStream which they are associated with so that incremental
// completion of the IO can notify the appropriate stream of completion.
class NET_EXPORT_PRIVATE SpdyIOBuffer {
 public:
  // Constructor
  // |buffer| is the actual data buffer.
  // |size| is the size of the data buffer.
  // |priority| is the priority of this buffer.
  // |stream| is a pointer to the stream which is managing this buffer.
  SpdyIOBuffer(IOBuffer* buffer, int size, RequestPriority priority,
               SpdyStream* stream);
  // Declare this instead of using the default so that we avoid needing to
  // include spdy_stream.h.
  SpdyIOBuffer(const SpdyIOBuffer& rhs);
  SpdyIOBuffer();
  ~SpdyIOBuffer();
  // Declare this instead of using the default so that we avoid needing to
  // include spdy_stream.h.
  SpdyIOBuffer& operator=(const SpdyIOBuffer& rhs);

  // Accessors.
  DrainableIOBuffer* buffer() const { return buffer_; }
  size_t size() const { return buffer_->size(); }
  void release();
  RequestPriority priority() const { return priority_; }
  const scoped_refptr<SpdyStream>& stream() const { return stream_; }

  // Comparison operator to support sorting.
  bool operator<(const SpdyIOBuffer& other) const {
    if (priority_ != other.priority_)
      return priority_ < other.priority_;
    return position_ > other.position_;
  }

 private:
  scoped_refptr<DrainableIOBuffer> buffer_;
  RequestPriority priority_;
  uint64 position_;
  scoped_refptr<SpdyStream> stream_;
  static uint64 order_;  // Maintains a FIFO order for equal priorities.
};

}  // namespace net

#endif  // NET_SPDY_SPDY_IO_BUFFER_H_
