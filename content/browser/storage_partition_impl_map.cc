// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/storage_partition_impl_map.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/stl_util.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/threading/sequenced_worker_pool.h"
#include "content/browser/appcache/chrome_appcache_service.h"
#include "content/browser/fileapi/browser_file_system_helper.h"
#include "content/browser/fileapi/chrome_blob_storage_context.h"
#include "content/browser/histogram_internals_request_job.h"
#include "content/browser/loader/resource_request_info_impl.h"
#include "content/browser/net/view_blob_internals_job_factory.h"
#include "content/browser/net/view_http_cache_job_factory.h"
#include "content/browser/resource_context_impl.h"
#include "content/browser/storage_partition_impl.h"
#include "content/browser/tcmalloc_internals_request_job.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/url_constants.h"
#include "crypto/sha2.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_context.h"
#include "webkit/appcache/view_appcache_internals_job.h"
#include "webkit/blob/blob_data.h"
#include "webkit/blob/blob_url_request_job_factory.h"
#include "webkit/fileapi/file_system_url_request_job_factory.h"

using appcache::AppCacheService;
using fileapi::FileSystemContext;
using webkit_blob::BlobStorageController;

namespace content {

namespace {

class BlobProtocolHandler : public webkit_blob::BlobProtocolHandler {
 public:
  BlobProtocolHandler(
      webkit_blob::BlobStorageController* blob_storage_controller,
      fileapi::FileSystemContext* file_system_context,
      base::MessageLoopProxy* loop_proxy)
      : webkit_blob::BlobProtocolHandler(blob_storage_controller,
                                         file_system_context,
                                         loop_proxy) {}

  virtual ~BlobProtocolHandler() {}

 private:
  virtual scoped_refptr<webkit_blob::BlobData>
      LookupBlobData(net::URLRequest* request) const {
    const ResourceRequestInfoImpl* info =
        ResourceRequestInfoImpl::ForRequest(request);
    if (!info)
      return NULL;
    return info->requested_blob_data();
  }

  DISALLOW_COPY_AND_ASSIGN(BlobProtocolHandler);
};

// Adds a bunch of debugging urls. We use an interceptor instead of a protocol
// handler because we want to reuse the chrome://scheme (everyone is familiar
// with it, and no need to expose the content/chrome separation through our UI).
class DeveloperProtocolHandler
    : public net::URLRequestJobFactory::Interceptor {
 public:
  DeveloperProtocolHandler(
      AppCacheService* appcache_service,
      BlobStorageController* blob_storage_controller)
      : appcache_service_(appcache_service),
        blob_storage_controller_(blob_storage_controller) {}
  virtual ~DeveloperProtocolHandler() {}

  virtual net::URLRequestJob* MaybeIntercept(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate) const OVERRIDE {
    // Check for chrome://view-http-cache/*, which uses its own job type.
    if (ViewHttpCacheJobFactory::IsSupportedURL(request->url()))
      return ViewHttpCacheJobFactory::CreateJobForRequest(request,
                                                          network_delegate);

    // Next check for chrome://appcache-internals/, which uses its own job type.
    if (request->url().SchemeIs(chrome::kChromeUIScheme) &&
        request->url().host() == chrome::kChromeUIAppCacheInternalsHost) {
      return appcache::ViewAppCacheInternalsJobFactory::CreateJobForRequest(
          request, network_delegate, appcache_service_);
    }

    // Next check for chrome://blob-internals/, which uses its own job type.
    if (ViewBlobInternalsJobFactory::IsSupportedURL(request->url())) {
      return ViewBlobInternalsJobFactory::CreateJobForRequest(
          request, network_delegate, blob_storage_controller_);
    }

#if defined(USE_TCMALLOC)
    // Next check for chrome://tcmalloc/, which uses its own job type.
    if (request->url().SchemeIs(chrome::kChromeUIScheme) &&
        request->url().host() == chrome::kChromeUITcmallocHost) {
      return new TcmallocInternalsRequestJob(request, network_delegate);
    }
#endif

    // Next check for chrome://histograms/, which uses its own job type.
    if (request->url().SchemeIs(chrome::kChromeUIScheme) &&
        request->url().host() == chrome::kChromeUIHistogramHost) {
      return new HistogramInternalsRequestJob(request, network_delegate);
    }

    return NULL;
  }

  virtual net::URLRequestJob* MaybeInterceptRedirect(
        const GURL& location,
        net::URLRequest* request,
        net::NetworkDelegate* network_delegate) const OVERRIDE {
    return NULL;
  }

  virtual net::URLRequestJob* MaybeInterceptResponse(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate) const OVERRIDE {
    return NULL;
  }

  virtual bool WillHandleProtocol(const std::string& protocol) const {
    return protocol == chrome::kChromeUIScheme;
  }

 private:
  AppCacheService* appcache_service_;
  BlobStorageController* blob_storage_controller_;
};

void InitializeURLRequestContext(
    net::URLRequestContextGetter* context_getter,
    AppCacheService* appcache_service,
    FileSystemContext* file_system_context,
    ChromeBlobStorageContext* blob_storage_context) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!context_getter)
    return;  // tests.

