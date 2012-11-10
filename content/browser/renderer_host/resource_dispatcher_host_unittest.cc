// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/resource_dispatcher_host_impl.h"

#include <vector>

#include "base/bind.h"
#include "base/file_path.h"
#include "base/memory/scoped_vector.h"
#include "base/message_loop.h"
#include "base/process_util.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "content/browser/browser_thread_impl.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/renderer_host/resource_dispatcher_host_impl.h"
#include "content/browser/renderer_host/resource_message_filter.h"
#include "content/common/child_process_host_impl.h"
#include "content/common/resource_messages.h"
#include "content/common/view_messages.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/resource_dispatcher_host_delegate.h"
#include "content/public/browser/resource_throttle.h"
#include "content/public/common/resource_response.h"
#include "content/public/test/test_browser_context.h"
#include "content/test/test_content_browser_client.h"
#include "net/base/net_errors.h"
#include "net/base/upload_data.h"
#include "net/http/http_util.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_job.h"
#include "net/url_request/url_request_simple_job.h"
#include "net/url_request/url_request_test_job.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/appcache/appcache_interfaces.h"

// TODO(eroman): Write unit tests for SafeBrowsing that exercise
//               SafeBrowsingResourceHandler.

namespace content {

namespace {

// Returns the resource response header structure for this request.
void GetResponseHead(const std::vector<IPC::Message>& messages,
                     content::ResourceResponseHead* response_head) {
  ASSERT_GE(messages.size(), 2U);

  // The first messages should be received response.
  ASSERT_EQ(ResourceMsg_ReceivedResponse::ID, messages[0].type());

  PickleIterator iter(messages[0]);
  int request_id;
  ASSERT_TRUE(IPC::ReadParam(&messages[0], &iter, &request_id));
  ASSERT_TRUE(IPC::ReadParam(&messages[0], &iter, response_head));
}

void GenerateIPCMessage(
    scoped_refptr<ResourceMessageFilter> filter,
    scoped_ptr<IPC::Message> message) {
  bool msg_is_ok;
  ResourceDispatcherHostImpl::Get()->OnMessageReceived(
      *message, filter.get(), &msg_is_ok);
}

}  // namespace

static int RequestIDForMessage(const IPC::Message& msg) {
  int request_id = -1;
  switch (msg.type()) {
    case ResourceMsg_UploadProgress::ID:
    case ResourceMsg_ReceivedResponse::ID:
    case ResourceMsg_ReceivedRedirect::ID:
    case ResourceMsg_DataReceived::ID:
    case ResourceMsg_RequestComplete::ID:
      request_id = IPC::MessageIterator(msg).NextInt();
      break;
  }
  return request_id;
}

static ResourceHostMsg_Request CreateResourceRequest(
    const char* method,
    ResourceType::Type type,
    const GURL& url) {
  ResourceHostMsg_Request request;
  request.method = std::string(method);
  request.url = url;
  request.first_party_for_cookies = url;  // bypass third-party cookie blocking
  request.referrer_policy = WebKit::WebReferrerPolicyDefault;
  request.load_flags = 0;
  request.origin_pid = 0;
  request.resource_type = type;
  request.request_context = 0;
  request.appcache_host_id = appcache::kNoHostId;
  request.download_to_file = false;
  request.is_main_frame = true;
  request.frame_id = 0;
  request.parent_is_main_frame = false;
  request.parent_frame_id = -1;
  request.transition_type = content::PAGE_TRANSITION_LINK;
  request.allow_download = true;
  return request;
}

// Spin up the message loop to kick off the request.
static void KickOffRequest() {
  MessageLoop::current()->RunAllPending();
}

// We may want to move this to a shared space if it is useful for something else
class ResourceIPCAccumulator {
 public:
  void AddMessage(const IPC::Message& msg) {
    messages_.push_back(msg);
  }

  // This groups the messages by their request ID. The groups will be in order
  // that the first message for each request ID was received, and the messages
  // within the groups will be in the order that they appeared.
  // Note that this clears messages_.
  typedef std::vector< std::vector<IPC::Message> > ClassifiedMessages;
  void GetClassifiedMessages(ClassifiedMessages* msgs);

 private:
  std::vector<IPC::Message> messages_;
};

// This is very inefficient as a result of repeatedly extracting the ID, use
// only for tests!
void ResourceIPCAccumulator::GetClassifiedMessages(ClassifiedMessages* msgs) {
  while (!messages_.empty()) {
    // Ignore unknown message types as it is valid for code to generated other
    // IPCs as side-effects that we are not testing here.
    int cur_id = RequestIDForMessage(messages_[0]);
    if (cur_id != -1) {
      std::vector<IPC::Message> cur_requests;
      cur_requests.push_back(messages_[0]);
      // find all other messages with this ID
      for (int i = 1; i < static_cast<int>(messages_.size()); i++) {
        int id = RequestIDForMessage(messages_[i]);
        if (id == cur_id) {
          cur_requests.push_back(messages_[i]);
          messages_.erase(messages_.begin() + i);
          i--;
        }
      }
      msgs->push_back(cur_requests);
    }
    messages_.erase(messages_.begin());
  }
}

class MockURLRequestContextSelector
    : public ResourceMessageFilter::URLRequestContextSelector {
 public:
  explicit MockURLRequestContextSelector(
      net::URLRequestContext* request_context)
      : request_context_(request_context) {}

  virtual net::URLRequestContext* GetRequestContext(
      ResourceType::Type request_type) {
    return request_context_;
  }

 private:
  net::URLRequestContext* const request_context_;
};

// This class forwards the incoming messages to the ResourceDispatcherHostTest.
// This is used to emulate different sub-processes, since this filter will
// have a different ID than the original. For the test, we want all the incoming
// messages to go to the same place, which is why this forwards.
class ForwardingFilter : public ResourceMessageFilter {
 public:
  explicit ForwardingFilter(IPC::Sender* dest,
                            content::ResourceContext* resource_context)
    : ResourceMessageFilter(
        ChildProcessHostImpl::GenerateChildProcessUniqueId(),
        content::PROCESS_TYPE_RENDERER,
        resource_context,
        new MockURLRequestContextSelector(
            resource_context->GetRequestContext())),
      dest_(dest) {
    OnChannelConnected(base::GetCurrentProcId());
  }

  // ResourceMessageFilter override
  virtual bool Send(IPC::Message* msg) {
    if (!dest_)
      return false;
    return dest_->Send(msg);
  }

 protected:
  virtual ~ForwardingFilter() {}

 private:
  IPC::Sender* dest_;

  DISALLOW_COPY_AND_ASSIGN(ForwardingFilter);
};

// This class is a variation on URLRequestTestJob in that it does
// not complete start upon entry, only when specifically told to.
class URLRequestTestDelayedStartJob : public net::URLRequestTestJob {
 public:
  URLRequestTestDelayedStartJob(net::URLRequest* request)
      : net::URLRequestTestJob(request) {
    Init();
  }
  URLRequestTestDelayedStartJob(net::URLRequest* request, bool auto_advance)
      : net::URLRequestTestJob(request, auto_advance) {
    Init();
  }
  URLRequestTestDelayedStartJob(net::URLRequest* request,
                                const std::string& response_headers,
                                const std::string& response_data,
                                bool auto_advance)
      : net::URLRequestTestJob(
          request, response_headers, response_data, auto_advance) {
    Init();
  }

  // Do nothing until you're told to.
  virtual void Start() {}

  // Finish starting a URL request whose job is an instance of
  // URLRequestTestDelayedStartJob.  It is illegal to call this routine
  // with a URLRequest that does not use URLRequestTestDelayedStartJob.
  static void CompleteStart(net::URLRequest* request) {
    for (URLRequestTestDelayedStartJob* job = list_head_;
         job;
         job = job->next_) {
      if (job->request() == request) {
        job->net::URLRequestTestJob::Start();
        return;
      }
    }
    NOTREACHED();
  }

