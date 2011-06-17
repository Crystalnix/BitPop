// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browser_about_handler.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/i18n/number_formatting.h"
#include "base/json/json_writer.h"
#include "base/memory/singleton.h"
#include "base/metrics/histogram.h"
#include "base/metrics/stats_table.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/threading/thread.h"
#include "base/tracked_objects.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/about_flags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/memory_details.h"
#include "chrome/browser/metrics/histogram_synchronizer.h"
#include "chrome/browser/net/predictor_api.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/common/about_handler.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/net/gaia/google_service_auth_error.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "content/browser/browser_thread.h"
#include "content/browser/gpu_process_host.h"
#include "content/browser/renderer_host/render_process_host.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/common/gpu_messages.h"
#include "googleurl/src/gurl.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "net/base/escape.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "webkit/glue/webkit_glue.h"
#include "webkit/glue/plugins/plugin_list.h"
#include "webkit/plugins/npapi/webplugininfo.h"

#ifdef CHROME_V8
#include "v8/include/v8.h"
#endif

#if defined(OS_WIN)
#include "chrome/browser/enumerate_modules_model_win.h"
#elif defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/cros/syslogs_library.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/version_loader.h"
#include "content/browser/zygote_host_linux.h"
#elif defined(OS_LINUX)
#include "content/browser/zygote_host_linux.h"
#endif

#if defined(USE_TCMALLOC)
#include "third_party/tcmalloc/chromium/src/google/malloc_extension.h"
#endif

using base::Time;
using base::TimeDelta;

#if defined(USE_TCMALLOC)
// static
AboutTcmallocOutputs* AboutTcmallocOutputs::GetInstance() {
  return Singleton<AboutTcmallocOutputs>::get();
}

AboutTcmallocOutputs::AboutTcmallocOutputs() {}

AboutTcmallocOutputs::~AboutTcmallocOutputs() {}

// Glue between the callback task and the method in the singleton.
void AboutTcmallocRendererCallback(base::ProcessId pid,
                                   const std::string& output) {
  AboutTcmallocOutputs::GetInstance()->RendererCallback(pid, output);
}
#endif

namespace {

// The (alphabetized) paths used for the about pages.
// Note: Keep these in sync with url_constants.h
const char kAppCacheInternalsPath[] = "appcache-internals";
const char kBlobInternalsPath[] = "blob-internals";
const char kCreditsPath[] = "credits";
const char kCachePath[] = "view-http-cache";
#if defined(OS_WIN)
const char kConflictsPath[] = "conflicts";
#endif
const char kDnsPath[] = "dns";
const char kFlagsPath[] = "flags";
const char kGpuPath[] = "gpu-internals";
const char kHistogramsPath[] = "histograms";
const char kMemoryRedirectPath[] = "memory-redirect";
const char kMemoryPath[] = "memory";
const char kStatsPath[] = "stats";
const char kTasksPath[] = "tasks";
const char kTcmallocPath[] = "tcmalloc";
const char kTermsPath[] = "terms";
const char kVersionPath[] = "version";
const char kAboutPath[] = "about";
// Not about:* pages, but included to make about:about look nicer
const char kNetInternalsPath[] = "net-internals";
const char kPluginsPath[] = "plugins";
const char kSyncInternalsPath[] = "sync-internals";

#if defined(OS_LINUX)
const char kLinuxProxyConfigPath[] = "linux-proxy-config";
const char kSandboxPath[] = "sandbox";
#endif

#if defined(OS_CHROMEOS)
const char kNetworkPath[] = "network";
const char kOSCreditsPath[] = "os-credits";
const char kEULAPathFormat[] = "/usr/share/chromeos-assets/eula/%s/eula.html";
#endif

// Add path here to be included in about:about
const char *kAllAboutPaths[] = {
  kAboutPath,
  kAppCacheInternalsPath,
  kBlobInternalsPath,
  kCachePath,
  kCreditsPath,
#if defined(OS_WIN)
  kConflictsPath,
#endif
  kDnsPath,
  kFlagsPath,
  kGpuPath,
  kHistogramsPath,
  kMemoryPath,
  kNetInternalsPath,
  kPluginsPath,
  kStatsPath,
  kSyncInternalsPath,
#ifdef TRACK_ALL_TASK_OBJECTS
  kTasksPath,
#endif  // TRACK_ALL_TASK_OBJECTS
  kTcmallocPath,
  kTermsPath,
  kVersionPath,
#if defined(OS_LINUX)
  kSandboxPath,
#endif
#if defined(OS_CHROMEOS)
  kNetworkPath,
  kOSCreditsPath,
#endif
  };

// When you type about:memory, it actually loads an intermediate URL that
// redirects you to the final page. This avoids the problem where typing
// "about:memory" on the new tab page or any other page where a process
// transition would occur to the about URL will cause some confusion.
//
// The problem is that during the processing of the memory page, there are two
// processes active, the original and the destination one. This can create the
// impression that we're using more resources than we actually are. This
// redirect solves the problem by eliminating the process transition during the
// time that about memory is being computed.
std::string GetAboutMemoryRedirectResponse() {
  return "<meta http-equiv=\"refresh\" "
      "content=\"0;chrome://about/memory\">";
}

class AboutSource : public ChromeURLDataManager::DataSource {
 public:
  // Creates our datasource.
  AboutSource();

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id);

  virtual std::string GetMimeType(const std::string&) const {
    return "text/html";
  }

  // Send the response data.
  void FinishDataRequest(const std::string& html, int request_id);

 private:
  virtual ~AboutSource();

  DISALLOW_COPY_AND_ASSIGN(AboutSource);
};

// Handling about:memory is complicated enough to encapsulate its related
// methods into a single class. The user should create it (on the heap) and call
// its |StartFetch()| method.
class AboutMemoryHandler : public MemoryDetails {
 public:
  AboutMemoryHandler(AboutSource* source, int request_id)
    : source_(source), request_id_(request_id) {}


  virtual void OnDetailsAvailable();

 private:
  ~AboutMemoryHandler() {}

  void BindProcessMetrics(DictionaryValue* data,
                          ProcessMemoryInformation* info);
  void AppendProcess(ListValue* child_data, ProcessMemoryInformation* info);

