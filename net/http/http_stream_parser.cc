// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_parser.h"

#include "base/compiler_specific.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "net/base/address_list.h"
#include "net/base/auth.h"
#include "net/base/io_buffer.h"
#include "net/base/ssl_cert_request_info.h"
#include "net/http/http_net_log_params.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/client_socket_handle.h"

namespace {

static const size_t kMaxMergedHeaderAndBodySize = 1400;

std::string GetResponseHeaderLines(const net::HttpResponseHeaders& headers) {
  std::string raw_headers = headers.raw_headers();
  const char* null_separated_headers = raw_headers.c_str();
  const char* header_line = null_separated_headers;
  std::string cr_separated_headers;
  while (header_line[0] != 0) {
    cr_separated_headers += header_line;
    cr_separated_headers += "\n";
    header_line += strlen(header_line) + 1;
  }
  return cr_separated_headers;
}

// Return true if |headers| contain multiple |field_name| fields.  If
// |count_same_value| is false, returns false if all copies of the field have
// the same value.
bool HeadersContainMultipleCopiesOfField(
    const net::HttpResponseHeaders& headers,
    const std::string& field_name,
    bool count_same_value) {
  void* it = NULL;
  std::string field_value;
  if (!headers.EnumerateHeader(&it, field_name, &field_value))
    return false;
  // There's at least one |field_name| header.  Check if there are any more
  // such headers, and if so, return true if they have different values or
  // |count_same_value| is true.
  std::string field_value2;
  while (headers.EnumerateHeader(&it, field_name, &field_value2)) {
    if (count_same_value || field_value != field_value2)
      return true;
  }
  return false;
}

}  // namespace

namespace net {

// 2 CRLFs + max of 8 hex chars.
const size_t HttpStreamParser::kChunkHeaderFooterSize = 12;

HttpStreamParser::HttpStreamParser(ClientSocketHandle* connection,
                                   const HttpRequestInfo* request,
                                   GrowableIOBuffer* read_buffer,
                                   const BoundNetLog& net_log)
    : io_state_(STATE_NONE),
      request_(request),
      request_headers_(NULL),
      request_body_(NULL),
      read_buf_(read_buffer),
      read_buf_unused_offset_(0),
      response_header_start_offset_(-1),
      response_body_length_(-1),
      response_body_read_(0),
      chunked_decoder_(NULL),
      user_read_buf_(NULL),
      user_read_buf_len_(0),
      connection_(connection),
      net_log_(net_log),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          io_callback_(
              base::Bind(&HttpStreamParser::OnIOComplete,
                         base::Unretained(this)))),
      chunk_buffer_size_(UploadDataStream::GetBufferSize() +
                         kChunkHeaderFooterSize),
      chunk_length_(0),
      chunk_length_without_encoding_(0),
      sent_last_chunk_(false) {
}

HttpStreamParser::~HttpStreamParser() {
  if (request_body_ != NULL && request_body_->is_chunked())
    request_body_->set_chunk_callback(NULL);
}

