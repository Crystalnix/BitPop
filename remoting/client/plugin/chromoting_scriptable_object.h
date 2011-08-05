// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This implements the JavaScript class entrypoint for the plugin instance.
// The Javascript API is defined as follows.
//
// interface ChromotingScriptableObject {
//
//   // Chromoting session API version (for this plugin).
//   // This is compared with the javascript API version to verify that they are
//   // compatible.
//   readonly attribute unsigned short apiVersion;
//
//   // The oldest API version that we support.
//   // This will differ from |apiVersion| if we decide to maintain backward
//   // compatibility with older API versions.
//   readonly attribute unsigned short apiMinVersion;
//
//   // Connection status.
//   readonly attribute unsigned short status;
//
//   // Constants for connection status.
//   const unsigned short STATUS_UNKNOWN = 0;
//   const unsigned short STATUS_CONNECTING = 1;
//   const unsigned short STATUS_INITIALIZING = 2;
//   const unsigned short STATUS_CONNECTED = 3;
//   const unsigned short STATUS_CLOSED = 4;
//   const unsigned short STATUS_FAILED = 5;
//
//   // Connection quality.
//   readonly attribute unsigned short quality;
//
//   // Constants for connection quality
//   const unsigned short QUALITY_UNKNOWN = 0;
//   const unsigned short QUALITY_GOOD = 1;
//   const unsigned short QUALITY_BAD = 2;
//
//   // JS callback function so we can signal the JS UI when the connection
//   // status has been updated.
//   attribute Function connectionInfoUpdate;
//
//   // JS callback function to call when there is new debug info to display
//   // in the client UI.
//   attribute Function debugInfo;
//
//   attribute Function desktopSizeUpdate;
//
//   // This function is called when login information for the host machine is
//   // needed.
//   //
//   // User of this object should respond with calling submitLoginInfo() when
//   // username and password is available.
//   //
//   // This function will be called multiple times until login was successful
//   // or the maximum number of login attempts has been reached. In the
//   // later case |connection_status| is changed to STATUS_FAILED.
//   attribute Function loginChallenge;
//
//   // JS callback function to send an XMPP IQ stanza for performing the
//   // signaling in a jingle connection.  The callback function should be
//   // of type void(string request_xml).
//   attribute Function sendIq;
//
//   // Dimension of the desktop area.
//   readonly attribute int desktopWidth;
//   readonly attribute int desktopHeight;
//
//   // Statistics.
//   // Video Bandwidth in bytes per second.
//   readonly attribute float videoBandwidth;
//   // Latency for capturing in milliseconds.
//   readonly attribute int videoCaptureLatency;
//   // Latency for video encoding in milliseconds.
//   readonly attribute int videoEncodeLatency;
//   // Latency for video decoding in milliseconds.
//   readonly attribute int videoDecodeLatency;
//   // Latency for rendering in milliseconds.
//   readonly attribute int videoRenderLatency;
//   // Latency between an event is sent and a corresponding video packet is
//   // received.
//   readonly attribute int roundTripLatency;
//
//   // Methods for establishing a Chromoting connection.
//   //
//   // When using the sandboxed versions, sendIq must be set and responses to
//   // calls on sendIq must be piped back into onIq().
//   //
//   // Note that auth_token_with_service should be specified as
//   // "auth_service:auth_token". For example, "oauth2:5/aBd123".
//   void connect(string host_jid, string auth_token_with_service,
//                optional string access_code);
//   // Non-sandboxed version used for debugging/testing.
//   // TODO(garykac): Remove this version once we no longer need it.
//   void connectUnsandboxed(string host_jid, string username,
//                           string xmpp_token, optional string access_code);
//
//   // Terminating a Chromoting connection.
//   void disconnect();
//
//   // Method for submitting login information.
//   void submitLoginInfo(string username, string password);
//
//   // Method for setting scale-to-fit.
//   void setScaleToFit(bool scale_to_fit);
//
//   // Method for receiving an XMPP IQ stanza in response to a previous
//   // sendIq() invocation. Other packets will be silently dropped.
//   void onIq(string response_xml);
// }

#ifndef REMOTING_CLIENT_PLUGIN_CHROMOTING_SCRIPTABLE_OBJECT_H_
#define REMOTING_CLIENT_PLUGIN_CHROMOTING_SCRIPTABLE_OBJECT_H_

