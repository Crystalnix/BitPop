// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_FRAME_TEST_TEST_SERVER_H_
#define CHROME_FRAME_TEST_TEST_SERVER_H_

// Implementation of an HTTP server for tests.
// To instantiate the server, make sure you have a message loop on the
// current thread and then create an instance of the SimpleWebServer class.
// The server uses two basic concepts, a request and a response.
// The Response interface represents an item (e.g. a document) available from
// the server.  A Request object represents a request from a client (e.g. a
// browser).  There are several basic Response classes implemented in this file,
// all derived from the Response interface.
//
// Here's a simple example that starts a web server that can serve up
// a single document (http://localhost:1337/foo).
// All other requests will get a 404.
//
//  MessageLoopForUI loop;
//  test_server::SimpleWebServer server(1337);
//  test_server::SimpleResponse document("/foo", "Hello World!");
//  test_server.AddResponse(&document);
//  loop.MessageLoop::Run();
//
// To close the web server, just go to http://localhost:1337/quit.
//
// All Response classes count how many times they have been accessed.  Just
// call Response::accessed().
//
// To implement a custom response object (e.g. to match against a request
// based on some data, serve up dynamic content or take some action on the
// server), just inherit from  one of the response classes or directly from the
// Response interface and add your response object to the server's list of
// response objects.

#include <list>
#include <string>

#include "base/basictypes.h"
#include "base/file_util.h"
#include "net/base/listen_socket.h"

namespace test_server {

class Request {
 public:
  Request() : content_length_(0) {
  }

  void ParseHeaders(const std::string& headers);

  const std::string& method() const {
    return method_;
  }

  const std::string& path() const {
    return path_;
  }

  // Returns the argument section of a GET path.
  // Note: does currently not work for POST request.
  std::string arguments() const {
    std::string ret;
    std::string::size_type pos = path_.find('?');
    if (pos != std::string::npos)
      ret = path_.substr(pos + 1);
    return ret;
  }

  const std::string& headers() const {
    return headers_;
  }

  const std::string& content() const {
    return content_;
  }

  size_t content_length() const {
    return content_length_;
  }

  bool AllContentReceived() const {
    return method_.length() && content_.size() >= content_length_;
  }

  void OnDataReceived(const std::string& data);

 protected:
  std::string method_;
  std::string path_;
  std::string version_;
  std::string headers_;
  std::string content_;
  size_t content_length_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Request);
};

// Manages request headers for a single request.
// For each successful request that's made, the server will keep an instance
// of this class so that they can be checked even after the server has been
// shut down.
class Connection {
 public:
  explicit Connection(net::ListenSocket* sock) : socket_(sock) {
  }

  ~Connection() {
  }

  bool IsSame(const net::ListenSocket* socket) const {
    return socket_ == socket;
  }

  const Request& request() const {
    return request_;
  }

  Request& request() {
    return request_;
  }

 protected:
  scoped_refptr<net::ListenSocket> socket_;
  Request request_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Connection);
};

// Abstract interface with default implementations for some of the methods and
// a counter for how many times the response object has served requests.
class Response {
 public:
  Response() : accessed_(0) {
  }

  virtual ~Response() {
  }

  // Returns true if this response object should be used for a given request.
  virtual bool Matches(const Request& r) const = 0;

  // Response objects can optionally supply their own HTTP headers, completely
  // bypassing the default ones.
  virtual bool GetCustomHeaders(std::string* headers) const {
    return false;
  }

  // Optionally provide a content type.  Return false if you don't specify
  // a content type.
  virtual bool GetContentType(std::string* content_type) const {
    return false;
  }

  virtual size_t ContentLength() const {
    return 0;
  }

  virtual void WriteContents(net::ListenSocket* socket) const {
  }

  virtual void IncrementAccessCounter() {
    accessed_++;
  }

  size_t accessed() const {
    return accessed_;
  }

 protected:
  size_t accessed_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Response);
};

// Partial implementation of Response that matches a request's path.
// This is just a convenience implementation for the boilerplate implementation
// of Matches().  Don't instantiate directly.
class ResponseForPath : public Response {
 public:
  explicit ResponseForPath(const char* request_path)
      : request_path_(request_path) {
  }

  virtual bool Matches(const Request& r) const {
    std::string path = r.path();
    std::string::size_type pos = path.find('?');
    if (pos != std::string::npos)
      path = path.substr(0, pos);
    return path.compare(request_path_) == 0;
  }

 protected:
   std::string request_path_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ResponseForPath);
};

// A very basic implementation of a response.
// A simple response matches a single document path on the server
// (e.g. "/foo") and returns a document in the form of a string.
class SimpleResponse : public ResponseForPath {
 public:
  SimpleResponse(const char* request_path, const std::string& contents)
      : ResponseForPath(request_path), contents_(contents) {
  }

  virtual void WriteContents(net::ListenSocket* socket) const {
    socket->Send(contents_.c_str(), contents_.length(), false);
  }

  virtual size_t ContentLength() const {
    return contents_.length();
  }

 protected:
  std::string contents_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SimpleResponse);
};

// To serve up files from the web server, create an instance of FileResponse
// and add it to the server's list of responses.  The content type of the
// file will be determined by calling FindMimeFromData which examines the
// contents of the file and performs registry lookups.
class FileResponse : public ResponseForPath {
 public:
  FileResponse(const char* request_path, const FilePath& file_path)
      : ResponseForPath(request_path), file_path_(file_path) {
  }

  virtual bool GetContentType(std::string* content_type) const;
  virtual void WriteContents(net::ListenSocket* socket) const;
  virtual size_t ContentLength() const;

 protected:
  FilePath file_path_;
  mutable scoped_ptr<file_util::MemoryMappedFile> file_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FileResponse);
};

