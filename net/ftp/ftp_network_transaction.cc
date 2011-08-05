// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ftp/ftp_network_transaction.h"

#include "base/compiler_specific.h"
#include "base/metrics/histogram.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "net/base/address_list.h"
#include "net/base/connection_type_histograms.h"
#include "net/base/escape.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"
#include "net/base/net_util.h"
#include "net/ftp/ftp_network_session.h"
#include "net/ftp/ftp_request_info.h"
#include "net/ftp/ftp_util.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/stream_socket.h"

// TODO(ibrar): Try to avoid sscanf.
#if !defined(COMPILER_MSVC)
#define sscanf_s sscanf
#endif

const char kCRLF[] = "\r\n";

const int kCtrlBufLen = 1024;

namespace {

// Returns true if |input| can be safely used as a part of FTP command.
bool IsValidFTPCommandString(const std::string& input) {
  // RFC 959 only allows ASCII strings, but at least Firefox can send non-ASCII
  // characters in the command if the request path contains them. To be
  // compatible, we do the same and allow non-ASCII characters in a command.

  // Protect agains newline injection attack.
  if (input.find_first_of("\r\n") != std::string::npos)
    return false;

  return true;
}

enum ErrorClass {
  // The requested action was initiated. The client should expect another
  // reply before issuing the next command.
  ERROR_CLASS_INITIATED,

  // The requested action has been successfully completed.
  ERROR_CLASS_OK,

  // The command has been accepted, but to complete the operation, more
  // information must be sent by the client.
  ERROR_CLASS_INFO_NEEDED,

  // The command was not accepted and the requested action did not take place.
  // This condition is temporary, and the client is encouraged to restart the
  // command sequence.
  ERROR_CLASS_TRANSIENT_ERROR,

  // The command was not accepted and the requested action did not take place.
  // This condition is rather permanent, and the client is discouraged from
  // repeating the exact request.
  ERROR_CLASS_PERMANENT_ERROR,
};

// Returns the error class for given response code. Caller should ensure
// that |response_code| is in range 100-599.
ErrorClass GetErrorClass(int response_code) {
  if (response_code >= 100 && response_code <= 199)
    return ERROR_CLASS_INITIATED;

  if (response_code >= 200 && response_code <= 299)
    return ERROR_CLASS_OK;

  if (response_code >= 300 && response_code <= 399)
    return ERROR_CLASS_INFO_NEEDED;

  if (response_code >= 400 && response_code <= 499)
    return ERROR_CLASS_TRANSIENT_ERROR;

  if (response_code >= 500 && response_code <= 599)
    return ERROR_CLASS_PERMANENT_ERROR;

  // We should not be called on invalid error codes.
  NOTREACHED() << response_code;
  return ERROR_CLASS_PERMANENT_ERROR;
}

// Returns network error code for received FTP |response_code|.
int GetNetErrorCodeForFtpResponseCode(int response_code) {
  switch (response_code) {
    case 421:
      return net::ERR_FTP_SERVICE_UNAVAILABLE;
    case 426:
      return net::ERR_FTP_TRANSFER_ABORTED;
    case 450:
      return net::ERR_FTP_FILE_BUSY;
    case 500:
    case 501:
      return net::ERR_FTP_SYNTAX_ERROR;
    case 502:
    case 504:
      return net::ERR_FTP_COMMAND_NOT_SUPPORTED;
    case 503:
      return net::ERR_FTP_BAD_COMMAND_SEQUENCE;
    default:
      return net::ERR_FTP_FAILED;
  }
}

// From RFC 2428 Section 3:
//   The text returned in response to the EPSV command MUST be:
//     <some text> (<d><d><d><tcp-port><d>)
//   <d> is a delimiter character, ideally to be |
bool ExtractPortFromEPSVResponse(const net::FtpCtrlResponse& response,
                                 int* port) {
  if (response.lines.size() != 1)
    return false;
  const char* ptr = response.lines[0].c_str();
  while (*ptr && *ptr != '(')
    ++ptr;
  if (!*ptr)
    return false;
  char sep = *(++ptr);
  if (!sep || isdigit(sep) || *(++ptr) != sep || *(++ptr) != sep)
    return false;
  if (!isdigit(*(++ptr)))
    return false;
  *port = *ptr - '0';
  while (isdigit(*(++ptr))) {
    *port *= 10;
    *port += *ptr - '0';
  }
  if (*ptr != sep)
    return false;

  return true;
}

// There are two way we can receive IP address and port.
// (127,0,0,1,23,21) IP address and port encapsulated in ().
// 127,0,0,1,23,21  IP address and port without ().
//
// See RFC 959, Section 4.1.2
bool ExtractPortFromPASVResponse(const net::FtpCtrlResponse& response,
                                 int* port) {
  if (response.lines.size() != 1)
    return false;
  const char* ptr = response.lines[0].c_str();
  while (*ptr && *ptr != '(')  // Try with bracket.
    ++ptr;
  if (*ptr) {
    ++ptr;
  } else {
    ptr = response.lines[0].c_str();  // Try without bracket.
    while (*ptr && *ptr != ',')
      ++ptr;
    while (*ptr && *ptr != ' ')
      --ptr;
  }
  int i0, i1, i2, i3, p0, p1;
  if (sscanf_s(ptr, "%d,%d,%d,%d,%d,%d", &i0, &i1, &i2, &i3, &p0, &p1) != 6)
    return false;

  // Ignore the IP address supplied in the response. We are always going
  // to connect back to the same server to prevent FTP PASV port scanning.
  *port = (p0 << 8) + p1;
  return true;
}

}  // namespace

