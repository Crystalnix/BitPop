// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/chrome_content_client.h"

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/process_util.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/win/windows_version.h"
#include "chrome/common/child_process_logging.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/render_messages.h"
#include "content/common/pepper_plugin_registry.h"
#include "remoting/client/plugin/pepper_entrypoints.h"

#if defined(OS_WIN)
#include "content/common/sandbox_policy.h"
#include "sandbox/src/sandbox.h"
#endif

namespace {

const char* kPDFPluginName = "Chrome PDF Viewer";
const char* kPDFPluginMimeType = "application/pdf";
const char* kPDFPluginExtension = "pdf";
const char* kPDFPluginDescription = "Portable Document Format";

const char* kNaClPluginName = "Chrome NaCl";
const char* kNaClPluginMimeType = "application/x-nacl";
const char* kNaClPluginExtension = "nexe";
const char* kNaClPluginDescription = "Native Client Executable";

#if defined(ENABLE_REMOTING)
const char* kRemotingViewerPluginName = "Remoting Viewer";
const FilePath::CharType kRemotingViewerPluginPath[] =
    FILE_PATH_LITERAL("internal-remoting-viewer");
// Use a consistent MIME-type regardless of branding.
const char* kRemotingViewerPluginMimeType =
    "application/vnd.chromium.remoting-viewer";
// TODO(wez): Remove the old MIME-type once client code no longer needs it.
const char* kRemotingViewerPluginOldMimeType =
    "pepper-application/x-chromoting";
#endif

const char* kFlashPluginName = "Shockwave Flash";
const char* kFlashPluginSwfMimeType = "application/x-shockwave-flash";
const char* kFlashPluginSwfExtension = "swf";
const char* kFlashPluginSwfDescription = "Shockwave Flash";
const char* kFlashPluginSplMimeType = "application/futuresplash";
const char* kFlashPluginSplExtension = "spl";
const char* kFlashPluginSplDescription = "FutureSplash Player";

#if !defined(NACL_WIN64)  // The code this needs isn't linked on Win64 builds.

// Appends the known built-in plugins to the given vector. Some built-in
// plugins are "internal" which means they are compiled into the Chrome binary,
// and some are extra shared libraries distributed with the browser (these are
// not marked internal, aside from being automatically registered, they're just
// regular plugins).
void ComputeBuiltInPlugins(std::vector<PepperPluginInfo>* plugins) {
  // PDF.
  //
  // Once we're sandboxed, we can't know if the PDF plugin is available or not;
  // but (on Linux) this function is always called once before we're sandboxed.
  // So the first time through test if the file is available and then skip the
  // check on subsequent calls if yes.
  static bool skip_pdf_file_check = false;
  FilePath path;
  if (PathService::Get(chrome::FILE_PDF_PLUGIN, &path)) {
    if (skip_pdf_file_check || file_util::PathExists(path)) {
      PepperPluginInfo pdf;
      pdf.path = path;
      pdf.name = kPDFPluginName;
      webkit::npapi::WebPluginMimeType pdf_mime_type(kPDFPluginMimeType,
                                                     kPDFPluginExtension,
                                                     kPDFPluginDescription);
      pdf.mime_types.push_back(pdf_mime_type);
      plugins->push_back(pdf);

      skip_pdf_file_check = true;
    }
  }

  // Handle the Native Client plugin just like the PDF plugin.
  static bool skip_nacl_file_check = false;
  if (PathService::Get(chrome::FILE_NACL_PLUGIN, &path)) {
    if (skip_nacl_file_check || file_util::PathExists(path)) {
      PepperPluginInfo nacl;
      nacl.path = path;
      nacl.name = kNaClPluginName;
      // Enable the Native Client Plugin based on the command line.
      nacl.enabled = CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableNaCl);
      webkit::npapi::WebPluginMimeType nacl_mime_type(kNaClPluginMimeType,
                                                      kNaClPluginExtension,
                                                      kNaClPluginDescription);
      nacl.mime_types.push_back(nacl_mime_type);
      plugins->push_back(nacl);

      skip_nacl_file_check = true;
    }
  }

