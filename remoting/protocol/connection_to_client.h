// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_CONNECTION_TO_CLIENT_H_
#define REMOTING_PROTOCOL_CONNECTION_TO_CLIENT_H_

#include <deque>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "remoting/protocol/session.h"
#include "remoting/protocol/video_writer.h"

namespace remoting {
namespace protocol {

class ClientStub;
class HostStub;
class InputStub;
class HostMessageDispatcher;

// This class represents a remote viewer connected to the chromoting host
// through a libjingle connection. A viewer object is responsible for sending
// screen updates and other messages to the remote viewer. It is also
// responsible for receiving and parsing data from the remote viewer and
// delegating events to the event handler.
class ConnectionToClient :
    public base::RefCountedThreadSafe<ConnectionToClient> {
 public:
  class EventHandler {
   public:
    virtual ~EventHandler() {}

    // Called when the network connection is opened.
    virtual void OnConnectionOpened(ConnectionToClient* connection) = 0;

    // Called when the network connection is closed.
    virtual void OnConnectionClosed(ConnectionToClient* connection) = 0;

    // Called when the network connection has failed.
    virtual void OnConnectionFailed(ConnectionToClient* connection) = 0;
  };

  // Constructs a ConnectionToClient object. |message_loop| is the message loop
  // that this object runs on. A viewer object receives events and messages from
  // a libjingle channel, these events are delegated to |handler|.
  // It is guaranteed that |handler| is called only on the |message_loop|.
  ConnectionToClient(MessageLoop* message_loop,
                     EventHandler* handler);

  virtual void Init(Session* session);

  // Returns the connection in use.
  virtual Session* session();

  // Disconnect the client connection. This method is allowed to be called
  // more than once and calls after the first one will be ignored.
  //
  // After this method is called all the send method calls will be ignored.
  virtual void Disconnect();

  // Send encoded update stream data to the viewer.
  virtual VideoStub* video_stub();

  // Return pointer to ClientStub.
  virtual ClientStub* client_stub();

  // These two setters should be called before Init().
  virtual void set_host_stub(HostStub* host_stub);
  virtual void set_input_stub(InputStub* input_stub);

 protected:
  friend class base::RefCountedThreadSafe<ConnectionToClient>;
  virtual ~ConnectionToClient();

 private:
  // Callback for protocol Session.
  void OnSessionStateChange(Session::State state);

  // Process a libjingle state change event on the |loop_|.
  void StateChangeTask(Session::State state);

  void OnClosed();

  // The libjingle channel used to send and receive data from the remote client.
  scoped_refptr<Session> session_;

  scoped_ptr<VideoWriter> video_writer_;

  // ClientStub for sending messages to the client.
  scoped_ptr<ClientStub> client_stub_;

  // The message loop that this object runs on.
  MessageLoop* loop_;

  // Event handler for handling events sent from this object.
  EventHandler* handler_;

  // HostStub for receiving control events from the client.
  HostStub* host_stub_;

  // InputStub for receiving input events from the client.
  InputStub* input_stub_;

  // Dispatcher for submitting messages to stubs.
  scoped_ptr<HostMessageDispatcher> dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(ConnectionToClient);
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_CONNECTION_TO_CLIENT_H_