namespace net {

FtpNetworkTransaction::FtpNetworkTransaction(
    FtpNetworkSession* session,
    ClientSocketFactory* socket_factory)
    : command_sent_(COMMAND_NONE),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          io_callback_(this, &FtpNetworkTransaction::OnIOComplete)),
      user_callback_(NULL),
      session_(session),
      request_(NULL),
      resolver_(session->host_resolver()),
      read_ctrl_buf_(new IOBuffer(kCtrlBufLen)),
      ctrl_response_buffer_(new FtpCtrlResponseBuffer()),
      read_data_buf_len_(0),
      last_error_(OK),
      system_type_(SYSTEM_TYPE_UNKNOWN),
      // Use image (binary) transfer by default. It should always work,
      // whereas the ascii transfer may damage binary data.
      data_type_(DATA_TYPE_IMAGE),
      resource_type_(RESOURCE_TYPE_UNKNOWN),
      use_epsv_(true),
      data_connection_port_(0),
      socket_factory_(socket_factory),
      next_state_(STATE_NONE) {
}

FtpNetworkTransaction::~FtpNetworkTransaction() {
}

int FtpNetworkTransaction::Stop(int error) {
  if (command_sent_ == COMMAND_QUIT)
    return error;

  next_state_ = STATE_CTRL_WRITE_QUIT;
  last_error_ = error;
  return OK;
}

int FtpNetworkTransaction::RestartIgnoringLastError(
    CompletionCallback* callback) {
  return ERR_NOT_IMPLEMENTED;
}

int FtpNetworkTransaction::Start(const FtpRequestInfo* request_info,
                                 CompletionCallback* callback,
                                 const BoundNetLog& net_log) {
  net_log_ = net_log;
  request_ = request_info;

  if (request_->url.has_username()) {
    GetIdentityFromURL(request_->url, &username_, &password_);
  } else {
    username_ = ASCIIToUTF16("anonymous");
    password_ = ASCIIToUTF16("chrome@example.com");
  }

  DetectTypecode();

  next_state_ = STATE_CTRL_RESOLVE_HOST;
  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    user_callback_ = callback;
  return rv;
}

int FtpNetworkTransaction::RestartWithAuth(const string16& username,
                                           const string16& password,
                                           CompletionCallback* callback) {
  ResetStateForRestart();

  username_ = username;
  password_ = password;

  next_state_ = STATE_CTRL_RESOLVE_HOST;
  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    user_callback_ = callback;
  return rv;
}

int FtpNetworkTransaction::Read(IOBuffer* buf,
                                int buf_len,
                                CompletionCallback* callback) {
  DCHECK(buf);
  DCHECK_GT(buf_len, 0);

  read_data_buf_ = buf;
  read_data_buf_len_ = buf_len;

  next_state_ = STATE_DATA_READ;
  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    user_callback_ = callback;
  return rv;
}

const FtpResponseInfo* FtpNetworkTransaction::GetResponseInfo() const {
  return &response_;
}

LoadState FtpNetworkTransaction::GetLoadState() const {
  if (next_state_ == STATE_CTRL_RESOLVE_HOST_COMPLETE)
    return LOAD_STATE_RESOLVING_HOST;

  if (next_state_ == STATE_CTRL_CONNECT_COMPLETE ||
      next_state_ == STATE_DATA_CONNECT_COMPLETE)
    return LOAD_STATE_CONNECTING;

  if (next_state_ == STATE_DATA_READ_COMPLETE)
    return LOAD_STATE_READING_RESPONSE;

  if (command_sent_ == COMMAND_RETR && read_data_buf_.get())
    return LOAD_STATE_READING_RESPONSE;

  if (command_sent_ == COMMAND_QUIT)
    return LOAD_STATE_IDLE;

  if (command_sent_ != COMMAND_NONE)
    return LOAD_STATE_SENDING_REQUEST;

  return LOAD_STATE_IDLE;
}

uint64 FtpNetworkTransaction::GetUploadProgress() const {
  return 0;
}

void FtpNetworkTransaction::ResetStateForRestart() {
  command_sent_ = COMMAND_NONE;
  user_callback_ = NULL;
  response_ = FtpResponseInfo();
  read_ctrl_buf_ = new IOBuffer(kCtrlBufLen);
  ctrl_response_buffer_.reset(new FtpCtrlResponseBuffer());
  read_data_buf_ = NULL;
  read_data_buf_len_ = 0;
  if (write_buf_)
    write_buf_->SetOffset(0);
  last_error_ = OK;
  data_connection_port_ = 0;
  ctrl_socket_.reset();
  data_socket_.reset();
  next_state_ = STATE_NONE;
}

