// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_websocket_stream.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "net/base/completion_callback.h"
#include "net/proxy/proxy_server.h"
#include "net/spdy/spdy_http_utils.h"
#include "net/spdy/spdy_protocol.h"
#include "net/spdy/spdy_session.h"
#include "net/spdy/spdy_test_util.h"
#include "net/spdy/spdy_websocket_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

struct SpdyWebSocketStreamEvent {
  enum EventType {
    EVENT_CREATED,
    EVENT_SENT_HEADERS,
    EVENT_RECEIVED_HEADER,
    EVENT_SENT_DATA,
    EVENT_RECEIVED_DATA,
    EVENT_CLOSE,
  };
  SpdyWebSocketStreamEvent(EventType type,
                           const spdy::SpdyHeaderBlock& headers,
                           int result,
                           const std::string& data)
      : event_type(type),
        headers(headers),
        result(result),
        data(data) {}

  EventType event_type;
  spdy::SpdyHeaderBlock headers;
  int result;
  std::string data;
};

}  // namespace

namespace net {

class SpdyWebSocketStreamEventRecorder : public SpdyWebSocketStream::Delegate {
 public:
  explicit SpdyWebSocketStreamEventRecorder(const CompletionCallback& callback)
      : callback_(callback) {}
  virtual ~SpdyWebSocketStreamEventRecorder() {}

  typedef base::Callback<void(SpdyWebSocketStreamEvent*)> StreamEventCallback;

  void SetOnCreated(const StreamEventCallback& callback) {
    on_created_ = callback;
  }
  void SetOnSentHeaders(const StreamEventCallback& callback) {
    on_sent_headers_ = callback;
  }
  void SetOnReceivedHeader(
      const StreamEventCallback& callback) {
    on_received_header_ = callback;
  }
  void SetOnSentData(const StreamEventCallback& callback) {
    on_sent_data_ = callback;
  }
  void SetOnReceivedData(
      const StreamEventCallback& callback) {
    on_received_data_ = callback;
  }
  void SetOnClose(const StreamEventCallback& callback) {
    on_close_ = callback;
  }

  virtual void OnCreatedSpdyStream(int result) {
    events_.push_back(
        SpdyWebSocketStreamEvent(SpdyWebSocketStreamEvent::EVENT_CREATED,
                                 spdy::SpdyHeaderBlock(),
                                 result,
                                 std::string()));
    if (!on_created_.is_null())
      on_created_.Run(&events_.back());
  }
  virtual void OnSentSpdyHeaders(int result) {
    events_.push_back(
        SpdyWebSocketStreamEvent(SpdyWebSocketStreamEvent::EVENT_SENT_HEADERS,
                                 spdy::SpdyHeaderBlock(),
                                 result,
                                 std::string()));
    if (!on_sent_data_.is_null())
      on_sent_data_.Run(&events_.back());
  }
  virtual int OnReceivedSpdyResponseHeader(
      const spdy::SpdyHeaderBlock& headers, int status) {
    events_.push_back(
        SpdyWebSocketStreamEvent(
            SpdyWebSocketStreamEvent::EVENT_RECEIVED_HEADER,
            headers,
            status,
            std::string()));
    if (!on_received_header_.is_null())
      on_received_header_.Run(&events_.back());
    return status;
  }
  virtual void OnSentSpdyData(int amount_sent) {
    events_.push_back(
        SpdyWebSocketStreamEvent(
            SpdyWebSocketStreamEvent::EVENT_SENT_DATA,
            spdy::SpdyHeaderBlock(),
            amount_sent,
            std::string()));
    if (!on_sent_data_.is_null())
      on_sent_data_.Run(&events_.back());
  }
  virtual void OnReceivedSpdyData(const char* data, int length) {
    events_.push_back(
        SpdyWebSocketStreamEvent(
            SpdyWebSocketStreamEvent::EVENT_RECEIVED_DATA,
            spdy::SpdyHeaderBlock(),
            length,
            std::string(data, length)));
    if (!on_received_data_.is_null())
      on_received_data_.Run(&events_.back());
  }
  virtual void OnCloseSpdyStream() {
    events_.push_back(
        SpdyWebSocketStreamEvent(
            SpdyWebSocketStreamEvent::EVENT_CLOSE,
            spdy::SpdyHeaderBlock(),
            OK,
            std::string()));
    if (!on_close_.is_null())
      on_close_.Run(&events_.back());
    if (!callback_.is_null())
      callback_.Run(OK);
  }