  // This code only modifies the URLRequestJobFactory on the context
  // to handle blob: URLs, filesystem: URLs, and to let AppCache intercept
  // the appropriate requests.  This is in addition to the slew of other
  // initializtion that is done in during creation of the URLRequestContext.
  // We cannot yet centralize this code because URLRequestContext needs
  // to be created before the StoragePartition context.
  //
  // TODO(ajwong): Fix the ordering so all the initialization is in one spot.
  net::URLRequestContext* context = context_getter->GetURLRequestContext();
  net::URLRequestJobFactory* job_factory =
      const_cast<net::URLRequestJobFactory*>(context->job_factory());

  // Note: if this is called twice with 2 request contexts that share one job
  // factory (as is the case with a media request context and its related
  // normal request context) then this will early exit.
  if (job_factory->IsHandledProtocol(chrome::kBlobScheme))
    return;  // Already initialized this JobFactory.

  bool set_protocol = job_factory->SetProtocolHandler(
      chrome::kBlobScheme,
      new BlobProtocolHandler(
          blob_storage_context->controller(),
          file_system_context,
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE)));
  DCHECK(set_protocol);
  set_protocol = job_factory->SetProtocolHandler(
      chrome::kFileSystemScheme,
      CreateFileSystemProtocolHandler(file_system_context));
  DCHECK(set_protocol);

  job_factory->AddInterceptor(
      new DeveloperProtocolHandler(appcache_service,
                                   blob_storage_context->controller()));

  // TODO(jam): Add the ProtocolHandlerRegistryIntercepter here!
}

// These constants are used to create the directory structure under the profile
// where renderers with a non-default storage partition keep their persistent
// state. This will contain a set of directories that partially mirror the
// directory structure of BrowserContext::GetPath().
//
// The kStoragePartitionDirname contains an extensions directory which is
// further partitioned by extension id, followed by another level of directories
// for the "default" extension storage partition and one directory for each
// persistent partition used by a webview tag. Example:
//
//   Storage/ext/ABCDEF/def
//   Storage/ext/ABCDEF/hash(partition name)
//
// The code in GetStoragePartitionPath() constructs these path names.
//
// TODO(nasko): Move extension related path code out of content.
const FilePath::CharType kStoragePartitionDirname[] =
    FILE_PATH_LITERAL("Storage");
const FilePath::CharType kExtensionsDirname[] =
    FILE_PATH_LITERAL("ext");
const FilePath::CharType kDefaultPartitionDirname[] =
    FILE_PATH_LITERAL("def");
const FilePath::CharType kTrashDirname[] =
    FILE_PATH_LITERAL("trash");

// Because partition names are user specified, they can be arbitrarily long
// which makes them unsuitable for paths names. We use a truncation of a
// SHA256 hash to perform a deterministic shortening of the string. The
// kPartitionNameHashBytes constant controls the length of the truncation.
// We use 6 bytes, which gives us 99.999% reliability against collisions over
// 1 million partition domains.
//
// Analysis:
// We assume that all partition names within one partition domain are
// controlled by the the same entity. Thus there is no chance for adverserial
// attack and all we care about is accidental collision. To get 5 9s over
// 1 million domains, we need the probability of a collision in any one domain
// to be
//
//    p < nroot(1000000, .99999) ~= 10^-11
//
// We use the following birthday attack approximation to calculate the max
// number of unique names for this probability:
//
//    n(p,H) = sqrt(2*H * ln(1/(1-p)))
//
// For a 6-byte hash, H = 2^(6*8).  n(10^-11, H) ~= 75
//
// An average partition domain is likely to have less than 10 unique
// partition names which is far lower than 75.
//
// Note, that for 4 9s of reliability, the limit is 237 partition names per
// partition domain.
const int kPartitionNameHashBytes = 6;

// Needed for selecting all files in ObliterateOneDirectory() below.
#if defined(OS_POSIX)
const int kAllFileTypes = file_util::FileEnumerator::FILES |
                          file_util::FileEnumerator::DIRECTORIES |
                          file_util::FileEnumerator::SHOW_SYM_LINKS;