int HttpStreamParser::SendRequest(const std::string& request_line,
                                  const HttpRequestHeaders& headers,
                                  UploadDataStream* request_body,
                                  HttpResponseInfo* response,
                                  const CompletionCallback& callback) {
  DCHECK_EQ(STATE_NONE, io_state_);
  DCHECK(callback_.is_null());
  DCHECK(!callback.is_null());
  DCHECK(response);

  if (net_log_.IsLoggingAllEvents()) {
    net_log_.AddEvent(
        NetLog::TYPE_HTTP_TRANSACTION_SEND_REQUEST_HEADERS,
        make_scoped_refptr(new NetLogHttpRequestParameter(
            request_line, headers)));
  }
  DVLOG(1) << __FUNCTION__ << "()"
           << " request_line = \"" << request_line << "\""
           << " headers = \"" << headers.ToString() << "\"";
  response_ = response;

  // Put the peer's IP address and port into the response.
  AddressList address;
  int result = connection_->socket()->GetPeerAddress(&address);
  if (result != OK)
    return result;
  response_->socket_address = HostPortPair::FromAddrInfo(address.head());

  std::string request = request_line + headers.ToString();
  request_body_.reset(request_body);
  if (request_body_ != NULL && request_body_->is_chunked()) {
    request_body_->set_chunk_callback(this);
    chunk_buf_ = new IOBuffer(chunk_buffer_size_);
  }

  io_state_ = STATE_SENDING_HEADERS;

  // If we have a small request body, then we'll merge with the headers into a
  // single write.
  bool did_merge = false;
  if (ShouldMergeRequestHeadersAndBody(request, request_body_.get())) {
    size_t merged_size = request.size() + request_body->size();
    scoped_refptr<IOBuffer> merged_request_headers_and_body(
        new IOBuffer(merged_size));
    // We'll repurpose |request_headers_| to store the merged headers and
    // body.
    request_headers_ = new DrainableIOBuffer(
        merged_request_headers_and_body, merged_size);

    char *buf = request_headers_->data();
    memcpy(buf, request.data(), request.size());
    buf += request.size();

    size_t todo = request_body_->size();
    while (todo) {
      size_t buf_len = request_body_->buf_len();
      memcpy(buf, request_body_->buf()->data(), buf_len);
      todo -= buf_len;
      buf += buf_len;
      request_body_->MarkConsumedAndFillBuffer(buf_len);
    }
    DCHECK(request_body_->eof());

    did_merge = true;
  }

  if (!did_merge) {
    // If we didn't merge the body with the headers, then |request_headers_|
    // contains just the HTTP headers.
    scoped_refptr<StringIOBuffer> headers_io_buf(new StringIOBuffer(request));
    request_headers_ = new DrainableIOBuffer(headers_io_buf,
                                             headers_io_buf->size());
  }

  result = DoLoop(OK);
  if (result == ERR_IO_PENDING)
    callback_ = callback;

  return result > 0 ? OK : result;
}

int HttpStreamParser::ReadResponseHeaders(const CompletionCallback& callback) {
  DCHECK(io_state_ == STATE_REQUEST_SENT || io_state_ == STATE_DONE);
  DCHECK(callback_.is_null());
  DCHECK(!callback.is_null());

  // This function can be called with io_state_ == STATE_DONE if the
  // connection is closed after seeing just a 1xx response code.
  if (io_state_ == STATE_DONE)
    return ERR_CONNECTION_CLOSED;

  int result = OK;
  io_state_ = STATE_READ_HEADERS;

  if (read_buf_->offset() > 0) {
    // Simulate the state where the data was just read from the socket.
    result = read_buf_->offset() - read_buf_unused_offset_;
    read_buf_->set_offset(read_buf_unused_offset_);
  }
  if (result > 0)
    io_state_ = STATE_READ_HEADERS_COMPLETE;

  result = DoLoop(result);
  if (result == ERR_IO_PENDING)
    callback_ = callback;

  return result > 0 ? OK : result;
}

void HttpStreamParser::Close(bool not_reusable) {
  if (not_reusable && connection_->socket())
    connection_->socket()->Disconnect();
  connection_->Reset();
}

int HttpStreamParser::ReadResponseBody(IOBuffer* buf, int buf_len,
                                       const CompletionCallback& callback) {
  DCHECK(io_state_ == STATE_BODY_PENDING || io_state_ == STATE_DONE);
  DCHECK(callback_.is_null());
  DCHECK(!callback.is_null());
  DCHECK_LE(buf_len, kMaxBufSize);

  if (io_state_ == STATE_DONE)
    return OK;

  user_read_buf_ = buf;
  user_read_buf_len_ = buf_len;
  io_state_ = STATE_READ_BODY;

  int result = DoLoop(OK);
  if (result == ERR_IO_PENDING)
    callback_ = callback;

  return result;
}

void HttpStreamParser::OnIOComplete(int result) {
  result = DoLoop(result);

  // The client callback can do anything, including destroying this class,
  // so any pending callback must be issued after everything else is done.
  if (result != ERR_IO_PENDING && !callback_.is_null()) {
    CompletionCallback c = callback_;
    callback_.Reset();
    c.Run(result);
  }
}

