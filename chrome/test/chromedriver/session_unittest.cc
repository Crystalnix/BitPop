// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "chrome/test/chromedriver/session.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(SessionAccessorTest, LocksSession) {
  scoped_ptr<Session> scoped_session(new Session("id"));
  Session* session = scoped_session.get();
  scoped_refptr<SessionAccessor> accessor(
      new SessionAccessorImpl(scoped_session.Pass()));
  scoped_ptr<base::AutoLock> lock;
  ASSERT_EQ(session, accessor->Access(&lock));
  ASSERT_TRUE(lock.get());
}
