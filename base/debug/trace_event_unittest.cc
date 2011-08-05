// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/trace_event.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/scoped_ptr.h"
#include "base/stringprintf.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace debug {

namespace {

struct JsonKeyValue {
  const char* key;
  const char* value;
};

class TraceEventTestFixture : public testing::Test {
 public:
  void ManualTestSetUp();
  void OnTraceDataCollected(TraceLog::RefCountedString* json_events_str);
  bool FindMatchingTraceEntry(const JsonKeyValue* key_values);
  bool FindNamePhase(const char* name, const char* phase);

  std::string trace_string_;
  ListValue trace_parsed_;

 private:
  // We want our singleton torn down after each test.
  ShadowingAtExitManager at_exit_manager_;
};

void TraceEventTestFixture::ManualTestSetUp() {
  TraceLog::Resurrect();
  TraceLog* tracelog = TraceLog::GetInstance();
  ASSERT_TRUE(tracelog);
  ASSERT_FALSE(tracelog->IsEnabled());
  tracelog->SetOutputCallback(
    base::Bind(&TraceEventTestFixture::OnTraceDataCollected,
               base::Unretained(this)));
}

void TraceEventTestFixture::OnTraceDataCollected(
    TraceLog::RefCountedString* json_events_str) {
  trace_string_ += json_events_str->data;

  scoped_ptr<Value> root;
  root.reset(base::JSONReader::Read(json_events_str->data, false));

  ListValue* root_list = NULL;
  ASSERT_TRUE(root.get());
  ASSERT_TRUE(root->GetAsList(&root_list));

  // Move items into our aggregate collection
  while (root_list->GetSize()) {
    Value* item = NULL;
    root_list->Remove(0, &item);
    trace_parsed_.Append(item);
  }
}

static bool IsKeyValueInDict(const JsonKeyValue* key_value,
                             DictionaryValue* dict) {
  Value* value = NULL;
  std::string value_str;
  if (dict->Get(key_value->key, &value) &&
      value->GetAsString(&value_str) &&
      value_str == key_value->value)
    return true;

  // Recurse to test arguments
  DictionaryValue* args_dict = NULL;
  dict->GetDictionary("args", &args_dict);
  if (args_dict)
    return IsKeyValueInDict(key_value, args_dict);

  return false;
}

static bool IsAllKeyValueInDict(const JsonKeyValue* key_values,
                                DictionaryValue* dict) {
  // Scan all key_values, they must all be present and equal.
  while (key_values && key_values->key) {
    if (!IsKeyValueInDict(key_values, dict))
      return false;
    ++key_values;
  }
  return true;
}

bool TraceEventTestFixture::FindMatchingTraceEntry(
    const JsonKeyValue* key_values) {
  // Scan all items
  size_t trace_parsed_count = trace_parsed_.GetSize();
  for (size_t i = 0; i < trace_parsed_count; i++) {
    Value* value = NULL;
    trace_parsed_.Get(i, &value);
    if (!value || value->GetType() != Value::TYPE_DICTIONARY)
      continue;
    DictionaryValue* dict = static_cast<DictionaryValue*>(value);

    if (IsAllKeyValueInDict(key_values, dict))
      return true;
  }
  return false;
}

bool TraceEventTestFixture::FindNamePhase(const char* name, const char* phase) {
  JsonKeyValue key_values[] = {
    {"name", name},
    {"ph", phase},
    {0, 0}
  };
  return FindMatchingTraceEntry(key_values);
}

bool IsStringInDict(const char* string_to_match, DictionaryValue* dict) {
  for (DictionaryValue::key_iterator ikey = dict->begin_keys();
       ikey != dict->end_keys(); ++ikey) {
    Value* child = NULL;
    if (!dict->GetWithoutPathExpansion(*ikey, &child))
      continue;

    if ((*ikey).find(string_to_match) != std::string::npos)
      return true;

    std::string value_str;
    child->GetAsString(&value_str);
    if (value_str.find(string_to_match) != std::string::npos)
      return true;
  }

  // Recurse to test arguments
  DictionaryValue* args_dict = NULL;
  dict->GetDictionary("args", &args_dict);
  if (args_dict)
    return IsStringInDict(string_to_match, args_dict);

  return false;
}

DictionaryValue* FindTraceEntry(const ListValue& trace_parsed,
                                const char *string_to_match,
                                DictionaryValue* match_after_this_item = NULL) {
  // Scan all items
  size_t trace_parsed_count = trace_parsed.GetSize();
  for (size_t i = 0; i < trace_parsed_count; i++) {
    Value* value = NULL;
    trace_parsed.Get(i, &value);
    if (match_after_this_item) {
      if (value == match_after_this_item)
         match_after_this_item = NULL;
      continue;
    }
    if (!value || value->GetType() != Value::TYPE_DICTIONARY)
      continue;
    DictionaryValue* dict = static_cast<DictionaryValue*>(value);

    if (IsStringInDict(string_to_match, dict))
      return dict;
  }
  return NULL;
}

void DataCapturedCallTraces(WaitableEvent* task_complete_event) {
  {
    TRACE_EVENT_BEGIN_ETW("TRACE_EVENT_BEGIN_ETW call", 1122, "extrastring1");
    TRACE_EVENT_END_ETW("TRACE_EVENT_END_ETW call", 3344, "extrastring2");
    TRACE_EVENT_INSTANT_ETW("TRACE_EVENT_INSTANT_ETW call",
                            5566, "extrastring3");

    TRACE_EVENT0("all", "TRACE_EVENT0 call");
    TRACE_EVENT1("all", "TRACE_EVENT1 call", "name1", "value1");
    TRACE_EVENT2("all", "TRACE_EVENT2 call",
                 "name1", "value1",
                 "name2", "value2");

    TRACE_EVENT_INSTANT0("all", "TRACE_EVENT_INSTANT0 call");
    TRACE_EVENT_INSTANT1("all", "TRACE_EVENT_INSTANT1 call", "name1", "value1");
    TRACE_EVENT_INSTANT2("all", "TRACE_EVENT_INSTANT2 call",
                         "name1", "value1",
                         "name2", "value2");

    TRACE_EVENT_BEGIN0("all", "TRACE_EVENT_BEGIN0 call");
    TRACE_EVENT_BEGIN1("all", "TRACE_EVENT_BEGIN1 call", "name1", "value1");
    TRACE_EVENT_BEGIN2("all", "TRACE_EVENT_BEGIN2 call",
                       "name1", "value1",
                       "name2", "value2");

    TRACE_EVENT_END0("all", "TRACE_EVENT_END0 call");
    TRACE_EVENT_END1("all", "TRACE_EVENT_END1 call", "name1", "value1");
    TRACE_EVENT_END2("all", "TRACE_EVENT_END2 call",
                     "name1", "value1",
                     "name2", "value2");
  } // Scope close causes TRACE_EVENT0 etc to send their END events.

  if (task_complete_event)
    task_complete_event->Signal();
}

void DataCapturedValidateTraces(const ListValue& trace_parsed,
                                 const std::string& trace_string) {
  DictionaryValue* item = NULL;

#define EXPECT_FIND_(string) \
  EXPECT_TRUE((item = FindTraceEntry(trace_parsed, string)));
#define EXPECT_NOT_FIND_(string) \
  EXPECT_FALSE((item = FindTraceEntry(trace_parsed, string)));
#define EXPECT_SUB_FIND_(string) \
  if (item) EXPECT_TRUE((IsStringInDict(string, item)));

  EXPECT_FIND_("ETW Trace Event");
  EXPECT_FIND_("all");
  EXPECT_FIND_("TRACE_EVENT_BEGIN_ETW call");
  {
    int int_val = 0;
    EXPECT_TRUE(item && item->GetInteger("args.id", &int_val));
    EXPECT_EQ(1122, int_val);
  }
  EXPECT_SUB_FIND_("extrastring1");
  EXPECT_FIND_("TRACE_EVENT_END_ETW call");
  EXPECT_FIND_("TRACE_EVENT_INSTANT_ETW call");
  EXPECT_FIND_("TRACE_EVENT0 call");
  {
    std::string ph_begin;
    std::string ph_end;
    EXPECT_TRUE((item = FindTraceEntry(trace_parsed, "TRACE_EVENT0 call")));
    EXPECT_TRUE((item && item->GetString("ph", &ph_begin)));
    EXPECT_TRUE((item = FindTraceEntry(trace_parsed, "TRACE_EVENT0 call",
                                       item)));
    EXPECT_TRUE((item && item->GetString("ph", &ph_end)));
    EXPECT_EQ("B", ph_begin);
    EXPECT_EQ("E", ph_end);
  }
  EXPECT_FIND_("TRACE_EVENT1 call");
  EXPECT_FIND_("TRACE_EVENT2 call");
  EXPECT_SUB_FIND_("name1");
  EXPECT_SUB_FIND_("value1");
  EXPECT_SUB_FIND_("name2");
  EXPECT_SUB_FIND_("value2");
  EXPECT_FIND_("TRACE_EVENT_INSTANT0 call");
  EXPECT_FIND_("TRACE_EVENT_INSTANT1 call");
  EXPECT_FIND_("TRACE_EVENT_INSTANT2 call");
  EXPECT_SUB_FIND_("name1");
  EXPECT_SUB_FIND_("value1");
  EXPECT_SUB_FIND_("name2");
  EXPECT_SUB_FIND_("value2");
  EXPECT_FIND_("TRACE_EVENT_BEGIN0 call");
  EXPECT_FIND_("TRACE_EVENT_BEGIN1 call");
  EXPECT_FIND_("TRACE_EVENT_BEGIN2 call");
  EXPECT_SUB_FIND_("name1");
  EXPECT_SUB_FIND_("value1");
  EXPECT_SUB_FIND_("name2");
  EXPECT_SUB_FIND_("value2");
  EXPECT_FIND_("TRACE_EVENT_END0 call");
  EXPECT_FIND_("TRACE_EVENT_END1 call");
  EXPECT_FIND_("TRACE_EVENT_END2 call");
  EXPECT_SUB_FIND_("name1");
  EXPECT_SUB_FIND_("value1");
  EXPECT_SUB_FIND_("name2");
  EXPECT_SUB_FIND_("value2");
}

}  // namespace

