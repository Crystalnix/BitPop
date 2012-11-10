// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_INDEXED_DB_PROXY_WEBIDBOBJECTSTORE_IMPL_H_
#define CONTENT_COMMON_INDEXED_DB_PROXY_WEBIDBOBJECTSTORE_IMPL_H_

#include "base/basictypes.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBObjectStore.h"

namespace WebKit {
class WebIDBCallbacks;
class WebIDBIndex;
class WebIDBKey;
class WebIDBKeyRange;
class WebString;
}

class RendererWebIDBObjectStoreImpl : public WebKit::WebIDBObjectStore {
 public:
  explicit RendererWebIDBObjectStoreImpl(int32 idb_object_store_id);
  virtual ~RendererWebIDBObjectStoreImpl();

  // WebKit::WebIDBObjectStore
  virtual void get(const WebKit::WebIDBKeyRange& key_range,
                   WebKit::WebIDBCallbacks* callbacks,
                   const WebKit::WebIDBTransaction& transaction,
                   WebKit::WebExceptionCode& ec);
  virtual void putWithIndexKeys(
      const WebKit::WebSerializedScriptValue&,
      const WebKit::WebIDBKey&,
      PutMode,
      WebKit::WebIDBCallbacks*,
      const WebKit::WebIDBTransaction&,
      const WebKit::WebVector<WebKit::WebString>&,
      const WebKit::WebVector<WebKit::WebIDBObjectStore::WebIndexKeys>&,
      WebKit::WebExceptionCode&);
  virtual void deleteFunction(const WebKit::WebIDBKeyRange& key_range,
                              WebKit::WebIDBCallbacks* callbacks,
                              const WebKit::WebIDBTransaction& transaction,
                              WebKit::WebExceptionCode& ec);
  virtual void clear(WebKit::WebIDBCallbacks* callbacks,
                     const WebKit::WebIDBTransaction& transaction,
                     WebKit::WebExceptionCode& ec);

  virtual WebKit::WebIDBIndex* createIndex(
      const WebKit::WebString& name,
      const WebKit::WebIDBKeyPath& key_path,
      bool unique,
      bool multi_entry,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode& ec);
  // Transfers ownership of the WebIDBIndex to the caller.
  virtual WebKit::WebIDBIndex* index(const WebKit::WebString& name,
                                     WebKit::WebExceptionCode& ec);
  virtual void deleteIndex(const WebKit::WebString& name,
                           const WebKit::WebIDBTransaction& transaction,
                           WebKit::WebExceptionCode& ec);

  virtual void openCursor(const WebKit::WebIDBKeyRange& idb_key_range,
                  unsigned short direction,
                  WebKit::WebIDBCallbacks* callbacks,
                  const WebKit::WebIDBTransaction& transaction,
                  WebKit::WebExceptionCode& ec);

  virtual void count(const WebKit::WebIDBKeyRange& idb_key_range,
                     WebKit::WebIDBCallbacks* callbacks,
                     const WebKit::WebIDBTransaction& transaction,
                     WebKit::WebExceptionCode& ec);

 private:
  int32 idb_object_store_id_;
};

#endif  // CONTENT_COMMON_INDEXED_DB_PROXY_WEBIDBOBJECTSTORE_IMPL_H_
