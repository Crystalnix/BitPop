// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CUSTOM_HANDLERS_PROTOCOL_HANDLER_REGISTRY_H_
#define CHROME_BROWSER_CUSTOM_HANDLERS_PROTOCOL_HANDLER_REGISTRY_H_
#pragma once

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/values.h"
#include "chrome/browser/custom_handlers/protocol_handler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/shell_integration.h"
#include "content/common/notification_service.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_job.h"

// This is where handlers for protocols registered with
// navigator.registerProtocolHandler() are registered. Each Profile owns an
// instance of this class, which is initialized on browser start through
// Profile::InitRegisteredProtocolHandlers(), and they should be the only
// instances of this class.

class ProtocolHandlerRegistry
    : public base::RefCountedThreadSafe<ProtocolHandlerRegistry> {
 public:
  // TODO(koz): Refactor this to eliminate the unnecessary virtuals. All that
  // should be needed is a way to ensure that the list of websafe protocols is
  // updated.
  class Delegate {
   public:
    virtual ~Delegate();
    virtual void RegisterExternalHandler(const std::string& protocol);
    virtual void DeregisterExternalHandler(const std::string& protocol);
    virtual bool IsExternalHandlerRegistered(const std::string& protocol);
    virtual void RegisterWithOSAsDefaultClient(const std::string& protocol);
  };

  class DefaultClientObserver
      : public ShellIntegration::DefaultWebClientObserver {
   public:
    explicit DefaultClientObserver(ProtocolHandlerRegistry* registry);
    virtual ~DefaultClientObserver();

    // Get response from the worker regarding whether Chrome is the default
    // handler for the protocol.
    virtual void SetDefaultWebClientUIState(
        ShellIntegration::DefaultWebClientUIState state);

    // Give the observer a handle to the worker, so we can find out the protocol
    // when we're called and also tell the worker if we get deleted.
    void SetWorker(ShellIntegration::DefaultProtocolClientWorker* worker);

   private:
    // This is a raw pointer, not reference counted, intentionally. In general
    // subclasses of DefaultWebClientObserver are not able to be refcounted
    // e.g. the browser options page
    ProtocolHandlerRegistry* registry_;
    scoped_refptr<ShellIntegration::DefaultProtocolClientWorker> worker_;

    DISALLOW_COPY_AND_ASSIGN(DefaultClientObserver);
  };

  typedef std::map<std::string, ProtocolHandler> ProtocolHandlerMap;
  typedef std::vector<ProtocolHandler> ProtocolHandlerList;
  typedef std::map<std::string, ProtocolHandlerList> ProtocolHandlerMultiMap;
  typedef std::vector<DefaultClientObserver*> DefaultClientObserverList;

  ProtocolHandlerRegistry(Profile* profile, Delegate* delegate);
  ~ProtocolHandlerRegistry();

  // Called when the user accepts the registration of a given protocol handler.
  void OnAcceptRegisterProtocolHandler(const ProtocolHandler& handler);

  // Called when the user denies the registration of a given protocol handler.
  void OnDenyRegisterProtocolHandler(const ProtocolHandler& handler);

  // Called when the user indicates that they don't want to be asked about the
  // given protocol handler again.
  void OnIgnoreRegisterProtocolHandler(const ProtocolHandler& handler);

  // Clears the default for the provided protocol.
  void ClearDefault(const std::string& scheme);

  // Returns true if this handler is the default handler for its protocol.
  bool IsDefault(const ProtocolHandler& handler) const;

  // Loads a user's registered protocol handlers.
  void Load();

  // Returns the offset in the list of handlers for a protocol of the default
  // handler for that protocol.
  int GetHandlerIndex(const std::string& scheme) const;

  // Get the list of protocol handlers for the given scheme.
  ProtocolHandlerList GetHandlersFor(const std::string& scheme) const;

  // Yields a list of the protocols that have handlers registered in this
  // registry.
  void GetRegisteredProtocols(std::vector<std::string>* output) const;

  // Returns true if we allow websites to register handlers for the given
  // scheme.
  bool CanSchemeBeOverridden(const std::string& scheme) const;

  // Returns true if an identical protocol handler has already been registered.
  bool IsRegistered(const ProtocolHandler& handler) const;

  // Returns true if the protocol handler is being ignored.
  bool IsIgnored(const ProtocolHandler& handler) const;

  // Causes the given protocol handler to not be ignored anymore.
  void RemoveIgnoredHandler(const ProtocolHandler& handler);

  // Returns true if the protocol has a registered protocol handler.
  bool IsHandledProtocol(const std::string& scheme) const;

  // Removes the given protocol handler from the registry.
  void RemoveHandler(const ProtocolHandler& handler);

  // Remove the default handler for the given protocol.
  void RemoveDefaultHandler(const std::string& scheme);

  // Returns the default handler for this protocol, or an empty handler if none
  // exists.
  const ProtocolHandler& GetHandlerFor(const std::string& scheme) const;

  // Creates a URL request job for the given request if there is a matching
  // protocol handler, returns NULL otherwise.
  net::URLRequestJob* MaybeCreateJob(net::URLRequest* request) const;

  // Puts this registry in the enabled state - registered protocol handlers
  // will handle requests.
  void Enable();

  // Puts this registry in the disabled state - registered protocol handlers
  // will not handle requests.
  void Disable();

  // This is called by the UI thread when the system is shutting down. This
  // does finalization which must be done on the UI thread.
  void Finalize();

  // Registers the preferences that we store registered protocol handlers in.
  static void RegisterPrefs(PrefService* prefService);

  bool enabled() const { return enabled_; }

 private:
  friend class base::RefCountedThreadSafe<ProtocolHandlerRegistry>;

  // Returns true if the protocol has a registered protocol handler.
  bool IsHandledProtocolInternal(const std::string& scheme) const;

  // Returns true if this handler is the default handler for its protocol.
  bool IsDefaultInternal(const ProtocolHandler& handler) const;

  // Returns true if we allow websites to register handlers for the given
  // scheme.
  bool CanSchemeBeOverriddenInternal(const std::string& scheme) const;

  // Returns true if an identical protocol handler has already been registered.
  bool IsRegisteredInternal(const ProtocolHandler& handler) const;

  // Returns the default handler for this protocol, or an empty handler if none
  // exists.
  const ProtocolHandler& GetHandlerForInternal(const std::string& scheme) const;

  // Removes the given protocol handler from the registry.
  void RemoveHandlerInternal(const ProtocolHandler& handler);

  // Saves a user's registered protocol handlers.
  void Save();

  // Returns a pointer to the list of handlers registered for the given scheme,
  // or NULL if there are none.
  const ProtocolHandlerList* GetHandlerList(const std::string& scheme) const;

  // Makes this ProtocolHandler the default handler for its protocol.
  void SetDefault(const ProtocolHandler& handler);

  // Insert the given ProtocolHandler into the registry.
  void InsertHandler(const ProtocolHandler& handler);

  // Returns a JSON list of protocol handlers. The caller is responsible for
  // deleting this Value.
  Value* EncodeRegisteredHandlers();

  // Returns a JSON list of ignored protocol handlers. The caller is
  // responsible for deleting this Value.
  Value* EncodeIgnoredHandlers();

  // Sends a notification of the given type to the NotificationService.
  void NotifyChanged();

  // Registers a new protocol handler.
  void RegisterProtocolHandler(const ProtocolHandler& handler);

  // Get the DictionaryValues stored under the given pref name that are valid
  // ProtocolHandler values.
  std::vector<const DictionaryValue*> GetHandlersFromPref(
      const char* pref_name) const;

  // Ignores future requests to register the given protocol handler.
  void IgnoreProtocolHandler(const ProtocolHandler& handler);

  // Register
  void IgnoreHandlerFromValue(const DictionaryValue* value);

  // Map from protocols (strings) to protocol handlers.
  ProtocolHandlerMultiMap protocol_handlers_;

  // Protocol handlers that the user has told us to ignore.
  ProtocolHandlerList ignored_protocol_handlers_;

  // Protocol handlers that are the defaults for a given protocol.
  ProtocolHandlerMap default_handlers_;

  // The Profile that owns this ProtocolHandlerRegistry.
  Profile* profile_;

  // The Delegate that registers / deregisters external handlers on our behalf.
  scoped_ptr<Delegate> delegate_;

  // If false then registered protocol handlers will not be used to handle
  // requests.
  bool enabled_;

  // Whether or not we are loading.
  bool is_loading_;

  // This lock ensures that only a single thread is accessing this object at a
  // time, that is, it covers the entire class. This is to prevent race
  // conditions from concurrent access from the IO and UI threads.
  // TODO(koz): Remove the necessity of this lock by using message passing.
  mutable base::Lock lock_;

  DefaultClientObserverList default_client_observers_;

  friend class ProtocolHandlerRegistryTest;

  DISALLOW_COPY_AND_ASSIGN(ProtocolHandlerRegistry);
};
#endif  // CHROME_BROWSER_CUSTOM_HANDLERS_PROTOCOL_HANDLER_REGISTRY_H_
