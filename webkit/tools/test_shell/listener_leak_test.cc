// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "v8/include/v8.h"
#include "v8/include/v8-profiler.h"
#include "webkit/glue/webkit_glue.h"
#include "webkit/tools/test_shell/test_shell.h"
#include "webkit/tools/test_shell/test_shell_test.h"

class ListenerLeakTest : public TestShellTest {
};

static const v8::HeapGraphNode* GetProperty(const v8::HeapGraphNode* node,
                                            v8::HeapGraphEdge::Type type,
                                            const char* name) {
  for (int i = 0, count = node->GetChildrenCount(); i < count; ++i) {
    const v8::HeapGraphEdge* prop = node->GetChild(i);
    if (prop->GetType() == type) {
      v8::String::AsciiValue prop_name(prop->GetName());
      if (strcmp(name, *prop_name) == 0) return prop->GetToNode();
    }
  }
  return NULL;
}

static int GetNumObjects(const char* constructor) {
  v8::HandleScope scope;
  const v8::HeapSnapshot* snapshot =
    v8::HeapProfiler::TakeSnapshot(v8::String::New(""),
                                   v8::HeapSnapshot::kFull);
  CHECK(snapshot);
  int count = 0;
  for (int i = 0; i < snapshot->GetNodesCount(); ++i) {
    const v8::HeapGraphNode* node = snapshot->GetNode(i);
    if (node->GetType() != v8::HeapGraphNode::kObject) continue;
    v8::String::AsciiValue node_name(node->GetName());
    if (strcmp(constructor, *node_name) == 0) {
      const v8::HeapGraphNode* constructor_prop =
        GetProperty(node, v8::HeapGraphEdge::kProperty, "constructor");
      // Skip an Object instance named after the constructor.
      if (constructor_prop != NULL) {
        v8::String::AsciiValue constructor_name(constructor_prop->GetName());
        if (strcmp(constructor, *constructor_name) == 0) continue;
      }
      ++count;
    }
  }
  return count;
}

// This test tries to create a reference cycle between node and its listener.
// See http://crbug/17400.
TEST_F(ListenerLeakTest, ReferenceCycle) {
  FilePath listener_file;
  ASSERT_TRUE(PathService::Get(base::DIR_SOURCE_ROOT, &listener_file));
  listener_file = listener_file.Append(FILE_PATH_LITERAL("webkit"))
      .Append(FILE_PATH_LITERAL("data"))
      .Append(FILE_PATH_LITERAL("listener"))
      .Append(FILE_PATH_LITERAL("listener_leak1.html"));
  test_shell_->LoadFile(listener_file);
  test_shell_->WaitTestFinished();
  ASSERT_EQ(0, GetNumObjects("EventListenerLeakTestObject1"));
}

// This test sets node onclick many times to expose a possible memory
// leak where all listeners get referenced by the node.
TEST_F(ListenerLeakTest, HiddenReferences) {
  FilePath listener_file;
  ASSERT_TRUE(PathService::Get(base::DIR_SOURCE_ROOT, &listener_file));
  listener_file = listener_file.Append(FILE_PATH_LITERAL("webkit"))
      .Append(FILE_PATH_LITERAL("data"))
      .Append(FILE_PATH_LITERAL("listener"))
      .Append(FILE_PATH_LITERAL("listener_leak2.html"));
  test_shell_->LoadFile(listener_file);
  test_shell_->WaitTestFinished();
  ASSERT_EQ(1, GetNumObjects("EventListenerLeakTestObject2"));
}