// Simple Test for emitting data and validating it was received.
TEST_F(TraceEventTestFixture, DataCaptured) {
  ManualTestSetUp();
  TraceLog::GetInstance()->SetEnabled(true);

  DataCapturedCallTraces(NULL);

  TraceLog::GetInstance()->SetEnabled(false);

  DataCapturedValidateTraces(trace_parsed_, trace_string_);
}

// Simple Test for time threshold events.
TEST_F(TraceEventTestFixture, DataCapturedThreshold) {
  ManualTestSetUp();
  TraceLog::GetInstance()->SetEnabled(true);

  // Test that events at the same level are properly filtered by threshold.
  {
    TRACE_EVENT_IF_LONGER_THAN0(100, "time", "threshold 100");
    TRACE_EVENT_IF_LONGER_THAN0(1000, "time", "threshold 1000");
    TRACE_EVENT_IF_LONGER_THAN0(10000, "time", "threshold 10000");
    // 100+ seconds to avoid flakiness.
    TRACE_EVENT_IF_LONGER_THAN0(100000000, "time", "threshold long1");
    TRACE_EVENT_IF_LONGER_THAN0(200000000, "time", "threshold long2");
    base::PlatformThread::Sleep(20); // 20000 us
  }

  // Test that a normal nested event remains after it's parent event is dropped.
  {
    TRACE_EVENT_IF_LONGER_THAN0(1000000, "time", "2threshold10000");
    {
      TRACE_EVENT0("time", "nonthreshold1");
    }
  }

  // Test that parent thresholded events are dropped while some nested events
  // remain.
  {
    TRACE_EVENT0("time", "nonthreshold3");
    {
      TRACE_EVENT_IF_LONGER_THAN0(200000000, "time", "3thresholdlong2");
      {
        TRACE_EVENT_IF_LONGER_THAN0(100000000, "time", "3thresholdlong1");
        {
          TRACE_EVENT_IF_LONGER_THAN0(10000, "time", "3threshold10000");
          {
            TRACE_EVENT_IF_LONGER_THAN0(1000, "time", "3threshold1000");
            {
              TRACE_EVENT_IF_LONGER_THAN0(100, "time", "3threshold100");
              base::PlatformThread::Sleep(20);
            }
          }
        }
      }
    }
  }

  // Test that child thresholded events are dropped while some parent events
  // remain.
  {
    TRACE_EVENT0("time", "nonthreshold4");
    {
      TRACE_EVENT_IF_LONGER_THAN0(100, "time", "4threshold100");
      {
        TRACE_EVENT_IF_LONGER_THAN0(1000, "time", "4threshold1000");
        {
          TRACE_EVENT_IF_LONGER_THAN0(10000, "time", "4threshold10000");
          {
            TRACE_EVENT_IF_LONGER_THAN0(100000000, "time",
                                        "4thresholdlong1");
            {
              TRACE_EVENT_IF_LONGER_THAN0(200000000, "time",
                                          "4thresholdlong2");
              base::PlatformThread::Sleep(20);
            }
          }
        }
      }
    }
  }

  TraceLog::GetInstance()->SetEnabled(false);

#define EXPECT_FIND_BE_(str) \
  EXPECT_TRUE(FindNamePhase(str, "B")); \
  EXPECT_TRUE(FindNamePhase(str, "E"))
#define EXPECT_NOT_FIND_BE_(str) \
  EXPECT_FALSE(FindNamePhase(str, "B")); \
  EXPECT_FALSE(FindNamePhase(str, "E"))

  EXPECT_FIND_BE_("threshold 100");
  EXPECT_FIND_BE_("threshold 1000");
  EXPECT_FIND_BE_("threshold 10000");
  EXPECT_NOT_FIND_BE_("threshold long1");
  EXPECT_NOT_FIND_BE_("threshold long2");

  EXPECT_NOT_FIND_BE_("2threshold10000");
  EXPECT_FIND_BE_("nonthreshold1");

  EXPECT_FIND_BE_("nonthreshold3");
  EXPECT_FIND_BE_("3threshold100");
  EXPECT_FIND_BE_("3threshold1000");
  EXPECT_FIND_BE_("3threshold10000");
  EXPECT_NOT_FIND_BE_("3thresholdlong1");
  EXPECT_NOT_FIND_BE_("3thresholdlong2");

  EXPECT_FIND_BE_("nonthreshold4");
  EXPECT_FIND_BE_("4threshold100");
  EXPECT_FIND_BE_("4threshold1000");
  EXPECT_FIND_BE_("4threshold10000");
  EXPECT_NOT_FIND_BE_("4thresholdlong1");
  EXPECT_NOT_FIND_BE_("4thresholdlong2");
}