void HttpStreamParser::OnChunkAvailable() {
  // This method may get called while sending the headers or body, so check
  // before processing the new data. If we were still initializing or sending
  // headers, we will automatically start reading the chunks once we get into
  // STATE_SENDING_CHUNKED_BODY so nothing to do here.
  DCHECK(io_state_ == STATE_SENDING_HEADERS ||
         io_state_ == STATE_SENDING_CHUNKED_BODY);
  if (io_state_ == STATE_SENDING_CHUNKED_BODY)
    OnIOComplete(0);
}

int HttpStreamParser::DoLoop(int result) {
  bool can_do_more = true;
  do {
    switch (io_state_) {
      case STATE_SENDING_HEADERS:
        if (result < 0)
          can_do_more = false;
        else
          result = DoSendHeaders(result);
        break;
      case STATE_SENDING_CHUNKED_BODY:
        if (result < 0)
          can_do_more = false;
        else
          result = DoSendChunkedBody(result);
        break;
      case STATE_SENDING_NON_CHUNKED_BODY:
        if (result < 0)
          can_do_more = false;
        else
          result = DoSendNonChunkedBody(result);
        break;
      case STATE_REQUEST_SENT:
        DCHECK(result != ERR_IO_PENDING);
        can_do_more = false;
        break;
      case STATE_READ_HEADERS:
        net_log_.BeginEvent(NetLog::TYPE_HTTP_STREAM_PARSER_READ_HEADERS, NULL);
        result = DoReadHeaders();
        break;
      case STATE_READ_HEADERS_COMPLETE:
        result = DoReadHeadersComplete(result);
        net_log_.EndEventWithNetErrorCode(
            NetLog::TYPE_HTTP_STREAM_PARSER_READ_HEADERS, result);
        break;
      case STATE_BODY_PENDING:
        DCHECK(result != ERR_IO_PENDING);
        can_do_more = false;
        break;
      case STATE_READ_BODY:
        result = DoReadBody();
        // DoReadBodyComplete handles error conditions.
        break;
      case STATE_READ_BODY_COMPLETE:
        result = DoReadBodyComplete(result);
        break;
      case STATE_DONE:
        DCHECK(result != ERR_IO_PENDING);
        can_do_more = false;
        break;
      default:
        NOTREACHED();
        can_do_more = false;
        break;
    }
  } while (result != ERR_IO_PENDING && can_do_more);

  return result;
}

int HttpStreamParser::DoSendHeaders(int result) {
  request_headers_->DidConsume(result);
  int bytes_remaining = request_headers_->BytesRemaining();
  if (bytes_remaining > 0) {
    // Record our best estimate of the 'request time' as the time when we send
    // out the first bytes of the request headers.
    if (bytes_remaining == request_headers_->size()) {
      response_->request_time = base::Time::Now();
    }
    result = connection_->socket()->Write(request_headers_,
                                          bytes_remaining,
                                          io_callback_);
  } else if (request_body_ != NULL && request_body_->is_chunked()) {
    io_state_ = STATE_SENDING_CHUNKED_BODY;
    result = OK;
  } else if (request_body_ != NULL && request_body_->size() > 0 &&
             // !eof() indicates that the body wasn't merged.
             !request_body_->eof()) {
    io_state_ = STATE_SENDING_NON_CHUNKED_BODY;
    result = OK;
  } else {
    io_state_ = STATE_REQUEST_SENT;
  }
  return result;
}

int HttpStreamParser::DoSendChunkedBody(int result) {
  // |result| is the number of bytes sent from the last call to
  // DoSendChunkedBody(), or 0 (i.e. OK) the first time.

  chunk_length_ -= result;
  if (chunk_length_) {
    // Move the remaining data in the chunk buffer to the beginning.
    memmove(chunk_buf_->data(), chunk_buf_->data() + result, chunk_length_);
    return connection_->socket()->Write(chunk_buf_, chunk_length_,
                                        io_callback_);
  }

  if (sent_last_chunk_) {
    io_state_ = STATE_REQUEST_SENT;
    return OK;
  }

  // |chunk_length_without_encoding_| is 0 when DoSendBody() is first
  // called, hence the first call to MarkConsumedAndFillBuffer() is a noop.
  request_body_->MarkConsumedAndFillBuffer(chunk_length_without_encoding_);
  chunk_length_without_encoding_ = 0;

  if (request_body_->eof()) {
    chunk_length_ = EncodeChunk(
        base::StringPiece(), chunk_buf_->data(), chunk_buffer_size_);
    sent_last_chunk_ = true;
  } else if (request_body_->buf_len() > 0) {
    // Encode and send the buffer as 1 chunk.
    const base::StringPiece payload(request_body_->buf()->data(),
                                    request_body_->buf_len());
    chunk_length_ = EncodeChunk(payload, chunk_buf_->data(),
                                chunk_buffer_size_);
    chunk_length_without_encoding_ = payload.size();
  } else {
    // Nothing to send. More POST data is yet to come?
    return ERR_IO_PENDING;
  }

  return connection_->socket()->Write(chunk_buf_, chunk_length_,
                                      io_callback_);
}

