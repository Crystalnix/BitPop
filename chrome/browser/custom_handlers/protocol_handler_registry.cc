// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/custom_handlers/protocol_handler_registry.h"

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/custom_handlers/register_protocol_handler_infobar_delegate.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile_io_data.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/custom_handlers/protocol_handler.h"
#include "chrome/common/pref_names.h"
#include "content/browser/child_process_security_policy.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "net/base/network_delegate.h"
#include "net/url_request/url_request_redirect_job.h"

using content::BrowserThread;

// ProtocolHandlerRegistry -----------------------------------------------------

ProtocolHandlerRegistry::ProtocolHandlerRegistry(Profile* profile,
    Delegate* delegate)
    : profile_(profile),
      delegate_(delegate),
      enabled_(true),
      enabled_io_(enabled_),
      is_loading_(false) {
}

ProtocolHandlerRegistry::~ProtocolHandlerRegistry() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(default_client_observers_.empty());
}

void ProtocolHandlerRegistry::Finalize() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  delegate_.reset(NULL);
  // We free these now in case there are any outstanding workers running. If
  // we didn't free them they could respond to workers and try to update the
  // protocol handler registry after it was deleted.
  // Observers remove themselves from this list when they are deleted; so
  // we delete the last item until none are left in the list.
  while (!default_client_observers_.empty()) {
    delete default_client_observers_.back();
  }
}

const ProtocolHandlerRegistry::ProtocolHandlerList*
ProtocolHandlerRegistry::GetHandlerList(
    const std::string& scheme) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandlerMultiMap::const_iterator p = protocol_handlers_.find(scheme);
  if (p == protocol_handlers_.end()) {
    return NULL;
  }
  return &p->second;
}

ProtocolHandlerRegistry::ProtocolHandlerList
ProtocolHandlerRegistry::GetHandlersFor(
    const std::string& scheme) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandlerMultiMap::const_iterator p = protocol_handlers_.find(scheme);
  if (p == protocol_handlers_.end()) {
    return ProtocolHandlerList();
  }
  return p->second;
}

ProtocolHandlerRegistry::ProtocolHandlerList
ProtocolHandlerRegistry::GetIgnoredHandlers() {
  return ignored_protocol_handlers_;
}

void ProtocolHandlerRegistry::RegisterProtocolHandler(
    const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(CanSchemeBeOverridden(handler.protocol()));
  DCHECK(!handler.IsEmpty());
  if (IsRegistered(handler)) {
    return;
  }
  if (enabled_ && !delegate_->IsExternalHandlerRegistered(handler.protocol()))
    delegate_->RegisterExternalHandler(handler.protocol());
  InsertHandler(handler);
}

void ProtocolHandlerRegistry::InsertHandler(const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandlerMultiMap::iterator p =
      protocol_handlers_.find(handler.protocol());

  if (p != protocol_handlers_.end()) {
    p->second.push_back(handler);
    return;
  }

  ProtocolHandlerList new_list;
  new_list.push_back(handler);
  protocol_handlers_[handler.protocol()] = new_list;
}

void ProtocolHandlerRegistry::IgnoreProtocolHandler(
    const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ignored_protocol_handlers_.push_back(handler);
}

void ProtocolHandlerRegistry::Enable() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (enabled_) {
    return;
  }
  enabled_ = true;
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&ProtocolHandlerRegistry::EnableIO, this));
  ProtocolHandlerMap::const_iterator p;
  for (p = default_handlers_.begin(); p != default_handlers_.end(); ++p) {
    delegate_->RegisterExternalHandler(p->first);
  }
  Save();
  NotifyChanged();
}

void ProtocolHandlerRegistry::Disable() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!enabled_) {
    return;
  }
  enabled_ = false;
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&ProtocolHandlerRegistry::DisableIO, this));
  ProtocolHandlerMap::const_iterator p;
  for (p = default_handlers_.begin(); p != default_handlers_.end(); ++p) {
    delegate_->DeregisterExternalHandler(p->first);
  }
  Save();
  NotifyChanged();
}

