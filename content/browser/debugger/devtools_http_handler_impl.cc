// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/debugger/devtools_http_handler_impl.h"

#include <utility>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/json/json_writer.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/message_loop_proxy.h"
#include "base/string_number_conversions.h"
#include "base/stringprintf.h"
#include "base/threading/thread.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/devtools_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host_registry.h"
#include "content/public/browser/devtools_client_host.h"
#include "content/public/browser/devtools_http_handler_delegate.h"
#include "content/public/browser/devtools_manager.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/content_client.h"
#include "googleurl/src/gurl.h"
#include "grit/devtools_resources_map.h"
#include "net/base/escape.h"
#include "net/base/io_buffer.h"
#include "net/server/http_server_request_info.h"
#include "net/url_request/url_request_context.h"

namespace content {

const int kBufferSize = 16 * 1024;

namespace {

// An internal implementation of DevToolsClientHost that delegates
// messages sent for DevToolsClient to a DebuggerShell instance.
class DevToolsClientHostImpl : public DevToolsClientHost {
 public:
  DevToolsClientHostImpl(
      net::HttpServer* server,
      int connection_id)
      : server_(server),
        connection_id_(connection_id) {
  }
  ~DevToolsClientHostImpl() {}

  // DevToolsClientHost interface
  virtual void InspectedTabClosing() {
    BrowserThread::PostTask(
        BrowserThread::IO,
        FROM_HERE,
        base::Bind(&net::HttpServer::Close, server_, connection_id_));
  }

  virtual void DispatchOnInspectorFrontend(const std::string& data) {
    BrowserThread::PostTask(
        BrowserThread::IO,
        FROM_HERE,
        base::Bind(&net::HttpServer::SendOverWebSocket,
                   server_,
                   connection_id_,
                   data));
  }

  virtual void TabReplaced(WebContents* new_tab) {
  }

 private:
  virtual void FrameNavigating(const std::string& url) {}
  net::HttpServer* server_;
  int connection_id_;
};

static int next_id = 1;

class TabContentsIDHelper : public content::WebContentsObserver {
 public:
  static int GetID(TabContents* contents) {
    TabContentsToIdMap::iterator it = tab_contents_to_id_.Get().find(contents);
    if (it != tab_contents_to_id_.Get().end())
      return it->second;
    TabContentsIDHelper* wrapper = new TabContentsIDHelper(contents);
    return wrapper->id_;
  }

  static TabContents* GetTabContents(int id) {
    IdToTabContentsMap::iterator it = id_to_tab_contents_.Get().find(id);
    if (it != id_to_tab_contents_.Get().end())
      return it->second;
    return NULL;
  }

 private:
  explicit TabContentsIDHelper(TabContents* tab)
      : content::WebContentsObserver(tab),
        id_(next_id++) {
    id_to_tab_contents_.Get()[id_] = tab;
    tab_contents_to_id_.Get()[tab] = id_;
  }

  virtual ~TabContentsIDHelper() {}

  virtual void WebContentsDestroyed(WebContents* contents) OVERRIDE {
    id_to_tab_contents_.Get().erase(id_);
    tab_contents_to_id_.Get().erase((static_cast<TabContents*>(contents)));
    delete this;
  }