#else
const int kAllFileTypes = file_util::FileEnumerator::FILES |
                          file_util::FileEnumerator::DIRECTORIES;
#endif

FilePath GetStoragePartitionDomainPath(
    const std::string& partition_domain) {
  CHECK(IsStringUTF8(partition_domain));

  return FilePath(kStoragePartitionDirname).Append(kExtensionsDirname)
      .Append(FilePath::FromUTF8Unsafe(partition_domain));
}

// Helper function for doing a depth-first deletion of the data on disk.
// Examines paths directly in |current_dir| (no recursion) and tries to
// delete from disk anything that is in, or isn't a parent of something in
// |paths_to_keep|. Paths that need further expansion are added to
// |paths_to_consider|.
void ObliterateOneDirectory(const FilePath& current_dir,
                            const std::vector<FilePath>& paths_to_keep,
                            std::vector<FilePath>* paths_to_consider) {
  CHECK(current_dir.IsAbsolute());

  file_util::FileEnumerator enumerator(current_dir, false, kAllFileTypes);
  for (FilePath to_delete = enumerator.Next(); !to_delete.empty();
       to_delete = enumerator.Next()) {
    // Enum tracking which of the 3 possible actions to take for |to_delete|.
    enum { kSkip, kEnqueue, kDelete } action = kDelete;

    for (std::vector<FilePath>::const_iterator to_keep = paths_to_keep.begin();
         to_keep != paths_to_keep.end();
         ++to_keep) {
      if (to_delete == *to_keep) {
        action = kSkip;
        break;
      } else if (to_delete.IsParent(*to_keep)) {
        // |to_delete| contains a path to keep. Add to stack for further
        // processing.
        action = kEnqueue;
        break;
      }
    }

    switch (action) {
      case kDelete:
        file_util::Delete(to_delete, true);
        break;

      case kEnqueue:
        paths_to_consider->push_back(to_delete);
        break;

      case kSkip:
        break;
    }
  }
}

// Synchronously attempts to delete |unnormalized_root|, preserving only
// entries in |paths_to_keep|. If there are no entries in |paths_to_keep| on
// disk, then it completely removes |unnormalized_root|. All paths must be
// absolute paths.
void BlockingObliteratePath(
    const FilePath& unnormalized_browser_context_root,
    const FilePath& unnormalized_root,
    const std::vector<FilePath>& paths_to_keep,
    const scoped_refptr<base::TaskRunner>& closure_runner,
    const base::Closure& on_gc_required) {
  // Early exit required because file_util::AbsolutePath() will fail on POSIX
  // if |unnormalized_root| does not exist. This is safe because there is
  // nothing to do in this situation anwyays.
  if (!file_util::PathExists(unnormalized_root)) {
    return;
  }

  // Never try to obliterate things outside of the browser context root or the
  // browser context root itself. Die hard.
  FilePath root = unnormalized_root;
  FilePath browser_context_root = unnormalized_browser_context_root;
  CHECK(file_util::AbsolutePath(&root));
  CHECK(file_util::AbsolutePath(&browser_context_root));
  CHECK(file_util::ContainsPath(browser_context_root, root) &&
        browser_context_root != root);

  // Reduce |paths_to_keep| set to those under the root and actually on disk.
  std::vector<FilePath> valid_paths_to_keep;
  for (std::vector<FilePath>::const_iterator it = paths_to_keep.begin();
       it != paths_to_keep.end();
       ++it) {
    if (root.IsParent(*it) && file_util::PathExists(*it))
      valid_paths_to_keep.push_back(*it);
  }

  // If none of the |paths_to_keep| are valid anymore then we just whack the
  // root and be done with it.  Otherwise, signal garbage collection and do
  // a best-effort delete of the on-disk structures.
  if (valid_paths_to_keep.empty()) {
    file_util::Delete(root, true);
    return;
  }
  closure_runner->PostTask(FROM_HERE, on_gc_required);

  // Otherwise, start at the root and delete everything that is not in
  // |valid_paths_to_keep|.
  std::vector<FilePath> paths_to_consider;
  paths_to_consider.push_back(root);
  while(!paths_to_consider.empty()) {
    FilePath path = paths_to_consider.back();
    paths_to_consider.pop_back();
    ObliterateOneDirectory(path, valid_paths_to_keep, &paths_to_consider);
  }
}