std::vector<const DictionaryValue*>
ProtocolHandlerRegistry::GetHandlersFromPref(const char* pref_name) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  std::vector<const DictionaryValue*> result;
  PrefService* prefs = profile_->GetPrefs();
  if (!prefs->HasPrefPath(pref_name)) {
    return result;
  }

  const ListValue* handlers = prefs->GetList(pref_name);
  if (handlers) {
    for (size_t i = 0; i < handlers->GetSize(); ++i) {
      DictionaryValue* dict;
      if (!handlers->GetDictionary(i, &dict))
        continue;
      if (ProtocolHandler::IsValidDict(dict)) {
        result.push_back(dict);
      }
    }
  }
  return result;
}

namespace {

// If true default protocol handlers will be removed if the OS level
// registration for a protocol is no longer Chrome.
bool ShouldRemoveHandlersNotInOS() {
#if defined(OS_LINUX)
  // We don't do this on Linux as the OS registration there is not reliable,
  // and Chrome OS doesn't have any notion of OS registration.
  // TODO(benwells): When Linux support is more reliable remove this
  // difference (http://crbug.com/88255).
  return false;
#else
  const CommandLine& cmd_line = *CommandLine::ForCurrentProcess();
  return ShellIntegration::CanSetAsDefaultProtocolClient() &&
      !cmd_line.HasSwitch(switches::kDisableCustomProtocolOSCheck);
#endif
}

} // namespace

void ProtocolHandlerRegistry::Load() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  is_loading_ = true;
  PrefService* prefs = profile_->GetPrefs();
  if (prefs->HasPrefPath(prefs::kCustomHandlersEnabled)) {
    enabled_ = prefs->GetBoolean(prefs::kCustomHandlersEnabled);
    BrowserThread::PostTask(
        BrowserThread::IO,
        FROM_HERE,
        base::Bind(enabled_ ? &ProtocolHandlerRegistry::EnableIO :
                   &ProtocolHandlerRegistry::DisableIO, this));
  }
  std::vector<const DictionaryValue*> registered_handlers =
      GetHandlersFromPref(prefs::kRegisteredProtocolHandlers);
  for (std::vector<const DictionaryValue*>::const_iterator p =
       registered_handlers.begin();
       p != registered_handlers.end(); ++p) {
    ProtocolHandler handler = ProtocolHandler::CreateProtocolHandler(*p);
    RegisterProtocolHandler(handler);
    bool is_default = false;
    if ((*p)->GetBoolean("default", &is_default) && is_default) {
      SetDefault(handler);
    }
  }
  std::vector<const DictionaryValue*> ignored_handlers =
    GetHandlersFromPref(prefs::kIgnoredProtocolHandlers);
  for (std::vector<const DictionaryValue*>::const_iterator p =
       ignored_handlers.begin();
       p != ignored_handlers.end(); ++p) {
    IgnoreProtocolHandler(ProtocolHandler::CreateProtocolHandler(*p));
  }
  is_loading_ = false;

  // For each default protocol handler, check that we are still registered
  // with the OS as the default application.
  if (ShouldRemoveHandlersNotInOS()) {
    for (ProtocolHandlerMap::const_iterator p = default_handlers_.begin();
         p != default_handlers_.end(); ++p) {
      ProtocolHandler handler = p->second;
      DefaultClientObserver* observer = delegate_->CreateShellObserver(this);
      scoped_refptr<ShellIntegration::DefaultProtocolClientWorker> worker;
      worker = delegate_->CreateShellWorker(observer, handler.protocol());
      observer->SetWorker(worker);
      default_client_observers_.push_back(observer);
      worker->StartCheckIsDefault();
    }
  }
}