  static bool DelayedStartQueueEmpty() {
    return !list_head_;
  }

  static void ClearQueue() {
    if (list_head_) {
      LOG(ERROR)
          << "Unreleased entries on URLRequestTestDelayedStartJob delay queue"
          << "; may result in leaks.";
      list_head_ = NULL;
    }
  }

 protected:
  virtual ~URLRequestTestDelayedStartJob() {
    for (URLRequestTestDelayedStartJob** job = &list_head_; *job;
         job = &(*job)->next_) {
      if (*job == this) {
        *job = (*job)->next_;
        return;
      }
    }
    NOTREACHED();
  }

 private:
  void Init() {
    next_ = list_head_;
    list_head_ = this;
  }

  static URLRequestTestDelayedStartJob* list_head_;
  URLRequestTestDelayedStartJob* next_;
};

URLRequestTestDelayedStartJob*
URLRequestTestDelayedStartJob::list_head_ = NULL;

// This class is a variation on URLRequestTestJob in that it
// returns IO_pending errors before every read, not just the first one.
class URLRequestTestDelayedCompletionJob : public net::URLRequestTestJob {
 public:
  explicit URLRequestTestDelayedCompletionJob(net::URLRequest* request)
      : net::URLRequestTestJob(request) {}
  URLRequestTestDelayedCompletionJob(net::URLRequest* request,
                                     bool auto_advance)
      : net::URLRequestTestJob(request, auto_advance) {}
  URLRequestTestDelayedCompletionJob(net::URLRequest* request,
                                     const std::string& response_headers,
                                     const std::string& response_data,
                                     bool auto_advance)
      : net::URLRequestTestJob(request, response_headers,
                               response_data, auto_advance) {}

 protected:
  ~URLRequestTestDelayedCompletionJob() {}

 private:
  virtual bool NextReadAsync() OVERRIDE { return true; }
};

class URLRequestBigJob : public net::URLRequestSimpleJob {
 public:
  URLRequestBigJob(net::URLRequest* request)
      : net::URLRequestSimpleJob(request) {
  }

  virtual int GetData(std::string* mime_type,
                      std::string* charset,
                      std::string* data,
                      const net::CompletionCallback& callback) const OVERRIDE {
    *mime_type = "text/plain";
    *charset = "UTF-8";

    std::string text;
    int count;
    if (!ParseURL(request_->url(), &text, &count))
      return net::ERR_INVALID_URL;

    data->reserve(text.size() * count);
    for (int i = 0; i < count; ++i)
      data->append(text);

    return net::OK;
  }

 private:
  virtual ~URLRequestBigJob() {}

  // big-job:substring,N
  static bool ParseURL(const GURL& url, std::string* text, int* count) {
    std::vector<std::string> parts;
    base::SplitString(url.path(), ',', &parts);

    if (parts.size() != 2)
      return false;

    *text = parts[0];
    return base::StringToInt(parts[1], count);
  }
};

// Associated with an URLRequest to determine if the URLRequest gets deleted.
class TestUserData : public base::SupportsUserData::Data {
 public:
  explicit TestUserData(bool* was_deleted)
      : was_deleted_(was_deleted) {
  }

  ~TestUserData() {
    *was_deleted_ = true;
  }

 private:
  bool* was_deleted_;
};

class TransfersAllNavigationsContentBrowserClient
    : public TestContentBrowserClient {
 public:
  virtual bool ShouldSwapProcessesForRedirect(ResourceContext* resource_context,
                                              const GURL& current_url,
                                              const GURL& new_url) {
    return true;
  }
};

enum {
  DEFER_NONE                = 0,
  DEFER_STARTING_REQUEST    = 1 << 0,
  DEFER_PROCESSING_RESPONSE = 1 << 1
};

// Throttle that tracks the current throttle blocking a request.  Only one
// can throttle any request at a time.
class GenericResourceThrottle : public content::ResourceThrottle {
 public:
  explicit GenericResourceThrottle(int defer_flags)
      : defer_flags_(defer_flags) {
  }

  virtual ~GenericResourceThrottle() {
    if (active_throttle_ == this)
      active_throttle_ = NULL;
  }

  // content::ResourceThrottle implementation:
  virtual void WillStartRequest(bool* defer) OVERRIDE {
    ASSERT_EQ(NULL, active_throttle_);
    if (defer_flags_ & DEFER_STARTING_REQUEST) {
      active_throttle_ = this;
      *defer = true;
    }
  }

  virtual void WillProcessResponse(bool* defer) OVERRIDE {
    ASSERT_EQ(NULL, active_throttle_);
    if (defer_flags_ & DEFER_PROCESSING_RESPONSE) {
      active_throttle_ = this;
      *defer = true;
    }
  }

  void Resume() {
    ASSERT_TRUE(this == active_throttle_);
    active_throttle_ = NULL;
    controller()->Resume();
  }

  static GenericResourceThrottle* active_throttle() {
    return active_throttle_;
  }

 private:
  int defer_flags_;  // bit-wise union of DEFER_XXX flags.