void FtpNetworkTransaction::DoCallback(int rv) {
  DCHECK(rv != ERR_IO_PENDING);
  DCHECK(user_callback_);

  // Since Run may result in Read being called, clear callback_ up front.
  CompletionCallback* c = user_callback_;
  user_callback_ = NULL;
  c->Run(rv);
}

void FtpNetworkTransaction::OnIOComplete(int result) {
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING)
    DoCallback(rv);
}

int FtpNetworkTransaction::ProcessCtrlResponse() {
  FtpCtrlResponse response = ctrl_response_buffer_->PopResponse();

  int rv = OK;
  switch (command_sent_) {
    case COMMAND_NONE:
      // TODO(phajdan.jr): Check for errors in the welcome message.
      next_state_ = STATE_CTRL_WRITE_USER;
      break;
    case COMMAND_USER:
      rv = ProcessResponseUSER(response);
      break;
    case COMMAND_PASS:
      rv = ProcessResponsePASS(response);
      break;
    case COMMAND_SYST:
      rv = ProcessResponseSYST(response);
      break;
    case COMMAND_PWD:
      rv = ProcessResponsePWD(response);
      break;
    case COMMAND_TYPE:
      rv = ProcessResponseTYPE(response);
      break;
    case COMMAND_EPSV:
      rv = ProcessResponseEPSV(response);
      break;
    case COMMAND_PASV:
      rv = ProcessResponsePASV(response);
      break;
    case COMMAND_SIZE:
      rv = ProcessResponseSIZE(response);
      break;
    case COMMAND_RETR:
      rv = ProcessResponseRETR(response);
      break;
    case COMMAND_CWD:
      rv = ProcessResponseCWD(response);
      break;
    case COMMAND_LIST:
      rv = ProcessResponseLIST(response);
      break;
    case COMMAND_QUIT:
      rv = ProcessResponseQUIT(response);
      break;
    default:
      LOG(DFATAL) << "Unexpected value of command_sent_: " << command_sent_;
      return ERR_UNEXPECTED;
  }

  // We may get multiple responses for some commands,
  // see http://crbug.com/18036.
  while (ctrl_response_buffer_->ResponseAvailable() && rv == OK) {
    response = ctrl_response_buffer_->PopResponse();

    switch (command_sent_) {
      case COMMAND_RETR:
        rv = ProcessResponseRETR(response);
        break;
      case COMMAND_LIST:
        rv = ProcessResponseLIST(response);
        break;
      default:
        // Multiple responses for other commands are invalid.
        return Stop(ERR_INVALID_RESPONSE);
    }
  }

  return rv;
}

// Used to prepare and send FTP command.
int FtpNetworkTransaction::SendFtpCommand(const std::string& command,
                                          Command cmd) {
  // If we send a new command when we still have unprocessed responses
  // for previous commands, the response receiving code will have no way to know
  // which responses are for which command.
  DCHECK(!ctrl_response_buffer_->ResponseAvailable());

  DCHECK(!write_command_buf_);
  DCHECK(!write_buf_);

  if (!IsValidFTPCommandString(command)) {
    // Callers should validate the command themselves and return a more specific
    // error code.
    NOTREACHED();
    return Stop(ERR_UNEXPECTED);
  }

  command_sent_ = cmd;

  write_command_buf_ = new IOBufferWithSize(command.length() + 2);
  write_buf_ = new DrainableIOBuffer(write_command_buf_,
                                     write_command_buf_->size());
  memcpy(write_command_buf_->data(), command.data(), command.length());
  memcpy(write_command_buf_->data() + command.length(), kCRLF, 2);

  next_state_ = STATE_CTRL_WRITE;
  return OK;
}

std::string FtpNetworkTransaction::GetRequestPathForFtpCommand(
    bool is_directory) const {
  std::string path(current_remote_directory_);
  if (request_->url.has_path()) {
    std::string gurl_path(request_->url.path());

    // Get rid of the typecode, see RFC 1738 section 3.2.2. FTP url-path.
    std::string::size_type pos = gurl_path.rfind(';');
    if (pos != std::string::npos)
      gurl_path.resize(pos);

    path.append(gurl_path);
  }
  // Make sure that if the path is expected to be a file, it won't end
  // with a trailing slash.
  if (!is_directory && path.length() > 1 && path[path.length() - 1] == '/')
    path.erase(path.length() - 1);
  UnescapeRule::Type unescape_rules = UnescapeRule::SPACES |
                                      UnescapeRule::URL_SPECIAL_CHARS;
  // This may unescape to non-ASCII characters, but we allow that. See the
  // comment for IsValidFTPCommandString.
  path = UnescapeURLComponent(path, unescape_rules);

  if (system_type_ == SYSTEM_TYPE_VMS) {
    if (is_directory)
      path = FtpUtil::UnixDirectoryPathToVMS(path);
    else
      path = FtpUtil::UnixFilePathToVMS(path);
  }

  DCHECK(IsValidFTPCommandString(path));
  return path;
}