  int id_;
  typedef std::map<int, TabContents*> IdToTabContentsMap;
  static base::LazyInstance<IdToTabContentsMap>::Leaky
      id_to_tab_contents_;
  typedef std::map<TabContents*, int> TabContentsToIdMap;
  static base::LazyInstance<TabContentsToIdMap>::Leaky
      tab_contents_to_id_;
};

base::LazyInstance<TabContentsIDHelper::IdToTabContentsMap>::Leaky
    TabContentsIDHelper::id_to_tab_contents_ = LAZY_INSTANCE_INITIALIZER;
base::LazyInstance<TabContentsIDHelper::TabContentsToIdMap>::Leaky
    TabContentsIDHelper::tab_contents_to_id_ = LAZY_INSTANCE_INITIALIZER;

}  // namespace

// static
int DevToolsHttpHandler::GetFrontendResourceId(const std::string& name) {
  for (size_t i = 0; i < kDevtoolsResourcesSize; ++i) {
    if (name == kDevtoolsResources[i].name)
      return kDevtoolsResources[i].value;
  }
  return -1;
}

// static
DevToolsHttpHandler* DevToolsHttpHandler::Start(
    const std::string& ip,
    int port,
    const std::string& frontend_url,
    DevToolsHttpHandlerDelegate* delegate) {
  DevToolsHttpHandlerImpl* http_handler =
      new DevToolsHttpHandlerImpl(ip, port, frontend_url, delegate);
  http_handler->Start();
  return http_handler;
}

DevToolsHttpHandlerImpl::~DevToolsHttpHandlerImpl() {
  // Stop() must be called prior to this being called
  DCHECK(server_.get() == NULL);
}

void DevToolsHttpHandlerImpl::Start() {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&DevToolsHttpHandlerImpl::Init, this));
}

void DevToolsHttpHandlerImpl::Stop() {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&DevToolsHttpHandlerImpl::TeardownAndRelease, this));
}

static std::string PathWithoutParams(const std::string& path) {
  size_t query_position = path.find("?");
  if (query_position != std::string::npos)
    return path.substr(0, query_position);
  return path;
}

static std::string GetMimeType(const std::string& filename) {
  if (EndsWith(filename, ".html", false)) {
    return "text/html";
  } else if (EndsWith(filename, ".css", false)) {
    return "text/css";
  } else if (EndsWith(filename, ".js", false)) {
    return "application/javascript";
  } else if (EndsWith(filename, ".png", false)) {
    return "image/png";
  } else if (EndsWith(filename, ".gif", false)) {
    return "image/gif";
  }
  NOTREACHED();
  return "text/plain";
}

void DevToolsHttpHandlerImpl::OnHttpRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info) {
  if (info.path.find("/json") == 0) {
    // Pages discovery json request.
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&DevToolsHttpHandlerImpl::OnJsonRequestUI,
                   this,
                   connection_id,
                   info));
    return;
  }

  if (info.path == "" || info.path == "/") {
    std::string response = delegate_->GetDiscoveryPageHTML();
    server_->Send200(connection_id, response, "text/html; charset=UTF-8");
    return;
  }

  // Proxy static files from chrome-devtools://devtools/*.
  net::URLRequestContext* request_context = delegate_->GetURLRequestContext();
  if (!request_context) {
    server_->Send404(connection_id);
    return;
  }

  net::URLRequest* request;

  if (info.path.find("/devtools/") == 0) {
    // Serve front-end files from resource bundle.
    std::string filename = PathWithoutParams(info.path.substr(10));

    if (delegate_->BundlesFrontendResources()) {
      int resource_id = DevToolsHttpHandler::GetFrontendResourceId(filename);
      if (resource_id != -1) {
        base::StringPiece data =
            content::GetContentClient()->GetDataResource(resource_id);
        server_->Send200(connection_id,
                         data.as_string(),
                         GetMimeType(filename));
      }
      return;
    }
    std::string base_url = delegate_->GetFrontendResourcesBaseURL();
    request = new net::URLRequest(GURL(base_url + filename), this);
  } else if (info.path.find("/thumb/") == 0) {
    request = new net::URLRequest(GURL("chrome:/" + info.path), this);
  } else {
    server_->Send404(connection_id);
    return;
  }

  Bind(request, connection_id);
  request->set_context(request_context);
  request->Start();
}

void DevToolsHttpHandlerImpl::OnWebSocketRequest(
    int connection_id,
    const net::HttpServerRequestInfo& request) {
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(
          &DevToolsHttpHandlerImpl::OnWebSocketRequestUI,
          this,
          connection_id,
          request));
}