  // The currently active throttle, if any.
  static GenericResourceThrottle* active_throttle_;
};
// static
GenericResourceThrottle* GenericResourceThrottle::active_throttle_ = NULL;

class TestResourceDispatcherHostDelegate
    : public content::ResourceDispatcherHostDelegate {
 public:
  TestResourceDispatcherHostDelegate()
      : create_two_throttles_(false),
        defer_flags_(DEFER_NONE) {
  }

  void set_url_request_user_data(base::SupportsUserData::Data* user_data) {
    user_data_.reset(user_data);
  }

  void set_defer_flags(int value) {
    defer_flags_ = value;
  }

  void set_create_two_throttles(bool create_two_throttles) {
    create_two_throttles_ = create_two_throttles;
  }

  // ResourceDispatcherHostDelegate implementation:

  virtual void RequestBeginning(
      net::URLRequest* request,
      content::ResourceContext* resource_context,
      ResourceType::Type resource_type,
      int child_id,
      int route_id,
      bool is_continuation_of_transferred_request,
      ScopedVector<content::ResourceThrottle>* throttles) OVERRIDE {
    if (user_data_.get()) {
      const void* key = user_data_.get();
      request->SetUserData(key, user_data_.release());
    }

    if (defer_flags_ != DEFER_NONE) {
      throttles->push_back(new GenericResourceThrottle(defer_flags_));
      if (create_two_throttles_)
        throttles->push_back(new GenericResourceThrottle(defer_flags_));
    }
  }

 private:
  bool create_two_throttles_;
  int defer_flags_;
  scoped_ptr<base::SupportsUserData::Data> user_data_;
};

class ResourceDispatcherHostTest : public testing::Test,
                                   public IPC::Sender {
 public:
  ResourceDispatcherHostTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        file_thread_(BrowserThread::FILE_USER_BLOCKING, &message_loop_),
        cache_thread_(BrowserThread::CACHE, &message_loop_),
        io_thread_(BrowserThread::IO, &message_loop_),
        old_factory_(NULL),
        resource_type_(ResourceType::SUB_RESOURCE),
        send_data_received_acks_(false) {
    browser_context_.reset(new content::TestBrowserContext());
    BrowserContext::EnsureResourceContextInitialized(browser_context_.get());
    message_loop_.RunAllPending();
    filter_ = new ForwardingFilter(
        this, browser_context_->GetResourceContext());
  }
  // IPC::Sender implementation
  virtual bool Send(IPC::Message* msg) {
    accum_.AddMessage(*msg);

    if (send_data_received_acks_ &&
        msg->type() == ResourceMsg_DataReceived::ID) {
      GenerateDataReceivedACK(*msg);
    }

    delete msg;
    return true;
  }

 protected:
  // testing::Test
  virtual void SetUp() {
    DCHECK(!test_fixture_);
    test_fixture_ = this;
    ChildProcessSecurityPolicyImpl::GetInstance()->Add(0);
    net::URLRequest::Deprecated::RegisterProtocolFactory(
        "test",
        &ResourceDispatcherHostTest::Factory);
    EnsureTestSchemeIsAllowed();
    delay_start_ = false;
    delay_complete_ = false;
  }

  virtual void TearDown() {
    net::URLRequest::Deprecated::RegisterProtocolFactory("test", NULL);
    if (!scheme_.empty())
      net::URLRequest::Deprecated::RegisterProtocolFactory(
          scheme_, old_factory_);

    EXPECT_TRUE(URLRequestTestDelayedStartJob::DelayedStartQueueEmpty());
    URLRequestTestDelayedStartJob::ClearQueue();

    DCHECK(test_fixture_ == this);
    test_fixture_ = NULL;

    host_.Shutdown();

    ChildProcessSecurityPolicyImpl::GetInstance()->Remove(0);

    // Flush the message loop to make application verifiers happy.
    if (ResourceDispatcherHostImpl::Get())
      ResourceDispatcherHostImpl::Get()->CancelRequestsForContext(
          browser_context_->GetResourceContext());
    browser_context_.reset();
    message_loop_.RunAllPending();
  }

  // Creates a request using the current test object as the filter.
  void MakeTestRequest(int render_view_id,
                       int request_id,
                       const GURL& url);

  // Generates a request using the given filter. This will probably be a
  // ForwardingFilter.
  void MakeTestRequest(ResourceMessageFilter* filter,
                       int render_view_id,
                       int request_id,
                       const GURL& url);

  void CancelRequest(int request_id);

  void CompleteStartRequest(int request_id);
  void CompleteStartRequest(ResourceMessageFilter* filter, int request_id);

  void EnsureSchemeIsAllowed(const std::string& scheme) {
    ChildProcessSecurityPolicyImpl* policy =
        ChildProcessSecurityPolicyImpl::GetInstance();
    if (!policy->IsWebSafeScheme(scheme))
      policy->RegisterWebSafeScheme(scheme);
  }

  void EnsureTestSchemeIsAllowed() {
    EnsureSchemeIsAllowed("test");
  }

  // Sets a particular response for any request from now on. To switch back to
  // the default bahavior, pass an empty |headers|. |headers| should be raw-
  // formatted (NULLs instead of EOLs).
  void SetResponse(const std::string& headers, const std::string& data) {
    response_headers_ = net::HttpUtil::AssembleRawHeaders(headers.data(),
                                                          headers.size());
    response_data_ = data;
  }
  void SetResponse(const std::string& headers) {
    SetResponse(headers, std::string());
  }

  // Sets a particular resource type for any request from now on.
  void SetResourceType(ResourceType::Type type) {
    resource_type_ = type;
  }

  void SendDataReceivedACKs(bool send_acks) {
    send_data_received_acks_ = send_acks;
  }

  // Intercepts requests for the given protocol.
  void HandleScheme(const std::string& scheme) {
    DCHECK(scheme_.empty());
    DCHECK(!old_factory_);
    scheme_ = scheme;
    old_factory_ = net::URLRequest::Deprecated::RegisterProtocolFactory(
        scheme_, &ResourceDispatcherHostTest::Factory);
    EnsureSchemeIsAllowed(scheme);
  }

  // Our own net::URLRequestJob factory.
  static net::URLRequestJob* Factory(net::URLRequest* request,
                                     const std::string& scheme) {
    if (test_fixture_->response_headers_.empty()) {
      if (delay_start_) {
        return new URLRequestTestDelayedStartJob(request);
      } else if (delay_complete_) {
        return new URLRequestTestDelayedCompletionJob(request);
      } else if (scheme == "big-job") {
        return new URLRequestBigJob(request);
      } else {
        return new net::URLRequestTestJob(request);
      }
    } else {
      if (delay_start_) {
        return new URLRequestTestDelayedStartJob(
            request, test_fixture_->response_headers_,
            test_fixture_->response_data_, false);
      } else if (delay_complete_) {
        return new URLRequestTestDelayedCompletionJob(
            request, test_fixture_->response_headers_,
            test_fixture_->response_data_, false);
      } else {
        return new net::URLRequestTestJob(request,
                                          test_fixture_->response_headers_,
                                          test_fixture_->response_data_,
                                          false);
      }
    }
  }

  void SetDelayedStartJobGeneration(bool delay_job_start) {
    delay_start_ = delay_job_start;
  }

  void SetDelayedCompleteJobGeneration(bool delay_job_complete) {
    delay_complete_ = delay_job_complete;
  }

  void GenerateDataReceivedACK(const IPC::Message& msg) {
    EXPECT_EQ(ResourceMsg_DataReceived::ID, msg.type());

    int request_id = IPC::MessageIterator(msg).NextInt();
    scoped_ptr<IPC::Message> ack(
        new ResourceHostMsg_DataReceived_ACK(msg.routing_id(), request_id));

    MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&GenerateIPCMessage, filter_, base::Passed(&ack)));
  }

  MessageLoopForIO message_loop_;
  BrowserThreadImpl ui_thread_;
  BrowserThreadImpl file_thread_;
  BrowserThreadImpl cache_thread_;
  BrowserThreadImpl io_thread_;
  scoped_ptr<content::TestBrowserContext> browser_context_;
  scoped_refptr<ForwardingFilter> filter_;
  ResourceDispatcherHostImpl host_;
  ResourceIPCAccumulator accum_;
  std::string response_headers_;
  std::string response_data_;
  std::string scheme_;
  net::URLRequest::ProtocolFactory* old_factory_;
  ResourceType::Type resource_type_;
  bool send_data_received_acks_;
  static ResourceDispatcherHostTest* test_fixture_;
  static bool delay_start_;
  static bool delay_complete_;
};
// Static.
ResourceDispatcherHostTest* ResourceDispatcherHostTest::test_fixture_ = NULL;
bool ResourceDispatcherHostTest::delay_start_ = false;
bool ResourceDispatcherHostTest::delay_complete_ = false;

void ResourceDispatcherHostTest::MakeTestRequest(int render_view_id,
                                                 int request_id,
                                                 const GURL& url) {
  MakeTestRequest(filter_.get(), render_view_id, request_id, url);
}

void ResourceDispatcherHostTest::MakeTestRequest(
    ResourceMessageFilter* filter,
    int render_view_id,
    int request_id,
    const GURL& url) {
  ResourceHostMsg_Request request =
      CreateResourceRequest("GET", resource_type_, url);
  ResourceHostMsg_RequestResource msg(render_view_id, request_id, request);
  bool msg_was_ok;
  host_.OnMessageReceived(msg, filter, &msg_was_ok);
  KickOffRequest();
}

void ResourceDispatcherHostTest::CancelRequest(int request_id) {
  host_.CancelRequest(filter_->child_id(), request_id, false);
}

void ResourceDispatcherHostTest::CompleteStartRequest(int request_id) {
  CompleteStartRequest(filter_, request_id);
}

void ResourceDispatcherHostTest::CompleteStartRequest(
    ResourceMessageFilter* filter,
    int request_id) {
  GlobalRequestID gid(filter->child_id(), request_id);
  net::URLRequest* req = host_.GetURLRequest(gid);
  EXPECT_TRUE(req);
  if (req)
    URLRequestTestDelayedStartJob::CompleteStart(req);
}