void FtpNetworkTransaction::DetectTypecode() {
  if (!request_->url.has_path())
    return;
  std::string gurl_path(request_->url.path());

  // Extract the typecode, see RFC 1738 section 3.2.2. FTP url-path.
  std::string::size_type pos = gurl_path.rfind(';');
  if (pos == std::string::npos)
    return;
  std::string typecode_string(gurl_path.substr(pos));
  if (typecode_string == ";type=a") {
    data_type_ = DATA_TYPE_ASCII;
    resource_type_ = RESOURCE_TYPE_FILE;
  } else if (typecode_string == ";type=i") {
    data_type_ = DATA_TYPE_IMAGE;
    resource_type_ = RESOURCE_TYPE_FILE;
  } else if (typecode_string == ";type=d") {
    resource_type_ = RESOURCE_TYPE_DIRECTORY;
  }
}

int FtpNetworkTransaction::DoLoop(int result) {
  DCHECK(next_state_ != STATE_NONE);

  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_CTRL_RESOLVE_HOST:
        DCHECK(rv == OK);
        rv = DoCtrlResolveHost();
        break;
      case STATE_CTRL_RESOLVE_HOST_COMPLETE:
        rv = DoCtrlResolveHostComplete(rv);
        break;
      case STATE_CTRL_CONNECT:
        DCHECK(rv == OK);
        rv = DoCtrlConnect();
        break;
      case STATE_CTRL_CONNECT_COMPLETE:
        rv = DoCtrlConnectComplete(rv);
        break;
      case STATE_CTRL_READ:
        DCHECK(rv == OK);
        rv = DoCtrlRead();
        break;
      case STATE_CTRL_READ_COMPLETE:
        rv = DoCtrlReadComplete(rv);
        break;
      case STATE_CTRL_WRITE:
        DCHECK(rv == OK);
        rv = DoCtrlWrite();
        break;
      case STATE_CTRL_WRITE_COMPLETE:
        rv = DoCtrlWriteComplete(rv);
        break;
      case STATE_CTRL_WRITE_USER:
        DCHECK(rv == OK);
        rv = DoCtrlWriteUSER();
        break;
      case STATE_CTRL_WRITE_PASS:
        DCHECK(rv == OK);
        rv = DoCtrlWritePASS();
        break;
      case STATE_CTRL_WRITE_SYST:
        DCHECK(rv == OK);
        rv = DoCtrlWriteSYST();
        break;
      case STATE_CTRL_WRITE_PWD:
        DCHECK(rv == OK);
        rv = DoCtrlWritePWD();
        break;
      case STATE_CTRL_WRITE_TYPE:
        DCHECK(rv == OK);
        rv = DoCtrlWriteTYPE();
        break;
      case STATE_CTRL_WRITE_EPSV:
        DCHECK(rv == OK);
        rv = DoCtrlWriteEPSV();
        break;
      case STATE_CTRL_WRITE_PASV:
        DCHECK(rv == OK);
        rv = DoCtrlWritePASV();
        break;
      case STATE_CTRL_WRITE_RETR:
        DCHECK(rv == OK);
        rv = DoCtrlWriteRETR();
        break;
      case STATE_CTRL_WRITE_SIZE:
        DCHECK(rv == OK);
        rv = DoCtrlWriteSIZE();
        break;
      case STATE_CTRL_WRITE_CWD:
        DCHECK(rv == OK);
        rv = DoCtrlWriteCWD();
        break;
      case STATE_CTRL_WRITE_LIST:
        DCHECK(rv == OK);
        rv = DoCtrlWriteLIST();
        break;
      case STATE_CTRL_WRITE_QUIT:
        DCHECK(rv == OK);
        rv = DoCtrlWriteQUIT();
        break;
      case STATE_DATA_CONNECT:
        DCHECK(rv == OK);
        rv = DoDataConnect();
        break;
      case STATE_DATA_CONNECT_COMPLETE:
        rv = DoDataConnectComplete(rv);
        break;
      case STATE_DATA_READ:
        DCHECK(rv == OK);
        rv = DoDataRead();
        break;
      case STATE_DATA_READ_COMPLETE:
        rv = DoDataReadComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);
  return rv;
}

int FtpNetworkTransaction::DoCtrlResolveHost() {
  next_state_ = STATE_CTRL_RESOLVE_HOST_COMPLETE;

  HostResolver::RequestInfo info(HostPortPair::FromURL(request_->url));
  // No known referrer.
  return resolver_.Resolve(info, &addresses_, &io_callback_, net_log_);
}

int FtpNetworkTransaction::DoCtrlResolveHostComplete(int result) {
  if (result == OK)
    next_state_ = STATE_CTRL_CONNECT;
  return result;
}

int FtpNetworkTransaction::DoCtrlConnect() {
  next_state_ = STATE_CTRL_CONNECT_COMPLETE;
  ctrl_socket_.reset(socket_factory_->CreateTransportClientSocket(
        addresses_, net_log_.net_log(), net_log_.source()));
  return ctrl_socket_->Connect(&io_callback_);
}

int FtpNetworkTransaction::DoCtrlConnectComplete(int result) {
  if (result == OK) {
    // Put the peer's IP address and port into the response.
    AddressList address;
    result = ctrl_socket_->GetPeerAddress(&address);
    if (result == OK) {
      response_.socket_address = HostPortPair::FromAddrInfo(address.head());
      next_state_ = STATE_CTRL_READ;
    }
  }
  return result;
}

