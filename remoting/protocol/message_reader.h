// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_MESSAGE_READER_H_
#define REMOTING_PROTOCOL_MESSAGE_READER_H_

#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop_proxy.h"
#include "net/base/completion_callback.h"
#include "remoting/base/compound_buffer.h"
#include "remoting/protocol/message_decoder.h"

class MessageLoop;

namespace net {
class IOBuffer;
class Socket;
}  // namespace net

namespace remoting {
namespace protocol {

// MessageReader reads data from the socket asynchronously and calls
// callback for each message it receives. It stops calling the
// callback as soon as the socket is closed, so the socket should
// always be closed before the callback handler is destroyed.
//
// In order to throttle the stream, MessageReader doesn't try to read
// new data from the socket until all previously received messages are
// processed by the receiver (|done_task| is called for each message).
// It is still possible that the MessageReceivedCallback is called
// twice (so that there is more than one outstanding message),
// e.g. when we the sender sends multiple messages in one TCP packet.
class MessageReader : public base::RefCountedThreadSafe<MessageReader> {
 public:
  // The callback is given ownership of the second argument
  // (|done_task|).  The buffer (first argument) is owned by
  // MessageReader and is freed when the task specified by the second
  // argument is called.
  typedef base::Callback<void(CompoundBuffer*, const base::Closure&)>
      MessageReceivedCallback;

  MessageReader();
  virtual ~MessageReader();

  // Initialize the MessageReader with a socket. If a message is received
  // |callback| is called.
  void Init(net::Socket* socket, const MessageReceivedCallback& callback);

 private:
  void DoRead();
  void OnRead(int result);
  void HandleReadResult(int result);
  void OnDataReceived(net::IOBuffer* data, int data_size);
  void OnMessageDone(CompoundBuffer* message,
                     scoped_refptr<base::MessageLoopProxy> message_loop);
  void ProcessDoneEvent();

  net::Socket* socket_;

  // Set to true, when we have a socket read pending, and expecting
  // OnRead() to be called when new data is received.
  bool read_pending_;

  // Number of messages that we received, but haven't finished
  // processing yet, i.e. |done_task| hasn't been called for these
  // messages.
  int pending_messages_;

  bool closed_;
  scoped_refptr<net::IOBuffer> read_buffer_;

  MessageDecoder message_decoder_;

  // Callback is called when a message is received.
  MessageReceivedCallback message_received_callback_;
};

// Version of MessageReader for protocol buffer messages, that parses
// each incoming message.
template <class T>
class ProtobufMessageReader {
 public:
  typedef typename base::Callback<void(T*, const base::Closure&)>
      MessageReceivedCallback;

  ProtobufMessageReader() { };
  ~ProtobufMessageReader() { };

  void Init(net::Socket* socket, const MessageReceivedCallback& callback) {
    DCHECK(!callback.is_null());
    message_received_callback_ = callback;
    message_reader_ = new MessageReader();
    message_reader_->Init(
        socket, base::Bind(&ProtobufMessageReader<T>::OnNewData,
                           base::Unretained(this)));
  }

 private:
  void OnNewData(CompoundBuffer* buffer, const base::Closure& done_task) {
    T* message = new T();
    CompoundBufferInputStream stream(buffer);
    bool ret = message->ParseFromZeroCopyStream(&stream);
    if (!ret) {
      LOG(WARNING) << "Received message that is not a valid protocol buffer.";
      delete message;
    } else {
      DCHECK_EQ(stream.position(), buffer->total_bytes());
      message_received_callback_.Run(message, base::Bind(
          &ProtobufMessageReader<T>::OnDone, message, done_task));
    }
  }

  static void OnDone(T* message, const base::Closure& done_task) {
    delete message;
    done_task.Run();
  }

  scoped_refptr<MessageReader> message_reader_;
  MessageReceivedCallback message_received_callback_;
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_MESSAGE_READER_H_