void ProtocolHandlerRegistry::Save() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (is_loading_) {
    return;
  }
  scoped_ptr<Value> registered_protocol_handlers(EncodeRegisteredHandlers());
  scoped_ptr<Value> ignored_protocol_handlers(EncodeIgnoredHandlers());
  scoped_ptr<Value> enabled(Value::CreateBooleanValue(enabled_));
  profile_->GetPrefs()->Set(prefs::kRegisteredProtocolHandlers,
      *registered_protocol_handlers);
  profile_->GetPrefs()->Set(prefs::kIgnoredProtocolHandlers,
      *ignored_protocol_handlers);
  profile_->GetPrefs()->Set(prefs::kCustomHandlersEnabled, *enabled);
}

bool ProtocolHandlerRegistry::CanSchemeBeOverridden(
    const std::string& scheme) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  const ProtocolHandlerList* handlers = GetHandlerList(scheme);
  // If we already have a handler for this scheme, we can add more.
  if (handlers != NULL && !handlers->empty())
    return true;
  // Don't override a scheme if it already has an external handler.
  return !delegate_->IsExternalHandlerRegistered(scheme);
}

void ProtocolHandlerRegistry::GetRegisteredProtocols(
    std::vector<std::string>* output) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandlerMultiMap::const_iterator p;
  for (p = protocol_handlers_.begin(); p != protocol_handlers_.end(); ++p) {
    if (!p->second.empty())
      output->push_back(p->first);
  }
}

void ProtocolHandlerRegistry::RemoveIgnoredHandler(
    const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  bool should_notify = false;
  ProtocolHandlerList::iterator p = std::find(
      ignored_protocol_handlers_.begin(), ignored_protocol_handlers_.end(),
      handler);
  if (p != ignored_protocol_handlers_.end()) {
    ignored_protocol_handlers_.erase(p);
    Save();
    should_notify = true;
  }
  if (should_notify)
    NotifyChanged();
}

bool ProtocolHandlerRegistry::IsRegistered(
    const ProtocolHandler& handler) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  const ProtocolHandlerList* handlers = GetHandlerList(handler.protocol());
  if (!handlers) {
    return false;
  }
  return std::find(handlers->begin(), handlers->end(), handler) !=
      handlers->end();
}

bool ProtocolHandlerRegistry::HasRegisteredEquivalent(
    const ProtocolHandler& handler) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  const ProtocolHandlerList* handlers = GetHandlerList(handler.protocol());
  if (!handlers) {
    return false;
  }
  ProtocolHandlerList::const_iterator i;
  for (i = handlers->begin(); i != handlers->end(); ++i) {
    if (handler.IsEquivalent(*i)) {
      return true;
    }
  }
  return false;
}

bool ProtocolHandlerRegistry::IsIgnored(const ProtocolHandler& handler) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandlerList::const_iterator i;
  for (i = ignored_protocol_handlers_.begin();
       i != ignored_protocol_handlers_.end(); ++i) {
    if (*i == handler) {
      return true;
    }
  }
  return false;
}

bool ProtocolHandlerRegistry::HasIgnoredEquivalent(
    const ProtocolHandler& handler) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandlerList::const_iterator i;
  for (i = ignored_protocol_handlers_.begin();
       i != ignored_protocol_handlers_.end(); ++i) {
    if (handler.IsEquivalent(*i)) {
      return true;
    }
  }
  return false;
}

bool ProtocolHandlerRegistry::IsHandledProtocol(
    const std::string& scheme) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return enabled_ && !GetHandlerFor(scheme).IsEmpty();
}