// Returns a 302 (temporary redirect) to redirect the client from a path
// on the test server to a different URL.
class RedirectResponse : public ResponseForPath {
 public:
  RedirectResponse(const char* request_path, const std::string& redirect_url)
      : ResponseForPath(request_path), redirect_url_(redirect_url) {
  }

  virtual bool GetCustomHeaders(std::string* headers) const;

 protected:
  std::string redirect_url_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RedirectResponse);
};

// typedef for a list of connections.  Used by SimpleWebServer.
typedef std::list<Connection*> ConnectionList;

// Implementation of a simple http server.
// Before creating an instance of the server, make sure the current thread
// has a message loop.
class SimpleWebServer : public net::ListenSocket::ListenSocketDelegate {
 public:
  explicit SimpleWebServer(int port);
  virtual ~SimpleWebServer();

  void AddResponse(Response* response);

  // Ownership of response objects is by default assumed to be outside
  // of the SimpleWebServer class.
  // However, if the caller doesn't wish to maintain a list of response objects
  // but rather let this class hold the only references to those objects,
  // the caller can call this method to delete the objects as part of
  // the cleanup process.
  void DeleteAllResponses();

  // ListenSocketDelegate overrides.
  virtual void DidAccept(net::ListenSocket* server,
                         net::ListenSocket* connection);
  virtual void DidRead(net::ListenSocket* connection,
                       const char* data,
                       int len);
  virtual void DidClose(net::ListenSocket* sock);

  const ConnectionList& connections() const {
    return connections_;
  }

 protected:
  class QuitResponse : public SimpleResponse {
   public:
    QuitResponse()
        : SimpleResponse("/quit", "So long and thanks for all the fish.") {
    }

    virtual void QuitResponse::WriteContents(net::ListenSocket* socket) const {
      SimpleResponse::WriteContents(socket);
      MessageLoop::current()->Quit();
    }
  };

  Response* FindResponse(const Request& request) const;
  Connection* FindConnection(const net::ListenSocket* socket) const;

 protected:
  scoped_refptr<net::ListenSocket> server_;
  ConnectionList connections_;
  std::list<Response*> responses_;
  QuitResponse quit_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SimpleWebServer);
};

// Simple class holding incoming HTTP request. Can send the HTTP response
// at different rate - small chunks, on regular interval.
class ConfigurableConnection : public base::RefCounted<ConfigurableConnection> {
 public:
  struct SendOptions {
    enum Speed { IMMEDIATE, DELAYED, IMMEDIATE_HEADERS_DELAYED_CONTENT };
    SendOptions() : speed_(IMMEDIATE), chunk_size_(0), timeout_(0) { }
    SendOptions(Speed speed, int chunk_size, int64 timeout)
        : speed_(speed), chunk_size_(chunk_size), timeout_(timeout) {
    }

    Speed speed_;
    int chunk_size_;
    int64 timeout_;
  };

  explicit ConfigurableConnection(net::ListenSocket* sock)
      : socket_(sock),
        cur_pos_(0) {}

  // Send HTTP response with provided |headers| and |content|. Appends
  // "Context-Length:" header if the |content| is not empty.
  void Send(const std::string& headers, const std::string& content);

  // Send HTTP response with provided |headers| and |content|. Appends
  // "Context-Length:" header if the |content| is not empty.
  // Use the |options| to tweak the network speed behaviour.
  void SendWithOptions(const std::string& headers, const std::string& content,
                       const SendOptions& options);

 private:
  friend class HTTPTestServer;
  // Sends a chunk of the response and queues itself as a task for sending
  // next chunk of |data_|.
  void SendChunk();

  scoped_refptr<net::ListenSocket> socket_;
  Request r_;
  SendOptions options_;
  std::string data_;
  int cur_pos_;

  DISALLOW_COPY_AND_ASSIGN(ConfigurableConnection);
};

// Simple class used as a base class for mock webserver.
// Override virtual functions Get and Post and use passed ConfigurableConnection
// instance to send the response.
class HTTPTestServer : public net::ListenSocket::ListenSocketDelegate {
 public:
  HTTPTestServer(int port, const std::wstring& address, FilePath root_dir);
  virtual ~HTTPTestServer();

  // HTTP GET request is received. Override in derived classes.
  // |connection| can be used to send the response.
  virtual void Get(ConfigurableConnection* connection,
                   const std::wstring& path, const Request& r) = 0;

  // HTTP POST request is received. Override in derived classes.
  // |connection| can be used to send the response
  virtual void Post(ConfigurableConnection* connection,
                    const std::wstring& path, const Request& r) = 0;

  // Return the appropriate url with the specified path for this server.
  std::wstring Resolve(const std::wstring& path);

  FilePath root_dir() { return root_dir_; }

 protected:
  int port_;
  std::wstring address_;
  FilePath root_dir_;

 private:
  typedef std::list<scoped_refptr<ConfigurableConnection> > ConnectionList;
  ConnectionList::iterator FindConnection(const net::ListenSocket* socket);
  scoped_refptr<ConfigurableConnection> ConnectionFromSocket(
      const net::ListenSocket* socket);

  // ListenSocketDelegate overrides.
  virtual void DidAccept(net::ListenSocket* server, net::ListenSocket* socket);
  virtual void DidRead(net::ListenSocket* socket, const char* data, int len);
  virtual void DidClose(net::ListenSocket* socket);

  scoped_refptr<net::ListenSocket> server_;
  ConnectionList connection_list_;

  DISALLOW_COPY_AND_ASSIGN(HTTPTestServer);
};

}  // namespace test_server

#endif  // CHROME_FRAME_TEST_TEST_SERVER_H_