  const std::vector<SpdyWebSocketStreamEvent>& GetSeenEvents() const {
    return events_;
  }

 private:
  std::vector<SpdyWebSocketStreamEvent> events_;
  StreamEventCallback on_created_;
  StreamEventCallback on_sent_headers_;
  StreamEventCallback on_received_header_;
  StreamEventCallback on_sent_data_;
  StreamEventCallback on_received_data_;
  StreamEventCallback on_close_;
  CompletionCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(SpdyWebSocketStreamEventRecorder);
};

class SpdyWebSocketStreamTest : public testing::Test {
 public:
  OrderedSocketData* data() { return data_.get(); }

  void DoSendHelloFrame(SpdyWebSocketStreamEvent* event) {
    websocket_stream_->SendData(kMessageFrame, kMessageFrameLength);
  }

  void DoSendClosingFrame(SpdyWebSocketStreamEvent* event) {
    websocket_stream_->SendData(kClosingFrame, kClosingFrameLength);
  }

  void DoClose(SpdyWebSocketStreamEvent* event) {
    websocket_stream_->Close();
  }

  void DoSync(SpdyWebSocketStreamEvent* event) {
    sync_callback_.SetResult(OK);
  }

 protected:
  SpdyWebSocketStreamTest() {}
  virtual ~SpdyWebSocketStreamTest() {}

  virtual void SetUp() {
    EnableCompression(false);
    SpdySession::SetSSLMode(false);

    host_port_pair_.set_host("example.com");
    host_port_pair_.set_port(80);
    host_port_proxy_pair_.first = host_port_pair_;
    host_port_proxy_pair_.second = ProxyServer::Direct();

    const size_t max_concurrent_streams = 1;
    spdy::SettingsFlagsAndId id(0);
    id.set_id(spdy::SETTINGS_MAX_CONCURRENT_STREAMS);

    id.set_flags(spdy::SETTINGS_FLAG_PLEASE_PERSIST);
    spdy_settings_to_set_.push_back(
        spdy::SpdySetting(id, max_concurrent_streams));

    id.set_flags(spdy::SETTINGS_FLAG_PERSISTED);
    spdy_settings_to_send_.push_back(
        spdy::SpdySetting(id, max_concurrent_streams));
  }
  virtual void TearDown() {
    MessageLoop::current()->RunAllPending();
  }