// Deletes all entries inside the |storage_root| that are not in the
// |active_paths|.  Deletion is done in 2 steps:
//
//   (1) Moving all garbage collected paths into a trash directory.
//   (2) Asynchronously deleting the trash directory.
//
// The deletion is asynchronous because after (1) completes, calling code can
// safely continue to use the paths that had just been garbage collected
// without fear of race conditions.
//
// This code also ignores failed moves rather than attempting a smarter retry.
// Moves shouldn't fail here unless there is some out-of-band error (eg.,
// FS corruption). Retry logic is dangerous in the general case because
// there is not necessarily a guaranteed case where the logic may succeed.
//
// This function is still named BlockingGarbageCollect() because it does
// execute a few filesystem operations synchronously.
void BlockingGarbageCollect(
    const FilePath& storage_root,
    const scoped_refptr<base::TaskRunner>& file_access_runner,
    scoped_ptr<base::hash_set<FilePath> > active_paths) {
  CHECK(storage_root.IsAbsolute());

  file_util::FileEnumerator enumerator(storage_root, false, kAllFileTypes);
  FilePath trash_directory;
  if (!file_util::CreateTemporaryDirInDir(storage_root, kTrashDirname,
                                          &trash_directory)) {
    // Unable to continue without creating the trash directory so give up.
    return;
  }
  for (FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    if (active_paths->find(path) == active_paths->end() &&
        path != trash_directory) {
      // Since |trash_directory| is unique for each run of this function there
      // can be no colllisions on the move.
      file_util::Move(path, trash_directory.Append(path.BaseName()));
    }
  }

  file_access_runner->PostTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(&file_util::Delete), trash_directory,
                 true));
}

}  // namespace

// static
FilePath StoragePartitionImplMap::GetStoragePartitionPath(
    const std::string& partition_domain,
    const std::string& partition_name) {
  if (partition_domain.empty())
    return FilePath();

  FilePath path = GetStoragePartitionDomainPath(partition_domain);

  // TODO(ajwong): Mangle in-memory into this somehow, either by putting
  // it into the partition_name, or by manually adding another path component
  // here.  Otherwise, it's possible to have an in-memory StoragePartition and
  // a persistent one that return the same FilePath for GetPath().
  if (!partition_name.empty()) {
    // For analysis of why we can ignore collisions, see the comment above
    // kPartitionNameHashBytes.
    char buffer[kPartitionNameHashBytes];
    crypto::SHA256HashString(partition_name, &buffer[0],
                             sizeof(buffer));
    return path.AppendASCII(base::HexEncode(buffer, sizeof(buffer)));
  }

  return path.Append(kDefaultPartitionDirname);
}

StoragePartitionImplMap::StoragePartitionImplMap(
    BrowserContext* browser_context)
    : browser_context_(browser_context),
      resource_context_initialized_(false) {
  // Doing here instead of initializer list cause it's just too ugly to read.
  base::SequencedWorkerPool* blocking_pool = BrowserThread::GetBlockingPool();
  file_access_runner_ =
      blocking_pool->GetSequencedTaskRunner(blocking_pool->GetSequenceToken());
}

StoragePartitionImplMap::~StoragePartitionImplMap() {
  STLDeleteContainerPairSecondPointers(partitions_.begin(),
                                       partitions_.end());
}

StoragePartitionImpl* StoragePartitionImplMap::Get(
    const std::string& partition_domain,
    const std::string& partition_name,
    bool in_memory) {
  // TODO(ajwong): ResourceContexts no longer have any storage related state.
  // We should move this into a place where it is called once per
  // BrowserContext creation rather than piggybacking off the default context
  // creation.
  if (!resource_context_initialized_) {
    resource_context_initialized_ = true;
    InitializeResourceContext(browser_context_);
  }

  // Find the previously created partition if it's available.
  StoragePartitionConfig partition_config(
      partition_domain, partition_name, in_memory);

  PartitionMap::const_iterator it = partitions_.find(partition_config);
  if (it != partitions_.end())
    return it->second;

  FilePath partition_path =
      browser_context_->GetPath().Append(
          GetStoragePartitionPath(partition_domain, partition_name));
  StoragePartitionImpl* partition =
      StoragePartitionImpl::Create(browser_context_, in_memory,
                                   partition_path);
  partitions_[partition_config] = partition;

  // These calls must happen after StoragePartitionImpl::Create().
  partition->SetURLRequestContext(
      partition_domain.empty() ?
      browser_context_->GetRequestContext() :
      browser_context_->GetRequestContextForStoragePartition(
          partition->GetPath(), in_memory));
  partition->SetMediaURLRequestContext(
      partition_domain.empty() ?
      browser_context_->GetMediaRequestContext() :
      browser_context_->GetMediaRequestContextForStoragePartition(
          partition->GetPath(), in_memory));

  PostCreateInitialization(partition, in_memory);

  return partition;
}