void DevToolsHttpHandlerImpl::OnWebSocketMessage(
    int connection_id,
    const std::string& data) {
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(
          &DevToolsHttpHandlerImpl::OnWebSocketMessageUI,
          this,
          connection_id,
          data));
}

void DevToolsHttpHandlerImpl::OnClose(int connection_id) {
  ConnectionToRequestsMap::iterator it =
      connection_to_requests_io_.find(connection_id);
  if (it != connection_to_requests_io_.end()) {
    // Dispose delegating socket.
    for (std::set<net::URLRequest*>::iterator it2 = it->second.begin();
         it2 != it->second.end(); ++it2) {
      net::URLRequest* request = *it2;
      request->Cancel();
      request_to_connection_io_.erase(request);
      request_to_buffer_io_.erase(request);
      delete request;
    }
    connection_to_requests_io_.erase(connection_id);
  }

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(
          &DevToolsHttpHandlerImpl::OnCloseUI,
          this,
          connection_id));
}

struct PageInfo
{
  int id;
  std::string url;
  bool attached;
  std::string title;
  std::string thumbnail_url;
  std::string favicon_url;
  base::TimeTicks last_selected_time;
};
typedef std::vector<PageInfo> PageList;

static bool SortPageListByTime(const PageInfo& info1, const PageInfo& info2) {
  return info1.last_selected_time > info2.last_selected_time;
}

static PageList GeneratePageList(
    DevToolsHttpHandlerDelegate* delegate,
    int connection_id,
    const net::HttpServerRequestInfo& info) {
  typedef DevToolsHttpHandlerDelegate::InspectableTabs Tabs;
  Tabs inspectable_tabs = delegate->GetInspectableTabs();

  PageList page_list;
  for (Tabs::iterator it = inspectable_tabs.begin();
       it != inspectable_tabs.end(); ++it) {

    WebContents* web_contents = *it;
    NavigationController& controller = web_contents->GetController();

    NavigationEntry* entry = controller.GetActiveEntry();
    if (entry == NULL || !entry->GetURL().is_valid())
      continue;

    DevToolsAgentHost* agent = DevToolsAgentHostRegistry::GetDevToolsAgentHost(
        web_contents->GetRenderViewHost());
    DevToolsClientHost* client_host = DevToolsManager::GetInstance()->
        GetDevToolsClientHostFor(agent);
    PageInfo page_info;
    page_info.id = TabContentsIDHelper::GetID(
        static_cast<TabContents*>(web_contents));
    page_info.attached = client_host != NULL;
    page_info.url = entry->GetURL().spec();
    page_info.title = UTF16ToUTF8(net::EscapeForHTML(entry->GetTitle()));
    page_info.thumbnail_url = "/thumb/" + entry->GetURL().spec();
    page_info.favicon_url = entry->GetFavicon().url.spec();
    page_info.last_selected_time = web_contents->GetLastSelectedTime();
    page_list.push_back(page_info);
  }
  std::sort(page_list.begin(), page_list.end(), SortPageListByTime);
  return page_list;
}

void DevToolsHttpHandlerImpl::OnJsonRequestUI(
    int connection_id,
    const net::HttpServerRequestInfo& info) {
  PageList page_list = GeneratePageList(delegate_.get(),
                                        connection_id, info);
  ListValue json_pages_list;
  std::string host = info.headers["Host"];
  for (PageList::iterator i = page_list.begin();
       i != page_list.end(); ++i) {

    DictionaryValue* page_info = new DictionaryValue;
    json_pages_list.Append(page_info);
    page_info->SetString("title", i->title);
    page_info->SetString("url", i->url);
    page_info->SetString("thumbnailUrl", i->thumbnail_url);
    page_info->SetString("faviconUrl", i->favicon_url);
    if (!i->attached) {
      page_info->SetString("webSocketDebuggerUrl",
                           base::StringPrintf("ws://%s/devtools/page/%d",
                                              host.c_str(),
                                              i->id));
      std::string devtools_frontend_url = base::StringPrintf(
          "%s%shost=%s&page=%d",
          overridden_frontend_url_.c_str(),
          overridden_frontend_url_.find("?") == std::string::npos ? "?" : "&",
          host.c_str(),
          i->id);
      page_info->SetString("devtoolsFrontendUrl", devtools_frontend_url);
    }
  }

  std::string response;
  base::JSONWriter::Write(&json_pages_list, true, &response);
  Send200(connection_id, response, "application/json; charset=UTF-8");
}