int FtpNetworkTransaction::DoCtrlRead() {
  next_state_ = STATE_CTRL_READ_COMPLETE;
  return ctrl_socket_->Read(
      read_ctrl_buf_,
      kCtrlBufLen,
      &io_callback_);
}

int FtpNetworkTransaction::DoCtrlReadComplete(int result) {
  if (result == 0) {
    // Some servers (for example Pure-FTPd) apparently close the control
    // connection when anonymous login is not permitted. For more details
    // see http://crbug.com/25023.
    if (command_sent_ == COMMAND_USER && username_ == ASCIIToUTF16("anonymous"))
      response_.needs_auth = true;
    return Stop(ERR_EMPTY_RESPONSE);
  }
  if (result < 0)
    return Stop(result);

  ctrl_response_buffer_->ConsumeData(read_ctrl_buf_->data(), result);

  if (!ctrl_response_buffer_->ResponseAvailable()) {
    // Read more data from the control socket.
    next_state_ = STATE_CTRL_READ;
    return OK;
  }

  return ProcessCtrlResponse();
}

int FtpNetworkTransaction::DoCtrlWrite() {
  next_state_ = STATE_CTRL_WRITE_COMPLETE;

  return ctrl_socket_->Write(write_buf_,
                             write_buf_->BytesRemaining(),
                             &io_callback_);
}

int FtpNetworkTransaction::DoCtrlWriteComplete(int result) {
  if (result < 0)
    return result;

  write_buf_->DidConsume(result);
  if (write_buf_->BytesRemaining() == 0) {
    // Clear the write buffer.
    write_buf_ = NULL;
    write_command_buf_ = NULL;

    next_state_ = STATE_CTRL_READ;
  } else {
    next_state_ = STATE_CTRL_WRITE;
  }
  return OK;
}

// FTP Commands and responses

// USER Command.
int FtpNetworkTransaction::DoCtrlWriteUSER() {
  std::string command = "USER " + UTF16ToUTF8(username_);

  if (!IsValidFTPCommandString(command))
    return Stop(ERR_MALFORMED_IDENTITY);

  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_USER);
}

int FtpNetworkTransaction::ProcessResponseUSER(
    const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_OK:
      next_state_ = STATE_CTRL_WRITE_SYST;
      break;
    case ERROR_CLASS_INFO_NEEDED:
      next_state_ = STATE_CTRL_WRITE_PASS;
      break;
    case ERROR_CLASS_TRANSIENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_PERMANENT_ERROR:
      response_.needs_auth = true;
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }
  return OK;
}

// PASS command.
int FtpNetworkTransaction::DoCtrlWritePASS() {
  std::string command = "PASS " + UTF16ToUTF8(password_);

  if (!IsValidFTPCommandString(command))
    return Stop(ERR_MALFORMED_IDENTITY);

  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_PASS);
}

int FtpNetworkTransaction::ProcessResponsePASS(
    const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_OK:
      next_state_ = STATE_CTRL_WRITE_SYST;
      break;
    case ERROR_CLASS_INFO_NEEDED:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_TRANSIENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_PERMANENT_ERROR:
      response_.needs_auth = true;
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }
  return OK;
}

// SYST command.
int FtpNetworkTransaction::DoCtrlWriteSYST() {
  std::string command = "SYST";
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_SYST);
}

int FtpNetworkTransaction::ProcessResponseSYST(
    const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_INITIATED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_OK: {
      // All important info should be on the first line.
      std::string line = response.lines[0];
      // The response should be ASCII, which allows us to do case-insensitive
      // comparisons easily. If it is not ASCII, we leave the system type
      // as unknown.
      if (IsStringASCII(line)) {
        line = StringToLowerASCII(line);
        // The "magic" strings we test for below have been gathered by an
        // empirical study.
        if (line.find("l8") != std::string::npos ||
            line.find("unix") != std::string::npos ||
            line.find("bsd") != std::string::npos) {
          system_type_ = SYSTEM_TYPE_UNIX;
        } else if (line.find("win32") != std::string::npos ||
                   line.find("windows") != std::string::npos) {
          system_type_ = SYSTEM_TYPE_WINDOWS;
        } else if (line.find("os/2") != std::string::npos) {
          system_type_ = SYSTEM_TYPE_OS2;
        } else if (line.find("vms") != std::string::npos) {
          system_type_ = SYSTEM_TYPE_VMS;
        }
      }
      next_state_ = STATE_CTRL_WRITE_PWD;
      break;
    }
    case ERROR_CLASS_INFO_NEEDED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_TRANSIENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_PERMANENT_ERROR:
      // Server does not recognize the SYST command so proceed.
      next_state_ = STATE_CTRL_WRITE_PWD;
      break;
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }
  return OK;
}

// PWD command.
int FtpNetworkTransaction::DoCtrlWritePWD() {
  std::string command = "PWD";
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_PWD);
}