#include <map>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"

#include "ppapi/cpp/dev/scriptable_object_deprecated.h"
#include "ppapi/cpp/var.h"

namespace remoting {

class ChromotingInstance;
class PepperXmppProxy;

enum ConnectionStatus {
  STATUS_UNKNOWN = 0,
  STATUS_CONNECTING,
  STATUS_INITIALIZING,
  STATUS_CONNECTED,
  STATUS_CLOSED,
  STATUS_FAILED,
};

enum ConnectionQuality {
  QUALITY_UNKNOWN = 0,
  QUALITY_GOOD,
  QUALITY_BAD,
};

class ChromotingScriptableObject
    : public pp::deprecated::ScriptableObject,
      public base::SupportsWeakPtr<ChromotingScriptableObject> {
 public:
  explicit ChromotingScriptableObject(ChromotingInstance* instance);
  virtual ~ChromotingScriptableObject();

  virtual void Init();

  // Override the ScriptableObject functions.
  virtual bool HasProperty(const pp::Var& name, pp::Var* exception);
  virtual bool HasMethod(const pp::Var& name, pp::Var* exception);
  virtual pp::Var GetProperty(const pp::Var& name, pp::Var* exception);
  virtual void GetAllPropertyNames(std::vector<pp::Var>* properties,
                                   pp::Var* exception);
  virtual void SetProperty(const pp::Var& name,
                           const pp::Var& value,
                           pp::Var* exception);
  virtual pp::Var Call(const pp::Var& method_name,
                       const std::vector<pp::Var>& args,
                       pp::Var* exception);

  void SetConnectionInfo(ConnectionStatus status, ConnectionQuality quality);
  void LogDebugInfo(const std::string& info);
  void SetDesktopSize(int width, int height);

  // This should be called to signal JS code to provide login information.
  void SignalLoginChallenge();

  // Attaches the XmppProxy used for issuing and receivng IQ stanzas for
  // initializing a jingle connection from within the sandbox.
  void AttachXmppProxy(PepperXmppProxy* xmpp_proxy);

  // Sends an IQ stanza, serialized as an xml string, into Javascript for
  // handling.
  void SendIq(const std::string& request_xml);

 private:
  typedef std::map<std::string, int> PropertyNameMap;
  typedef pp::Var (ChromotingScriptableObject::*MethodHandler)(
      const std::vector<pp::Var>& args, pp::Var* exception);
  struct PropertyDescriptor {
    explicit PropertyDescriptor(const std::string& n, pp::Var a)
        : name(n), attribute(a), method(NULL) {
    }

    explicit PropertyDescriptor(const std::string& n, MethodHandler m)
        : name(n), method(m) {
    }

    enum Type {
      ATTRIBUTE,
      METHOD,
    } type;

    std::string name;
    pp::Var attribute;
    MethodHandler method;
  };

  // Routines to add new attribute, method properties.
  void AddAttribute(const std::string& name, pp::Var attribute);
  void AddMethod(const std::string& name, MethodHandler handler);

  // This should be called to signal the JS code that the connection status has
  // changed.
  void SignalConnectionInfoChange();

  // Signal the JS code that the desktop size has changed.
  void SignalDesktopSizeChange();

  pp::Var DoConnect(const std::vector<pp::Var>& args, pp::Var* exception);
  pp::Var DoConnectUnsandboxed(const std::vector<pp::Var>& args,
                               pp::Var* exception);
  pp::Var DoDisconnect(const std::vector<pp::Var>& args, pp::Var* exception);

  // This method is called by JS to provide login information.
  pp::Var DoSubmitLogin(const std::vector<pp::Var>& args, pp::Var* exception);

  // This method is called by JS to set scale-to-fit.
  pp::Var DoSetScaleToFit(const std::vector<pp::Var>& args, pp::Var* exception);

  // This method is caleld by Javascript to provide responses to sendIq()
  // requests when establishing a sandboxed Chromoting connection.
  pp::Var DoOnIq(const std::vector<pp::Var>& args, pp::Var* exception);

  PropertyNameMap property_names_;
  std::vector<PropertyDescriptor> properties_;
  scoped_refptr<PepperXmppProxy> xmpp_proxy_;

  ChromotingInstance* instance_;
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_PLUGIN_CHROMOTING_SCRIPTABLE_OBJECT_H_