void DevToolsHttpHandlerImpl::OnWebSocketRequestUI(
    int connection_id,
    const net::HttpServerRequestInfo& request) {
  std::string prefix = "/devtools/page/";
  size_t pos = request.path.find(prefix);
  if (pos != 0) {
    Send404(connection_id);
    return;
  }
  std::string page_id = request.path.substr(prefix.length());
  int id = 0;
  if (!base::StringToInt(page_id, &id)) {
    Send500(connection_id, "Invalid page id: " + page_id);
    return;
  }

  TabContents* web_contents = TabContentsIDHelper::GetTabContents(id);
  if (web_contents == NULL) {
    Send500(connection_id, "No such page id: " + page_id);
    return;
  }

  DevToolsManager* manager = DevToolsManager::GetInstance();
  DevToolsAgentHost* agent = DevToolsAgentHostRegistry::GetDevToolsAgentHost(
      web_contents->GetRenderViewHost());
  if (manager->GetDevToolsClientHostFor(agent)) {
    Send500(connection_id, "Page with given id is being inspected: " + page_id);
    return;
  }

  DevToolsClientHostImpl* client_host =
      new DevToolsClientHostImpl(server_, connection_id);
  connection_to_client_host_ui_[connection_id] = client_host;

  manager->RegisterDevToolsClientHostFor(agent, client_host);

  AcceptWebSocket(connection_id, request);
}

void DevToolsHttpHandlerImpl::OnWebSocketMessageUI(
    int connection_id,
    const std::string& data) {
  ConnectionToClientHostMap::iterator it =
      connection_to_client_host_ui_.find(connection_id);
  if (it == connection_to_client_host_ui_.end())
    return;

  DevToolsManager* manager = DevToolsManager::GetInstance();
  manager->DispatchOnInspectorBackend(it->second, data);
}

void DevToolsHttpHandlerImpl::OnCloseUI(int connection_id) {
  ConnectionToClientHostMap::iterator it =
      connection_to_client_host_ui_.find(connection_id);
  if (it != connection_to_client_host_ui_.end()) {
    DevToolsClientHostImpl* client_host =
        static_cast<DevToolsClientHostImpl*>(it->second);
    DevToolsManager::GetInstance()->ClientHostClosing(client_host);
    delete client_host;
    connection_to_client_host_ui_.erase(connection_id);
  }
}

void DevToolsHttpHandlerImpl::OnResponseStarted(net::URLRequest* request) {
  RequestToSocketMap::iterator it = request_to_connection_io_.find(request);
  if (it == request_to_connection_io_.end())
    return;

  int connection_id = it->second;

  std::string content_type;
  request->GetMimeType(&content_type);

  if (request->status().is_success()) {
    server_->Send(connection_id,
                  base::StringPrintf("HTTP/1.1 200 OK\r\n"
                                     "Content-Type:%s\r\n"
                                     "Transfer-Encoding: chunked\r\n"
                                     "\r\n",
                                     content_type.c_str()));
  } else {
    server_->Send404(connection_id);
  }

  int bytes_read = 0;
  // Some servers may treat HEAD requests as GET requests.  To free up the
  // network connection as soon as possible, signal that the request has
  // completed immediately, without trying to read any data back (all we care
  // about is the response code and headers, which we already have).
  net::IOBuffer* buffer = request_to_buffer_io_[request].get();
  if (request->status().is_success())
    request->Read(buffer, kBufferSize, &bytes_read);
  OnReadCompleted(request, bytes_read);
}