  scoped_refptr<AboutSource> source_;
  int request_id_;

  DISALLOW_COPY_AND_ASSIGN(AboutMemoryHandler);
};

#if defined(OS_CHROMEOS)
// ChromeOSAboutVersionHandler is responsible for loading the Chrome OS
// version.
// ChromeOSAboutVersionHandler handles deleting itself once the version has
// been obtained and AboutSource notified.
class ChromeOSAboutVersionHandler {
 public:
  ChromeOSAboutVersionHandler(AboutSource* source, int request_id);

  // Callback from chromeos::VersionLoader giving the version.
  void OnVersion(chromeos::VersionLoader::Handle handle,
                 std::string version);

 private:
  // Where the results are fed to.
  scoped_refptr<AboutSource> source_;

  // ID identifying the request.
  int request_id_;

  // Handles asynchronously loading the version.
  chromeos::VersionLoader loader_;

  // Used to request the version.
  CancelableRequestConsumer consumer_;

  DISALLOW_COPY_AND_ASSIGN(ChromeOSAboutVersionHandler);
};

class ChromeOSTermsHandler
    : public base::RefCountedThreadSafe<ChromeOSTermsHandler> {
 public:
  static void Start(AboutSource* source, int request_id) {
    scoped_refptr<ChromeOSTermsHandler> handler(
        new ChromeOSTermsHandler(source, request_id));
    handler->StartOnUIThread();
  }

 private:
  ChromeOSTermsHandler(AboutSource* source, int request_id)
    : source_(source),
      request_id_(request_id),
      locale_(WizardController::GetInitialLocale()) {
  }

  void StartOnUIThread() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        NewRunnableMethod(this, &ChromeOSTermsHandler::LoadFileOnFileThread));
  }

  void LoadFileOnFileThread() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
    std::string path = StringPrintf(kEULAPathFormat, locale_.c_str());
    if (!file_util::ReadFileToString(FilePath(path), &contents_)) {
      // No EULA for given language - try en-US as default.
      path = StringPrintf(kEULAPathFormat, "en-US");
      if (!file_util::ReadFileToString(FilePath(path), &contents_)) {
        // File with EULA not found, ResponseOnUIThread will load EULA from
        // resources if contents_ is empty.
        contents_.clear();
      }
    }
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this, &ChromeOSTermsHandler::ResponseOnUIThread));
  }

  void ResponseOnUIThread() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    if (contents_.empty()) {
      contents_ = ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_TERMS_HTML).as_string();
    }
    source_->FinishDataRequest(contents_, request_id_);
  }

  // Where the results are fed to.
  scoped_refptr<AboutSource> source_;

  // ID identifying the request.
  int request_id_;

  std::string locale_;

  std::string contents_;

  DISALLOW_COPY_AND_ASSIGN(ChromeOSTermsHandler);
};

#endif

// Individual about handlers ---------------------------------------------------

std::string AboutAbout() {
  std::string html("<html><head><title>About Pages</title></head>\n"
      "<body><h2>List of About pages</h2>\n<ul>");
  std::vector<std::string> paths(AboutPaths());
  for (std::vector<std::string>::const_iterator i = paths.begin();
       i != paths.end(); ++i) {
    html += "<li><a href='chrome://";
    if ((*i != kAppCacheInternalsPath) &&
        (*i != kBlobInternalsPath) &&
        (*i != kCachePath) &&
  #if defined(OS_WIN)
        (*i != kConflictsPath) &&
  #endif
        (*i != kFlagsPath) &&
        (*i != kGpuPath) &&
        (*i != kNetInternalsPath) &&
        (*i != kPluginsPath)) {
      html += "about/";
    }
    html += *i + "/'>about:" + *i + "</a></li>\n";
  }
  const char *debug[] = { "crash", "kill", "hang", "shorthang",
                          "gpucrash", "gpuhang" };
  html += "</ul>\n<h2>For Debug</h2>\n"
      "<p>The following pages are for debugging purposes only. Because they "
      "crash or hang the renderer, they're not linked directly; you can type "
      "them into the address bar if you need them.</p>\n<ul>";
  for (size_t i = 0; i < arraysize(debug); i++)
    html += "<li>about:" + std::string(debug[i]) + "</li>\n";
  html += "</ul>\n</body></html>";
  return html;
}

#if defined(OS_CHROMEOS)

// Html output helper functions
// TODO(stevenjb): L10N this.

// Helper function to wrap Html with <th> tag.
static std::string WrapWithTH(std::string text) {
  return "<th>" + text + "</th>";
}

// Helper function to wrap Html with <td> tag.
static std::string WrapWithTD(std::string text) {
  return "<td>" + text + "</td>";
}

// Helper function to create an Html table header for a Network.
static std::string ToHtmlTableHeader(const chromeos::Network* network) {
  std::string str =
      WrapWithTH("Name") +
      WrapWithTH("Active") +
      WrapWithTH("State");
  if (network->type() == chromeos::TYPE_WIFI ||
      network->type() == chromeos::TYPE_CELLULAR) {
    str += WrapWithTH("Auto-Connect");
    str += WrapWithTH("Strength");
  }
  if (network->type() == chromeos::TYPE_WIFI) {
    str += WrapWithTH("Encryption");
    str += WrapWithTH("Passphrase");
    str += WrapWithTH("Identity");
    str += WrapWithTH("Certificate");
  }
  if (network->type() == chromeos::TYPE_CELLULAR) {
    str += WrapWithTH("Technology");
    str += WrapWithTH("Connectivity");
    str += WrapWithTH("Activation");
    str += WrapWithTH("Roaming");
  }
  if (network->type() == chromeos::TYPE_VPN) {
    str += WrapWithTH("Host");
    str += WrapWithTH("Provider Type");
    str += WrapWithTH("PSK Passphrase");
    str += WrapWithTH("Username");
    str += WrapWithTH("User Passphrase");
  }
  str += WrapWithTH("Error");
  str += WrapWithTH("IP Address");
  return str;
}