void ProtocolHandlerRegistry::RemoveHandler(
    const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandlerList& handlers = protocol_handlers_[handler.protocol()];
  ProtocolHandlerList::iterator p =
      std::find(handlers.begin(), handlers.end(), handler);
  if (p != handlers.end()) {
    handlers.erase(p);
  }
  ProtocolHandlerMap::iterator q = default_handlers_.find(handler.protocol());
  if (q != default_handlers_.end() && q->second == handler) {
    // Make the new top handler in the list the default.
    if (!handlers.empty()) {
      // NOTE We pass a copy because SetDefault() modifies handlers.
      SetDefault(ProtocolHandler(handlers[0]));
    } else {
      BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE,
          base::Bind(&ProtocolHandlerRegistry::ClearDefaultIO, this,
                     q->second.protocol()));
      default_handlers_.erase(q);
    }
  }

  if (!IsHandledProtocol(handler.protocol())) {
    delegate_->DeregisterExternalHandler(handler.protocol());
  }
  Save();
  NotifyChanged();
}

void ProtocolHandlerRegistry::RemoveDefaultHandler(const std::string& scheme) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandler current_default = GetHandlerFor(scheme);
  if (!current_default.IsEmpty())
    RemoveHandler(current_default);
}

static const ProtocolHandler& LookupHandler(
    const ProtocolHandlerRegistry::ProtocolHandlerMap& handler_map,
    const std::string& scheme) {
  ProtocolHandlerRegistry::ProtocolHandlerMap::const_iterator p =
      handler_map.find(scheme);
  if (p != handler_map.end()) {
    return p->second;
  }
  return ProtocolHandler::EmptyProtocolHandler();
}

Value* ProtocolHandlerRegistry::EncodeRegisteredHandlers() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ListValue* protocol_handlers = new ListValue();
  for (ProtocolHandlerMultiMap::iterator i = protocol_handlers_.begin();
       i != protocol_handlers_.end(); ++i) {
    for (ProtocolHandlerList::iterator j = i->second.begin();
         j != i->second.end(); ++j) {
      DictionaryValue* encoded = j->Encode();
      if (IsDefault(*j)) {
        encoded->Set("default", Value::CreateBooleanValue(true));
      }
      protocol_handlers->Append(encoded);
    }
  }
  return protocol_handlers;
}

Value* ProtocolHandlerRegistry::EncodeIgnoredHandlers() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ListValue* handlers = new ListValue();
  for (ProtocolHandlerList::iterator i = ignored_protocol_handlers_.begin();
       i != ignored_protocol_handlers_.end(); ++i) {
    handlers->Append(i->Encode());
  }
  return handlers;
}

bool ProtocolHandlerRegistry::SilentlyHandleRegisterHandlerRequest(
    const ProtocolHandler& handler) {
  if (handler.IsEmpty() || !CanSchemeBeOverridden(handler.protocol()))
    return true;

  if (!enabled() || IsRegistered(handler) || HasIgnoredEquivalent(handler))
    return true;

  if (AttemptReplace(handler))
    return true;

  return false;
}

void ProtocolHandlerRegistry::OnAcceptRegisterProtocolHandler(
    const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  RegisterProtocolHandler(handler);
  SetDefault(handler);
  Save();
  NotifyChanged();
}

void ProtocolHandlerRegistry::OnDenyRegisterProtocolHandler(
    const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  RegisterProtocolHandler(handler);
  Save();
  NotifyChanged();
}

void ProtocolHandlerRegistry::OnIgnoreRegisterProtocolHandler(
    const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  IgnoreProtocolHandler(handler);
  Save();
  NotifyChanged();
}

bool ProtocolHandlerRegistry::AttemptReplace(const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandler old_default = GetHandlerFor(handler.protocol());
  bool make_new_handler_default = handler.IsSameOrigin(old_default);
  ProtocolHandlerList to_replace(GetReplacedHandlers(handler));
  if (to_replace.empty())
    return false;
  for (ProtocolHandlerList::iterator p = to_replace.begin();
       p != to_replace.end(); ++p) {
    RemoveHandler(*p);
  }
  if (make_new_handler_default) {
    OnAcceptRegisterProtocolHandler(handler);
  } else {
    InsertHandler(handler);
    NotifyChanged();
  }
  return true;
}