int FtpNetworkTransaction::ProcessResponsePWD(const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_INITIATED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_OK: {
      // The info we look for should be on the first line.
      std::string line = response.lines[0];
      if (line.empty())
        return Stop(ERR_INVALID_RESPONSE);
      std::string::size_type quote_pos = line.find('"');
      if (quote_pos != std::string::npos) {
        line = line.substr(quote_pos + 1);
        quote_pos = line.find('"');
        if (quote_pos == std::string::npos)
          return Stop(ERR_INVALID_RESPONSE);
        line = line.substr(0, quote_pos);
      }
      if (system_type_ == SYSTEM_TYPE_VMS)
        line = FtpUtil::VMSPathToUnix(line);
      if (line.length() && line[line.length() - 1] == '/')
        line.erase(line.length() - 1);
      current_remote_directory_ = line;
      next_state_ = STATE_CTRL_WRITE_TYPE;
      break;
    }
    case ERROR_CLASS_INFO_NEEDED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_TRANSIENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_PERMANENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }
  return OK;
}

// TYPE command.
int FtpNetworkTransaction::DoCtrlWriteTYPE() {
  std::string command = "TYPE ";
  if (data_type_ == DATA_TYPE_ASCII) {
    command += "A";
  } else if (data_type_ == DATA_TYPE_IMAGE) {
    command += "I";
  } else {
    NOTREACHED();
    return Stop(ERR_UNEXPECTED);
  }
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_TYPE);
}

int FtpNetworkTransaction::ProcessResponseTYPE(
    const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_INITIATED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_OK:
      next_state_ = use_epsv_ ? STATE_CTRL_WRITE_EPSV : STATE_CTRL_WRITE_PASV;
      break;
    case ERROR_CLASS_INFO_NEEDED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_TRANSIENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_PERMANENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }
  return OK;
}

// EPSV command
int FtpNetworkTransaction::DoCtrlWriteEPSV() {
  const std::string command = "EPSV";
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_EPSV);
}

int FtpNetworkTransaction::ProcessResponseEPSV(
    const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_INITIATED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_OK:
      if (!ExtractPortFromEPSVResponse( response, &data_connection_port_))
        return Stop(ERR_INVALID_RESPONSE);
      if (data_connection_port_ < 1024 ||
          !IsPortAllowedByFtp(data_connection_port_))
        return Stop(ERR_UNSAFE_PORT);
      next_state_ = STATE_DATA_CONNECT;
      break;
    case ERROR_CLASS_INFO_NEEDED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_TRANSIENT_ERROR:
    case ERROR_CLASS_PERMANENT_ERROR:
      use_epsv_ = false;
      next_state_ = STATE_CTRL_WRITE_PASV;
      return OK;
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }
  return OK;
}

// PASV command
int FtpNetworkTransaction::DoCtrlWritePASV() {
  std::string command = "PASV";
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_PASV);
}

int FtpNetworkTransaction::ProcessResponsePASV(
    const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_INITIATED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_OK:
      if (!ExtractPortFromPASVResponse(response, &data_connection_port_))
        return Stop(ERR_INVALID_RESPONSE);
      if (data_connection_port_ < 1024 ||
          !IsPortAllowedByFtp(data_connection_port_))
        return Stop(ERR_UNSAFE_PORT);
      next_state_ = STATE_DATA_CONNECT;
      break;
    case ERROR_CLASS_INFO_NEEDED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_TRANSIENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_PERMANENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }
  return OK;
}

// RETR command
int FtpNetworkTransaction::DoCtrlWriteRETR() {
  std::string command = "RETR " + GetRequestPathForFtpCommand(false);
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_RETR);
}

int FtpNetworkTransaction::ProcessResponseRETR(
    const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_INITIATED:
      // We want the client to start reading the response at this point.
      // It got here either through Start or RestartWithAuth. We want that
      // method to complete. Not setting next state here will make DoLoop exit
      // and in turn make Start/RestartWithAuth complete.
      resource_type_ = RESOURCE_TYPE_FILE;
      break;
    case ERROR_CLASS_OK:
      resource_type_ = RESOURCE_TYPE_FILE;
      next_state_ = STATE_CTRL_WRITE_QUIT;
      break;
    case ERROR_CLASS_INFO_NEEDED:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_TRANSIENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_PERMANENT_ERROR:
      // Code 550 means "Failed to open file". Other codes are unrelated,
      // like "Not logged in" etc.
      if (response.status_code != 550 || resource_type_ == RESOURCE_TYPE_FILE)
        return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));

      // It's possible that RETR failed because the path is a directory.
      resource_type_ = RESOURCE_TYPE_DIRECTORY;

      // We're going to try CWD next, but first send a PASV one more time,
      // because some FTP servers, including FileZilla, require that.
      // See http://crbug.com/25316.
      next_state_ = use_epsv_ ? STATE_CTRL_WRITE_EPSV : STATE_CTRL_WRITE_PASV;
      break;
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }

  // We should be sure about our resource type now. Otherwise we risk
  // an infinite loop (RETR can later send CWD, and CWD can later send RETR).
  DCHECK_NE(RESOURCE_TYPE_UNKNOWN, resource_type_);

  return OK;
}