// Test that data sent from other threads is gathered
TEST_F(TraceEventTestFixture, DataCapturedOnThread) {
  ManualTestSetUp();
  TraceLog::GetInstance()->SetEnabled(true);

  Thread thread("1");
  WaitableEvent task_complete_event(false, false);
  thread.Start();

  thread.message_loop()->PostTask(
    FROM_HERE, NewRunnableFunction(&DataCapturedCallTraces,
                                   &task_complete_event));
  task_complete_event.Wait();

  TraceLog::GetInstance()->SetEnabled(false);
  thread.Stop();
  DataCapturedValidateTraces(trace_parsed_, trace_string_);
}

namespace {

void DataCapturedCallManyTraces(int thread_id, int num_events,
                                 WaitableEvent* task_complete_event) {
  for (int i = 0; i < num_events; i++) {
    TRACE_EVENT_INSTANT2("all", "multi thread event",
                         "thread", thread_id,
                         "event", i);
  }

  if (task_complete_event)
    task_complete_event->Signal();
}

void DataCapturedValidateManyTraces(const ListValue& trace_parsed,
                                     const std::string& trace_string,
                                     int num_threads, int num_events) {
  std::map<int, std::map<int, bool> > results;

  size_t trace_parsed_count = trace_parsed.GetSize();
  for (size_t i = 0; i < trace_parsed_count; i++) {
    Value* value = NULL;
    trace_parsed.Get(i, &value);
    if (!value || value->GetType() != Value::TYPE_DICTIONARY)
      continue;
    DictionaryValue* dict = static_cast<DictionaryValue*>(value);
    std::string name;
    dict->GetString("name", &name);
    if (name != "multi thread event")
      continue;

    int thread = 0;
    int event = 0;
    EXPECT_TRUE(dict->GetInteger("args.thread", &thread));
    EXPECT_TRUE(dict->GetInteger("args.event", &event));
    results[thread][event] = true;
  }

  EXPECT_FALSE(results[-1][-1]);
  for (int thread = 0; thread < num_threads; thread++) {
    for (int event = 0; event < num_events; event++) {
      EXPECT_TRUE(results[thread][event]);
    }
  }
}

}  // namespace

