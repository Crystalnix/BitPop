// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_INDEXED_DB_INDEXED_DB_DISPATCHER_H_
#define CONTENT_COMMON_INDEXED_DB_INDEXED_DB_DISPATCHER_H_

#include <map>
#include <vector>

#include "base/id_map.h"
#include "base/nullable_string16.h"
#include "content/common/content_export.h"
#include "ipc/ipc_sync_message_filter.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebExceptionCode.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBCursor.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBDatabase.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBDatabaseCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBObjectStore.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBTransaction.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBTransactionCallbacks.h"
#include "webkit/glue/worker_task_runner.h"

struct IndexedDBMsg_CallbacksSuccessCursorContinue_Params;
struct IndexedDBMsg_CallbacksSuccessCursorPrefetch_Params;
struct IndexedDBMsg_CallbacksSuccessIDBCursor_Params;

namespace WebKit {
class WebFrame;
class WebIDBKeyRange;
}

namespace content {
class IndexedDBKey;
class IndexedDBKeyPath;
class IndexedDBKeyRange;
class RendererWebIDBCursorImpl;
class RendererWebIDBDatabaseImpl;
class SerializedScriptValue;

CONTENT_EXPORT extern const size_t kMaxIDBValueSizeInBytes;

// Handle the indexed db related communication for this context thread - the
// main thread and each worker thread have their own copies.
class CONTENT_EXPORT IndexedDBDispatcher
    : public webkit_glue::WorkerTaskRunner::Observer {
 public:
  // Constructor made public to allow RenderThreadImpl to own a copy without
  // failing a NOTREACHED in ThreadSpecificInstance in tests that instantiate
  // two copies of RenderThreadImpl on the same thread.  Everyone else probably
  // wants to use ThreadSpecificInstance().
  IndexedDBDispatcher();
  virtual ~IndexedDBDispatcher();
  static IndexedDBDispatcher* ThreadSpecificInstance();

  // webkit_glue::WorkerTaskRunner::Observer implementation.
  virtual void OnWorkerRunLoopStopped() OVERRIDE;

  void OnMessageReceived(const IPC::Message& msg);
  static bool Send(IPC::Message* msg);

  void RequestIDBFactoryGetDatabaseNames(
      WebKit::WebIDBCallbacks* callbacks,
      const string16& origin,
      WebKit::WebFrame* web_frame);

  void RequestIDBFactoryOpen(
      const string16& name,
      int64 version,
      WebKit::WebIDBCallbacks* callbacks,
      WebKit::WebIDBDatabaseCallbacks* database_callbacks,
      const string16& origin,
      WebKit::WebFrame* web_frame);

  void RequestIDBFactoryOpen(
      const string16& name,
      int64 version,
      int64 transaction_id,
      WebKit::WebIDBCallbacks* callbacks,
      WebKit::WebIDBDatabaseCallbacks* database_callbacks,
      const string16& origin,
      WebKit::WebFrame* web_frame);

  void RequestIDBFactoryDeleteDatabase(
      const string16& name,
      WebKit::WebIDBCallbacks* callbacks,
      const string16& origin,
      WebKit::WebFrame* web_frame);

  void RequestIDBCursorAdvance(
      unsigned long count,
      WebKit::WebIDBCallbacks* callbacks_ptr,
      int32 ipc_cursor_id,
      WebKit::WebExceptionCode* ec);

  void RequestIDBCursorContinue(
      const IndexedDBKey& key,
      WebKit::WebIDBCallbacks* callbacks_ptr,
      int32 ipc_cursor_id,
      WebKit::WebExceptionCode* ec);

  void RequestIDBCursorPrefetch(
      int n,
      WebKit::WebIDBCallbacks* callbacks_ptr,
      int32 ipc_cursor_id,
      WebKit::WebExceptionCode* ec);

  void RequestIDBCursorPrefetchReset(int used_prefetches, int unused_prefetches,
                                     int32 ipc_cursor_id);

  void RequestIDBCursorDelete(
      WebKit::WebIDBCallbacks* callbacks_ptr,
      int32 ipc_cursor_id,
      WebKit::WebExceptionCode* ec);

  void RequestIDBDatabaseClose(
      int32 ipc_database_id);

  void RequestIDBIndexOpenObjectCursor(
      const WebKit::WebIDBKeyRange& idb_key_range,
      unsigned short direction,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_index_id,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RequestIDBIndexOpenKeyCursor(
      const WebKit::WebIDBKeyRange& idb_key_range,
      unsigned short direction,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_index_id,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RequestIDBIndexCount(
      const WebKit::WebIDBKeyRange& idb_key_range,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_index_id,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RequestIDBIndexGetObject(
      const IndexedDBKeyRange& key_range,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_index_id,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RequestIDBIndexGetKey(
      const IndexedDBKeyRange& key_range,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_index_id,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RequestIDBObjectStoreGet(
      const IndexedDBKeyRange& key_range,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_object_store_id,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RequestIDBObjectStorePut(
      const SerializedScriptValue& value,
      const IndexedDBKey& key,
      WebKit::WebIDBObjectStore::PutMode putMode,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_object_store_id,
      const WebKit::WebIDBTransaction& transaction,
      const WebKit::WebVector<long long>& indexIds,
      const WebKit::WebVector<WebKit::WebVector<
          WebKit::WebIDBKey> >& indexKeys);

  void RequestIDBObjectStoreDelete(
      const IndexedDBKeyRange& key_range,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_object_store_id,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RequestIDBObjectStoreClear(
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_object_store_id,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RequestIDBObjectStoreOpenCursor(
      const WebKit::WebIDBKeyRange& idb_key_range,
      WebKit::WebIDBCursor::Direction direction,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_object_store_id,
      WebKit::WebIDBTransaction::TaskType task_type,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RequestIDBObjectStoreCount(
      const WebKit::WebIDBKeyRange& idb_key_range,
      WebKit::WebIDBCallbacks* callbacks,
      int32 ipc_object_store_id,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode* ec);

  void RegisterWebIDBTransactionCallbacks(
      WebKit::WebIDBTransactionCallbacks* callbacks,
      int32 id);

  void CursorDestroyed(int32 ipc_cursor_id);
  void DatabaseDestroyed(int32 ipc_database_id);

  static int32 TransactionId(const WebKit::WebIDBTransaction& transaction);

 private:
  FRIEND_TEST_ALL_PREFIXES(IndexedDBDispatcherTest, ValueSizeTest);

  // IDBCallback message handlers.
  void OnSuccessIDBDatabase(int32 ipc_thread_id,
                            int32 ipc_response_id,
                            int32 ipc_object_id);
  void OnSuccessIndexedDBKey(int32 ipc_thread_id,
                             int32 ipc_response_id,
                             const IndexedDBKey& key);
  void OnSuccessOpenCursor(
      const IndexedDBMsg_CallbacksSuccessIDBCursor_Params& p);
  void OnSuccessCursorContinue(
      const IndexedDBMsg_CallbacksSuccessCursorContinue_Params& p);
  void OnSuccessCursorPrefetch(
      const IndexedDBMsg_CallbacksSuccessCursorPrefetch_Params& p);
  void OnSuccessStringList(int32 ipc_thread_id,
                           int32 ipc_response_id,
                           const std::vector<string16>& value);
  void OnSuccessSerializedScriptValue(
      int32 ipc_thread_id,
      int32 ipc_response_id,
      const SerializedScriptValue& value);
  void OnSuccessSerializedScriptValueWithKey(
      int32 ipc_thread_id,
      int32 ipc_response_id,
      const SerializedScriptValue& value,
      const IndexedDBKey& primary_key,
      const IndexedDBKeyPath& key_path);
  void OnSuccessInteger(
      int32 ipc_thread_id,
      int32 ipc_response_id,
      int64 value);
  void OnSuccessUndefined(
      int32 ipc_thread_id,
      int32 ipc_response_id);
  void OnError(int32 ipc_thread_id,
               int32 ipc_response_id,
               int code,
               const string16& message);
  void OnBlocked(int32 ipc_thread_id, int32 ipc_response_id);
  void OnIntBlocked(int32 ipc_thread_id, int32 ipc_response_id,
                    int64 existing_version);
  void OnUpgradeNeeded(int32 ipc_thread_id,
                       int32 ipc_response_id,
                       int32 ipc_transaction_id,
                       int32 ipc_database_id,
                       int64 old_version);
  void OnAbort(int32 ipc_thread_id,
               int32 ipc_transaction_id,
               int code,
               const string16& message);
  void OnComplete(int32 ipc_thread_id, int32 ipc_transaction_id);
  void OnForcedClose(int32 ipc_thread_id, int32 ipc_database_id);
  void OnVersionChange(int32 ipc_thread_id,
                       int32 ipc_database_id,
                       const string16& newVersion);
  void OnIntVersionChange(int32 ipc_thread_id,
                          int32 ipc_database_id,
                          int64 old_version,
                          int64 new_version);

  // Reset cursor prefetch caches for all cursors except exception_cursor_id.
  void ResetCursorPrefetchCaches(int32 ipc_exception_cursor_id = -1);

  // Careful! WebIDBCallbacks wraps non-threadsafe data types. It must be
  // destroyed and used on the same thread it was created on.
  IDMap<WebKit::WebIDBCallbacks, IDMapOwnPointer> pending_callbacks_;
  IDMap<WebKit::WebIDBTransactionCallbacks, IDMapOwnPointer>
      pending_transaction_callbacks_;
  IDMap<WebKit::WebIDBDatabaseCallbacks, IDMapOwnPointer>
      pending_database_callbacks_;

  // Map from cursor id to RendererWebIDBCursorImpl.
  std::map<int32, RendererWebIDBCursorImpl*> cursors_;

  std::map<int32, RendererWebIDBDatabaseImpl*> databases_;

  DISALLOW_COPY_AND_ASSIGN(IndexedDBDispatcher);
};

}  // namespace content

#endif  // CONTENT_COMMON_INDEXED_DB_INDEXED_DB_DISPATCHER_H_