void CheckSuccessfulRequest(const std::vector<IPC::Message>& messages,
                            const std::string& reference_data) {
  // A successful request will have received 4 messages:
  //     ReceivedResponse    (indicates headers received)
  //     DataReceived        (data)
  //    XXX DataReceived        (0 bytes remaining from a read)
  //     RequestComplete     (request is done)
  //
  // This function verifies that we received 4 messages and that they
  // are appropriate.
  ASSERT_EQ(3U, messages.size());

  // The first messages should be received response
  ASSERT_EQ(ResourceMsg_ReceivedResponse::ID, messages[0].type());

  // followed by the data, currently we only do the data in one chunk, but
  // should probably test multiple chunks later
  ASSERT_EQ(ResourceMsg_DataReceived::ID, messages[1].type());

  PickleIterator iter(messages[1]);
  int request_id;
  ASSERT_TRUE(IPC::ReadParam(&messages[1], &iter, &request_id));
  base::SharedMemoryHandle shm_handle;
  ASSERT_TRUE(IPC::ReadParam(&messages[1], &iter, &shm_handle));
  uint32 data_len;
  ASSERT_TRUE(IPC::ReadParam(&messages[1], &iter, &data_len));

  ASSERT_EQ(reference_data.size(), data_len);
  base::SharedMemory shared_mem(shm_handle, true);  // read only
  shared_mem.Map(data_len);
  const char* data = static_cast<char*>(shared_mem.memory());
  ASSERT_EQ(0, memcmp(reference_data.c_str(), data, data_len));

  // followed by a 0-byte read
  //ASSERT_EQ(ResourceMsg_DataReceived::ID, messages[2].type());

  // the last message should be all data received
  ASSERT_EQ(ResourceMsg_RequestComplete::ID, messages[2].type());
}

// Tests whether many messages get dispatched properly.
TEST_F(ResourceDispatcherHostTest, TestMany) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  MakeTestRequest(0, 1, net::URLRequestTestJob::test_url_1());
  MakeTestRequest(0, 2, net::URLRequestTestJob::test_url_2());
  MakeTestRequest(0, 3, net::URLRequestTestJob::test_url_3());

  // flush all the pending requests
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  // sorts out all the messages we saw by request
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  // there are three requests, so we should have gotten them classified as such
  ASSERT_EQ(3U, msgs.size());

  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_1());
  CheckSuccessfulRequest(msgs[1], net::URLRequestTestJob::test_data_2());
  CheckSuccessfulRequest(msgs[2], net::URLRequestTestJob::test_data_3());
}

// Tests whether messages get canceled properly. We issue three requests,
// cancel one of them, and make sure that each sent the proper notifications.
TEST_F(ResourceDispatcherHostTest, Cancel) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  MakeTestRequest(0, 1, net::URLRequestTestJob::test_url_1());
  MakeTestRequest(0, 2, net::URLRequestTestJob::test_url_2());
  MakeTestRequest(0, 3, net::URLRequestTestJob::test_url_3());
  CancelRequest(2);

  // flush all the pending requests
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}
  MessageLoop::current()->RunAllPending();

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  // there are three requests, so we should have gotten them classified as such
  ASSERT_EQ(3U, msgs.size());

  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_1());
  CheckSuccessfulRequest(msgs[2], net::URLRequestTestJob::test_data_3());

  // Check that request 2 got canceled.
  ASSERT_EQ(2U, msgs[1].size());
  ASSERT_EQ(ResourceMsg_ReceivedResponse::ID, msgs[1][0].type());
  ASSERT_EQ(ResourceMsg_RequestComplete::ID, msgs[1][1].type());

  int request_id;
  net::URLRequestStatus status;

  PickleIterator iter(msgs[1][1]);
  ASSERT_TRUE(IPC::ReadParam(&msgs[1][1], &iter, &request_id));
  ASSERT_TRUE(IPC::ReadParam(&msgs[1][1], &iter, &status));

  EXPECT_EQ(net::URLRequestStatus::CANCELED, status.status());
}

TEST_F(ResourceDispatcherHostTest, CancelWhileStartIsDeferred) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  bool was_deleted = false;

  // Arrange to have requests deferred before starting.
  TestResourceDispatcherHostDelegate delegate;
  delegate.set_defer_flags(DEFER_STARTING_REQUEST);
  delegate.set_url_request_user_data(new TestUserData(&was_deleted));
  host_.SetDelegate(&delegate);

  MakeTestRequest(0, 1, net::URLRequestTestJob::test_url_1());
  CancelRequest(1);

  // Our TestResourceThrottle should not have been deleted yet.  This is to
  // ensure that destruction of the URLRequest happens asynchronously to
  // calling CancelRequest.
  EXPECT_FALSE(was_deleted);

  // flush all the pending requests
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}
  MessageLoop::current()->RunAllPending();

  EXPECT_TRUE(was_deleted);

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));
}

TEST_F(ResourceDispatcherHostTest, PausedStartError) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  // Arrange to have requests deferred before processing response headers.
  TestResourceDispatcherHostDelegate delegate;
  delegate.set_defer_flags(DEFER_PROCESSING_RESPONSE);
  host_.SetDelegate(&delegate);

  SetDelayedStartJobGeneration(true);
  MakeTestRequest(0, 1, net::URLRequestTestJob::test_url_error());
  CompleteStartRequest(1);

  // flush all the pending requests
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}
  MessageLoop::current()->RunAllPending();

  EXPECT_EQ(0, host_.pending_requests());
}

TEST_F(ResourceDispatcherHostTest, ThrottleAndResumeTwice) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  // Arrange to have requests deferred before starting.
  TestResourceDispatcherHostDelegate delegate;
  delegate.set_defer_flags(DEFER_STARTING_REQUEST);
  delegate.set_create_two_throttles(true);
  host_.SetDelegate(&delegate);

  // Make sure the first throttle blocked the request, and then resume.
  MakeTestRequest(0, 1, net::URLRequestTestJob::test_url_1());
  GenericResourceThrottle* first_throttle =
      GenericResourceThrottle::active_throttle();
  ASSERT_TRUE(first_throttle);
  first_throttle->Resume();

  // Make sure the second throttle blocked the request, and then resume.
  ASSERT_TRUE(GenericResourceThrottle::active_throttle());
  ASSERT_NE(first_throttle, GenericResourceThrottle::active_throttle());
  GenericResourceThrottle::active_throttle()->Resume();

  ASSERT_FALSE(GenericResourceThrottle::active_throttle());

  // The request is started asynchronously.
  MessageLoop::current()->RunAllPending();

  // Flush all the pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.pending_requests());
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));

  // Make sure the request completed successfully.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(1U, msgs.size());
  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_1());
}

// The host delegate acts as a second one so we can have some requests
// pending and some canceled.
class TestFilter : public ForwardingFilter {
 public:
  explicit TestFilter(content::ResourceContext* resource_context)
      : ForwardingFilter(NULL, resource_context),
        has_canceled_(false),
        received_after_canceled_(0) {
  }

  // ForwardingFilter override
  virtual bool Send(IPC::Message* msg) OVERRIDE {
    // no messages should be received when the process has been canceled
    if (has_canceled_)
      received_after_canceled_++;
    delete msg;
    return true;
  }

  bool has_canceled_;
  int received_after_canceled_;

 private:
  virtual ~TestFilter() {}
};

// Tests CancelRequestsForProcess
TEST_F(ResourceDispatcherHostTest, TestProcessCancel) {
  scoped_refptr<TestFilter> test_filter = new TestFilter(
      browser_context_->GetResourceContext());

  // request 1 goes to the test delegate
  ResourceHostMsg_Request request = CreateResourceRequest(
      "GET", ResourceType::SUB_RESOURCE, net::URLRequestTestJob::test_url_1());

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  MakeTestRequest(test_filter.get(), 0, 1,
                  net::URLRequestTestJob::test_url_1());

  // request 2 goes to us
  MakeTestRequest(0, 2, net::URLRequestTestJob::test_url_2());

  // request 3 goes to the test delegate
  MakeTestRequest(test_filter.get(), 0, 3,
                  net::URLRequestTestJob::test_url_3());

  // Make sure all requests have finished stage one. test_url_1 will have
  // finished.
  MessageLoop::current()->RunAllPending();

  // TODO(mbelshe):
  // Now that the async IO path is in place, the IO always completes on the
  // initial call; so the requests have already completed.  This basically
  // breaks the whole test.
  //EXPECT_EQ(3, host_.pending_requests());

  // Process each request for one level so one callback is called.
  for (int i = 0; i < 2; i++)
    EXPECT_TRUE(net::URLRequestTestJob::ProcessOnePendingMessage());

  // Cancel the requests to the test process.
  host_.CancelRequestsForProcess(filter_->child_id());
  test_filter->has_canceled_ = true;

  // Flush all the pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.pending_requests());
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));

  // The test delegate should not have gotten any messages after being canceled.
  ASSERT_EQ(0, test_filter->received_after_canceled_);

  // We should have gotten exactly one result.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(1U, msgs.size());
  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_2());
}