// SIZE command
int FtpNetworkTransaction::DoCtrlWriteSIZE() {
  std::string command = "SIZE " + GetRequestPathForFtpCommand(false);
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_SIZE);
}

int FtpNetworkTransaction::ProcessResponseSIZE(
    const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_INITIATED:
      break;
    case ERROR_CLASS_OK:
      if (response.lines.size() != 1)
        return Stop(ERR_INVALID_RESPONSE);
      int64 size;
      if (!base::StringToInt64(response.lines[0], &size))
        return Stop(ERR_INVALID_RESPONSE);
      if (size < 0)
        return Stop(ERR_INVALID_RESPONSE);

      // A successful response to SIZE does not mean the resource is a file.
      // Some FTP servers (for example, the qnx one) send a SIZE even for
      // directories.
      response_.expected_content_size = size;
      break;
    case ERROR_CLASS_INFO_NEEDED:
      break;
    case ERROR_CLASS_TRANSIENT_ERROR:
      break;
    case ERROR_CLASS_PERMANENT_ERROR:
      // It's possible that SIZE failed because the path is a directory.
      if (resource_type_ == RESOURCE_TYPE_UNKNOWN &&
          response.status_code != 550) {
        return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
      }
      break;
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }

  if (resource_type_ == RESOURCE_TYPE_FILE)
    next_state_ = STATE_CTRL_WRITE_RETR;
  else
    next_state_ = STATE_CTRL_WRITE_CWD;

  return OK;
}

// CWD command
int FtpNetworkTransaction::DoCtrlWriteCWD() {
  std::string command = "CWD " + GetRequestPathForFtpCommand(true);
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_CWD);
}

int FtpNetworkTransaction::ProcessResponseCWD(const FtpCtrlResponse& response) {
  // We should never issue CWD if we know the target resource is a file.
  DCHECK_NE(RESOURCE_TYPE_FILE, resource_type_);

  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_INITIATED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_OK:
      next_state_ = STATE_CTRL_WRITE_LIST;
      break;
    case ERROR_CLASS_INFO_NEEDED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_TRANSIENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_PERMANENT_ERROR:
      if (response.status_code == 550) {
        if (resource_type_ == RESOURCE_TYPE_DIRECTORY) {
          // We're assuming that the resource is a directory, but the server
          // says it's not true. The most probable interpretation is that it
          // doesn't exist (with FTP we can't be sure).
          return Stop(ERR_FILE_NOT_FOUND);
        }

        // We are here because SIZE failed and we are not sure what the resource
        // type is. It could still be file, and SIZE could fail because of
        // an access error (http://crbug.com/56734). Try RETR just to be sure.
        resource_type_ = RESOURCE_TYPE_FILE;
        next_state_ = STATE_CTRL_WRITE_RETR;
        return OK;
      }

      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }
  return OK;
}

// LIST command
int FtpNetworkTransaction::DoCtrlWriteLIST() {
  std::string command(system_type_ == SYSTEM_TYPE_VMS ? "LIST *.*;0" : "LIST");
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_LIST);
}

int FtpNetworkTransaction::ProcessResponseLIST(
    const FtpCtrlResponse& response) {
  switch (GetErrorClass(response.status_code)) {
    case ERROR_CLASS_INITIATED:
      // We want the client to start reading the response at this point.
      // It got here either through Start or RestartWithAuth. We want that
      // method to complete. Not setting next state here will make DoLoop exit
      // and in turn make Start/RestartWithAuth complete.
      response_.is_directory_listing = true;
      break;
    case ERROR_CLASS_OK:
      response_.is_directory_listing = true;
      next_state_ = STATE_CTRL_WRITE_QUIT;
      break;
    case ERROR_CLASS_INFO_NEEDED:
      return Stop(ERR_INVALID_RESPONSE);
    case ERROR_CLASS_TRANSIENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    case ERROR_CLASS_PERMANENT_ERROR:
      return Stop(GetNetErrorCodeForFtpResponseCode(response.status_code));
    default:
      NOTREACHED();
      return Stop(ERR_UNEXPECTED);
  }
  return OK;
}

// QUIT command
int FtpNetworkTransaction::DoCtrlWriteQUIT() {
  std::string command = "QUIT";
  next_state_ = STATE_CTRL_READ;
  return SendFtpCommand(command, COMMAND_QUIT);
}

int FtpNetworkTransaction::ProcessResponseQUIT(
    const FtpCtrlResponse& response) {
  ctrl_socket_->Disconnect();
  return last_error_;
}

// Data Connection

int FtpNetworkTransaction::DoDataConnect() {
  next_state_ = STATE_DATA_CONNECT_COMPLETE;
  AddressList data_address;
  // Connect to the same host as the control socket to prevent PASV port
  // scanning attacks.
  int rv = ctrl_socket_->GetPeerAddress(&data_address);
  if (rv != OK)
    return Stop(rv);
  data_address.SetPort(data_connection_port_);
  data_socket_.reset(socket_factory_->CreateTransportClientSocket(
        data_address, net_log_.net_log(), net_log_.source()));
  return data_socket_->Connect(&io_callback_);
}