  // The Remoting Viewer plugin is built-in, but behind a flag for now.
#if defined(ENABLE_REMOTING)
  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableRemoting)) {
    PepperPluginInfo info;
    info.is_internal = true;
    info.name = kRemotingViewerPluginName;
    info.path = FilePath(kRemotingViewerPluginPath);
    webkit::npapi::WebPluginMimeType remoting_mime_type(
        kRemotingViewerPluginMimeType,
        std::string(),
        std::string());
    info.mime_types.push_back(remoting_mime_type);
    webkit::npapi::WebPluginMimeType old_remoting_mime_type(
        kRemotingViewerPluginOldMimeType,
        std::string(),
        std::string());
    info.mime_types.push_back(old_remoting_mime_type);
    info.internal_entry_points.get_interface = remoting::PPP_GetInterface;
    info.internal_entry_points.initialize_module =
        remoting::PPP_InitializeModule;
    info.internal_entry_points.shutdown_module = remoting::PPP_ShutdownModule;

    plugins->push_back(info);
  }
#endif
}

void AddOutOfProcessFlash(std::vector<PepperPluginInfo>* plugins) {
  // Flash being out of process is handled separately than general plugins
  // for testing purposes.
  bool flash_out_of_process = !CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kPpapiFlashInProcess);

  // Handle any Pepper Flash first.
  const CommandLine::StringType flash_path =
      CommandLine::ForCurrentProcess()->GetSwitchValueNative(
          switches::kPpapiFlashPath);
  if (flash_path.empty())
    return;

  PepperPluginInfo plugin;
  plugin.is_out_of_process = flash_out_of_process;
  plugin.path = FilePath(flash_path);
  plugin.name = kFlashPluginName;

  const std::string flash_version =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
              switches::kPpapiFlashVersion);
  std::vector<std::string> flash_version_numbers;
  base::SplitString(flash_version, '.', &flash_version_numbers);
  if (flash_version_numbers.size() < 1)
    flash_version_numbers.push_back("10");
  // |SplitString()| puts in an empty string given an empty string. :(
  else if (flash_version_numbers[0].empty())
    flash_version_numbers[0] = "10";
  if (flash_version_numbers.size() < 2)
    flash_version_numbers.push_back("2");
  if (flash_version_numbers.size() < 3)
    flash_version_numbers.push_back("999");
  if (flash_version_numbers.size() < 4)
    flash_version_numbers.push_back("999");
  // E.g., "Shockwave Flash 10.2 r154":
  plugin.description = plugin.name + " " + flash_version_numbers[0] + "." +
      flash_version_numbers[1] + " r" + flash_version_numbers[2];
  plugin.version = JoinString(flash_version_numbers, '.');
  webkit::npapi::WebPluginMimeType swf_mime_type(kFlashPluginSwfMimeType,
                                                 kFlashPluginSwfExtension,
                                                 kFlashPluginSwfDescription);
  plugin.mime_types.push_back(swf_mime_type);
  webkit::npapi::WebPluginMimeType spl_mime_type(kFlashPluginSplMimeType,
                                                 kFlashPluginSplExtension,
                                                 kFlashPluginSplDescription);
  plugin.mime_types.push_back(spl_mime_type);
  plugins->push_back(plugin);
}

#endif  // !defined(NACL_WIN64)

#if defined(OS_WIN)
// Launches the privileged flash broker, used when flash is sandboxed.
// The broker is the same flash dll, except that it uses a different
// entrypoint (BrokerMain) and it is hosted in windows' generic surrogate
// process rundll32. After launching the broker we need to pass to
// the flash plugin the process id of the broker via the command line
// using --flash-broker=pid.
// More info about rundll32 at http://support.microsoft.com/kb/164787.
bool LoadFlashBroker(const FilePath& plugin_path, CommandLine* cmd_line) {
  FilePath rundll;
  if (!PathService::Get(base::DIR_SYSTEM, &rundll))
    return false;
  rundll = rundll.AppendASCII("rundll32.exe");
  // Rundll32 cannot handle paths with spaces, so we use the short path.
  wchar_t short_path[MAX_PATH];
  if (0 == ::GetShortPathNameW(plugin_path.value().c_str(),
                               short_path, arraysize(short_path)))
    return false;
  // Here is the kicker, if the user has disabled 8.3 (short path) support
  // on the volume GetShortPathNameW does not fail but simply returns the
  // input path. In this case if the path had any spaces then rundll32 will
  // incorrectly interpret its parameters. So we quote the path, even though
  // the kb/164787 says you should not.
  std::wstring cmd_final =
      base::StringPrintf(L"%ls \"%ls\",BrokerMain browser=chrome",
                         rundll.value().c_str(),
                         short_path);
  base::ProcessHandle process;
  if (!base::LaunchApp(cmd_final, false, true, &process))
    return false;

  cmd_line->AppendSwitchASCII("flash-broker",
                              base::Int64ToString(::GetProcessId(process)));

  // The flash broker, unders some circumstances can linger beyond the lifetime
  // of the flash player, so we put it in a job object, when the browser
  // terminates the job object is destroyed (by the OS) and the flash broker
  // is terminated.
  HANDLE job = ::CreateJobObjectW(NULL, NULL);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits = {0};
  job_limits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (::SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                &job_limits, sizeof(job_limits))) {
    ::AssignProcessToJobObject(job, process);
    // Yes, we are leaking the object here. Read comment above.
  } else {
    ::CloseHandle(job);
    return false;
  }

  ::CloseHandle(process);
  return true;
}
#endif  // OS_WIN

}  // namespace