void DevToolsHttpHandlerImpl::OnReadCompleted(net::URLRequest* request,
                                                  int bytes_read) {
  RequestToSocketMap::iterator it = request_to_connection_io_.find(request);
  if (it == request_to_connection_io_.end())
    return;

  int connection_id = it->second;

  net::IOBuffer* buffer = request_to_buffer_io_[request].get();
  do {
    if (!request->status().is_success() || bytes_read <= 0)
      break;
    std::string chunk_size = base::StringPrintf("%X\r\n", bytes_read);
    server_->Send(connection_id, chunk_size);
    server_->Send(connection_id, buffer->data(), bytes_read);
    server_->Send(connection_id, "\r\n");
  } while (request->Read(buffer, kBufferSize, &bytes_read));


  // See comments re: HEAD requests in OnResponseStarted().
  if (!request->status().is_io_pending()) {
    server_->Send(connection_id, "0\r\n\r\n");
    RequestCompleted(request);
  }
}

DevToolsHttpHandlerImpl::DevToolsHttpHandlerImpl(
    const std::string& ip,
    int port,
    const std::string& frontend_url,
    DevToolsHttpHandlerDelegate* delegate)
    : ip_(ip),
      port_(port),
      overridden_frontend_url_(frontend_url),
      delegate_(delegate) {
  if (overridden_frontend_url_.empty())
      overridden_frontend_url_ = "/devtools/devtools.html";

  AddRef();
}

void DevToolsHttpHandlerImpl::Init() {
  server_ = new net::HttpServer(ip_, port_, this);
}

// Run on I/O thread
void DevToolsHttpHandlerImpl::TeardownAndRelease() {
  server_ = NULL;
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&DevToolsHttpHandlerImpl::Release, this));
}

void DevToolsHttpHandlerImpl::Bind(net::URLRequest* request,
                                       int connection_id) {
  request_to_connection_io_[request] = connection_id;
  ConnectionToRequestsMap::iterator it =
      connection_to_requests_io_.find(connection_id);
  if (it == connection_to_requests_io_.end()) {
    std::pair<int, std::set<net::URLRequest*> > value(
        connection_id,
        std::set<net::URLRequest*>());
    it = connection_to_requests_io_.insert(value).first;
  }
  it->second.insert(request);
  request_to_buffer_io_[request] = new net::IOBuffer(kBufferSize);
}

void DevToolsHttpHandlerImpl::RequestCompleted(net::URLRequest* request) {
  RequestToSocketMap::iterator it = request_to_connection_io_.find(request);
  if (it == request_to_connection_io_.end())
    return;

  int connection_id = it->second;
  request_to_connection_io_.erase(request);
  ConnectionToRequestsMap::iterator it2 =
      connection_to_requests_io_.find(connection_id);
  it2->second.erase(request);
  request_to_buffer_io_.erase(request);
  delete request;
}

void DevToolsHttpHandlerImpl::Send200(int connection_id,
                                          const std::string& data,
                                          const std::string& mime_type) {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&net::HttpServer::Send200,
                 server_.get(),
                 connection_id,
                 data,
                 mime_type));
}

void DevToolsHttpHandlerImpl::Send404(int connection_id) {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&net::HttpServer::Send404, server_.get(), connection_id));
}

void DevToolsHttpHandlerImpl::Send500(int connection_id,
                                      const std::string& message) {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&net::HttpServer::Send500, server_.get(), connection_id,
                 message));
}

void DevToolsHttpHandlerImpl::AcceptWebSocket(
    int connection_id,
    const net::HttpServerRequestInfo& request) {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&net::HttpServer::AcceptWebSocket, server_.get(),
                 connection_id, request));
}

}  // namespace content
