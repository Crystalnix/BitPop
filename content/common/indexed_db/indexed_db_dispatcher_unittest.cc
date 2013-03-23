// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "content/common/indexed_db/indexed_db_dispatcher.h"
#include "content/common/indexed_db/indexed_db_key.h"
#include "content/public/common/serialized_script_value.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebExceptionCode.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBTransaction.h"

using WebKit::WebVector;
using WebKit::WebString;

namespace content {

class FakeWebIDBTransaction : public WebKit::WebIDBTransaction {
 public:
  FakeWebIDBTransaction() {}
};

// TODO(alecflett): Reenable this test when IDB code in webkit
// enforces size limits. See http://crbug.com/160577
TEST(IndexedDBDispatcherTest, DISABLED_ValueSizeTest) {
  string16 data;
  data.resize(kMaxIDBValueSizeInBytes / sizeof(char16) + 1, 'x');
  const bool kIsNull = false;
  const bool kIsInvalid = false;
  const SerializedScriptValue value(kIsNull, kIsInvalid, data);
  const int32 ipc_dummy_id = -1;

  {
    IndexedDBDispatcher dispatcher;
    IndexedDBKey key;
    key.SetNumber(0);
    dispatcher.RequestIDBObjectStorePut(
        value,
        key,
        WebKit::WebIDBObjectStore::AddOrUpdate,
        static_cast<WebKit::WebIDBCallbacks*>(NULL),
        ipc_dummy_id,
        FakeWebIDBTransaction(),
        WebVector<long long>(),
        WebVector<WebVector<WebKit::WebIDBKey> >());
  }
}

}  // namespace content
