// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(ajwong): We need to come up with a better description of the
// responsibilities for each thread.

#ifndef REMOTING_CLIENT_PLUGIN_CHROMOTING_INSTANCE_H_
#define REMOTING_CLIENT_PLUGIN_CHROMOTING_INSTANCE_H_

#include <string>

#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_rect.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/private/instance_private.h"
#include "remoting/base/scoped_thread_proxy.h"
#include "remoting/client/client_context.h"
#include "remoting/client/plugin/chromoting_scriptable_object.h"
#include "remoting/client/plugin/pepper_plugin_thread_delegate.h"
#include "remoting/protocol/connection_to_host.h"

namespace pp {
class InputEvent;
class Module;
}  // namespace pp

namespace remoting {

namespace protocol {
class ConnectionToHost;
class KeyEventTracker;
}  // namespace protocol

class ChromotingClient;
class ChromotingStats;
class ClientContext;
class FrameConsumerProxy;
class MouseInputFilter;
class PepperInputHandler;
class PepperView;
class RectangleUpdateDecoder;

struct ClientConfig;

class ChromotingInstance : public pp::InstancePrivate {
 public:
  // The mimetype for which this plugin is registered.
  static const char kMimeType[];

  explicit ChromotingInstance(PP_Instance instance);
  virtual ~ChromotingInstance();

  // pp::Instance interface.
  virtual void DidChangeView(const pp::Rect& position,
                             const pp::Rect& clip) OVERRIDE;
  virtual bool Init(uint32_t argc, const char* argn[],
                    const char* argv[]) OVERRIDE;
  virtual bool HandleInputEvent(const pp::InputEvent& event) OVERRIDE;

  // pp::InstancePrivate interface.
  virtual pp::Var GetInstanceObject() OVERRIDE;

  // Convenience wrapper to get the ChromotingScriptableObject.
  ChromotingScriptableObject* GetScriptableObject();

  // Initiates and cancels connections.
  void Connect(const ClientConfig& config);
  void Disconnect();

  // Called by ChromotingScriptableObject to set scale-to-fit.
  void SetScaleToFit(bool scale_to_fit);

  // Return statistics record by ChromotingClient.
  // If no connection is currently active then NULL will be returned.
  ChromotingStats* GetStats();

  void ReleaseAllKeys();

  bool DoScaling() const { return scale_to_fit_; }

  // Registers a global log message handler that redirects the log output to
  // our plugin instance.
  // This is called by the plugin's PPP_InitializeModule.
  // Note that no logging will be processed unless a ChromotingInstance has been
  // registered for logging (see RegisterLoggingInstance).
  static void RegisterLogMessageHandler();

  // Registers this instance so it processes messages sent by the global log
  // message handler. This overwrites any previously registered instance.
  void RegisterLoggingInstance();

  // Unregisters this instance so that debug log messages will no longer be sent
  // to it. If this instance is not the currently registered logging instance,
  // then the currently registered instance will stay in effect.
  void UnregisterLoggingInstance();

  // A Log Message Handler that is called after each LOG message has been
  // processed. This must be of type LogMessageHandlerFunction defined in
  // base/logging.h.
  static bool LogToUI(int severity, const char* file, int line,
                      size_t message_start, const std::string& str);

 private:
  FRIEND_TEST_ALL_PREFIXES(ChromotingInstanceTest, TestCaseSetup);

  void ProcessLogToUI(const std::string& message);

  bool initialized_;

  PepperPluginThreadDelegate plugin_thread_delegate_;
  scoped_refptr<PluginMessageLoopProxy> plugin_message_loop_;
  ClientContext context_;
  scoped_ptr<protocol::ConnectionToHost> host_connection_;
  scoped_ptr<PepperView> view_;

  // True if scale to fit is enabled.
  bool scale_to_fit_;

  scoped_refptr<FrameConsumerProxy> consumer_proxy_;
  scoped_refptr<RectangleUpdateDecoder> rectangle_decoder_;
  scoped_ptr<MouseInputFilter> mouse_input_filter_;
  scoped_ptr<protocol::KeyEventTracker> key_event_tracker_;
  scoped_ptr<PepperInputHandler> input_handler_;
  scoped_ptr<ChromotingClient> client_;

  // XmppProxy is a refcounted interface used to perform thread-switching and
  // detaching between objects whose lifetimes are controlled by pepper, and
  // jingle_glue objects. This is used when if we start a sandboxed jingle
  // connection.
  scoped_refptr<PepperXmppProxy> xmpp_proxy_;

  // JavaScript interface to control this instance.
  // This wraps a ChromotingScriptableObject in a pp::Var.
  pp::Var instance_object_;

  scoped_ptr<ScopedThreadProxy> thread_proxy_;

  DISALLOW_COPY_AND_ASSIGN(ChromotingInstance);
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_PLUGIN_CHROMOTING_INSTANCE_H_