// Tests blocking and resuming requests.
TEST_F(ResourceDispatcherHostTest, TestBlockingResumingRequests) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));

  host_.BlockRequestsForRoute(filter_->child_id(), 1);
  host_.BlockRequestsForRoute(filter_->child_id(), 2);
  host_.BlockRequestsForRoute(filter_->child_id(), 3);

  MakeTestRequest(0, 1, net::URLRequestTestJob::test_url_1());
  MakeTestRequest(1, 2, net::URLRequestTestJob::test_url_2());
  MakeTestRequest(0, 3, net::URLRequestTestJob::test_url_3());
  MakeTestRequest(1, 4, net::URLRequestTestJob::test_url_1());
  MakeTestRequest(2, 5, net::URLRequestTestJob::test_url_2());
  MakeTestRequest(3, 6, net::URLRequestTestJob::test_url_3());

  // Flush all the pending requests
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  // Sort out all the messages we saw by request
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  // All requests but the 2 for the RVH 0 should have been blocked.
  ASSERT_EQ(2U, msgs.size());

  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_1());
  CheckSuccessfulRequest(msgs[1], net::URLRequestTestJob::test_data_3());

  // Resume requests for RVH 1 and flush pending requests.
  host_.ResumeBlockedRequestsForRoute(filter_->child_id(), 1);
  KickOffRequest();
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  msgs.clear();
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(2U, msgs.size());
  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_2());
  CheckSuccessfulRequest(msgs[1], net::URLRequestTestJob::test_data_1());

  // Test that new requests are not blocked for RVH 1.
  MakeTestRequest(1, 7, net::URLRequestTestJob::test_url_1());
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}
  msgs.clear();
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(1U, msgs.size());
  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_1());

  // Now resumes requests for all RVH (2 and 3).
  host_.ResumeBlockedRequestsForRoute(filter_->child_id(), 2);
  host_.ResumeBlockedRequestsForRoute(filter_->child_id(), 3);
  KickOffRequest();
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));

  msgs.clear();
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(2U, msgs.size());
  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_2());
  CheckSuccessfulRequest(msgs[1], net::URLRequestTestJob::test_data_3());
}

// Tests blocking and canceling requests.
TEST_F(ResourceDispatcherHostTest, TestBlockingCancelingRequests) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));

  host_.BlockRequestsForRoute(filter_->child_id(), 1);

  MakeTestRequest(0, 1, net::URLRequestTestJob::test_url_1());
  MakeTestRequest(1, 2, net::URLRequestTestJob::test_url_2());
  MakeTestRequest(0, 3, net::URLRequestTestJob::test_url_3());
  MakeTestRequest(1, 4, net::URLRequestTestJob::test_url_1());

  // Flush all the pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  // Sort out all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  // The 2 requests for the RVH 0 should have been processed.
  ASSERT_EQ(2U, msgs.size());

  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_1());
  CheckSuccessfulRequest(msgs[1], net::URLRequestTestJob::test_data_3());

  // Cancel requests for RVH 1.
  host_.CancelBlockedRequestsForRoute(filter_->child_id(), 1);
  KickOffRequest();
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));

  msgs.clear();
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(0U, msgs.size());
}

// Tests that blocked requests are canceled if their associated process dies.
TEST_F(ResourceDispatcherHostTest, TestBlockedRequestsProcessDies) {
  // This second filter is used to emulate a second process.
  scoped_refptr<ForwardingFilter> second_filter = new ForwardingFilter(
      this, browser_context_->GetResourceContext());

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));
  EXPECT_EQ(0,
            host_.GetOutstandingRequestsMemoryCost(second_filter->child_id()));

  host_.BlockRequestsForRoute(second_filter->child_id(), 0);

  MakeTestRequest(filter_.get(), 0, 1, net::URLRequestTestJob::test_url_1());
  MakeTestRequest(second_filter.get(), 0, 2,
                  net::URLRequestTestJob::test_url_2());
  MakeTestRequest(filter_.get(), 0, 3, net::URLRequestTestJob::test_url_3());
  MakeTestRequest(second_filter.get(), 0, 4,
                  net::URLRequestTestJob::test_url_1());

  // Simulate process death.
  host_.CancelRequestsForProcess(second_filter->child_id());

  // Flush all the pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));
  EXPECT_EQ(0,
            host_.GetOutstandingRequestsMemoryCost(second_filter->child_id()));

  // Sort out all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  // The 2 requests for the RVH 0 should have been processed.
  ASSERT_EQ(2U, msgs.size());

  CheckSuccessfulRequest(msgs[0], net::URLRequestTestJob::test_data_1());
  CheckSuccessfulRequest(msgs[1], net::URLRequestTestJob::test_data_3());

  EXPECT_TRUE(host_.blocked_loaders_map_.empty());
}

// Tests that blocked requests don't leak when the ResourceDispatcherHost goes
// away.  Note that we rely on Purify for finding the leaks if any.
// If this test turns the Purify bot red, check the ResourceDispatcherHost
// destructor to make sure the blocked requests are deleted.
TEST_F(ResourceDispatcherHostTest, TestBlockedRequestsDontLeak) {
  // This second filter is used to emulate a second process.
  scoped_refptr<ForwardingFilter> second_filter = new ForwardingFilter(
      this, browser_context_->GetResourceContext());

  host_.BlockRequestsForRoute(filter_->child_id(), 1);
  host_.BlockRequestsForRoute(filter_->child_id(), 2);
  host_.BlockRequestsForRoute(second_filter->child_id(), 1);

  MakeTestRequest(filter_.get(), 0, 1, net::URLRequestTestJob::test_url_1());
  MakeTestRequest(filter_.get(), 1, 2, net::URLRequestTestJob::test_url_2());
  MakeTestRequest(filter_.get(), 0, 3, net::URLRequestTestJob::test_url_3());
  MakeTestRequest(second_filter.get(), 1, 4,
                  net::URLRequestTestJob::test_url_1());
  MakeTestRequest(filter_.get(), 2, 5, net::URLRequestTestJob::test_url_2());
  MakeTestRequest(filter_.get(), 2, 6, net::URLRequestTestJob::test_url_3());

  host_.CancelRequestsForProcess(filter_->child_id());
  host_.CancelRequestsForProcess(second_filter->child_id());

  // Flush all the pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}
}

// Test the private helper method "CalculateApproximateMemoryCost()".
TEST_F(ResourceDispatcherHostTest, CalculateApproximateMemoryCost) {
  net::URLRequestContext context;
  net::URLRequest req(GURL("http://www.google.com"), NULL, &context);
  EXPECT_EQ(4427,
            ResourceDispatcherHostImpl::CalculateApproximateMemoryCost(&req));

  // Add 9 bytes of referrer.
  req.set_referrer("123456789");
  EXPECT_EQ(4436,
            ResourceDispatcherHostImpl::CalculateApproximateMemoryCost(&req));

  // Add 33 bytes of upload content.
  std::string upload_content;
  upload_content.resize(33);
  std::fill(upload_content.begin(), upload_content.end(), 'x');
  req.AppendBytesToUpload(upload_content.data(), upload_content.size());

  // Since the upload throttling is disabled, this has no effect on the cost.
  EXPECT_EQ(4436,
            ResourceDispatcherHostImpl::CalculateApproximateMemoryCost(&req));
}