namespace chrome {

const char* ChromeContentClient::kPDFPluginName = ::kPDFPluginName;
const char* ChromeContentClient::kNaClPluginName = ::kNaClPluginName;

void ChromeContentClient::SetActiveURL(const GURL& url) {
  child_process_logging::SetActiveURL(url);
}

void ChromeContentClient::SetGpuInfo(const GPUInfo& gpu_info) {
  child_process_logging::SetGpuInfo(gpu_info);
}

void ChromeContentClient::AddPepperPlugins(
    std::vector<PepperPluginInfo>* plugins) {
#if !defined(NACL_WIN64)  // The code this needs isn't linked on Win64 builds.
  ComputeBuiltInPlugins(plugins);
  AddOutOfProcessFlash(plugins);
#endif
}

bool ChromeContentClient::CanSendWhileSwappedOut(const IPC::Message* msg) {
  // Any Chrome-specific messages that must be allowed to be sent from swapped
  // out renderers.
  switch (msg->type()) {
    case ViewHostMsg_DomOperationResponse::ID:
      return true;
    default:
      break;
  }
  return false;
}

bool ChromeContentClient::CanHandleWhileSwappedOut(
    const IPC::Message& msg) {
  // Any Chrome-specific messages (apart from those listed in
  // CanSendWhileSwappedOut) that must be handled by the browser when sent from
  // swapped out renderers.
  switch (msg.type()) {
    case ViewHostMsg_Snapshot::ID:
      return true;
    default:
      break;
  }
  return false;
}

#if defined(OS_WIN)
bool ChromeContentClient::SandboxPlugin(CommandLine* command_line,
                                        sandbox::TargetPolicy* policy) {
  std::wstring plugin_dll = command_line->
      GetSwitchValueNative(switches::kPluginPath);

  FilePath builtin_flash;
  if (!PathService::Get(chrome::FILE_FLASH_PLUGIN, &builtin_flash))
    return false;

  FilePath plugin_path(plugin_dll);
  if (plugin_path != builtin_flash)
    return false;

  if (base::win::GetVersion() <= base::win::VERSION_XP ||
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableFlashSandbox)) {
    return false;
  }

  // Add the policy for the pipes.
  sandbox::ResultCode result = sandbox::SBOX_ALL_OK;
  result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_NAMED_PIPES,
                           sandbox::TargetPolicy::NAMEDPIPES_ALLOW_ANY,
                           L"\\\\.\\pipe\\chrome.*");
  if (result != sandbox::SBOX_ALL_OK) {
    NOTREACHED();
    return false;
  }

  // Spawn the flash broker and apply sandbox policy.
  if (LoadFlashBroker(plugin_path, command_line)) {
    policy->SetJobLevel(sandbox::JOB_UNPROTECTED, 0);
    policy->SetTokenLevel(sandbox::USER_RESTRICTED_SAME_ACCESS,
                          sandbox::USER_INTERACTIVE);
    policy->SetIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
  } else {
    // Could not start the broker, use a very weak policy instead.
    DLOG(WARNING) << "Failed to start flash broker";
    policy->SetJobLevel(sandbox::JOB_UNPROTECTED, 0);
    policy->SetTokenLevel(
        sandbox::USER_UNPROTECTED, sandbox::USER_UNPROTECTED);
  }

  return true;
}
#endif

}  // namespace chrome