  void EnableCompression(bool enabled) {
    spdy::SpdyFramer::set_enable_compression_default(enabled);
  }
  void Prepare(spdy::SpdyStreamId stream_id) {
    stream_id_ = stream_id;

    const char* const request_headers[] = {
      "url", "ws://example.com/echo",
      "origin", "http://example.com/wsdemo",
    };

    int request_header_count = arraysize(request_headers) / 2;

    const char* const response_headers[] = {
      "sec-websocket-location", "ws://example.com/echo",
      "sec-websocket-origin", "http://example.com/wsdemo",
    };

    int response_header_count = arraysize(response_headers) / 2;

    request_frame_.reset(ConstructSpdyWebSocketHandshakeRequestFrame(
        request_headers,
        request_header_count,
        stream_id_,
        HIGHEST));
    response_frame_.reset(ConstructSpdyWebSocketHandshakeResponseFrame(
        response_headers,
        response_header_count,
        stream_id_,
        HIGHEST));

    message_frame_.reset(ConstructSpdyWebSocketDataFrame(
        kMessageFrame,
        kMessageFrameLength,
        stream_id_,
        false));

    closing_frame_.reset(ConstructSpdyWebSocketDataFrame(
        kClosingFrame,
        kClosingFrameLength,
        stream_id_,
        false));
  }
  int InitSession(MockRead* reads, size_t reads_count,
                  MockWrite* writes, size_t writes_count,
                  bool throttling) {
    data_.reset(new OrderedSocketData(reads, reads_count,
                                      writes, writes_count));
    session_deps_.socket_factory->AddSocketDataProvider(data_.get());
    http_session_ = SpdySessionDependencies::SpdyCreateSession(&session_deps_);
    SpdySessionPool* spdy_session_pool(http_session_->spdy_session_pool());

    if (throttling) {
      // Set max concurrent streams to 1.
      spdy_session_pool->http_server_properties()->SetSpdySettings(
          host_port_pair_, spdy_settings_to_set_);
    }

    EXPECT_FALSE(spdy_session_pool->HasSession(host_port_proxy_pair_));
    session_ = spdy_session_pool->Get(host_port_proxy_pair_, BoundNetLog());
    EXPECT_TRUE(spdy_session_pool->HasSession(host_port_proxy_pair_));
    transport_params_ = new TransportSocketParams(host_port_pair_, MEDIUM,
                                                  false, false);
    TestCompletionCallback callback;
    scoped_ptr<ClientSocketHandle> connection(new ClientSocketHandle);
    EXPECT_EQ(ERR_IO_PENDING,
              connection->Init(host_port_pair_.ToString(), transport_params_,
                               MEDIUM, callback.callback(),
                               http_session_->GetTransportSocketPool(),
                               BoundNetLog()));
    EXPECT_EQ(OK, callback.WaitForResult());
    return session_->InitializeWithSocket(connection.release(), false, OK);
  }
  void SendRequest() {
    linked_ptr<spdy::SpdyHeaderBlock> headers(new spdy::SpdyHeaderBlock);
    (*headers)["url"] = "ws://example.com/echo";
    (*headers)["origin"] = "http://example.com/wsdemo";

    websocket_stream_->SendRequest(headers);
  }

  spdy::SpdySettings spdy_settings_to_set_;
  spdy::SpdySettings spdy_settings_to_send_;
  SpdySessionDependencies session_deps_;
  scoped_ptr<OrderedSocketData> data_;
  scoped_refptr<HttpNetworkSession> http_session_;
  scoped_refptr<SpdySession> session_;
  scoped_refptr<TransportSocketParams> transport_params_;
  scoped_ptr<SpdyWebSocketStream> websocket_stream_;
  spdy::SpdyStreamId stream_id_;
  scoped_ptr<spdy::SpdyFrame> request_frame_;
  scoped_ptr<spdy::SpdyFrame> response_frame_;
  scoped_ptr<spdy::SpdyFrame> message_frame_;
  scoped_ptr<spdy::SpdyFrame> closing_frame_;
  HostPortPair host_port_pair_;
  HostPortProxyPair host_port_proxy_pair_;
  TestCompletionCallback completion_callback_;
  TestCompletionCallback sync_callback_;