// Helper function to create an Html table row for a Network.
static std::string ToHtmlTableRow(const chromeos::Network* network) {
  std::string str =
      WrapWithTD(network->name()) +
      WrapWithTD(base::IntToString(network->is_active())) +
      WrapWithTD(network->GetStateString());
  if (network->type() == chromeos::TYPE_WIFI ||
      network->type() == chromeos::TYPE_CELLULAR) {
    const chromeos::WirelessNetwork* wireless =
        static_cast<const chromeos::WirelessNetwork*>(network);
    str += WrapWithTD(base::IntToString(wireless->auto_connect()));
    str += WrapWithTD(base::IntToString(wireless->strength()));
  }
  if (network->type() == chromeos::TYPE_WIFI) {
    const chromeos::WifiNetwork* wifi =
        static_cast<const chromeos::WifiNetwork*>(network);
    str += WrapWithTD(wifi->GetEncryptionString());
    str += WrapWithTD(std::string(wifi->passphrase().length(), '*'));
    str += WrapWithTD(wifi->identity());
    str += WrapWithTD(wifi->cert_path());
  }
  if (network->type() == chromeos::TYPE_CELLULAR) {
    const chromeos::CellularNetwork* cell =
        static_cast<const chromeos::CellularNetwork*>(network);
    str += WrapWithTH(cell->GetNetworkTechnologyString());
    str += WrapWithTH(cell->GetConnectivityStateString());
    str += WrapWithTH(cell->GetActivationStateString());
    str += WrapWithTH(cell->GetRoamingStateString());
  }
  if (network->type() == chromeos::TYPE_VPN) {
    const chromeos::VirtualNetwork* vpn =
        static_cast<const chromeos::VirtualNetwork*>(network);
    str += WrapWithTH(vpn->server_hostname());
    str += WrapWithTH(vpn->GetProviderTypeString());
    str += WrapWithTD(std::string(vpn->psk_passphrase().length(), '*'));
    str += WrapWithTH(vpn->username());
    str += WrapWithTD(std::string(vpn->user_passphrase().length(), '*'));
  }
  str += WrapWithTD(network->failed() ? network->GetErrorString() : "");
  str += WrapWithTD(network->ip_address());
  return str;
}

std::string GetNetworkHtmlInfo(int refresh) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  std::string output;
  output.append("<html><head><title>About Network</title>");
  if (refresh > 0)
    output.append("<meta http-equiv=\"refresh\" content=\"" +
                  base::IntToString(refresh) + "\"/>");
  output.append("</head><body>");
  if (refresh > 0) {
    output.append("(Auto-refreshing page every " +
                  base::IntToString(refresh) + "s)");
  } else {
    output.append("(To auto-refresh this page: about:network/&lt;secs&gt;)");
  }

  if (cros->ethernet_enabled()) {
    output.append("<h3>Ethernet:</h3><table border=1>");
    const chromeos::EthernetNetwork* ethernet = cros->ethernet_network();
    if (ethernet) {
      output.append("<tr>" + ToHtmlTableHeader(ethernet) + "</tr>");
      output.append("<tr>" + ToHtmlTableRow(ethernet) + "</tr>");
    }
  }

  if (cros->wifi_enabled()) {
    output.append("</table><h3>Wifi Networks:</h3><table border=1>");
    const chromeos::WifiNetworkVector& wifi_networks = cros->wifi_networks();
    for (size_t i = 0; i < wifi_networks.size(); ++i) {
      if (i == 0)
        output.append("<tr>" + ToHtmlTableHeader(wifi_networks[i]) +
                      "</tr>");
      output.append("<tr>" + ToHtmlTableRow(wifi_networks[i]) + "</tr>");
    }
  }

  if (cros->cellular_enabled()) {
    output.append("</table><h3>Cellular Networks:</h3><table border=1>");
    const chromeos::CellularNetworkVector& cellular_networks =
        cros->cellular_networks();
    for (size_t i = 0; i < cellular_networks.size(); ++i) {
      if (i == 0)
        output.append("<tr>" + ToHtmlTableHeader(cellular_networks[i]) +
                      "</tr>");
      output.append("<tr>" + ToHtmlTableRow(cellular_networks[i]) + "</tr>");
    }
  }

  {
    output.append("</table><h3>Virtual Networks:</h3><table border=1>");
    const chromeos::VirtualNetworkVector& virtual_networks =
        cros->virtual_networks();
    for (size_t i = 0; i < virtual_networks.size(); ++i) {
      if (i == 0)
        output.append("<tr>" + ToHtmlTableHeader(virtual_networks[i]) +
                      "</tr>");
      output.append("<tr>" + ToHtmlTableRow(virtual_networks[i]) + "</tr>");
    }
  }

  {
    output.append(
        "</table><h3>Remembered Wi-Fi Networks:</h3><table border=1>");
    const chromeos::WifiNetworkVector& remembered_wifi_networks =
        cros->remembered_wifi_networks();
    for (size_t i = 0; i < remembered_wifi_networks.size(); ++i) {
      if (i == 0)
        output.append("<tr>" +
                      ToHtmlTableHeader(remembered_wifi_networks[i]) + "</tr>");
      output.append("<tr>" + ToHtmlTableRow(remembered_wifi_networks[i]) +
                    "</tr>");
    }
  }

  output.append("</table></body></html>");
  return output;
}

std::string AboutNetwork(const std::string& query) {
  int refresh;
  base::StringToInt(query, &refresh);
  return GetNetworkHtmlInfo(refresh);
}
#endif

// AboutDnsHandler bounces the request back to the IO thread to collect
// the DNS information.
class AboutDnsHandler : public base::RefCountedThreadSafe<AboutDnsHandler> {
 public:
  static void Start(AboutSource* source, int request_id) {
    scoped_refptr<AboutDnsHandler> handler(
        new AboutDnsHandler(source, request_id));
    handler->StartOnUIThread();
  }

 private:
  AboutDnsHandler(AboutSource* source, int request_id)
      : source_(source),
        request_id_(request_id) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  }

  // Calls FinishOnUIThread() on completion.
  void StartOnUIThread() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        NewRunnableMethod(this, &AboutDnsHandler::StartOnIOThread));
  }

  void StartOnIOThread() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

    std::string data;
    chrome_browser_net::PredictorGetHtmlInfo(&data);

    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this, &AboutDnsHandler::FinishOnUIThread, data));
  }

  void FinishOnUIThread(const std::string& data) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    source_->FinishDataRequest(data, request_id_);
  }

  // Where the results are fed to.
  scoped_refptr<AboutSource> source_;

  // ID identifying the request.
  int request_id_;

  DISALLOW_COPY_AND_ASSIGN(AboutDnsHandler);
};

