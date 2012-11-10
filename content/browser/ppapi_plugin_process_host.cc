// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/ppapi_plugin_process_host.h"

#include <string>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/process_util.h"
#include "base/utf_string_conversions.h"
#include "content/browser/browser_child_process_host_impl.h"
#include "content/browser/plugin_service_impl.h"
#include "content/browser/renderer_host/render_message_filter.h"
#include "content/common/child_process_host_impl.h"
#include "content/common/child_process_messages.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/pepper_plugin_info.h"
#include "content/public/common/process_type.h"
#include "ipc/ipc_switches.h"
#include "net/base/network_change_notifier.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ui/base/ui_base_switches.h"
#include "webkit/plugins/plugin_switches.h"

using content::ChildProcessHost;
using content::ChildProcessHostImpl;

class PpapiPluginProcessHost::PluginNetworkObserver
    : public net::NetworkChangeNotifier::IPAddressObserver,
      public net::NetworkChangeNotifier::ConnectionTypeObserver {
 public:
  explicit PluginNetworkObserver(PpapiPluginProcessHost* process_host)
      : process_host_(process_host) {
    net::NetworkChangeNotifier::AddIPAddressObserver(this);
    net::NetworkChangeNotifier::AddConnectionTypeObserver(this);
  }

  ~PluginNetworkObserver() {
    net::NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
    net::NetworkChangeNotifier::RemoveIPAddressObserver(this);
  }

  // IPAddressObserver implementation.
  virtual void OnIPAddressChanged() OVERRIDE {
    // TODO(brettw) bug 90246: This doesn't seem correct. The online/offline
    // notification seems like it should be sufficient, but I don't see that
    // when I unplug and replug my network cable. Sending this notification when
    // "something" changes seems to make Flash reasonably happy, but seems
    // wrong. We should really be able to provide the real online state in
    // OnConnectionTypeChanged().
    process_host_->Send(new PpapiMsg_SetNetworkState(true));
  }

  // ConnectionTypeObserver implementation.
  virtual void OnConnectionTypeChanged(
      net::NetworkChangeNotifier::ConnectionType type) {
    process_host_->Send(new PpapiMsg_SetNetworkState(
        type != net::NetworkChangeNotifier::CONNECTION_NONE));
  }

 private:
  PpapiPluginProcessHost* const process_host_;
};

PpapiPluginProcessHost::~PpapiPluginProcessHost() {
  DVLOG(1) << "PpapiPluginProcessHost" << (is_broker_ ? "[broker]" : "")
           << "~PpapiPluginProcessHost()";
  CancelRequests();
}

PpapiPluginProcessHost* PpapiPluginProcessHost::CreatePluginHost(
    const content::PepperPluginInfo& info,
    const FilePath& profile_data_directory,
    net::HostResolver* host_resolver) {
  PpapiPluginProcessHost* plugin_host = new PpapiPluginProcessHost(
      info, profile_data_directory, host_resolver);
  if (plugin_host->Init(info))
    return plugin_host;

  NOTREACHED();  // Init is not expected to fail.
  return NULL;
}

PpapiPluginProcessHost* PpapiPluginProcessHost::CreateBrokerHost(
    const content::PepperPluginInfo& info) {
  PpapiPluginProcessHost* plugin_host =
      new PpapiPluginProcessHost();
  if (plugin_host->Init(info))
    return plugin_host;

  NOTREACHED();  // Init is not expected to fail.
  return NULL;
}

bool PpapiPluginProcessHost::Send(IPC::Message* message) {
  return process_->Send(message);
}

void PpapiPluginProcessHost::OpenChannelToPlugin(Client* client) {
  if (process_->GetHost()->IsChannelOpening()) {
    // The channel is already in the process of being opened.  Put
    // this "open channel" request into a queue of requests that will
    // be run once the channel is open.
    pending_requests_.push_back(client);
    return;
  }

  // We already have an open channel, send a request right away to plugin.
  RequestPluginChannel(client);
}