  static const char kMessageFrame[];
  static const char kClosingFrame[];
  static const size_t kMessageFrameLength;
  static const size_t kClosingFrameLength;
};

const char SpdyWebSocketStreamTest::kMessageFrame[] = "\0hello\xff";
const char SpdyWebSocketStreamTest::kClosingFrame[] = "\xff\0";
const size_t SpdyWebSocketStreamTest::kMessageFrameLength =
    arraysize(SpdyWebSocketStreamTest::kMessageFrame) - 1;
const size_t SpdyWebSocketStreamTest::kClosingFrameLength =
    arraysize(SpdyWebSocketStreamTest::kClosingFrame) - 1;

TEST_F(SpdyWebSocketStreamTest, Basic) {
  Prepare(1);
  MockWrite writes[] = {
    CreateMockWrite(*request_frame_.get(), 1),
    CreateMockWrite(*message_frame_.get(), 3),
    CreateMockWrite(*closing_frame_.get(), 5)
  };

  MockRead reads[] = {
    CreateMockRead(*response_frame_.get(), 2),
    CreateMockRead(*message_frame_.get(), 4),
    // Skip sequence 6 to notify closing has been sent.
    CreateMockRead(*closing_frame_.get(), 7),
    MockRead(false, 0, 8)  // EOF cause OnCloseSpdyStream event.
  };

  EXPECT_EQ(OK, InitSession(reads, arraysize(reads),
                            writes, arraysize(writes), false));

  SpdyWebSocketStreamEventRecorder delegate(completion_callback_.callback());
  delegate.SetOnReceivedHeader(
      base::Bind(&SpdyWebSocketStreamTest::DoSendHelloFrame,
                 base::Unretained(this)));
  delegate.SetOnReceivedData(
      base::Bind(&SpdyWebSocketStreamTest::DoSendClosingFrame,
                 base::Unretained(this)));

  websocket_stream_.reset(new SpdyWebSocketStream(session_, &delegate));

  BoundNetLog net_log;
  GURL url("ws://example.com/echo");
  ASSERT_EQ(OK, websocket_stream_->InitializeStream(url, HIGHEST, net_log));

  ASSERT_TRUE(websocket_stream_->stream_);
  EXPECT_EQ(stream_id_, websocket_stream_->stream_->stream_id());

  SendRequest();

  completion_callback_.WaitForResult();

  websocket_stream_.reset();

  const std::vector<SpdyWebSocketStreamEvent>& events =
      delegate.GetSeenEvents();
  ASSERT_EQ(7U, events.size());

  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_HEADERS,
            events[0].event_type);
  EXPECT_LT(0, events[0].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_HEADER,
            events[1].event_type);
  EXPECT_EQ(OK, events[1].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_DATA,
            events[2].event_type);
  EXPECT_EQ(static_cast<int>(kMessageFrameLength), events[2].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_DATA,
            events[3].event_type);
  EXPECT_EQ(static_cast<int>(kMessageFrameLength), events[3].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_DATA,
            events[4].event_type);
  EXPECT_EQ(static_cast<int>(kClosingFrameLength), events[4].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_DATA,
            events[5].event_type);
  EXPECT_EQ(static_cast<int>(kClosingFrameLength), events[5].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_CLOSE,
            events[6].event_type);
  EXPECT_EQ(OK, events[6].result);

  // EOF close SPDY session.
  EXPECT_TRUE(!http_session_->spdy_session_pool()->HasSession(
      host_port_proxy_pair_));
  EXPECT_TRUE(data()->at_read_eof());
  EXPECT_TRUE(data()->at_write_eof());
}

TEST_F(SpdyWebSocketStreamTest, DestructionBeforeClose) {
  Prepare(1);
  MockWrite writes[] = {
    CreateMockWrite(*request_frame_.get(), 1),
    CreateMockWrite(*message_frame_.get(), 3)
  };

  MockRead reads[] = {
    CreateMockRead(*response_frame_.get(), 2),
    CreateMockRead(*message_frame_.get(), 4),
    MockRead(true, ERR_IO_PENDING, 5)
  };

  EXPECT_EQ(OK, InitSession(reads, arraysize(reads),
                            writes, arraysize(writes), false));

  SpdyWebSocketStreamEventRecorder delegate(completion_callback_.callback());
  delegate.SetOnReceivedHeader(
      base::Bind(&SpdyWebSocketStreamTest::DoSendHelloFrame,
                 base::Unretained(this)));
  delegate.SetOnReceivedData(
      base::Bind(&SpdyWebSocketStreamTest::DoSync, base::Unretained(this)));

  websocket_stream_.reset(new SpdyWebSocketStream(session_, &delegate));

  BoundNetLog net_log;
  GURL url("ws://example.com/echo");
  ASSERT_EQ(OK, websocket_stream_->InitializeStream(url, HIGHEST, net_log));

  SendRequest();

  sync_callback_.WaitForResult();

  // WebSocketStream destruction remove its SPDY stream from the session.
  EXPECT_TRUE(session_->IsStreamActive(stream_id_));
  websocket_stream_.reset();
  EXPECT_FALSE(session_->IsStreamActive(stream_id_));

  const std::vector<SpdyWebSocketStreamEvent>& events =
      delegate.GetSeenEvents();
  ASSERT_GE(4U, events.size());

  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_HEADERS,
            events[0].event_type);
  EXPECT_LT(0, events[0].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_HEADER,
            events[1].event_type);
  EXPECT_EQ(OK, events[1].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_DATA,
            events[2].event_type);
  EXPECT_EQ(static_cast<int>(kMessageFrameLength), events[2].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_DATA,
            events[3].event_type);
  EXPECT_EQ(static_cast<int>(kMessageFrameLength), events[3].result);

  EXPECT_TRUE(http_session_->spdy_session_pool()->HasSession(
      host_port_proxy_pair_));
  EXPECT_TRUE(data()->at_read_eof());
  EXPECT_TRUE(data()->at_write_eof());
}