int HttpStreamParser::DoSendNonChunkedBody(int result) {
  // |result| is the number of bytes sent from the last call to
  // DoSendNonChunkedBody(), or 0 (i.e. OK) the first time.

  // The first call to MarkConsumedAndFillBuffer() is a noop as |result| is 0.
  request_body_->MarkConsumedAndFillBuffer(result);

  if (!request_body_->eof()) {
    int buf_len = static_cast<int>(request_body_->buf_len());
    result = connection_->socket()->Write(request_body_->buf(), buf_len,
                                          io_callback_);
  } else {
    io_state_ = STATE_REQUEST_SENT;
  }
  return result;
}

int HttpStreamParser::DoReadHeaders() {
  io_state_ = STATE_READ_HEADERS_COMPLETE;

  // Grow the read buffer if necessary.
  if (read_buf_->RemainingCapacity() == 0)
    read_buf_->SetCapacity(read_buf_->capacity() + kHeaderBufInitialSize);

  // http://crbug.com/16371: We're seeing |user_buf_->data()| return NULL.
  // See if the user is passing in an IOBuffer with a NULL |data_|.
  CHECK(read_buf_->data());

  return connection_->socket()->Read(read_buf_,
                                     read_buf_->RemainingCapacity(),
                                     io_callback_);
}

int HttpStreamParser::DoReadHeadersComplete(int result) {
  if (result == 0)
    result = ERR_CONNECTION_CLOSED;

  if (result < 0 && result != ERR_CONNECTION_CLOSED) {
    io_state_ = STATE_DONE;
    return result;
  }
  // If we've used the connection before, then we know it is not a HTTP/0.9
  // response and return ERR_CONNECTION_CLOSED.
  if (result == ERR_CONNECTION_CLOSED && read_buf_->offset() == 0 &&
      connection_->is_reused()) {
    io_state_ = STATE_DONE;
    return result;
  }

  // Record our best estimate of the 'response time' as the time when we read
  // the first bytes of the response headers.
  if (read_buf_->offset() == 0 && result != ERR_CONNECTION_CLOSED)
    response_->response_time = base::Time::Now();

  if (result == ERR_CONNECTION_CLOSED) {
    // The connection closed before we detected the end of the headers.
    // parse things as well as we can and let the caller decide what to do.
    if (read_buf_->offset() == 0) {
      // The connection was closed before any data was sent. Likely an error
      // rather than empty HTTP/0.9 response.
      io_state_ = STATE_DONE;
      return ERR_EMPTY_RESPONSE;
    } else {
      int end_offset;
      if (response_header_start_offset_ >= 0) {
        io_state_ = STATE_READ_BODY_COMPLETE;
        end_offset = read_buf_->offset();
      } else {
        io_state_ = STATE_BODY_PENDING;
        end_offset = 0;
      }
      int rv = DoParseResponseHeaders(end_offset);
      if (rv < 0)
        return rv;
      return result;
    }
  }

  read_buf_->set_offset(read_buf_->offset() + result);
  DCHECK_LE(read_buf_->offset(), read_buf_->capacity());
  DCHECK_GE(result,  0);

  int end_of_header_offset = ParseResponseHeaders();

  // Note: -1 is special, it indicates we haven't found the end of headers.
  // Anything less than -1 is a net::Error, so we bail out.
  if (end_of_header_offset < -1)
    return end_of_header_offset;

  if (end_of_header_offset == -1) {
    io_state_ = STATE_READ_HEADERS;
    // Prevent growing the headers buffer indefinitely.
    if (read_buf_->offset() - read_buf_unused_offset_ >= kMaxHeaderBufSize) {
      io_state_ = STATE_DONE;
      return ERR_RESPONSE_HEADERS_TOO_BIG;
    }
  } else {
    // Note where the headers stop.
    read_buf_unused_offset_ = end_of_header_offset;

    if (response_->headers->response_code() / 100 == 1) {
      // After processing a 1xx response, the caller will ask for the next
      // header, so reset state to support that.  We don't just skip these
      // completely because 1xx codes aren't acceptable when establishing a
      // tunnel.
      io_state_ = STATE_REQUEST_SENT;
      response_header_start_offset_ = -1;
    } else {
      io_state_ = STATE_BODY_PENDING;
      CalculateResponseBodySize();
      // If the body is 0, the caller may not call ReadResponseBody, which
      // is where any extra data is copied to read_buf_, so we move the
      // data here and transition to DONE.
      if (response_body_length_ == 0) {
        io_state_ = STATE_DONE;
        int extra_bytes = read_buf_->offset() - read_buf_unused_offset_;
        if (extra_bytes) {
          CHECK_GT(extra_bytes, 0);
          memmove(read_buf_->StartOfBuffer(),
                  read_buf_->StartOfBuffer() + read_buf_unused_offset_,
                  extra_bytes);
        }
        read_buf_->SetCapacity(extra_bytes);
        read_buf_unused_offset_ = 0;
        return OK;
      }
    }
  }
  return result;
}

