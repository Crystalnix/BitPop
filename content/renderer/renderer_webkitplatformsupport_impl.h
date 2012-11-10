// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_RENDERER_WEBKITPLATFORMSUPPORT_IMPL_H_
#define CONTENT_RENDERER_RENDERER_WEBKITPLATFORMSUPPORT_IMPL_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/platform_file.h"
#include "content/common/content_export.h"
#include "content/common/webkitplatformsupport_impl.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebGraphicsContext3D.h"

class RendererClipboardClient;
class WebSharedWorkerRepositoryImpl;
class WebFileSystemImpl;

namespace content {
class GamepadSharedMemoryReader;
}

namespace webkit_glue {
class WebClipboardImpl;
}

class CONTENT_EXPORT RendererWebKitPlatformSupportImpl
    : public content::WebKitPlatformSupportImpl {
 public:
  RendererWebKitPlatformSupportImpl();
  virtual ~RendererWebKitPlatformSupportImpl();

  void set_plugin_refresh_allowed(bool plugin_refresh_allowed) {
    plugin_refresh_allowed_ = plugin_refresh_allowed;
  }
  // WebKitPlatformSupport methods:
  virtual WebKit::WebClipboard* clipboard() OVERRIDE;
  virtual WebKit::WebMimeRegistry* mimeRegistry() OVERRIDE;
  virtual WebKit::WebFileUtilities* fileUtilities() OVERRIDE;
  virtual WebKit::WebSandboxSupport* sandboxSupport() OVERRIDE;
  virtual WebKit::WebCookieJar* cookieJar() OVERRIDE;
  virtual bool sandboxEnabled() OVERRIDE;
  virtual unsigned long long visitedLinkHash(
      const char* canonicalURL, size_t length) OVERRIDE;
  virtual bool isLinkVisited(unsigned long long linkHash) OVERRIDE;
  virtual WebKit::WebMessagePortChannel* createMessagePortChannel() OVERRIDE;
  virtual void prefetchHostName(const WebKit::WebString&) OVERRIDE;
  virtual void cacheMetadata(
      const WebKit::WebURL&, double, const char*, size_t) OVERRIDE;
  virtual WebKit::WebString defaultLocale() OVERRIDE;
  virtual void suddenTerminationChanged(bool enabled) OVERRIDE;
  virtual WebKit::WebStorageNamespace* createLocalStorageNamespace(
      const WebKit::WebString& path, unsigned quota) OVERRIDE;
  virtual WebKit::WebKitPlatformSupport::FileHandle databaseOpenFile(
      const WebKit::WebString& vfs_file_name, int desired_flags) OVERRIDE;
  virtual int databaseDeleteFile(const WebKit::WebString& vfs_file_name,
                                 bool sync_dir) OVERRIDE;
  virtual long databaseGetFileAttributes(
      const WebKit::WebString& vfs_file_name) OVERRIDE;
  virtual long long databaseGetFileSize(
      const WebKit::WebString& vfs_file_name) OVERRIDE;
  virtual long long databaseGetSpaceAvailableForOrigin(
      const WebKit::WebString& origin_identifier) OVERRIDE;
  virtual WebKit::WebString signedPublicKeyAndChallengeString(
      unsigned key_size_index,
      const WebKit::WebString& challenge,
      const WebKit::WebURL& url) OVERRIDE;
  virtual void screenColorProfile(WebKit::WebVector<char>* to_profile) OVERRIDE;
  virtual WebKit::WebIDBFactory* idbFactory() OVERRIDE;
  virtual void createIDBKeysFromSerializedValuesAndKeyPath(
      const WebKit::WebVector<WebKit::WebSerializedScriptValue>& values,
      const WebKit::WebIDBKeyPath& keyPath,
      WebKit::WebVector<WebKit::WebIDBKey>& keys) OVERRIDE;
  virtual WebKit::WebSerializedScriptValue injectIDBKeyIntoSerializedValue(
      const WebKit::WebIDBKey& key,
      const WebKit::WebSerializedScriptValue& value,
      const WebKit::WebIDBKeyPath& keyPath) OVERRIDE;
  virtual WebKit::WebFileSystem* fileSystem() OVERRIDE;
  virtual WebKit::WebSharedWorkerRepository* sharedWorkerRepository() OVERRIDE;
  virtual bool canAccelerate2dCanvas();
  virtual double audioHardwareSampleRate() OVERRIDE;
  virtual size_t audioHardwareBufferSize() OVERRIDE;
  virtual WebKit::WebAudioDevice* createAudioDevice(
      size_t buffer_size, unsigned channels, double sample_rate,
      WebKit::WebAudioDevice::RenderCallback* callback) OVERRIDE;
  virtual WebKit::WebBlobRegistry* blobRegistry() OVERRIDE;
  virtual void sampleGamepads(WebKit::WebGamepads&) OVERRIDE;
  virtual WebKit::WebString userAgent(const WebKit::WebURL& url) OVERRIDE;
  virtual void GetPlugins(bool refresh,
                          std::vector<webkit::WebPluginInfo>* plugins) OVERRIDE;
  virtual WebKit::WebPeerConnection00Handler* createPeerConnection00Handler(
      WebKit::WebPeerConnection00HandlerClient* client) OVERRIDE;
  virtual WebKit::WebMediaStreamCenter* createMediaStreamCenter(
      WebKit::WebMediaStreamCenterClient* client) OVERRIDE;

  // Disables the WebSandboxSupport implementation for testing.
  // Tests that do not set up a full sandbox environment should call
  // SetSandboxEnabledForTesting(false) _before_ creating any instances
  // of this class, to ensure that we don't attempt to use sandbox-related
  // file descriptors or other resources.
  //
  // Returns the previous |enable| value.
  static bool SetSandboxEnabledForTesting(bool enable);

 protected:
  virtual GpuChannelHostFactory* GetGpuChannelHostFactory() OVERRIDE;

 private:
  bool CheckPreparsedJsCachingEnabled() const;

  scoped_ptr<RendererClipboardClient> clipboard_client_;
  scoped_ptr<webkit_glue::WebClipboardImpl> clipboard_;

  class FileUtilities;
  scoped_ptr<FileUtilities> file_utilities_;

  class MimeRegistry;
  scoped_ptr<MimeRegistry> mime_registry_;

  class SandboxSupport;
  scoped_ptr<SandboxSupport> sandbox_support_;

  // This counter keeps track of the number of times sudden termination is
  // enabled or disabled. It starts at 0 (enabled) and for every disable
  // increments by 1, for every enable decrements by 1. When it reaches 0,
  // we tell the browser to enable fast termination.
  int sudden_termination_disables_;

  // If true, then a GetPlugins call is allowed to rescan the disk.
  bool plugin_refresh_allowed_;

  // Implementation of the WebSharedWorkerRepository APIs (provides an interface
  // to WorkerService on the browser thread.
  scoped_ptr<WebSharedWorkerRepositoryImpl> shared_worker_repository_;

  scoped_ptr<WebKit::WebIDBFactory> web_idb_factory_;

  scoped_ptr<WebFileSystemImpl> web_file_system_;

  scoped_ptr<WebKit::WebBlobRegistry> blob_registry_;

  scoped_ptr<content::GamepadSharedMemoryReader> gamepad_shared_memory_reader_;
};

#endif  // CONTENT_RENDERER_RENDERER_WEBKITPLATFORMSUPPORT_IMPL_H_