TEST_F(SpdyWebSocketStreamTest, DestructionAfterExplicitClose) {
  Prepare(1);
  MockWrite writes[] = {
    CreateMockWrite(*request_frame_.get(), 1),
    CreateMockWrite(*message_frame_.get(), 3),
    CreateMockWrite(*closing_frame_.get(), 5)
  };

  MockRead reads[] = {
    CreateMockRead(*response_frame_.get(), 2),
    CreateMockRead(*message_frame_.get(), 4),
    MockRead(true, ERR_IO_PENDING, 6)
  };

  EXPECT_EQ(OK, InitSession(reads, arraysize(reads),
                            writes, arraysize(writes), false));

  SpdyWebSocketStreamEventRecorder delegate(completion_callback_.callback());
  delegate.SetOnReceivedHeader(
      base::Bind(&SpdyWebSocketStreamTest::DoSendHelloFrame,
                 base::Unretained(this)));
  delegate.SetOnReceivedData(
      base::Bind(&SpdyWebSocketStreamTest::DoClose, base::Unretained(this)));

  websocket_stream_.reset(new SpdyWebSocketStream(session_, &delegate));

  BoundNetLog net_log;
  GURL url("ws://example.com/echo");
  ASSERT_EQ(OK, websocket_stream_->InitializeStream(url, HIGHEST, net_log));

  SendRequest();

  completion_callback_.WaitForResult();

  // SPDY stream has already been removed from the session by Close().
  EXPECT_FALSE(session_->IsStreamActive(stream_id_));
  websocket_stream_.reset();

  const std::vector<SpdyWebSocketStreamEvent>& events =
      delegate.GetSeenEvents();
  ASSERT_EQ(5U, events.size());

  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_HEADERS,
            events[0].event_type);
  EXPECT_LT(0, events[0].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_HEADER,
            events[1].event_type);
  EXPECT_EQ(OK, events[1].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_DATA,
            events[2].event_type);
  EXPECT_EQ(static_cast<int>(kMessageFrameLength), events[2].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_DATA,
            events[3].event_type);
  EXPECT_EQ(static_cast<int>(kMessageFrameLength), events[3].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_CLOSE, events[4].event_type);

  EXPECT_TRUE(http_session_->spdy_session_pool()->HasSession(
      host_port_proxy_pair_));
}