// Test that data sent from multiple threads is gathered
TEST_F(TraceEventTestFixture, DataCapturedManyThreads) {
  ManualTestSetUp();
  TraceLog::GetInstance()->SetEnabled(true);

  const int num_threads = 4;
  const int num_events = 4000;
  Thread* threads[num_threads];
  WaitableEvent* task_complete_events[num_threads];
  for (int i = 0; i < num_threads; i++) {
    threads[i] = new Thread(StringPrintf("Thread %d", i).c_str());
    task_complete_events[i] = new WaitableEvent(false, false);
    threads[i]->Start();
    threads[i]->message_loop()->PostTask(
      FROM_HERE, NewRunnableFunction(&DataCapturedCallManyTraces,
                                     i, num_events, task_complete_events[i]));
  }

  for (int i = 0; i < num_threads; i++) {
    task_complete_events[i]->Wait();
  }

  TraceLog::GetInstance()->SetEnabled(false);

  for (int i = 0; i < num_threads; i++) {
    threads[i]->Stop();
    delete threads[i];
    delete task_complete_events[i];
  }

  DataCapturedValidateManyTraces(trace_parsed_, trace_string_,
                                  num_threads, num_events);
}

void TraceCallsWithCachedCategoryPointersPointers(const char* name_str) {
  TRACE_EVENT0("category name1", name_str);
  TRACE_EVENT_INSTANT0("category name2", name_str);
  TRACE_EVENT_BEGIN0("category name3", name_str);
  TRACE_EVENT_END0("category name4", name_str);
}