int HttpStreamParser::DoReadBody() {
  io_state_ = STATE_READ_BODY_COMPLETE;

  // There may be some data left over from reading the response headers.
  if (read_buf_->offset()) {
    int available = read_buf_->offset() - read_buf_unused_offset_;
    if (available) {
      CHECK_GT(available, 0);
      int bytes_from_buffer = std::min(available, user_read_buf_len_);
      memcpy(user_read_buf_->data(),
             read_buf_->StartOfBuffer() + read_buf_unused_offset_,
             bytes_from_buffer);
      read_buf_unused_offset_ += bytes_from_buffer;
      if (bytes_from_buffer == available) {
        read_buf_->SetCapacity(0);
        read_buf_unused_offset_ = 0;
      }
      return bytes_from_buffer;
    } else {
      read_buf_->SetCapacity(0);
      read_buf_unused_offset_ = 0;
    }
  }

  // Check to see if we're done reading.
  if (IsResponseBodyComplete())
    return 0;

  DCHECK_EQ(0, read_buf_->offset());
  return connection_->socket()->Read(user_read_buf_, user_read_buf_len_,
                                     io_callback_);
}

int HttpStreamParser::DoReadBodyComplete(int result) {
  // If we didn't get a Content-Length and aren't using a chunked encoding,
  // the only way to signal the end of a stream is to close the connection,
  // so we don't treat that as an error, though in some cases we may not
  // have completely received the resource.
  if (result == 0 && !IsResponseBodyComplete() && CanFindEndOfResponse())
    result = ERR_CONNECTION_CLOSED;

  // Filter incoming data if appropriate.  FilterBuf may return an error.
  if (result > 0 && chunked_decoder_.get()) {
    result = chunked_decoder_->FilterBuf(user_read_buf_->data(), result);
    if (result == 0 && !chunked_decoder_->reached_eof()) {
      // Don't signal completion of the Read call yet or else it'll look like
      // we received end-of-file.  Wait for more data.
      io_state_ = STATE_READ_BODY;
      return OK;
    }
  }

  if (result > 0)
    response_body_read_ += result;

  if (result <= 0 || IsResponseBodyComplete()) {
    io_state_ = STATE_DONE;

    // Save the overflow data, which can be in two places.  There may be
    // some left over in |user_read_buf_|, plus there may be more
    // in |read_buf_|.  But the part left over in |user_read_buf_| must have
    // come from the |read_buf_|, so there's room to put it back at the
    // start first.
    int additional_save_amount = read_buf_->offset() - read_buf_unused_offset_;
    int save_amount = 0;
    if (chunked_decoder_.get()) {
      save_amount = chunked_decoder_->bytes_after_eof();
    } else if (response_body_length_ >= 0) {
      int64 extra_data_read = response_body_read_ - response_body_length_;
      if (extra_data_read > 0) {
        save_amount = static_cast<int>(extra_data_read);
        if (result > 0)
          result -= save_amount;
      }
    }

    CHECK_LE(save_amount + additional_save_amount, kMaxBufSize);
    if (read_buf_->capacity() < save_amount + additional_save_amount) {
      read_buf_->SetCapacity(save_amount + additional_save_amount);
    }

    if (save_amount) {
      memcpy(read_buf_->StartOfBuffer(), user_read_buf_->data() + result,
             save_amount);
    }
    read_buf_->set_offset(save_amount);
    if (additional_save_amount) {
      memmove(read_buf_->data(),
              read_buf_->StartOfBuffer() + read_buf_unused_offset_,
              additional_save_amount);
      read_buf_->set_offset(save_amount + additional_save_amount);
    }
    read_buf_unused_offset_ = 0;
  } else {
    io_state_ = STATE_BODY_PENDING;
    user_read_buf_ = NULL;
    user_read_buf_len_ = 0;
  }

  return result;
}

