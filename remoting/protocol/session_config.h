// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_SESSION_CONFIG_H_
#define REMOTING_PROTOCOL_SESSION_CONFIG_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"

namespace remoting {
namespace protocol {

extern const int kDefaultStreamVersion;

// Struct for configuration parameters of a single channel.
// Some channels (like video) may have multiple underlying sockets that need
// to be configured simultaneously.
struct ChannelConfig {
  enum TransportType {
    TRANSPORT_STREAM,
    TRANSPORT_DATAGRAM,
    TRANSPORT_SRTP,
    TRANSPORT_RTP_DTLS,
  };

  enum Codec {
    CODEC_UNDEFINED,  // Used for event and control channels.
    CODEC_VERBATIM,
    CODEC_ZIP,
    CODEC_VP8,
  };

  ChannelConfig();
  ChannelConfig(TransportType transport, int version, Codec codec);

  // operator== is overloaded so that std::find() works with
  // std::vector<ChannelConfig>.
  bool operator==(const ChannelConfig& b) const;

  void Reset();

  TransportType transport;
  int version;
  Codec codec;
};

// SessionConfig is used by the chromoting Session to store negotiated
// chromotocol configuration.
class SessionConfig {
 public:
  void set_control_config(const ChannelConfig& control_config) {
    control_config_ = control_config;
  }
  const ChannelConfig& control_config() const { return control_config_; }
  void set_event_config(const ChannelConfig& event_config) {
    event_config_ = event_config;
  }
  const ChannelConfig& event_config() const { return event_config_; }
  void set_video_config(const ChannelConfig& video_config) {
    video_config_ = video_config;
  }
  const ChannelConfig& video_config() const { return video_config_; }

  static SessionConfig GetDefault();

 private:
  ChannelConfig control_config_;
  ChannelConfig event_config_;
  ChannelConfig video_config_;
};

// Defines session description that is sent from client to the host in the
// session-initiate message. It is different from the regular Config
// because it allows one to specify multiple configurations for each channel.
class CandidateSessionConfig {
 public:
  ~CandidateSessionConfig();

  const std::vector<ChannelConfig>& control_configs() const {
    return control_configs_;
  }

  std::vector<ChannelConfig>* mutable_control_configs() {
    return &control_configs_;
  }

  const std::vector<ChannelConfig>& event_configs() const {
    return event_configs_;
  }

  std::vector<ChannelConfig>* mutable_event_configs() {
    return &event_configs_;
  }

  const std::vector<ChannelConfig>& video_configs() const {
    return video_configs_;
  }

  std::vector<ChannelConfig>* mutable_video_configs() {
    return &video_configs_;
  }

  // Selects session configuration that is supported by both participants.
  // NULL is returned if such configuration doesn't exist. When selecting
  // channel configuration priority is given to the configs listed first
  // in |client_config|.
  bool Select(const CandidateSessionConfig* client_config,
              SessionConfig* result);

  // Returns true if |config| is supported.
  bool IsSupported(const SessionConfig& config) const;

  // Extracts final protocol configuration. Must be used for the description
  // received in the session-accept stanza. If the selection is ambiguous
  // (e.g. there is more than one configuration for one of the channel)
  // or undefined (e.g. no configurations for a channel) then NULL is returned.
  bool GetFinalConfig(SessionConfig* result) const;

  scoped_ptr<CandidateSessionConfig> Clone() const;

  static scoped_ptr<CandidateSessionConfig> CreateEmpty();
  static scoped_ptr<CandidateSessionConfig> CreateFrom(
      const SessionConfig& config);
  static scoped_ptr<CandidateSessionConfig> CreateDefault();

 private:
  CandidateSessionConfig();
  explicit CandidateSessionConfig(const CandidateSessionConfig& config);
  CandidateSessionConfig& operator=(const CandidateSessionConfig& b);

  static bool SelectCommonChannelConfig(
      const std::vector<ChannelConfig>& host_configs_,
      const std::vector<ChannelConfig>& client_configs_,
      ChannelConfig* config);
  static bool IsChannelConfigSupported(const std::vector<ChannelConfig>& vector,
                                       const ChannelConfig& value);

  std::vector<ChannelConfig> control_configs_;
  std::vector<ChannelConfig> event_configs_;
  std::vector<ChannelConfig> video_configs_;
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_SESSION_CONFIG_H_