ProtocolHandlerRegistry::ProtocolHandlerList
ProtocolHandlerRegistry::GetReplacedHandlers(
    const ProtocolHandler& handler) const {
  ProtocolHandlerList replaced_handlers;
  const ProtocolHandlerList* handlers = GetHandlerList(handler.protocol());
  if (!handlers)
    return replaced_handlers;
  for (ProtocolHandlerList::const_iterator p = handlers->begin();
       p != handlers->end(); p++) {
    if (handler.IsSameOrigin(*p)) {
      replaced_handlers.push_back(*p);
    }
  }
  return replaced_handlers;
}


// static
void ProtocolHandlerRegistry::RegisterPrefs(PrefService* pref_service) {
  pref_service->RegisterListPref(prefs::kRegisteredProtocolHandlers,
                                 PrefService::UNSYNCABLE_PREF);
  pref_service->RegisterListPref(prefs::kIgnoredProtocolHandlers,
                                 PrefService::UNSYNCABLE_PREF);
  pref_service->RegisterBooleanPref(prefs::kCustomHandlersEnabled, true,
                                    PrefService::UNSYNCABLE_PREF);
}

void ProtocolHandlerRegistry::SetDefault(const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ProtocolHandlerMap::const_iterator p = default_handlers_.find(
      handler.protocol());
  // If we're not loading, and we are setting a default for a new protocol,
  // register with the OS.
  if (!is_loading_ && p == default_handlers_.end())
      delegate_->RegisterWithOSAsDefaultClient(handler.protocol(), this);
  default_handlers_.erase(handler.protocol());
  default_handlers_.insert(std::make_pair(handler.protocol(), handler));
  PromoteHandler(handler);
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&ProtocolHandlerRegistry::SetDefaultIO, this, handler));
}

void ProtocolHandlerRegistry::ClearDefault(const std::string& scheme) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  default_handlers_.erase(scheme);
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&ProtocolHandlerRegistry::ClearDefaultIO, this, scheme));
  Save();
  NotifyChanged();
}

bool ProtocolHandlerRegistry::IsDefault(
    const ProtocolHandler& handler) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return GetHandlerFor(handler.protocol()) == handler;
}

const ProtocolHandler& ProtocolHandlerRegistry::GetHandlerFor(
    const std::string& scheme) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return LookupHandler(default_handlers_, scheme);
}

int ProtocolHandlerRegistry::GetHandlerIndex(const std::string& scheme) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  const ProtocolHandler& handler = GetHandlerFor(scheme);
  if (handler.IsEmpty())
    return -1;
  const ProtocolHandlerList* handlers = GetHandlerList(scheme);
  if (!handlers)
    return -1;

  ProtocolHandlerList::const_iterator p;
  int i;
  for (i = 0, p = handlers->begin(); p != handlers->end(); ++p, ++i) {
    if (*p == handler)
      return i;
  }
  return -1;
}

void ProtocolHandlerRegistry::PromoteHandler(const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(IsRegistered(handler));
  ProtocolHandlerMultiMap::iterator p =
      protocol_handlers_.find(handler.protocol());
  ProtocolHandlerList& list = p->second;
  list.erase(std::find(list.begin(), list.end(), handler));
  list.insert(list.begin(), handler);
}

void ProtocolHandlerRegistry::NotifyChanged() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROTOCOL_HANDLER_REGISTRY_CHANGED,
      content::Source<Profile>(profile_),
      content::NotificationService::NoDetails());
}

// IO thread methods -----------------------------------------------------------

void ProtocolHandlerRegistry::ClearDefaultIO(const std::string& scheme) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  default_handlers_io_.erase(scheme);
}

void ProtocolHandlerRegistry::SetDefaultIO(const ProtocolHandler& handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  ClearDefaultIO(handler.protocol());
  default_handlers_io_.insert(std::make_pair(handler.protocol(), handler));
}

bool ProtocolHandlerRegistry::IsHandledProtocolIO(
    const std::string& scheme) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  return enabled_io_ && !LookupHandler(default_handlers_io_, scheme).IsEmpty();
}