#if defined(USE_TCMALLOC)
std::string AboutTcmalloc(const std::string& query) {
  std::string data;
  AboutTcmallocOutputsType* outputs =
      AboutTcmallocOutputs::GetInstance()->outputs();

  // Display any stats for which we sent off requests the last time.
  data.append("<html><head><title>About tcmalloc</title></head><body>\n");
  data.append("<p>Stats as of last page load;");
  data.append("reload to get stats as of this page load.</p>\n");
  data.append("<table width=\"100%\">\n");
  for (AboutTcmallocOutputsType::const_iterator oit = outputs->begin();
       oit != outputs->end();
       oit++) {
    data.append("<tr><td bgcolor=\"yellow\">");
    data.append(oit->first);
    data.append("</td></tr>\n");
    data.append("<tr><td><pre>\n");
    data.append(oit->second);
    data.append("</pre></td></tr>\n");
  }
  data.append("</table>\n");
  data.append("</body></html>\n");

  // Reset our collector singleton.
  outputs->clear();

  // Populate the collector with stats from the local browser process
  // and send off requests to all the renderer processes.
  char buffer[1024 * 32];
  MallocExtension::instance()->GetStats(buffer, sizeof(buffer));
  std::string browser("Browser");
  AboutTcmallocOutputs::GetInstance()->SetOutput(browser, buffer);
  RenderProcessHost::iterator it(RenderProcessHost::AllHostsIterator());
  while (!it.IsAtEnd()) {
    it.GetCurrentValue()->Send(new ViewMsg_GetRendererTcmalloc);
    it.Advance();
  }

  return data;
}
#endif

std::string AboutHistograms(const std::string& query) {
  TimeDelta wait_time = TimeDelta::FromMilliseconds(10000);

  HistogramSynchronizer* current_synchronizer =
      HistogramSynchronizer::CurrentSynchronizer();
  DCHECK(current_synchronizer != NULL);
  current_synchronizer->FetchRendererHistogramsSynchronously(wait_time);

  std::string data;
  base::StatisticsRecorder::WriteHTMLGraph(query, &data);
  return data;
}

void AboutMemory(AboutSource* source, int request_id) {
  // The AboutMemoryHandler cleans itself up, but |StartFetch()| will want the
  // refcount to be greater than 0.
  scoped_refptr<AboutMemoryHandler>
      handler(new AboutMemoryHandler(source, request_id));
  handler->StartFetch();
}

#ifdef TRACK_ALL_TASK_OBJECTS
static std::string AboutObjects(const std::string& query) {
  std::string data;
  tracked_objects::ThreadData::WriteHTML(query, &data);
  return data;
}
#endif  // TRACK_ALL_TASK_OBJECTS

// Handler for filling in the "about:stats" page, as called by the browser's
// About handler processing.
// |query| is roughly the query string of the about:stats URL.
// Returns a string containing the HTML to render for the about:stats page.
// Conditional Output:
//      if |query| is "json", returns a JSON format of all counters.
//      if |query| is "raw", returns plain text of counter deltas.
//      otherwise, returns HTML with pretty JS/HTML to display the data.
std::string AboutStats(const std::string& query) {
  // We keep the DictionaryValue tree live so that we can do delta
  // stats computations across runs.
  static DictionaryValue root;
  static base::TimeTicks last_sample_time = base::TimeTicks::Now();

  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeDelta time_since_last_sample = now - last_sample_time;
  last_sample_time = now;

  base::StatsTable* table = base::StatsTable::current();
  if (!table)
    return std::string();

  // We maintain two lists - one for counters and one for timers.
  // Timers actually get stored on both lists.
  ListValue* counters;
  if (!root.GetList("counters", &counters)) {
    counters = new ListValue();
    root.Set("counters", counters);
  }

  ListValue* timers;
  if (!root.GetList("timers", &timers)) {
    timers = new ListValue();
    root.Set("timers", timers);
  }

  // NOTE: Counters start at index 1.
  for (int index = 1; index <= table->GetMaxCounters(); index++) {
    // Get the counter's full name
    std::string full_name = table->GetRowName(index);
    if (full_name.length() == 0)
      break;
    DCHECK_EQ(':', full_name[1]);
    char counter_type = full_name[0];
    std::string name = full_name.substr(2);

    // JSON doesn't allow '.' in names.
    size_t pos;
    while ((pos = name.find(".")) != std::string::npos)
      name.replace(pos, 1, ":");

    // Try to see if this name already exists.
    DictionaryValue* counter = NULL;
    for (size_t scan_index = 0;
         scan_index < counters->GetSize(); scan_index++) {
      DictionaryValue* dictionary;
      if (counters->GetDictionary(scan_index, &dictionary)) {
        std::string scan_name;
        if (dictionary->GetString("name", &scan_name) && scan_name == name) {
          counter = dictionary;
        }
      } else {
        NOTREACHED();  // Should always be there
      }
    }

    if (counter == NULL) {
      counter = new DictionaryValue();
      counter->SetString("name", name);
      counters->Append(counter);
    }

    switch (counter_type) {
      case 'c':
        {
          int new_value = table->GetRowValue(index);
          int prior_value = 0;
          int delta = 0;
          if (counter->GetInteger("value", &prior_value)) {
            delta = new_value - prior_value;
          }
          counter->SetInteger("value", new_value);
          counter->SetInteger("delta", delta);
        }
        break;
      case 'm':
        {
          // TODO(mbelshe): implement me.
        }
        break;
      case 't':
        {
          int time = table->GetRowValue(index);
          counter->SetInteger("time", time);

          // Store this on the timers list as well.
          timers->Append(counter);
        }
        break;
      default:
        NOTREACHED();
    }
  }

  std::string data;
  if (query == "json") {
    base::JSONWriter::WriteWithOptionalEscape(&root, true, false, &data);
  } else if (query == "raw") {
    // Dump the raw counters which have changed in text format.
    data = "<pre>";
    data.append(StringPrintf("Counter changes in the last %ldms\n",
        static_cast<long int>(time_since_last_sample.InMilliseconds())));
    for (size_t i = 0; i < counters->GetSize(); ++i) {
      Value* entry = NULL;
      bool rv = counters->Get(i, &entry);
      if (!rv)
        continue;  // None of these should fail.
      DictionaryValue* counter = static_cast<DictionaryValue*>(entry);
      int delta;
      rv = counter->GetInteger("delta", &delta);
      if (!rv)
        continue;
      if (delta > 0) {
        std::string name;
        rv = counter->GetString("name", &name);
        if (!rv)
          continue;
        int value;
        rv = counter->GetInteger("value", &value);
        if (!rv)
          continue;
        data.append(name);
        data.append(":");
        data.append(base::IntToString(delta));
        data.append("\n");
      }
    }
    data.append("</pre>");
  } else {
    // Get about_stats.html and process a pretty page.
    static const base::StringPiece stats_html(
        ResourceBundle::GetSharedInstance().GetRawDataResource(
            IDR_ABOUT_STATS_HTML));

    // Create jstemplate and return.
    data = jstemplate_builder::GetTemplateHtml(
        stats_html, &root, "t" /* template root node id */);

    // Clear the timer list since we stored the data in the timers list as well.
    for (int index = static_cast<int>(timers->GetSize())-1; index >= 0;
         index--) {
      Value* value;
      timers->Remove(index, &value);
      // We don't care about the value pointer; it's still tracked
      // on the counters list.
    }
  }

  return data;
}