int HttpStreamParser::ParseResponseHeaders() {
  int end_offset = -1;

  // Look for the start of the status line, if it hasn't been found yet.
  if (response_header_start_offset_ < 0) {
    response_header_start_offset_ = HttpUtil::LocateStartOfStatusLine(
        read_buf_->StartOfBuffer() + read_buf_unused_offset_,
        read_buf_->offset() - read_buf_unused_offset_);
  }

  if (response_header_start_offset_ >= 0) {
    end_offset = HttpUtil::LocateEndOfHeaders(
        read_buf_->StartOfBuffer() + read_buf_unused_offset_,
        read_buf_->offset() - read_buf_unused_offset_,
        response_header_start_offset_);
  } else if (read_buf_->offset() - read_buf_unused_offset_ >= 8) {
    // Enough data to decide that this is an HTTP/0.9 response.
    // 8 bytes = (4 bytes of junk) + "http".length()
    end_offset = 0;
  }

  if (end_offset == -1)
    return -1;

  int rv = DoParseResponseHeaders(end_offset);
  if (rv < 0)
    return rv;
  return end_offset + read_buf_unused_offset_;
}

int HttpStreamParser::DoParseResponseHeaders(int end_offset) {
  scoped_refptr<HttpResponseHeaders> headers;
  if (response_header_start_offset_ >= 0) {
    headers = new HttpResponseHeaders(HttpUtil::AssembleRawHeaders(
        read_buf_->StartOfBuffer() + read_buf_unused_offset_, end_offset));
  } else {
    // Enough data was read -- there is no status line.
    headers = new HttpResponseHeaders(std::string("HTTP/0.9 200 OK"));
  }

  // Check for multiple Content-Length headers with no Transfer-Encoding header.
  // If they exist, and have distinct values, it's a potential response
  // smuggling attack.
  if (!headers->HasHeader("Transfer-Encoding")) {
    if (HeadersContainMultipleCopiesOfField(*headers,
                                            "Content-Length",
                                            false)) {
      return ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_LENGTH;
    }
  }

  // Check for multiple Content-Disposition or Location headers.  If they exist,
  // it's also a potential response smuggling attack.
  if (HeadersContainMultipleCopiesOfField(*headers,
                                          "Content-Disposition",
                                          true)) {
    return ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_DISPOSITION;
  }
  if (HeadersContainMultipleCopiesOfField(*headers, "Location", true))
    return ERR_RESPONSE_HEADERS_MULTIPLE_LOCATION;

  response_->headers = headers;
  response_->vary_data.Init(*request_, *response_->headers);
  DVLOG(1) << __FUNCTION__ << "()"
           << " content_length = \""
           << response_->headers->GetContentLength() << "\n\""
           << " headers = \"" << GetResponseHeaderLines(*response_->headers)
           << "\"";
  return OK;
}