net::URLRequestJob* ProtocolHandlerRegistry::MaybeCreateJob(
    net::URLRequest* request) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  ProtocolHandler handler = LookupHandler(default_handlers_io_,
                                          request->url().scheme());
  if (handler.IsEmpty()) {
    return NULL;
  }
  GURL translated_url(handler.TranslateUrl(request->url()));
  if (!translated_url.is_valid()) {
    return NULL;
  }
  return new net::URLRequestRedirectJob(request, translated_url);
}

// Delegate --------------------------------------------------------------------

ProtocolHandlerRegistry::Delegate::~Delegate() {
}

void ProtocolHandlerRegistry::Delegate::RegisterExternalHandler(
    const std::string& protocol) {
  ChildProcessSecurityPolicy* policy =
    ChildProcessSecurityPolicy::GetInstance();
  if (!policy->IsWebSafeScheme(protocol)) {
    policy->RegisterWebSafeScheme(protocol);
  }
}

void ProtocolHandlerRegistry::Delegate::DeregisterExternalHandler(
    const std::string& protocol) {
}

ShellIntegration::DefaultProtocolClientWorker*
ProtocolHandlerRegistry::Delegate::CreateShellWorker(
    ShellIntegration::DefaultWebClientObserver* observer,
    const std::string& protocol) {
  return new ShellIntegration::DefaultProtocolClientWorker(observer, protocol);
}

ProtocolHandlerRegistry::DefaultClientObserver*
ProtocolHandlerRegistry::Delegate::CreateShellObserver(
    ProtocolHandlerRegistry* registry) {
  return new DefaultClientObserver(registry);
}

void ProtocolHandlerRegistry::Delegate::RegisterWithOSAsDefaultClient(
    const std::string& protocol, ProtocolHandlerRegistry* registry) {
  DefaultClientObserver* observer = CreateShellObserver(registry);
  // The worker pointer is reference counted. While it is running the
  // message loops of the FILE and UI thread will hold references to it
  // and it will be automatically freed once all its tasks have finished.
  scoped_refptr<ShellIntegration::DefaultProtocolClientWorker> worker;
  worker = CreateShellWorker(observer, protocol);
  observer->SetWorker(worker);
  registry->default_client_observers_.push_back(observer);
  worker->StartSetAsDefault();
}

bool ProtocolHandlerRegistry::Delegate::IsExternalHandlerRegistered(
    const std::string& protocol) {
  // NOTE(koz): This function is safe to call from any thread, despite living
  // in ProfileIOData.
  return ProfileIOData::IsHandledProtocol(protocol);
}

// DefaultClientObserver ------------------------------------------------------

ProtocolHandlerRegistry::DefaultClientObserver::DefaultClientObserver(
    ProtocolHandlerRegistry* registry)
    : worker_(NULL), registry_(registry) {
  DCHECK(registry_);
}

ProtocolHandlerRegistry::DefaultClientObserver::~DefaultClientObserver() {
  if (worker_) {
    worker_->ObserverDestroyed();
  };
  DefaultClientObserverList::iterator iter = std::find(
      registry_->default_client_observers_.begin(),
      registry_->default_client_observers_.end(), this);
  registry_->default_client_observers_.erase(iter);
}

void
ProtocolHandlerRegistry::DefaultClientObserver::SetDefaultWebClientUIState(
    ShellIntegration::DefaultWebClientUIState state) {
  if (worker_) {
    if (ShouldRemoveHandlersNotInOS() &&
        (state == ShellIntegration::STATE_NOT_DEFAULT)) {
      registry_->ClearDefault(worker_->protocol());
    }
  } else {
    NOTREACHED();
  }
}

void ProtocolHandlerRegistry::DefaultClientObserver::SetWorker(
    ShellIntegration::DefaultProtocolClientWorker* worker) {
  worker_ = worker;
}