PpapiPluginProcessHost::PpapiPluginProcessHost(
    const content::PepperPluginInfo& info,
    const FilePath& profile_data_directory,
    net::HostResolver* host_resolver)
    : network_observer_(new PluginNetworkObserver(this)),
      profile_data_directory_(profile_data_directory),
      is_broker_(false) {
  process_.reset(new BrowserChildProcessHostImpl(
      content::PROCESS_TYPE_PPAPI_PLUGIN, this));

  filter_ = new PepperMessageFilter(
      PepperMessageFilter::PLUGIN, host_resolver,
      ppapi::PpapiPermissions(info.permissions));

  file_filter_ = new PepperTrustedFileMessageFilter(
      process_->GetData().id, info.name, profile_data_directory);

  process_->GetHost()->AddFilter(filter_.get());
  process_->GetHost()->AddFilter(file_filter_.get());
}

PpapiPluginProcessHost::PpapiPluginProcessHost()
    : is_broker_(true) {
  process_.reset(new BrowserChildProcessHostImpl(
      content::PROCESS_TYPE_PPAPI_BROKER, this));
}

bool PpapiPluginProcessHost::Init(const content::PepperPluginInfo& info) {
  plugin_path_ = info.path;
  if (info.name.empty()) {
    process_->SetName(plugin_path_.BaseName().LossyDisplayName());
  } else {
    process_->SetName(UTF8ToUTF16(info.name));
  }

  std::string channel_id = process_->GetHost()->CreateChannel();
  if (channel_id.empty())
    return false;

  const CommandLine& browser_command_line = *CommandLine::ForCurrentProcess();
  CommandLine::StringType plugin_launcher =
      browser_command_line.GetSwitchValueNative(switches::kPpapiPluginLauncher);

#if defined(OS_LINUX)
  int flags = plugin_launcher.empty() ? ChildProcessHost::CHILD_ALLOW_SELF :
                                        ChildProcessHost::CHILD_NORMAL;
#else
  int flags = ChildProcessHost::CHILD_NORMAL;
#endif
  FilePath exe_path = ChildProcessHost::GetChildPath(flags);
  if (exe_path.empty())
    return false;

  CommandLine* cmd_line = new CommandLine(exe_path);
  cmd_line->AppendSwitchASCII(switches::kProcessType,
                              is_broker_ ? switches::kPpapiBrokerProcess
                                         : switches::kPpapiPluginProcess);
  cmd_line->AppendSwitchASCII(switches::kProcessChannelID, channel_id);

  // These switches are forwarded to both plugin and broker pocesses.
  static const char* kCommonForwardSwitches[] = {
    switches::kVModule
  };
  cmd_line->CopySwitchesFrom(browser_command_line, kCommonForwardSwitches,
                             arraysize(kCommonForwardSwitches));

  if (!is_broker_) {
    // TODO(vtl): Stop passing flash args in the command line, on windows is
    // going to explode.
    static const char* kPluginForwardSwitches[] = {
      switches::kNoSandbox,
      switches::kDisableSeccompFilterSandbox,
      switches::kPpapiFlashArgs,
      switches::kPpapiStartupDialog
    };
    cmd_line->CopySwitchesFrom(browser_command_line, kPluginForwardSwitches,
                               arraysize(kPluginForwardSwitches));
  }

  std::string locale =
      content::GetContentClient()->browser()->GetApplicationLocale();
  if (!locale.empty()) {
    // Pass on the locale so the plugin will know what language we're using.
    cmd_line->AppendSwitchASCII(switches::kLang, locale);
  }

  if (!plugin_launcher.empty())
    cmd_line->PrependWrapper(plugin_launcher);

  // On posix, never use the zygote for the broker. Also, only use the zygote if
  // the plugin is sandboxed, and we are not using a plugin launcher - having a
  // plugin launcher means we need to use another process instead of just
  // forking the zygote.
#if defined(OS_POSIX)
  bool use_zygote = !is_broker_ && plugin_launcher.empty() && info.is_sandboxed;
  if (!info.is_sandboxed)
    cmd_line->AppendSwitchASCII(switches::kNoSandbox, "");
#endif  // OS_POSIX
  process_->Launch(
#if defined(OS_WIN)
      FilePath(),
#elif defined(OS_POSIX)
      use_zygote,
      base::EnvironmentVector(),
#endif
      cmd_line);
  return true;
}