TEST_F(SpdyWebSocketStreamTest, IOPending) {
  Prepare(3);
  scoped_ptr<spdy::SpdyFrame> settings_frame(
      ConstructSpdySettings(spdy_settings_to_send_));
  MockWrite writes[] = {
    // Setting throttling make SpdySession send settings frame automatically.
    CreateMockWrite(*settings_frame.get(), 1),
    CreateMockWrite(*request_frame_.get(), 3),
    CreateMockWrite(*message_frame_.get(), 6),
    CreateMockWrite(*closing_frame_.get(), 9)
  };

  MockRead reads[] = {
    CreateMockRead(*settings_frame.get(), 2),
    CreateMockRead(*response_frame_.get(), 4),
    // Skip sequence 5 (I/O Pending)
    CreateMockRead(*message_frame_.get(), 7),
    // Skip sequence 8 (I/O Pending)
    CreateMockRead(*closing_frame_.get(), 10),
    MockRead(false, 0, 11)  // EOF cause OnCloseSpdyStream event.
  };

  EXPECT_EQ(OK, InitSession(reads, arraysize(reads),
                            writes, arraysize(writes), true));

  // Create a dummy WebSocketStream which cause ERR_IO_PENDING to another
  // WebSocketStream under test.
  SpdyWebSocketStreamEventRecorder block_delegate((CompletionCallback()));

  scoped_ptr<SpdyWebSocketStream> block_stream(
      new SpdyWebSocketStream(session_, &block_delegate));
  BoundNetLog block_net_log;
  GURL block_url("ws://example.com/block");
  ASSERT_EQ(OK,
            block_stream->InitializeStream(block_url, HIGHEST, block_net_log));

  // Create a WebSocketStream under test.
  SpdyWebSocketStreamEventRecorder delegate(completion_callback_.callback());
  delegate.SetOnCreated(
      base::Bind(&SpdyWebSocketStreamTest::DoSync, base::Unretained(this)));
  delegate.SetOnReceivedHeader(
      base::Bind(&SpdyWebSocketStreamTest::DoSendHelloFrame,
                 base::Unretained(this)));
  delegate.SetOnReceivedData(
      base::Bind(&SpdyWebSocketStreamTest::DoSendClosingFrame,
                 base::Unretained(this)));

  websocket_stream_.reset(new SpdyWebSocketStream(session_, &delegate));
  BoundNetLog net_log;
  GURL url("ws://example.com/echo");
  ASSERT_EQ(ERR_IO_PENDING, websocket_stream_->InitializeStream(
      url, HIGHEST, net_log));

  // Delete the fist stream to allow create the second stream.
  block_stream.reset();
  ASSERT_EQ(OK, sync_callback_.WaitForResult());

  SendRequest();

  completion_callback_.WaitForResult();

  websocket_stream_.reset();

  const std::vector<SpdyWebSocketStreamEvent>& block_events =
      block_delegate.GetSeenEvents();
  ASSERT_EQ(0U, block_events.size());

  const std::vector<SpdyWebSocketStreamEvent>& events =
      delegate.GetSeenEvents();
  ASSERT_EQ(8U, events.size());
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_CREATED,
            events[0].event_type);
  EXPECT_EQ(0, events[0].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_HEADERS,
            events[1].event_type);
  EXPECT_LT(0, events[1].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_HEADER,
            events[2].event_type);
  EXPECT_EQ(OK, events[2].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_DATA,
            events[3].event_type);
  EXPECT_EQ(static_cast<int>(kMessageFrameLength), events[3].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_DATA,
            events[4].event_type);
  EXPECT_EQ(static_cast<int>(kMessageFrameLength), events[4].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_SENT_DATA,
            events[5].event_type);
  EXPECT_EQ(static_cast<int>(kClosingFrameLength), events[5].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_RECEIVED_DATA,
            events[6].event_type);
  EXPECT_EQ(static_cast<int>(kClosingFrameLength), events[6].result);
  EXPECT_EQ(SpdyWebSocketStreamEvent::EVENT_CLOSE,
            events[7].event_type);
  EXPECT_EQ(OK, events[7].result);

  // EOF close SPDY session.
  EXPECT_TRUE(!http_session_->spdy_session_pool()->HasSession(
      host_port_proxy_pair_));
  EXPECT_TRUE(data()->at_read_eof());
  EXPECT_TRUE(data()->at_write_eof());
}

}  // namespace net