void HttpStreamParser::CalculateResponseBodySize() {
  // Figure how to determine EOF:

  // For certain responses, we know the content length is always 0. From
  // RFC 2616 Section 4.3 Message Body:
  //
  // For response messages, whether or not a message-body is included with
  // a message is dependent on both the request method and the response
  // status code (section 6.1.1). All responses to the HEAD request method
  // MUST NOT include a message-body, even though the presence of entity-
  // header fields might lead one to believe they do. All 1xx
  // (informational), 204 (no content), and 304 (not modified) responses
  // MUST NOT include a message-body. All other responses do include a
  // message-body, although it MAY be of zero length.
  switch (response_->headers->response_code()) {
    // Note that 1xx was already handled earlier.
    case 204:  // No Content
    case 205:  // Reset Content
    case 304:  // Not Modified
      response_body_length_ = 0;
      break;
  }
  if (request_->method == "HEAD")
    response_body_length_ = 0;

  if (response_body_length_ == -1) {
    // "Transfer-Encoding: chunked" trumps "Content-Length: N"
    if (response_->headers->IsChunkEncoded()) {
      chunked_decoder_.reset(new HttpChunkedDecoder());
    } else {
      response_body_length_ = response_->headers->GetContentLength();
      // If response_body_length_ is still -1, then we have to wait
      // for the server to close the connection.
    }
  }
}

uint64 HttpStreamParser::GetUploadProgress() const {
  if (!request_body_.get())
    return 0;

  return request_body_->position();
}

HttpResponseInfo* HttpStreamParser::GetResponseInfo() {
  return response_;
}

bool HttpStreamParser::IsResponseBodyComplete() const {
  if (chunked_decoder_.get())
    return chunked_decoder_->reached_eof();
  if (response_body_length_ != -1)
    return response_body_read_ >= response_body_length_;

  return false;  // Must read to EOF.
}

bool HttpStreamParser::CanFindEndOfResponse() const {
  return chunked_decoder_.get() || response_body_length_ >= 0;
}

bool HttpStreamParser::IsMoreDataBuffered() const {
  return read_buf_->offset() > read_buf_unused_offset_;
}

bool HttpStreamParser::IsConnectionReused() const {
  ClientSocketHandle::SocketReuseType reuse_type = connection_->reuse_type();
  return connection_->is_reused() ||
         reuse_type == ClientSocketHandle::UNUSED_IDLE;
}

void HttpStreamParser::SetConnectionReused() {
  connection_->set_is_reused(true);
}

bool HttpStreamParser::IsConnectionReusable() const {
  return connection_->socket() && connection_->socket()->IsConnectedAndIdle();
}

void HttpStreamParser::GetSSLInfo(SSLInfo* ssl_info) {
  if (request_->url.SchemeIs("https") && connection_->socket()) {
    SSLClientSocket* ssl_socket =
        static_cast<SSLClientSocket*>(connection_->socket());
    ssl_socket->GetSSLInfo(ssl_info);
  }
}

void HttpStreamParser::GetSSLCertRequestInfo(
    SSLCertRequestInfo* cert_request_info) {
  if (request_->url.SchemeIs("https") && connection_->socket()) {
    SSLClientSocket* ssl_socket =
        static_cast<SSLClientSocket*>(connection_->socket());
    ssl_socket->GetSSLCertRequestInfo(cert_request_info);
  }
}

int HttpStreamParser::EncodeChunk(const base::StringPiece& payload,
                                  char* output,
                                  size_t output_size) {
  if (output_size < payload.size() + kChunkHeaderFooterSize)
    return ERR_INVALID_ARGUMENT;

  char* cursor = output;
  // Add the header.
  const int num_chars = base::snprintf(output, output_size,
                                       "%X\r\n",
                                       static_cast<int>(payload.size()));
  cursor += num_chars;
  // Add the payload if any.
  if (payload.size() > 0) {
    memcpy(cursor, payload.data(), payload.size());
    cursor += payload.size();
  }
  // Add the trailing CRLF.
  memcpy(cursor, "\r\n", 2);
  cursor += 2;

  return cursor - output;
}

// static
bool HttpStreamParser::ShouldMergeRequestHeadersAndBody(
    const std::string& request_headers,
    const UploadDataStream* request_body) {
  if (request_body != NULL &&
      // IsInMemory() ensures that the request body is not chunked.
      request_body->IsInMemory() &&
      request_body->size() > 0) {
    size_t merged_size = request_headers.size() + request_body->size();
    if (merged_size <= kMaxMergedHeaderAndBodySize)
      return true;
  }
  return false;
}

}  // namespace net