void PpapiPluginProcessHost::RequestPluginChannel(Client* client) {
  base::ProcessHandle process_handle;
  int renderer_id;
  client->GetPpapiChannelInfo(&process_handle, &renderer_id);

  // We can't send any sync messages from the browser because it might lead to
  // a hang. See the similar code in PluginProcessHost for more description.
  PpapiMsg_CreateChannel* msg = new PpapiMsg_CreateChannel(
      renderer_id, client->OffTheRecord());
  msg->set_unblock(true);
  if (Send(msg)) {
    sent_requests_.push(client);
  } else {
    client->OnPpapiChannelOpened(IPC::ChannelHandle(), 0);
  }
}

void PpapiPluginProcessHost::OnProcessLaunched() {
}

void PpapiPluginProcessHost::OnProcessCrashed(int exit_code) {
  PluginServiceImpl::GetInstance()->RegisterPluginCrash(plugin_path_);
}

bool PpapiPluginProcessHost::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PpapiPluginProcessHost, msg)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_ChannelCreated,
                        OnRendererPluginChannelCreated)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  DCHECK(handled);
  return handled;
}

// Called when the browser <--> plugin channel has been established.
void PpapiPluginProcessHost::OnChannelConnected(int32 peer_pid) {
  // This will actually load the plugin. Errors will actually not be reported
  // back at this point. Instead, the plugin will fail to establish the
  // connections when we request them on behalf of the renderer(s).
  Send(new PpapiMsg_LoadPlugin(plugin_path_));

  // Process all pending channel requests from the renderers.
  for (size_t i = 0; i < pending_requests_.size(); i++)
    RequestPluginChannel(pending_requests_[i]);
  pending_requests_.clear();
}

// Called when the browser <--> plugin channel has an error. This normally
// means the plugin has crashed.
void PpapiPluginProcessHost::OnChannelError() {
  DVLOG(1) << "PpapiPluginProcessHost" << (is_broker_ ? "[broker]" : "")
           << "::OnChannelError()";
  // We don't need to notify the renderers that were communicating with the
  // plugin since they have their own channels which will go into the error
  // state at the same time. Instead, we just need to notify any renderers
  // that have requested a connection but have not yet received one.
  CancelRequests();
}

void PpapiPluginProcessHost::CancelRequests() {
  DVLOG(1) << "PpapiPluginProcessHost" << (is_broker_ ? "[broker]" : "")
           << "CancelRequests()";
  for (size_t i = 0; i < pending_requests_.size(); i++) {
    pending_requests_[i]->OnPpapiChannelOpened(IPC::ChannelHandle(), 0);
  }
  pending_requests_.clear();

  while (!sent_requests_.empty()) {
    sent_requests_.front()->OnPpapiChannelOpened(IPC::ChannelHandle(), 0);
    sent_requests_.pop();
  }
}

// Called when a new plugin <--> renderer channel has been created.
void PpapiPluginProcessHost::OnRendererPluginChannelCreated(
    const IPC::ChannelHandle& channel_handle) {
  if (sent_requests_.empty())
    return;

  // All requests should be processed FIFO, so the next item in the
  // sent_requests_ queue should be the one that the plugin just created.
  Client* client = sent_requests_.front();
  sent_requests_.pop();

  client->OnPpapiChannelOpened(channel_handle, process_->GetData().id);
}
