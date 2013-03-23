// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CALLBACKS_H_
#define CONTENT_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CALLBACKS_H_

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "content/browser/in_process_webkit/indexed_db_dispatcher_host.h"
#include "googleurl/src/gurl.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBCursor.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBDatabase.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBDatabaseError.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBTransaction.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebString.h"

namespace content {

class IndexedDBCallbacksBase : public WebKit::WebIDBCallbacks {
 public:
  virtual ~IndexedDBCallbacksBase();

  virtual void onError(const WebKit::WebIDBDatabaseError& error);
  virtual void onBlocked();
  virtual void onBlocked(long long old_version);

 protected:
  IndexedDBCallbacksBase(IndexedDBDispatcherHost* dispatcher_host,
                         int32 ipc_thread_id,
                         int32 ipc_response_id);
  IndexedDBDispatcherHost* dispatcher_host() const {
    return dispatcher_host_.get();
  }
  int32 ipc_thread_id() const { return ipc_thread_id_; }
  int32 ipc_response_id() const { return ipc_response_id_; }

 private:
  scoped_refptr<IndexedDBDispatcherHost> dispatcher_host_;
  int32 ipc_response_id_;
  int32 ipc_thread_id_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(IndexedDBCallbacksBase);
};

// TODO(dgrogan): Remove this class and change the remaining specializations
// into subclasses of IndexedDBCallbacksBase.
template <class WebObjectType>
class IndexedDBCallbacks : public IndexedDBCallbacksBase {
  DISALLOW_IMPLICIT_CONSTRUCTORS(IndexedDBCallbacks);
};

class IndexedDBCallbacksDatabase : public IndexedDBCallbacksBase {
 public:
  IndexedDBCallbacksDatabase(
      IndexedDBDispatcherHost* dispatcher_host,
      int32 ipc_thread_id,
      int32 ipc_response_id,
      const GURL& origin_url);

  virtual void onSuccess(WebKit::WebIDBDatabase* idb_object);
  virtual void onUpgradeNeeded(
      long long old_version,
      WebKit::WebIDBTransaction* transaction,
      WebKit::WebIDBDatabase* database);

 private:
  GURL origin_url_;
  int32 ipc_database_id_;
  DISALLOW_IMPLICIT_CONSTRUCTORS(IndexedDBCallbacksDatabase);
};

// WebIDBCursor uses:
// * onSuccess(WebIDBCursor*, WebIDBKey, WebIDBKey, SerializedScriptValue)
//   when an openCursor()/openKeyCursor() call has succeeded,
// * onSuccess(WebIDBKey, WebIDBKey, SerializedScriptValue)
//   when an advance()/continue() call has succeeded, or
// * onSuccess(SerializedScriptValue::nullValue())
//   to indicate it does not contain any data, i.e., there is no key within
//   the key range, or it has reached the end.
template <>
class IndexedDBCallbacks<WebKit::WebIDBCursor>
    : public IndexedDBCallbacksBase {
 public:
  IndexedDBCallbacks(
      IndexedDBDispatcherHost* dispatcher_host,
      int32 ipc_thread_id,
      int32 ipc_response_id,
      int32 ipc_cursor_id)
      : IndexedDBCallbacksBase(dispatcher_host, ipc_thread_id, ipc_response_id),
        ipc_cursor_id_(ipc_cursor_id) { }

  virtual void onSuccess(WebKit::WebIDBCursor* idb_object,
                         const WebKit::WebIDBKey& key,
                         const WebKit::WebIDBKey& primaryKey,
                         const WebKit::WebSerializedScriptValue& value);
  virtual void onSuccess(const WebKit::WebIDBKey& key,
                         const WebKit::WebIDBKey& primaryKey,
                         const WebKit::WebSerializedScriptValue& value);
  virtual void onSuccess(const WebKit::WebSerializedScriptValue& value);
  virtual void onSuccessWithPrefetch(
      const WebKit::WebVector<WebKit::WebIDBKey>& keys,
      const WebKit::WebVector<WebKit::WebIDBKey>& primaryKeys,
      const WebKit::WebVector<WebKit::WebSerializedScriptValue>& values);

 private:
  // The id of the cursor this callback concerns, or -1 if the cursor
  // does not exist yet.
  int32 ipc_cursor_id_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(IndexedDBCallbacks);
};

// WebIDBKey is implemented in WebKit as opposed to being an interface Chromium
// implements.  Thus we pass a const ___& version and thus we need this
// specialization.
template <>
class IndexedDBCallbacks<WebKit::WebIDBKey>
    : public IndexedDBCallbacksBase {
 public:
  IndexedDBCallbacks(IndexedDBDispatcherHost* dispatcher_host,
                     int32 ipc_thread_id,
                     int32 ipc_response_id)
      : IndexedDBCallbacksBase(dispatcher_host, ipc_thread_id,
                               ipc_response_id) { }

  virtual void onSuccess(const WebKit::WebIDBKey& value);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(IndexedDBCallbacks);
};

// WebDOMStringList is implemented in WebKit as opposed to being an
// interface Chromium implements.  Thus we pass a const ___& version and thus
// we need this specialization.
template <>
class IndexedDBCallbacks<WebKit::WebDOMStringList>
    : public IndexedDBCallbacksBase {
 public:
  IndexedDBCallbacks(
      IndexedDBDispatcherHost* dispatcher_host,
      int32 ipc_thread_id,
      int32 ipc_response_id)
      : IndexedDBCallbacksBase(dispatcher_host, ipc_thread_id,
                               ipc_response_id) { }

  virtual void onSuccess(const WebKit::WebDOMStringList& value);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(IndexedDBCallbacks);
};

// WebSerializedScriptValue is implemented in WebKit as opposed to being an
// interface Chromium implements.  Thus we pass a const ___& version and thus
// we need this specialization.
template <>
class IndexedDBCallbacks<WebKit::WebSerializedScriptValue>
    : public IndexedDBCallbacksBase {
 public:
  IndexedDBCallbacks(IndexedDBDispatcherHost* dispatcher_host,
                     int32 ipc_thread_id,
                     int32 ipc_response_id)
      : IndexedDBCallbacksBase(dispatcher_host, ipc_thread_id,
                               ipc_response_id) { }

  virtual void onSuccess(const WebKit::WebSerializedScriptValue& value);
  virtual void onSuccess(const WebKit::WebSerializedScriptValue& value,
                         const WebKit::WebIDBKey& key,
                         const WebKit::WebIDBKeyPath& keyPath);
  virtual void onSuccess(long long value);
  virtual void onSuccess();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(IndexedDBCallbacks);
};

}  // namespace content

#endif  // CONTENT_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CALLBACKS_H_