// Test the private helper method "IncrementOutstandingRequestsMemoryCost()".
TEST_F(ResourceDispatcherHostTest, IncrementOutstandingRequestsMemoryCost) {
  // Add some counts for render_process_host=7
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(7));
  EXPECT_EQ(1, host_.IncrementOutstandingRequestsMemoryCost(1, 7));
  EXPECT_EQ(2, host_.IncrementOutstandingRequestsMemoryCost(1, 7));
  EXPECT_EQ(3, host_.IncrementOutstandingRequestsMemoryCost(1, 7));

  // Add some counts for render_process_host=3
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(3));
  EXPECT_EQ(1, host_.IncrementOutstandingRequestsMemoryCost(1, 3));
  EXPECT_EQ(2, host_.IncrementOutstandingRequestsMemoryCost(1, 3));

  // Remove all the counts for render_process_host=7
  EXPECT_EQ(3, host_.GetOutstandingRequestsMemoryCost(7));
  EXPECT_EQ(2, host_.IncrementOutstandingRequestsMemoryCost(-1, 7));
  EXPECT_EQ(1, host_.IncrementOutstandingRequestsMemoryCost(-1, 7));
  EXPECT_EQ(0, host_.IncrementOutstandingRequestsMemoryCost(-1, 7));
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(7));

  // Remove all the counts for render_process_host=3
  EXPECT_EQ(2, host_.GetOutstandingRequestsMemoryCost(3));
  EXPECT_EQ(1, host_.IncrementOutstandingRequestsMemoryCost(-1, 3));
  EXPECT_EQ(0, host_.IncrementOutstandingRequestsMemoryCost(-1, 3));
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(3));

  // When an entry reaches 0, it should be deleted.
  EXPECT_TRUE(host_.outstanding_requests_memory_cost_map_.end() ==
      host_.outstanding_requests_memory_cost_map_.find(7));
  EXPECT_TRUE(host_.outstanding_requests_memory_cost_map_.end() ==
      host_.outstanding_requests_memory_cost_map_.find(3));
}

// Test that when too many requests are outstanding for a particular
// render_process_host_id, any subsequent request from it fails.
TEST_F(ResourceDispatcherHostTest, TooManyOutstandingRequests) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));

  // Expected cost of each request as measured by
  // ResourceDispatcherHost::CalculateApproximateMemoryCost().
  int kMemoryCostOfTest2Req =
      ResourceDispatcherHostImpl::kAvgBytesPerOutstandingRequest +
      std::string("GET").size() +
      net::URLRequestTestJob::test_url_2().spec().size();

  // Tighten the bound on the ResourceDispatcherHost, to speed things up.
  int kMaxCostPerProcess = 440000;
  host_.set_max_outstanding_requests_cost_per_process(kMaxCostPerProcess);

  // Determine how many instance of test_url_2() we can request before
  // throttling kicks in.
  size_t kMaxRequests = kMaxCostPerProcess / kMemoryCostOfTest2Req;

  // This second filter is used to emulate a second process.
  scoped_refptr<ForwardingFilter> second_filter = new ForwardingFilter(
      this, browser_context_->GetResourceContext());

  // Saturate the number of outstanding requests for our process.
  for (size_t i = 0; i < kMaxRequests; ++i) {
    MakeTestRequest(filter_.get(), 0, i + 1,
                    net::URLRequestTestJob::test_url_2());
  }

  // Issue two more requests for our process -- these should fail immediately.
  MakeTestRequest(filter_.get(), 0, kMaxRequests + 1,
                  net::URLRequestTestJob::test_url_2());
  MakeTestRequest(filter_.get(), 0, kMaxRequests + 2,
                  net::URLRequestTestJob::test_url_2());

  // Issue two requests for the second process -- these should succeed since
  // it is just process 0 that is saturated.
  MakeTestRequest(second_filter.get(), 0, kMaxRequests + 3,
                  net::URLRequestTestJob::test_url_2());
  MakeTestRequest(second_filter.get(), 0, kMaxRequests + 4,
                  net::URLRequestTestJob::test_url_2());

  // Flush all the pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}
  MessageLoop::current()->RunAllPending();

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(filter_->child_id()));

  // Sorts out all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  // We issued (kMaxRequests + 4) total requests.
  ASSERT_EQ(kMaxRequests + 4, msgs.size());

  // Check that the first kMaxRequests succeeded.
  for (size_t i = 0; i < kMaxRequests; ++i)
    CheckSuccessfulRequest(msgs[i], net::URLRequestTestJob::test_data_2());

  // Check that the subsequent two requests (kMaxRequests + 1) and
  // (kMaxRequests + 2) were failed, since the per-process bound was reached.
  for (int i = 0; i < 2; ++i) {
    // Should have sent a single RequestComplete message.
    int index = kMaxRequests + i;
    EXPECT_EQ(1U, msgs[index].size());
    EXPECT_EQ(ResourceMsg_RequestComplete::ID, msgs[index][0].type());

    // The RequestComplete message should have had status
    // (CANCELLED, ERR_INSUFFICIENT_RESOURCES).
    int request_id;
    net::URLRequestStatus status;

    PickleIterator iter(msgs[index][0]);
    EXPECT_TRUE(IPC::ReadParam(&msgs[index][0], &iter, &request_id));
    EXPECT_TRUE(IPC::ReadParam(&msgs[index][0], &iter, &status));

    EXPECT_EQ(index + 1, request_id);
    EXPECT_EQ(net::URLRequestStatus::CANCELED, status.status());
    EXPECT_EQ(net::ERR_INSUFFICIENT_RESOURCES, status.error());
  }

  // The final 2 requests should have succeeded.
  CheckSuccessfulRequest(msgs[kMaxRequests + 2],
                         net::URLRequestTestJob::test_data_2());
  CheckSuccessfulRequest(msgs[kMaxRequests + 3],
                         net::URLRequestTestJob::test_data_2());
}

// Tests that we sniff the mime type for a simple request.
TEST_F(ResourceDispatcherHostTest, MimeSniffed) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  std::string raw_headers("HTTP/1.1 200 OK\n\n");
  std::string response_data("<html><title>Test One</title></html>");
  SetResponse(raw_headers, response_data);

  HandleScheme("http");
  MakeTestRequest(0, 1, GURL("http:bla"));

  // Flush all pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  // Sorts out all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(1U, msgs.size());

  content::ResourceResponseHead response_head;
  GetResponseHead(msgs[0], &response_head);
  ASSERT_EQ("text/html", response_head.mime_type);
}

// Tests that we don't sniff the mime type when the server provides one.
TEST_F(ResourceDispatcherHostTest, MimeNotSniffed) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  std::string raw_headers("HTTP/1.1 200 OK\n"
                          "Content-type: image/jpeg\n\n");
  std::string response_data("<html><title>Test One</title></html>");
  SetResponse(raw_headers, response_data);

  HandleScheme("http");
  MakeTestRequest(0, 1, GURL("http:bla"));

  // Flush all pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  // Sorts out all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(1U, msgs.size());

  content::ResourceResponseHead response_head;
  GetResponseHead(msgs[0], &response_head);
  ASSERT_EQ("image/jpeg", response_head.mime_type);
}

// Tests that we don't sniff the mime type when there is no message body.
TEST_F(ResourceDispatcherHostTest, MimeNotSniffed2) {

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  SetResponse("HTTP/1.1 304 Not Modified\n\n");

  HandleScheme("http");
  MakeTestRequest(0, 1, GURL("http:bla"));

  // Flush all pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  // Sorts out all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(1U, msgs.size());

  content::ResourceResponseHead response_head;
  GetResponseHead(msgs[0], &response_head);
  ASSERT_EQ("", response_head.mime_type);
}

TEST_F(ResourceDispatcherHostTest, MimeSniff204) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  SetResponse("HTTP/1.1 204 No Content\n\n");

  HandleScheme("http");
  MakeTestRequest(0, 1, GURL("http:bla"));

  // Flush all pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  // Sorts out all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(1U, msgs.size());

  content::ResourceResponseHead response_head;
  GetResponseHead(msgs[0], &response_head);
  ASSERT_EQ("text/plain", response_head.mime_type);
}

