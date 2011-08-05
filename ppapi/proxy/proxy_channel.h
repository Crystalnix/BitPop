// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PROXY_PROXY_CHANNEL_H_
#define PPAPI_PROXY_PROXY_CHANNEL_H_

#include "base/memory/scoped_ptr.h"
#include "base/process.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_platform_file.h"
#include "ipc/ipc_sync_channel.h"

namespace base {
class MessageLoopProxy;
class WaitableEvent;
}

namespace IPC {
class TestSink;
}

namespace pp {
namespace proxy {

class VarSerializationRules;

class ProxyChannel : public IPC::Channel::Listener,
                     public IPC::Message::Sender {
 public:
  typedef void (*ShutdownModuleFunc)();

  class Delegate {
   public:
    // Returns the dedicated message loop for processing IPC requests.
    virtual base::MessageLoopProxy* GetIPCMessageLoop() = 0;

    // Returns the event object that becomes signalled when the main thread's
    // message loop exits.
    virtual base::WaitableEvent* GetShutdownEvent() = 0;
  };

  virtual ~ProxyChannel();

  // Alternative to InitWithChannel() for unit tests that want to send all
  // messages sent via this channel to the given test sink. The test sink
  // must outlive this class.
  void InitWithTestSink(IPC::TestSink* test_sink);

  // Shares a file handle (HANDLE / file descriptor) with the remote side. It
  // returns a handle that should be sent in exactly one IPC message. Upon
  // receipt, the remote side then owns that handle. Note: if sending the
  // message fails, the returned handle is properly closed by the IPC system. If
  // should_close_source is set to true, the original handle is closed by this
  // operation and should not be used again.
  IPC::PlatformFileForTransit ShareHandleWithRemote(
      base::PlatformFile handle,
      bool should_close_source);

  // IPC::Message::Sender implementation.
  virtual bool Send(IPC::Message* msg);

  // IPC::Channel::Listener implementation.
  virtual void OnChannelError();

  // Will be NULL in some unit tests and if the remote side has crashed.
  IPC::SyncChannel* channel() const {
    return channel_.get();
  }

#if defined(OS_POSIX)
  int GetRendererFD();
#endif

 protected:
  ProxyChannel(base::ProcessHandle remote_process_handle);

  // You must call this function before anything else. Returns true on success.
  // The delegate pointer must outlive this class, ownership is not
  // transferred.
  virtual bool InitWithChannel(Delegate* delegate,
                               const IPC::ChannelHandle& channel_handle,
                               bool is_client);

  ProxyChannel::Delegate* delegate() const {
    return delegate_;
  }

 private:
  // Non-owning pointer. Guaranteed non-NULL after init is called.
  ProxyChannel::Delegate* delegate_;

  base::ProcessHandle remote_process_handle_;  // See getter above.

  // When we're unit testing, this will indicate the sink for the messages to
  // be deposited so they can be inspected by the test. When non-NULL, this
  // indicates that the channel should not be used.
  IPC::TestSink* test_sink_;

  // Will be null for some tests when there is a test_sink_, and if the
  // remote side has crashed.
  scoped_ptr<IPC::SyncChannel> channel_;

  DISALLOW_COPY_AND_ASSIGN(ProxyChannel);
};

}  // namespace proxy
}  // namespace pp

#endif  // PPAPI_PROXY_PROXY_CHANNEL_H_
