// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Interface of a host that receives commands from a Chromoting client.
//
// This interface handles control messages defined in control.proto.

#ifndef REMOTING_PROTOCOL_HOST_STUB_H_
#define REMOTING_PROTOCOL_HOST_STUB_H_

#include "base/basictypes.h"

class Task;

namespace remoting {
namespace protocol {

class LocalLoginCredentials;
class SuggestResolutionRequest;

class HostStub {
 public:
  HostStub() {};
  virtual ~HostStub() {};

  virtual void SuggestResolution(
      const SuggestResolutionRequest* msg, Task* done) = 0;
  virtual void BeginSessionRequest(
      const LocalLoginCredentials* credentials, Task* done) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(HostStub);
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_HOST_STUB_H_