TEST_F(ResourceDispatcherHostTest, MimeSniffEmpty) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  SetResponse("HTTP/1.1 200 OK\n\n");

  HandleScheme("http");
  MakeTestRequest(0, 1, GURL("http:bla"));

  // Flush all pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  // Sorts out all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);
  ASSERT_EQ(1U, msgs.size());

  content::ResourceResponseHead response_head;
  GetResponseHead(msgs[0], &response_head);
  ASSERT_EQ("text/plain", response_head.mime_type);
}

// Tests for crbug.com/31266 (Non-2xx + application/octet-stream).
TEST_F(ResourceDispatcherHostTest, ForbiddenDownload) {
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  std::string raw_headers("HTTP/1.1 403 Forbidden\n"
                          "Content-disposition: attachment; filename=blah\n"
                          "Content-type: application/octet-stream\n\n");
  std::string response_data("<html><title>Test One</title></html>");
  SetResponse(raw_headers, response_data);

  // Only MAIN_FRAMEs can trigger a download.
  SetResourceType(ResourceType::MAIN_FRAME);

  HandleScheme("http");
  MakeTestRequest(0, 1, GURL("http:bla"));

  // Flush all pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));

  // Sorts out all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  // We should have gotten one RequestComplete message.
  ASSERT_EQ(1U, msgs[0].size());
  EXPECT_EQ(ResourceMsg_RequestComplete::ID, msgs[0][0].type());

  // The RequestComplete message should have had status
  // (CANCELED, ERR_FILE_NOT_FOUND).
  int request_id;
  net::URLRequestStatus status;

  PickleIterator iter(msgs[0][0]);
  EXPECT_TRUE(IPC::ReadParam(&msgs[0][0], &iter, &request_id));
  EXPECT_TRUE(IPC::ReadParam(&msgs[0][0], &iter, &status));

  EXPECT_EQ(1, request_id);
  EXPECT_EQ(net::URLRequestStatus::CANCELED, status.status());
  EXPECT_EQ(net::ERR_FILE_NOT_FOUND, status.error());
}

// Test for http://crbug.com/76202 .  We don't want to destroy a
// download request prematurely when processing a cancellation from
// the renderer.
TEST_F(ResourceDispatcherHostTest, IgnoreCancelForDownloads) {
  EXPECT_EQ(0, host_.pending_requests());

  int render_view_id = 0;
  int request_id = 1;

  std::string raw_headers("HTTP\n"
                          "Content-disposition: attachment; filename=foo\n\n");
  std::string response_data("01234567890123456789\x01foobar");

  // Get past sniffing metrics in the BufferedResourceHandler.  Note that
  // if we don't get past the sniffing metrics, the result will be that
  // the BufferedResourceHandler won't have figured out that it's a download,
  // won't have constructed a DownloadResourceHandler, and and the request
  // will be successfully canceled below, failing the test.
  response_data.resize(1025, ' ');

  SetResponse(raw_headers, response_data);
  SetResourceType(ResourceType::MAIN_FRAME);
  SetDelayedCompleteJobGeneration(true);
  HandleScheme("http");

  MakeTestRequest(render_view_id, request_id, GURL("http://example.com/blah"));
  // Return some data so that the request is identified as a download
  // and the proper resource handlers are created.
  EXPECT_TRUE(net::URLRequestTestJob::ProcessOnePendingMessage());

  // And now simulate a cancellation coming from the renderer.
  ResourceHostMsg_CancelRequest msg(filter_->child_id(), request_id);
  bool msg_was_ok;
  host_.OnMessageReceived(msg, filter_.get(), &msg_was_ok);

  // Since the request had already started processing as a download,
  // the cancellation above should have been ignored and the request
  // should still be alive.
  EXPECT_EQ(1, host_.pending_requests());

  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}
  EXPECT_EQ(0, host_.GetOutstandingRequestsMemoryCost(0));
}

TEST_F(ResourceDispatcherHostTest, CancelRequestsForContext) {
  EXPECT_EQ(0, host_.pending_requests());

  int render_view_id = 0;
  int request_id = 1;

  std::string raw_headers("HTTP\n"
                          "Content-disposition: attachment; filename=foo\n\n");
  std::string response_data("01234567890123456789\x01foobar");
  // Get past sniffing metrics.
  response_data.resize(1025, ' ');

  SetResponse(raw_headers, response_data);
  SetResourceType(ResourceType::MAIN_FRAME);
  SetDelayedCompleteJobGeneration(true);
  HandleScheme("http");

  MakeTestRequest(render_view_id, request_id, GURL("http://example.com/blah"));
  // Return some data so that the request is identified as a download
  // and the proper resource handlers are created.
  EXPECT_TRUE(net::URLRequestTestJob::ProcessOnePendingMessage());

  // And now simulate a cancellation coming from the renderer.
  ResourceHostMsg_CancelRequest msg(filter_->child_id(), request_id);
  bool msg_was_ok;
  host_.OnMessageReceived(msg, filter_.get(), &msg_was_ok);

  // Since the request had already started processing as a download,
  // the cancellation above should have been ignored and the request
  // should still be alive.
  EXPECT_EQ(1, host_.pending_requests());

  // Cancelling by other methods shouldn't work either.
  host_.CancelRequestsForProcess(render_view_id);
  EXPECT_EQ(1, host_.pending_requests());

  // Cancelling by context should work.
  host_.CancelRequestsForContext(filter_->resource_context());
  EXPECT_EQ(0, host_.pending_requests());
}

// Test the cancelling of requests that are being transferred to a new renderer
// due to a redirection.
TEST_F(ResourceDispatcherHostTest, CancelRequestsForContextTransferred) {
  EXPECT_EQ(0, host_.pending_requests());

  int render_view_id = 0;
  int request_id = 1;

  std::string raw_headers("HTTP/1.1 200 OK\n"
                          "Content-Type: text/html; charset=utf-8\n\n");
  std::string response_data("<html>foobar</html>");

  SetResponse(raw_headers, response_data);
  SetResourceType(ResourceType::MAIN_FRAME);
  HandleScheme("http");

  MakeTestRequest(render_view_id, request_id, GURL("http://example.com/blah"));

  GlobalRequestID global_request_id(filter_->child_id(), request_id);
  host_.MarkAsTransferredNavigation(global_request_id);

  // And now simulate a cancellation coming from the renderer.
  ResourceHostMsg_CancelRequest msg(filter_->child_id(), request_id);
  bool msg_was_ok;
  host_.OnMessageReceived(msg, filter_.get(), &msg_was_ok);

  // Since the request is marked as being transferred,
  // the cancellation above should have been ignored and the request
  // should still be alive.
  EXPECT_EQ(1, host_.pending_requests());

  // Cancelling by other methods shouldn't work either.
  host_.CancelRequestsForProcess(render_view_id);
  EXPECT_EQ(1, host_.pending_requests());

  // Cancelling by context should work.
  host_.CancelRequestsForContext(filter_->resource_context());
  EXPECT_EQ(0, host_.pending_requests());
}