int FtpNetworkTransaction::DoDataConnectComplete(int result) {
  if (result != OK && use_epsv_) {
    // It's possible we hit a broken server, sadly. They can break in different
    // ways. Some time out, some reset a connection. Fall back to PASV.
    // TODO(phajdan.jr): remember it for future transactions with this server.
    // TODO(phajdan.jr): write a test for this code path.
    use_epsv_ = false;
    next_state_ = STATE_CTRL_WRITE_PASV;
    return OK;
  }

  // Only record the connection error after we've applied all our fallbacks.
  // We want to capture the final error, one we're not going to recover from.
  RecordDataConnectionError(result);

  if (result != OK)
    return Stop(result);

  next_state_ = STATE_CTRL_WRITE_SIZE;
  return OK;
}

int FtpNetworkTransaction::DoDataRead() {
  DCHECK(read_data_buf_);
  DCHECK_GT(read_data_buf_len_, 0);

  if (data_socket_ == NULL || !data_socket_->IsConnected()) {
    // If we don't destroy the data socket completely, some servers will wait
    // for us (http://crbug.com/21127). The half-closed TCP connection needs
    // to be closed on our side too.
    data_socket_.reset();

    if (ctrl_socket_->IsConnected()) {
      // Wait for the server's response, we should get it before sending QUIT.
      next_state_ = STATE_CTRL_READ;
      return OK;
    }

    // We are no longer connected to the server, so just finish the transaction.
    return Stop(OK);
  }

  next_state_ = STATE_DATA_READ_COMPLETE;
  read_data_buf_->data()[0] = 0;
  return data_socket_->Read(read_data_buf_, read_data_buf_len_,
                            &io_callback_);
}

int FtpNetworkTransaction::DoDataReadComplete(int result) {
  return result;
}

// We're using a histogram as a group of counters, with one bucket for each
// enumeration value.  We're only interested in the values of the counters.
// Ignore the shape, average, and standard deviation of the histograms because
// they are meaningless.
//
// We use two histograms.  In the first histogram we tally whether the user has
// seen an error of that type during the session.  In the second histogram we
// tally the total number of times the users sees each errer.
void FtpNetworkTransaction::RecordDataConnectionError(int result) {
  // Gather data for http://crbug.com/3073. See how many users have trouble
  // establishing FTP data connection in passive FTP mode.
  enum {
    // Data connection successful.
    NET_ERROR_OK = 0,

    // Local firewall blocked the connection.
    NET_ERROR_ACCESS_DENIED = 1,

    // Connection timed out.
    NET_ERROR_TIMED_OUT = 2,

    // Connection has been estabilished, but then got broken (either reset
    // or aborted).
    NET_ERROR_CONNECTION_BROKEN = 3,

    // Connection has been refused.
    NET_ERROR_CONNECTION_REFUSED = 4,

    // No connection to the internet.
    NET_ERROR_INTERNET_DISCONNECTED = 5,

    // Could not reach the destination address.
    NET_ERROR_ADDRESS_UNREACHABLE = 6,

    // A programming error in our network stack.
    NET_ERROR_UNEXPECTED = 7,

    // Other kind of error.
    NET_ERROR_OTHER = 20,

    NUM_OF_NET_ERROR_TYPES
  } type;
  switch (result) {
    case OK:
      type = NET_ERROR_OK;
      break;
    case ERR_ACCESS_DENIED:
    case ERR_NETWORK_ACCESS_DENIED:
      type = NET_ERROR_ACCESS_DENIED;
      break;
    case ERR_TIMED_OUT:
      type = NET_ERROR_TIMED_OUT;
      break;
    case ERR_CONNECTION_ABORTED:
    case ERR_CONNECTION_RESET:
    case ERR_CONNECTION_CLOSED:
      type = NET_ERROR_CONNECTION_BROKEN;
      break;
    case ERR_CONNECTION_FAILED:
    case ERR_CONNECTION_REFUSED:
      type = NET_ERROR_CONNECTION_REFUSED;
      break;
    case ERR_INTERNET_DISCONNECTED:
      type = NET_ERROR_INTERNET_DISCONNECTED;
      break;
    case ERR_ADDRESS_INVALID:
    case ERR_ADDRESS_UNREACHABLE:
      type = NET_ERROR_ADDRESS_UNREACHABLE;
      break;
    case ERR_UNEXPECTED:
      type = NET_ERROR_UNEXPECTED;
      break;
    default:
      type = NET_ERROR_OTHER;
      break;
  };
  static bool had_error_type[NUM_OF_NET_ERROR_TYPES];

  DCHECK(type >= 0 && type < NUM_OF_NET_ERROR_TYPES);
  if (!had_error_type[type]) {
    had_error_type[type] = true;
    UMA_HISTOGRAM_ENUMERATION("Net.FtpDataConnectionErrorHappened",
        type, NUM_OF_NET_ERROR_TYPES);
  }
  UMA_HISTOGRAM_ENUMERATION("Net.FtpDataConnectionErrorCount",
      type, NUM_OF_NET_ERROR_TYPES);
}

}  // namespace net