// Test trace calls made after tracing singleton shut down.
//
// The singleton is destroyed by our base::AtExitManager, but there can be
// code still executing as the C++ static objects are destroyed. This test
// forces the singleton to destroy early, and intentinally makes trace calls
// afterwards.
TEST_F(TraceEventTestFixture, AtExit) {
  // Repeat this test a few times. Besides just showing robustness, it also
  // allows us to test that events at shutdown do not appear with valid events
  // recorded after the system is started again.
  for (int i = 0; i < 4; i++) {
    // Scope to contain the then destroy the TraceLog singleton.
    {
      base::ShadowingAtExitManager exit_manager_will_destroy_singletons;

      // Setup TraceLog singleton inside this test's exit manager scope
      // so that it will be destroyed when this scope closes.
      ManualTestSetUp();

      TRACE_EVENT_INSTANT0("all", "not recorded; system not enabled");

      TraceLog::GetInstance()->SetEnabled(true);

      TRACE_EVENT_INSTANT0("all", "is recorded 1; system has been enabled");
      // Trace calls that will cache pointers to categories; they're valid here
      TraceCallsWithCachedCategoryPointersPointers(
          "is recorded 2; system has been enabled");

      TraceLog::GetInstance()->SetEnabled(false);
    } // scope to destroy singleton
    ASSERT_FALSE(TraceLog::GetInstance());

    // Now that singleton is destroyed, check what trace events were recorded
    DictionaryValue* item = NULL;
    ListValue& trace_parsed = trace_parsed_;
    EXPECT_FIND_("is recorded 1");
    EXPECT_FIND_("is recorded 2");
    EXPECT_NOT_FIND_("not recorded");

    // Make additional trace event calls on the shutdown system. They should
    // all pass cleanly, but the data not be recorded. We'll verify that next
    // time around the loop (the only way to flush the trace buffers).
    TRACE_EVENT_BEGIN_ETW("not recorded; system shutdown", 0, NULL);
    TRACE_EVENT_END_ETW("not recorded; system shutdown", 0, NULL);
    TRACE_EVENT_INSTANT_ETW("not recorded; system shutdown", 0, NULL);
    TRACE_EVENT0("all", "not recorded; system shutdown");
    TRACE_EVENT_INSTANT0("all", "not recorded; system shutdown");
    TRACE_EVENT_BEGIN0("all", "not recorded; system shutdown");
    TRACE_EVENT_END0("all", "not recorded; system shutdown");

    TRACE_EVENT0("new category 0!", "not recorded; system shutdown");
    TRACE_EVENT_INSTANT0("new category 1!", "not recorded; system shutdown");
    TRACE_EVENT_BEGIN0("new category 2!", "not recorded; system shutdown");
    TRACE_EVENT_END0("new category 3!", "not recorded; system shutdown");

    // Cached categories should be safe to check, and still disable traces
    TraceCallsWithCachedCategoryPointersPointers(
        "not recorded; system shutdown");
  }
}

}  // namespace debug
}  // namespace base