TEST_F(ResourceDispatcherHostTest, TransferNavigation) {
  EXPECT_EQ(0, host_.pending_requests());

  int render_view_id = 0;
  int request_id = 1;

  // Configure initial request.
  SetResponse("HTTP/1.1 302 Found\n"
              "Location: http://other.com/blech\n\n");

  SetResourceType(ResourceType::MAIN_FRAME);
  HandleScheme("http");

  // Temporarily replace ContentBrowserClient with one that will trigger the
  // transfer navigation code paths.
  ContentBrowserClient* old_client = GetContentClient()->browser();
  TransfersAllNavigationsContentBrowserClient new_client;
  GetContentClient()->set_browser_for_testing(&new_client);

  MakeTestRequest(render_view_id, request_id, GURL("http://example.com/blah"));

  // Restore.
  GetContentClient()->set_browser_for_testing(old_client);

  // This second filter is used to emulate a second process.
  scoped_refptr<ForwardingFilter> second_filter = new ForwardingFilter(
      this, browser_context_->GetResourceContext());

  int new_render_view_id = 1;
  int new_request_id = 2;

  const std::string kResponseBody = "hello world";
  SetResponse("HTTP/1.1 200 OK\n"
              "Content-Type: text/plain\n\n",
              kResponseBody);

  ResourceHostMsg_Request request =
      CreateResourceRequest("GET", ResourceType::MAIN_FRAME,
                            GURL("http://other.com/blech"));
  request.transferred_request_child_id = filter_->child_id();
  request.transferred_request_request_id = request_id;

  ResourceHostMsg_RequestResource transfer_request_msg(
      new_render_view_id, new_request_id, request);
  bool msg_was_ok;
  host_.OnMessageReceived(transfer_request_msg, second_filter, &msg_was_ok);
  MessageLoop::current()->RunAllPending();

  // Flush all the pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  // Check generated messages.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  ASSERT_EQ(1U, msgs.size());
  CheckSuccessfulRequest(msgs[0], kResponseBody);
}

TEST_F(ResourceDispatcherHostTest, TransferNavigationAndThenRedirect) {
  EXPECT_EQ(0, host_.pending_requests());

  int render_view_id = 0;
  int request_id = 1;

  // Configure initial request.
  SetResponse("HTTP/1.1 302 Found\n"
              "Location: http://other.com/blech\n\n");

  SetResourceType(ResourceType::MAIN_FRAME);
  HandleScheme("http");

  // Temporarily replace ContentBrowserClient with one that will trigger the
  // transfer navigation code paths.
  ContentBrowserClient* old_client = GetContentClient()->browser();
  TransfersAllNavigationsContentBrowserClient new_client;
  GetContentClient()->set_browser_for_testing(&new_client);

  MakeTestRequest(render_view_id, request_id, GURL("http://example.com/blah"));

  // Restore.
  GetContentClient()->set_browser_for_testing(old_client);

  // This second filter is used to emulate a second process.
  scoped_refptr<ForwardingFilter> second_filter = new ForwardingFilter(
      this, browser_context_->GetResourceContext());

  int new_render_view_id = 1;
  int new_request_id = 2;

  // Delay the start of the next request so that we can setup the response for
  // the next URL.
  SetDelayedStartJobGeneration(true);

  SetResponse("HTTP/1.1 302 Found\n"
              "Location: http://other.com/blerg\n\n");

  ResourceHostMsg_Request request =
      CreateResourceRequest("GET", ResourceType::MAIN_FRAME,
                            GURL("http://other.com/blech"));
  request.transferred_request_child_id = filter_->child_id();
  request.transferred_request_request_id = request_id;

  ResourceHostMsg_RequestResource transfer_request_msg(
      new_render_view_id, new_request_id, request);
  bool msg_was_ok;
  host_.OnMessageReceived(transfer_request_msg, second_filter, &msg_was_ok);
  MessageLoop::current()->RunAllPending();

  // Response data for "http://other.com/blerg":
  const std::string kResponseBody = "hello world";
  SetResponse("HTTP/1.1 200 OK\n"
              "Content-Type: text/plain\n\n",
              kResponseBody);

  // OK, let the redirect happen.
  SetDelayedStartJobGeneration(false);
  CompleteStartRequest(second_filter, new_request_id);
  MessageLoop::current()->RunAllPending();

  // Flush all the pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  // Now, simulate the renderer choosing to follow the redirect.
  ResourceHostMsg_FollowRedirect redirect_msg(
      new_render_view_id, new_request_id, false, GURL());
  host_.OnMessageReceived(redirect_msg, second_filter, &msg_was_ok);
  MessageLoop::current()->RunAllPending();

  // Flush all the pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  // Check generated messages.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  ASSERT_EQ(1U, msgs.size());

  // We should have received a redirect followed by a "normal" payload.
  EXPECT_EQ(ResourceMsg_ReceivedRedirect::ID, msgs[0][0].type());
  msgs[0].erase(msgs[0].begin());
  CheckSuccessfulRequest(msgs[0], kResponseBody);
}

TEST_F(ResourceDispatcherHostTest, UnknownURLScheme) {
  EXPECT_EQ(0, host_.pending_requests());

  SetResourceType(ResourceType::MAIN_FRAME);
  HandleScheme("http");

  MakeTestRequest(0, 1, GURL("foo://bar"));

  // Flush all pending requests.
  while (net::URLRequestTestJob::ProcessOnePendingMessage()) {}

  // Sort all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  // We should have gotten one RequestComplete message.
  ASSERT_EQ(1U, msgs[0].size());
  EXPECT_EQ(ResourceMsg_RequestComplete::ID, msgs[0][0].type());

  // The RequestComplete message should have had status
  // (FAILED, ERR_UNKNOWN_URL_SCHEME).
  int request_id;
  net::URLRequestStatus status;

  PickleIterator iter(msgs[0][0]);
  EXPECT_TRUE(IPC::ReadParam(&msgs[0][0], &iter, &request_id));
  EXPECT_TRUE(IPC::ReadParam(&msgs[0][0], &iter, &status));

  EXPECT_EQ(1, request_id);
  EXPECT_EQ(net::URLRequestStatus::FAILED, status.status());
  EXPECT_EQ(net::ERR_UNKNOWN_URL_SCHEME, status.error());
}

TEST_F(ResourceDispatcherHostTest, DataReceivedACKs) {
  EXPECT_EQ(0, host_.pending_requests());

  SendDataReceivedACKs(true);

  HandleScheme("big-job");
  MakeTestRequest(0, 1, GURL("big-job:0123456789,1000000"));

  // Sort all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  size_t size = msgs[0].size();

  EXPECT_EQ(ResourceMsg_ReceivedResponse::ID, msgs[0][0].type());
  for (size_t i = 1; i < size - 1; ++i)
    EXPECT_EQ(ResourceMsg_DataReceived::ID, msgs[0][i].type());
  EXPECT_EQ(ResourceMsg_RequestComplete::ID, msgs[0][size - 1].type());
}

TEST_F(ResourceDispatcherHostTest, DelayedDataReceivedACKs) {
  EXPECT_EQ(0, host_.pending_requests());

  HandleScheme("big-job");
  MakeTestRequest(0, 1, GURL("big-job:0123456789,1000000"));

  // Sort all the messages we saw by request.
  ResourceIPCAccumulator::ClassifiedMessages msgs;
  accum_.GetClassifiedMessages(&msgs);

  // We expect 1x ReceivedResponse + Nx ReceivedData messages.
  EXPECT_EQ(ResourceMsg_ReceivedResponse::ID, msgs[0][0].type());
  for (size_t i = 1; i < msgs[0].size(); ++i)
    EXPECT_EQ(ResourceMsg_DataReceived::ID, msgs[0][i].type());

  // NOTE: If we fail the above checks then it means that we probably didn't
  // load a big enough response to trigger the delay mechanism we are trying to
  // test!

  msgs[0].erase(msgs[0].begin());

  // ACK all DataReceived messages until we find a RequestComplete message.
  bool complete = false;
  while (!complete) {
    for (size_t i = 0; i < msgs[0].size(); ++i) {
      if (msgs[0][i].type() == ResourceMsg_RequestComplete::ID) {
        complete = true;
        break;
      }

      EXPECT_EQ(ResourceMsg_DataReceived::ID, msgs[0][i].type());

      ResourceHostMsg_DataReceived_ACK msg(0, 1);
      bool msg_was_ok;
      host_.OnMessageReceived(msg, filter_.get(), &msg_was_ok);
    }

    MessageLoop::current()->RunAllPending();

    msgs.clear();
    accum_.GetClassifiedMessages(&msgs);
  }
}

}  // namespace content