#if defined(OS_LINUX)
std::string AboutLinuxProxyConfig() {
  std::string data;
  data.append("<!DOCTYPE HTML>\n");
  data.append("<html><head><meta charset=\"utf-8\"><title>");
  data.append(l10n_util::GetStringUTF8(IDS_ABOUT_LINUX_PROXY_CONFIG_TITLE));
  data.append("</title>");
  data.append("<style>body { max-width: 70ex; padding: 2ex 5ex; }</style>");
  data.append("</head><body>\n");
  FilePath binary = CommandLine::ForCurrentProcess()->GetProgram();
  data.append(l10n_util::GetStringFUTF8(
                  IDS_ABOUT_LINUX_PROXY_CONFIG_BODY,
                  l10n_util::GetStringUTF16(IDS_PRODUCT_NAME),
                  ASCIIToUTF16(binary.BaseName().value())));
  data.append("</body></html>\n");
  return data;
}

void AboutSandboxRow(std::string* data, const std::string& prefix, int name_id,
                     bool good) {
  data->append("<tr><td>");
  data->append(prefix);
  data->append(l10n_util::GetStringUTF8(name_id));
  if (good) {
    data->append("</td><td style=\"color: green;\">");
    data->append(
        l10n_util::GetStringUTF8(IDS_CONFIRM_MESSAGEBOX_YES_BUTTON_LABEL));
  } else {
    data->append("</td><td style=\"color: red;\">");
    data->append(
        l10n_util::GetStringUTF8(IDS_CONFIRM_MESSAGEBOX_NO_BUTTON_LABEL));
  }
  data->append("</td></tr>");
}

std::string AboutSandbox() {
  std::string data;
  data.append("<!DOCTYPE HTML>\n");
  data.append("<html><head><meta charset=\"utf-8\"><title>");
  data.append(l10n_util::GetStringUTF8(IDS_ABOUT_SANDBOX_TITLE));
  data.append("</title>");
  data.append("</head><body>\n");
  data.append("<h1>");
  data.append(l10n_util::GetStringUTF8(IDS_ABOUT_SANDBOX_TITLE));
  data.append("</h1>");

  const int status = ZygoteHost::GetInstance()->sandbox_status();

  data.append("<table>");

  AboutSandboxRow(&data, "", IDS_ABOUT_SANDBOX_SUID_SANDBOX,
                  status & ZygoteHost::kSandboxSUID);
  if (status & ZygoteHost::kSandboxPIDNS) {
    AboutSandboxRow(&data, "&nbsp;&nbsp;", IDS_ABOUT_SANDBOX_PID_NAMESPACES,
                    status & ZygoteHost::kSandboxPIDNS);
    AboutSandboxRow(&data, "&nbsp;&nbsp;", IDS_ABOUT_SANDBOX_NET_NAMESPACES,
                    status & ZygoteHost::kSandboxNetNS);
  }
  AboutSandboxRow(&data, "", IDS_ABOUT_SANDBOX_SECCOMP_SANDBOX,
                  status & ZygoteHost::kSandboxSeccomp);

  data.append("</table>");

  bool good = ((status & ZygoteHost::kSandboxSUID) &&
               (status & ZygoteHost::kSandboxPIDNS)) ||
              (status & ZygoteHost::kSandboxSeccomp);
  if (good) {
    data.append("<p style=\"color: green\">");
    data.append(l10n_util::GetStringUTF8(IDS_ABOUT_SANDBOX_OK));
  } else {
    data.append("<p style=\"color: red\">");
    data.append(l10n_util::GetStringUTF8(IDS_ABOUT_SANDBOX_BAD));
  }
  data.append("</p>");

  data.append("</body></html>\n");
  return data;
}
#endif