void StoragePartitionImplMap::AsyncObliterate(
    const GURL& site,
    const base::Closure& on_gc_required) {
  // This method should avoid creating any StoragePartition (which would
  // create more open file handles) so that it can delete as much of the
  // data off disk as possible.
  std::string partition_domain;
  std::string partition_name;
  bool in_memory = false;
  GetContentClient()->browser()->GetStoragePartitionConfigForSite(
      browser_context_, site, false, &partition_domain,
      &partition_name, &in_memory);

  // Find the active partitions for the domain. Because these partitions are
  // active, it is not possible to just delete the directories that contain
  // the backing data structures without causing the browser to crash. Instead,
  // of deleteing the directory, we tell each storage context later to
  // remove any data they have saved. This will leave the directory structure
  // intact but it will only contain empty databases.
  std::vector<StoragePartitionImpl*> active_partitions;
  std::vector<FilePath> paths_to_keep;
  for (PartitionMap::const_iterator it = partitions_.begin();
       it != partitions_.end();
       ++it) {
    const StoragePartitionConfig& config = it->first;
    if (config.partition_domain == partition_domain) {
      it->second->AsyncClearAllData();
      if (!config.in_memory) {
        paths_to_keep.push_back(it->second->GetPath());
      }
    }
  }

  // Start a best-effort delete of the on-disk storage excluding paths that are
  // known to still be in use. This is to delete any previously created
  // StoragePartition state that just happens to not have been used during this
  // run of the browser.
  FilePath domain_root = browser_context_->GetPath().Append(
      GetStoragePartitionDomainPath(partition_domain));

  BrowserThread::PostBlockingPoolTask(
      FROM_HERE,
      base::Bind(&BlockingObliteratePath, browser_context_->GetPath(),
                 domain_root, paths_to_keep,
                 base::MessageLoopProxy::current(), on_gc_required));
}

void StoragePartitionImplMap::GarbageCollect(
    scoped_ptr<base::hash_set<FilePath> > active_paths,
    const base::Closure& done) {
  // Include all paths for current StoragePartitions in the active_paths since
  // they cannot be deleted safely.
  for (PartitionMap::const_iterator it = partitions_.begin();
       it != partitions_.end();
       ++it) {
    const StoragePartitionConfig& config = it->first;
    if (!config.in_memory)
      active_paths->insert(it->second->GetPath());
  }

  // Find the directory holding the StoragePartitions and delete everything in
  // there that isn't considered active.
  FilePath storage_root = browser_context_->GetPath().Append(
      GetStoragePartitionDomainPath(std::string()));
  file_access_runner_->PostTaskAndReply(
      FROM_HERE,
      base::Bind(&BlockingGarbageCollect, storage_root,
                 file_access_runner_,
                 base::Passed(&active_paths)),
      done);
}

void StoragePartitionImplMap::ForEach(
    const BrowserContext::StoragePartitionCallback& callback) {
  for (PartitionMap::const_iterator it = partitions_.begin();
       it != partitions_.end();
       ++it) {
    callback.Run(it->second);
  }
}

void StoragePartitionImplMap::PostCreateInitialization(
    StoragePartitionImpl* partition,
    bool in_memory) {
  // Check first to avoid memory leak in unittests.
  if (BrowserThread::IsMessageLoopValid(BrowserThread::IO)) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&ChromeAppCacheService::InitializeOnIOThread,
                   partition->GetAppCacheService(),
                   in_memory ? FilePath() :
                       partition->GetPath().Append(kAppCacheDirname),
                   browser_context_->GetResourceContext(),
                   make_scoped_refptr(partition->GetURLRequestContext()),
                   make_scoped_refptr(
                       browser_context_->GetSpecialStoragePolicy())));

    // Add content's URLRequestContext's hooks.
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(
            &InitializeURLRequestContext,
            make_scoped_refptr(partition->GetURLRequestContext()),
            make_scoped_refptr(partition->GetAppCacheService()),
            make_scoped_refptr(partition->GetFileSystemContext()),
            make_scoped_refptr(
                ChromeBlobStorageContext::GetFor(browser_context_))));

    // We do not call InitializeURLRequestContext() for media contexts because,
    // other than the HTTP cache, the media contexts share the same backing
    // objects as their associated "normal" request context.  Thus, the previous
    // call serves to initialize the media request context for this storage
    // partition as well.
  }
}

}  // namespace content