std::string AboutVersion(DictionaryValue* localized_strings) {
  localized_strings->SetString("title",
      l10n_util::GetStringUTF16(IDS_ABOUT_VERSION_TITLE));
  chrome::VersionInfo version_info;

  std::string webkit_version = webkit_glue::GetWebKitVersion();
#ifdef CHROME_V8
  std::string js_version(v8::V8::GetVersion());
  std::string js_engine = "V8";
#else
  std::string js_version = webkit_version;
  std::string js_engine = "JavaScriptCore";
#endif

  localized_strings->SetString("name",
      l10n_util::GetStringUTF16(IDS_PRODUCT_NAME));
  localized_strings->SetString("version", version_info.Version());
  // Bug 79458: Need to evaluate the use of getting the version string on
  // this thread.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  localized_strings->SetString("version_modifier",
                               platform_util::GetVersionStringModifier());
  localized_strings->SetString("js_engine", js_engine);
  localized_strings->SetString("js_version", js_version);

  // Obtain the version of the first enabled Flash plugin.
  std::vector<webkit::npapi::WebPluginInfo> info_array;
  webkit::npapi::PluginList::Singleton()->GetPluginInfoArray(
      GURL(), "application/x-shockwave-flash", false, &info_array, NULL);
  string16 flash_version =
      l10n_util::GetStringUTF16(IDS_PLUGINS_DISABLED_PLUGIN);
  for (size_t i = 0; i < info_array.size(); ++i) {
    if (webkit::npapi::IsPluginEnabled(info_array[i])) {
      flash_version = info_array[i].version;
      break;
    }
  }
  localized_strings->SetString("flash_plugin", "Flash");
  localized_strings->SetString("flash_version", flash_version);
  localized_strings->SetString("webkit_version", webkit_version);
  localized_strings->SetString("company",
      l10n_util::GetStringUTF16(IDS_ABOUT_VERSION_COMPANY_NAME));
  localized_strings->SetString("copyright",
      l10n_util::GetStringUTF16(IDS_ABOUT_VERSION_COPYRIGHT));
  localized_strings->SetString("cl", version_info.LastChange());
  localized_strings->SetString("official",
      l10n_util::GetStringUTF16(
          version_info.IsOfficialBuild() ?
              IDS_ABOUT_VERSION_OFFICIAL
            : IDS_ABOUT_VERSION_UNOFFICIAL));
  localized_strings->SetString("user_agent_name",
      l10n_util::GetStringUTF16(IDS_ABOUT_VERSION_USER_AGENT));
  localized_strings->SetString("useragent", webkit_glue::GetUserAgent(GURL()));
  localized_strings->SetString("command_line_name",
      l10n_util::GetStringUTF16(IDS_ABOUT_VERSION_COMMAND_LINE));

#if defined(OS_WIN)
  localized_strings->SetString("command_line",
      WideToUTF16(CommandLine::ForCurrentProcess()->command_line_string()));
#elif defined(OS_POSIX)
  std::string command_line = "";
  typedef std::vector<std::string> ArgvList;
  const ArgvList& argv = CommandLine::ForCurrentProcess()->argv();
  for (ArgvList::const_iterator iter = argv.begin(); iter != argv.end(); iter++)
    command_line += " " + *iter;
  // TODO(viettrungluu): |command_line| could really have any encoding, whereas
  // below we assumes it's UTF-8.
  localized_strings->SetString("command_line", command_line);
#endif

  base::StringPiece version_html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_ABOUT_VERSION_HTML));

  return jstemplate_builder::GetTemplatesHtml(
      version_html, localized_strings, "t" /* template root node id */);
}

std::string VersionNumberToString(uint32 value) {
  int hi = (value >> 8) & 0xff;
  int low = value & 0xff;
  return base::IntToString(hi) + "." + base::IntToString(low);
}

// AboutSource -----------------------------------------------------------------

AboutSource::AboutSource()
    : DataSource(chrome::kAboutScheme, MessageLoop::current()) {
}

AboutSource::~AboutSource() {
}

void AboutSource::StartDataRequest(const std::string& path_raw,
    bool is_incognito, int request_id) {
  std::string path = path_raw;
  std::string info;
  if (path.find("/") != std::string::npos) {
    size_t pos = path.find("/");
    info = path.substr(pos + 1, path.length() - (pos + 1));
    path = path.substr(0, pos);
  }
  path = StringToLowerASCII(path);

  std::string response;
  if (path == kDnsPath) {
    AboutDnsHandler::Start(this, request_id);
    return;
  } else if (path == kHistogramsPath) {
    response = AboutHistograms(info);
  } else if (path == kMemoryPath) {
    AboutMemory(this, request_id);
    return;
  } else if (path == kMemoryRedirectPath) {
    response = GetAboutMemoryRedirectResponse();
#ifdef TRACK_ALL_TASK_OBJECTS
  } else if (path == kTasksPath) {
    response = AboutObjects(info);
#endif
  } else if (path == kStatsPath) {
    response = AboutStats(info);
#if defined(USE_TCMALLOC)
  } else if (path == kTcmallocPath) {
    response = AboutTcmalloc(info);
#endif
  } else if (path == kVersionPath || path.empty()) {
#if defined(OS_CHROMEOS)
    new ChromeOSAboutVersionHandler(this, request_id);
    return;
#else
    DictionaryValue value;
    response = AboutVersion(&value);
#endif
  } else if (path == kCreditsPath) {
    response = ResourceBundle::GetSharedInstance().GetRawDataResource(
        IDR_CREDITS_HTML).as_string();
  } else if (path == kAboutPath) {
    response = AboutAbout();
#if defined(OS_CHROMEOS)
  } else if (path == kOSCreditsPath) {
    response = ResourceBundle::GetSharedInstance().GetRawDataResource(
        IDR_OS_CREDITS_HTML).as_string();
  } else if (path == kNetworkPath) {
    response = AboutNetwork(info);
#endif
  } else if (path == kTermsPath) {
#if defined(OS_CHROMEOS)
    ChromeOSTermsHandler::Start(this, request_id);
    return;
#else
    response = ResourceBundle::GetSharedInstance().GetRawDataResource(
        IDR_TERMS_HTML).as_string();
#endif
#if defined(OS_LINUX)
  } else if (path == kLinuxProxyConfigPath) {
    response = AboutLinuxProxyConfig();
  } else if (path == kSandboxPath) {
    response = AboutSandbox();
#endif
  }

  FinishDataRequest(response, request_id);
}

void AboutSource::FinishDataRequest(const std::string& response,
                                    int request_id) {
  scoped_refptr<RefCountedBytes> html_bytes(new RefCountedBytes);
  html_bytes->data.resize(response.size());
  std::copy(response.begin(), response.end(), html_bytes->data.begin());
  SendResponse(request_id, html_bytes);
}

// AboutMemoryHandler ----------------------------------------------------------

// Helper for AboutMemory to bind results from a ProcessMetrics object
// to a DictionaryValue. Fills ws_usage and comm_usage so that the objects
// can be used in caller's scope (e.g for appending to a net total).
void AboutMemoryHandler::BindProcessMetrics(DictionaryValue* data,
                                            ProcessMemoryInformation* info) {
  DCHECK(data && info);

  // Bind metrics to dictionary.
  data->SetInteger("ws_priv", static_cast<int>(info->working_set.priv));
  data->SetInteger("ws_shareable",
    static_cast<int>(info->working_set.shareable));
  data->SetInteger("ws_shared", static_cast<int>(info->working_set.shared));
  data->SetInteger("comm_priv", static_cast<int>(info->committed.priv));
  data->SetInteger("comm_map", static_cast<int>(info->committed.mapped));
  data->SetInteger("comm_image", static_cast<int>(info->committed.image));
  data->SetInteger("pid", info->pid);
  data->SetString("version", info->version);
  data->SetInteger("processes", info->num_processes);
}

// Helper for AboutMemory to append memory usage information for all
// sub-processes (i.e. renderers, plugins) used by Chrome.
void AboutMemoryHandler::AppendProcess(ListValue* child_data,
                                       ProcessMemoryInformation* info) {
  DCHECK(child_data && info);

  // Append a new DictionaryValue for this renderer to our list.
  DictionaryValue* child = new DictionaryValue();
  child_data->Append(child);
  BindProcessMetrics(child, info);

  std::string child_label(
      ChildProcessInfo::GetFullTypeNameInEnglish(info->type,
                                                 info->renderer_type));
  if (info->is_diagnostics)
    child_label.append(" (diagnostics)");
  child->SetString("child_name", child_label);
  ListValue* titles = new ListValue();
  child->Set("titles", titles);
  for (size_t i = 0; i < info->titles.size(); ++i)
    titles->Append(new StringValue(info->titles[i]));
}


void AboutMemoryHandler::OnDetailsAvailable() {
  // the root of the JSON hierarchy for about:memory jstemplate
  DictionaryValue root;
  ListValue* browsers = new ListValue();
  root.Set("browsers", browsers);

  const std::vector<ProcessData>& browser_processes = processes();

  // Aggregate per-process data into browser summary data.
  std::wstring log_string;
  for (size_t index = 0; index < browser_processes.size(); index++) {
    if (browser_processes[index].processes.empty())
      continue;

    // Sum the information for the processes within this browser.
    ProcessMemoryInformation aggregate;
    ProcessMemoryInformationList::const_iterator iterator;
    iterator = browser_processes[index].processes.begin();
    aggregate.pid = iterator->pid;
    aggregate.version = iterator->version;
    while (iterator != browser_processes[index].processes.end()) {
      if (!iterator->is_diagnostics ||
          browser_processes[index].processes.size() == 1) {
        aggregate.working_set.priv += iterator->working_set.priv;
        aggregate.working_set.shared += iterator->working_set.shared;
        aggregate.working_set.shareable += iterator->working_set.shareable;
        aggregate.committed.priv += iterator->committed.priv;
        aggregate.committed.mapped += iterator->committed.mapped;
        aggregate.committed.image += iterator->committed.image;
        aggregate.num_processes++;
      }
      ++iterator;
    }
    DictionaryValue* browser_data = new DictionaryValue();
    browsers->Append(browser_data);
    browser_data->SetString("name", browser_processes[index].name);

    BindProcessMetrics(browser_data, &aggregate);

    // We log memory info as we record it.
    if (log_string.length() > 0)
      log_string.append(L", ");
    log_string.append(UTF16ToWide(browser_processes[index].name));
    log_string.append(L", ");
    log_string.append(UTF8ToWide(
        base::Int64ToString(aggregate.working_set.priv)));
    log_string.append(L", ");
    log_string.append(UTF8ToWide(
        base::Int64ToString(aggregate.working_set.shared)));
    log_string.append(L", ");
    log_string.append(UTF8ToWide(
        base::Int64ToString(aggregate.working_set.shareable)));
  }
  if (log_string.length() > 0)
    VLOG(1) << "memory: " << log_string;

  // Set the browser & renderer detailed process data.
  DictionaryValue* browser_data = new DictionaryValue();
  root.Set("browzr_data", browser_data);
  ListValue* child_data = new ListValue();
  root.Set("child_data", child_data);

  ProcessData process = browser_processes[0];  // Chrome is the first browser.
  root.SetString("current_browser_name", process.name);

  for (size_t index = 0; index < process.processes.size(); index++) {
    if (process.processes[index].type == ChildProcessInfo::BROWSER_PROCESS)
      BindProcessMetrics(browser_data, &process.processes[index]);
    else
      AppendProcess(child_data, &process.processes[index]);
  }

  root.SetBoolean("show_other_browsers",
      browser_defaults::kShowOtherBrowsersInAboutMemory);

  // Get about_memory.html
  static const base::StringPiece memory_html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_ABOUT_MEMORY_HTML));

  // Create jstemplate and return.
  std::string template_html = jstemplate_builder::GetTemplateHtml(
      memory_html, &root, "t" /* template root node id */);

  source_->FinishDataRequest(template_html, request_id_);
}

#if defined(OS_CHROMEOS)
// ChromeOSAboutVersionHandler  -----------------------------------------------

ChromeOSAboutVersionHandler::ChromeOSAboutVersionHandler(AboutSource* source,
                                                         int request_id)
    : source_(source),
      request_id_(request_id) {
  loader_.GetVersion(&consumer_,
                     NewCallback(this, &ChromeOSAboutVersionHandler::OnVersion),
                     chromeos::VersionLoader::VERSION_FULL);
}

void ChromeOSAboutVersionHandler::OnVersion(
    chromeos::VersionLoader::Handle handle,
    std::string version) {
  DictionaryValue localized_strings;
  localized_strings.SetString("os_name",
                              l10n_util::GetStringUTF16(IDS_PRODUCT_OS_NAME));
  localized_strings.SetString("os_version", version);
  localized_strings.SetBoolean("is_chrome_os", true);
  source_->FinishDataRequest(AboutVersion(&localized_strings), request_id_);

  // CancelableRequestProvider isn't happy when it's deleted and servicing a
  // task, so we delay the deletion.
  MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

#endif

// Returns true if |url|'s spec starts with |about_specifier|, and is
// terminated by the start of a path.
bool StartsWithAboutSpecifier(const GURL& url, const char* about_specifier) {
  return StartsWithASCII(url.spec(), about_specifier, true) &&
         (url.spec().size() == strlen(about_specifier) ||
          url.spec()[strlen(about_specifier)] == '/');
}

// Transforms a URL of the form "about:foo/XXX" to <url_prefix> + "XXX".
GURL RemapAboutURL(const std::string& url_prefix, const GURL& url) {
  std::string path;
  size_t split = url.spec().find('/');
  if (split != std::string::npos)
    path = url.spec().substr(split + 1);
  return GURL(url_prefix + path);
}

}  // namespace

// -----------------------------------------------------------------------------

bool WillHandleBrowserAboutURL(GURL* url, Profile* profile) {
  // We only handle about: schemes.
  if (!url->SchemeIs(chrome::kAboutScheme))
    return false;

  // about:blank is special. Frames are allowed to access about:blank,
  // but they are not allowed to access other types of about pages.
  // Just ignore the about:blank and let the TAB_CONTENTS_WEB handle it.
  if (LowerCaseEqualsASCII(url->spec(), chrome::kAboutBlankURL))
    return false;

  // Rewrite about:cache/* URLs to chrome://view-http-cache/*
  if (StartsWithAboutSpecifier(*url, chrome::kAboutCacheURL)) {
    *url = RemapAboutURL(chrome::kNetworkViewCacheURL, *url);
    return true;
  }

#if defined(OS_WIN)
  // Rewrite about:conflicts/* URLs to chrome://conflicts/*
  if (StartsWithAboutSpecifier(*url, chrome::kAboutConflicts)) {
    *url = GURL(chrome::kChromeUIConflictsURL);
    return true;
  }
#endif

  // Rewrite about:flags to chrome://flags/.
  if (LowerCaseEqualsASCII(url->spec(), chrome::kAboutFlagsURL)) {
    *url = GURL(chrome::kChromeUIFlagsURL);
    return true;
  }

  // Rewrite about:net-internals/* URLs to chrome://net-internals/*
  if (StartsWithAboutSpecifier(*url, chrome::kAboutNetInternalsURL)) {
    *url = RemapAboutURL(chrome::kNetworkViewInternalsURL, *url);
    return true;
  }

  // Rewrite about:gpu/* URLs to chrome://gpu-internals/*
  if (StartsWithAboutSpecifier(*url, chrome::kAboutGpuURL)) {
    *url = RemapAboutURL(chrome::kGpuInternalsURL, *url);
    return true;
  }

  // Rewrite about:appcache-internals/* URLs to chrome://appcache/*
  if (StartsWithAboutSpecifier(*url, chrome::kAboutAppCacheInternalsURL)) {
    *url = RemapAboutURL(chrome::kAppCacheViewInternalsURL, *url);
    return true;
  }

  // Rewrite about:sync-internals/* URLs (and about:sync, too, for
  // legacy reasons) to chrome://sync-internals/*
  if (StartsWithAboutSpecifier(*url, chrome::kAboutSyncInternalsURL) ||
      StartsWithAboutSpecifier(*url, chrome::kAboutSyncURL)) {
    *url = RemapAboutURL(chrome::kSyncViewInternalsURL, *url);
    return true;
  }

  // Rewrite about:plugins to chrome://plugins/.
  if (LowerCaseEqualsASCII(url->spec(), chrome::kAboutPluginsURL)) {
    *url = GURL(chrome::kChromeUIPluginsURL);
    return true;
  }

  // Handle URL to crash the browser process.
  if (LowerCaseEqualsASCII(url->spec(), chrome::kAboutBrowserCrash)) {
    // Induce an intentional crash in the browser process.
    int* bad_pointer = NULL;
    *bad_pointer = 42;
    return true;
  }

  // Handle URLs to wreck the gpu process.
  if (LowerCaseEqualsASCII(url->spec(), chrome::kAboutGpuCrashURL)) {
    GpuProcessHost::SendOnIO(
        0, content::CAUSE_FOR_GPU_LAUNCH_ABOUT_GPUCRASH, new GpuMsg_Crash());
  }
  if (LowerCaseEqualsASCII(url->spec(), chrome::kAboutGpuHangURL)) {
    GpuProcessHost::SendOnIO(
        0, content::CAUSE_FOR_GPU_LAUNCH_ABOUT_GPUHANG, new GpuMsg_Hang());
  }

  // There are a few about: URLs that we hand over to the renderer. If the
  // renderer wants them, don't do any rewriting.
  if (chrome_about_handler::WillHandle(*url))
    return false;

  // Anything else requires our special handler; make sure it's initialized.
  InitializeAboutDataSource(profile);

  // Special case about:memory to go through a redirect before ending up on
  // the final page. See GetAboutMemoryRedirectResponse above for why.
  if (LowerCaseEqualsASCII(url->path(), kMemoryPath)) {
    *url = GURL("chrome://about/memory-redirect");
    return true;
  }

  // Rewrite the about URL to use chrome:. WebKit treats all about URLS the
  // same (blank page), so if we want to display content, we need another
  // scheme.
  std::string about_url = "chrome://about/";
  about_url.append(url->path());
  *url = GURL(about_url);
  return true;
}

void InitializeAboutDataSource(Profile* profile) {
  profile->GetChromeURLDataManager()->AddDataSource(new AboutSource());
}

// This function gets called with the fixed-up chrome: URLs, so we have to
// compare against those instead of "about:blah".
bool HandleNonNavigationAboutURL(const GURL& url) {
  // about:ipc is currently buggy, so we disable it for official builds.
#if !defined(OFFICIAL_BUILD)

#if (defined(OS_MACOSX) || defined(OS_WIN)) && defined(IPC_MESSAGE_LOG_ENABLED)
  if (LowerCaseEqualsASCII(url.spec(), chrome::kChromeUIIPCURL)) {
    // Run the dialog. This will re-use the existing one if it's already up.
    browser::ShowAboutIPCDialog();
    return true;
  }
#endif

#endif  // OFFICIAL_BUILD

  return false;
}

std::vector<std::string> AboutPaths() {
  std::vector<std::string> paths;
  paths.reserve(arraysize(kAllAboutPaths));
  for (size_t i = 0; i < arraysize(kAllAboutPaths); i++)
    paths.push_back(kAllAboutPaths[i]);
  return paths;
}
