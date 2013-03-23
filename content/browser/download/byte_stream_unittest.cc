// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/download/byte_stream.h"

#include <deque>

#include "base/bind.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop.h"
#include "base/task_runner.h"
#include "net/base/io_buffer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace tracked_objects {
class Location;
}

namespace content {
namespace {

class MockTaskRunner : public base::SequencedTaskRunner {
 public:
  MockTaskRunner();

  // TaskRunner functions.
  MOCK_METHOD3(PostDelayedTask, bool(const tracked_objects::Location&,
                                     const base::Closure&, base::TimeDelta));

  MOCK_METHOD3(PostNonNestableDelayedTask, bool(
      const tracked_objects::Location&,
      const base::Closure&,
      base::TimeDelta));

  MOCK_CONST_METHOD0(RunsTasksOnCurrentThread, bool());

 protected:
  ~MockTaskRunner();
};

MockTaskRunner::MockTaskRunner() { }

MockTaskRunner::~MockTaskRunner() { }

void CountCallbacks(int* counter) {
  ++*counter;
}

}  // namespace

class ByteStreamTest : public testing::Test {
 public:
  ByteStreamTest();

  // Create a new IO buffer of the given |buffer_size|.  Details of the
  // contents of the created buffer will be kept, and can be validated
  // by ValidateIOBuffer.
  scoped_refptr<net::IOBuffer> NewIOBuffer(size_t buffer_size) {
    scoped_refptr<net::IOBuffer> buffer(new net::IOBuffer(buffer_size));
    char *bufferp = buffer->data();
    for (size_t i = 0; i < buffer_size; i++)
      bufferp[i] = (i + producing_seed_key_) % (1 << sizeof(char));
    pointer_queue_.push_back(bufferp);
    length_queue_.push_back(buffer_size);
    ++producing_seed_key_;
    return buffer;
  }

  // Create an IOBuffer of the appropriate size and add it to the
  // ByteStream, returning the result of the ByteStream::Write.
  // Separate function to avoid duplication of buffer_size in test
  // calls.
  bool Write(ByteStreamWriter* byte_stream_input, size_t buffer_size) {
    return byte_stream_input->Write(NewIOBuffer(buffer_size), buffer_size);
  }

  // Validate that we have the IOBuffer we expect.  This routine must be
  // called on buffers that were allocated from NewIOBuffer, and in the
  // order that they were allocated.  Calls to NewIOBuffer &&
  // ValidateIOBuffer may be interleaved.
  bool ValidateIOBuffer(
      scoped_refptr<net::IOBuffer> buffer, size_t buffer_size) {
    char *bufferp = buffer->data();

    char *expected_ptr = pointer_queue_.front();
    size_t expected_length = length_queue_.front();
    pointer_queue_.pop_front();
    length_queue_.pop_front();
    ++consuming_seed_key_;

    EXPECT_EQ(expected_ptr, bufferp);
    if (expected_ptr != bufferp)
      return false;

    EXPECT_EQ(expected_length, buffer_size);
    if (expected_length != buffer_size)
      return false;

    for (size_t i = 0; i < buffer_size; i++) {
      // Already incremented, so subtract one from the key.
      EXPECT_EQ(static_cast<int>((i + consuming_seed_key_ - 1)
                                 % (1 << sizeof(char))),
                bufferp[i]);
      if (static_cast<int>((i + consuming_seed_key_ - 1) %
                           (1 << sizeof(char))) != bufferp[i]) {
        return false;
      }
    }
    return true;
  }

 protected:
  MessageLoop message_loop_;

 private:
  int producing_seed_key_;
  int consuming_seed_key_;
  std::deque<char*> pointer_queue_;
  std::deque<size_t> length_queue_;
};

ByteStreamTest::ByteStreamTest()
    : producing_seed_key_(0),
      consuming_seed_key_(0) { }

// Confirm that filling and emptying the stream works properly, and that
// we get full triggers when we expect.
TEST_F(ByteStreamTest, ByteStream_PushBack) {
  scoped_ptr<ByteStreamWriter> byte_stream_input;
  scoped_ptr<ByteStreamReader> byte_stream_output;
  CreateByteStream(
      message_loop_.message_loop_proxy(), message_loop_.message_loop_proxy(),
      3 * 1024, &byte_stream_input, &byte_stream_output);

  // Push a series of IO buffers on; test pushback happening and
  // that it's advisory.
  EXPECT_TRUE(Write(byte_stream_input.get(), 1024));
  EXPECT_TRUE(Write(byte_stream_input.get(), 1024));
  EXPECT_TRUE(Write(byte_stream_input.get(), 1024));
  EXPECT_FALSE(Write(byte_stream_input.get(), 1));
  EXPECT_FALSE(Write(byte_stream_input.get(), 1024));
  // Flush
  byte_stream_input->Close(DOWNLOAD_INTERRUPT_REASON_NONE);
  message_loop_.RunUntilIdle();

  // Pull the IO buffers out; do we get the same buffers and do they
  // have the same contents?
  scoped_refptr<net::IOBuffer> output_io_buffer;
  size_t output_length;
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_COMPLETE,
            byte_stream_output->Read(&output_io_buffer, &output_length));
}

// Same as above, only use knowledge of the internals to confirm
// that we're getting pushback even when data's split across the two
// objects
TEST_F(ByteStreamTest, ByteStream_PushBackSplit) {
  scoped_ptr<ByteStreamWriter> byte_stream_input;
  scoped_ptr<ByteStreamReader> byte_stream_output;
  CreateByteStream(
      message_loop_.message_loop_proxy(), message_loop_.message_loop_proxy(),
      9 * 1024, &byte_stream_input, &byte_stream_output);

  // Push a series of IO buffers on; test pushback happening and
  // that it's advisory.
  EXPECT_TRUE(Write(byte_stream_input.get(), 1024));
  message_loop_.RunUntilIdle();
  EXPECT_TRUE(Write(byte_stream_input.get(), 1024));
  message_loop_.RunUntilIdle();
  EXPECT_TRUE(Write(byte_stream_input.get(), 1024));
  message_loop_.RunUntilIdle();
  EXPECT_TRUE(Write(byte_stream_input.get(), 1024));
  message_loop_.RunUntilIdle();
  EXPECT_FALSE(Write(byte_stream_input.get(), 6 * 1024));
  message_loop_.RunUntilIdle();

  // Pull the IO buffers out; do we get the same buffers and do they
  // have the same contents?
  scoped_refptr<net::IOBuffer> output_io_buffer;
  size_t output_length;
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));
}

// Confirm that a Close() notification transmits in-order
// with data on the stream.
TEST_F(ByteStreamTest, ByteStream_CompleteTransmits) {
  scoped_ptr<ByteStreamWriter> byte_stream_input;
  scoped_ptr<ByteStreamReader> byte_stream_output;

  scoped_refptr<net::IOBuffer> output_io_buffer;
  size_t output_length;

  // Empty stream, non-error case.
  CreateByteStream(
      message_loop_.message_loop_proxy(), message_loop_.message_loop_proxy(),
      3 * 1024, &byte_stream_input, &byte_stream_output);
  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  byte_stream_input->Close(DOWNLOAD_INTERRUPT_REASON_NONE);
  message_loop_.RunUntilIdle();
  ASSERT_EQ(ByteStreamReader::STREAM_COMPLETE,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_EQ(DOWNLOAD_INTERRUPT_REASON_NONE,
            byte_stream_output->GetStatus());

  // Non-empty stream, non-error case.
  CreateByteStream(
      message_loop_.message_loop_proxy(), message_loop_.message_loop_proxy(),
      3 * 1024, &byte_stream_input, &byte_stream_output);
  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(Write(byte_stream_input.get(), 1024));
  byte_stream_input->Close(DOWNLOAD_INTERRUPT_REASON_NONE);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));
  ASSERT_EQ(ByteStreamReader::STREAM_COMPLETE,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_EQ(DOWNLOAD_INTERRUPT_REASON_NONE,
            byte_stream_output->GetStatus());

  // Empty stream, non-error case.
  CreateByteStream(
      message_loop_.message_loop_proxy(), message_loop_.message_loop_proxy(),
      3 * 1024, &byte_stream_input, &byte_stream_output);
  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  byte_stream_input->Close(DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED);
  message_loop_.RunUntilIdle();
  ASSERT_EQ(ByteStreamReader::STREAM_COMPLETE,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_EQ(DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED,
            byte_stream_output->GetStatus());

  // Non-empty stream, non-error case.
  CreateByteStream(
      message_loop_.message_loop_proxy(), message_loop_.message_loop_proxy(),
      3 * 1024, &byte_stream_input, &byte_stream_output);
  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(Write(byte_stream_input.get(), 1024));
  byte_stream_input->Close(DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED);
  message_loop_.RunUntilIdle();
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));
  ASSERT_EQ(ByteStreamReader::STREAM_COMPLETE,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_EQ(DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED,
            byte_stream_output->GetStatus());
}

// Confirm that callbacks on the sink side are triggered when they should be.
TEST_F(ByteStreamTest, ByteStream_SinkCallback) {
  scoped_refptr<MockTaskRunner> task_runner(new StrictMock<MockTaskRunner>());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));

  scoped_ptr<ByteStreamWriter> byte_stream_input;
  scoped_ptr<ByteStreamReader> byte_stream_output;
  CreateByteStream(
      message_loop_.message_loop_proxy(), task_runner,
      10000, &byte_stream_input, &byte_stream_output);

  scoped_refptr<net::IOBuffer> output_io_buffer;
  size_t output_length;
  base::Closure intermediate_callback;

  // Note that the specifics of when the callbacks are called with regard
  // to how much data is pushed onto the stream is not (currently) part
  // of the interface contract.  If it becomes part of the contract, the
  // tests below should get much more precise.

  // Confirm callback called when you add more than 33% of the buffer.

  // Setup callback
  int num_callbacks = 0;
  byte_stream_output->RegisterCallback(
      base::Bind(CountCallbacks, &num_callbacks));
  EXPECT_CALL(*task_runner.get(), PostDelayedTask(_, _, base::TimeDelta()))
      .WillOnce(DoAll(SaveArg<1>(&intermediate_callback),
                      Return(true)));

  EXPECT_TRUE(Write(byte_stream_input.get(), 4000));
  message_loop_.RunUntilIdle();

  // Check callback results match expectations.
  ::testing::Mock::VerifyAndClearExpectations(task_runner.get());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));
  EXPECT_EQ(0, num_callbacks);
  intermediate_callback.Run();
  EXPECT_EQ(1, num_callbacks);

  // Check data and stream state.
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));
  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));

  // Confirm callback *isn't* called at less than 33% (by lack of
  // unexpected call on task runner).
  EXPECT_TRUE(Write(byte_stream_input.get(), 3000));
  message_loop_.RunUntilIdle();

  // This reflects an implementation artifact that data goes with callbacks,
  // which should not be considered part of the interface guarantee.
  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));
}

// Confirm that callbacks on the source side are triggered when they should
// be.
TEST_F(ByteStreamTest, ByteStream_SourceCallback) {
  scoped_refptr<MockTaskRunner> task_runner(new StrictMock<MockTaskRunner>());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));

  scoped_ptr<ByteStreamWriter> byte_stream_input;
  scoped_ptr<ByteStreamReader> byte_stream_output;
  CreateByteStream(
      task_runner, message_loop_.message_loop_proxy(),
      10000, &byte_stream_input, &byte_stream_output);

  scoped_refptr<net::IOBuffer> output_io_buffer;
  size_t output_length;
  base::Closure intermediate_callback;

  // Note that the specifics of when the callbacks are called with regard
  // to how much data is pulled from the stream is not (currently) part
  // of the interface contract.  If it becomes part of the contract, the
  // tests below should get much more precise.

  // Confirm callback called when about 33% space available, and not
  // at other transitions.

  // Setup expectations and add data.
  int num_callbacks = 0;
  byte_stream_input->RegisterCallback(
      base::Bind(CountCallbacks, &num_callbacks));
  EXPECT_TRUE(Write(byte_stream_input.get(), 2000));
  EXPECT_TRUE(Write(byte_stream_input.get(), 2001));
  EXPECT_FALSE(Write(byte_stream_input.get(), 6000));

  // Allow bytes to transition (needed for message passing implementation),
  // and get and validate the data.
  message_loop_.RunUntilIdle();
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  // Setup expectations.
  EXPECT_CALL(*task_runner.get(), PostDelayedTask(_, _, base::TimeDelta()))
      .WillOnce(DoAll(SaveArg<1>(&intermediate_callback),
                      Return(true)));

  // Grab data, triggering callback.  Recorded on dispatch, but doesn't
  // happen because it's caught by the mock.
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  ::testing::Mock::VerifyAndClearExpectations(task_runner.get());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  // Confirm that the callback passed to the mock does what we expect.
  EXPECT_EQ(0, num_callbacks);
  intermediate_callback.Run();
  EXPECT_EQ(1, num_callbacks);

  // Same drill with final buffer.
  EXPECT_CALL(*task_runner.get(), PostDelayedTask(_, _, base::TimeDelta()))
      .WillOnce(DoAll(SaveArg<1>(&intermediate_callback),
                      Return(true)));
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  ::testing::Mock::VerifyAndClearExpectations(task_runner.get());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));
  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_EQ(1, num_callbacks);
  intermediate_callback.Run();
  // Should have updated the internal structures but not called the
  // callback.
  EXPECT_EQ(1, num_callbacks);
}

// Confirm that racing a change to a sink callback with a post results
// in the new callback being called.
TEST_F(ByteStreamTest, ByteStream_SinkInterrupt) {
  scoped_refptr<MockTaskRunner> task_runner(new StrictMock<MockTaskRunner>());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));

  scoped_ptr<ByteStreamWriter> byte_stream_input;
  scoped_ptr<ByteStreamReader> byte_stream_output;
  CreateByteStream(
      message_loop_.message_loop_proxy(), task_runner,
      10000, &byte_stream_input, &byte_stream_output);

  scoped_refptr<net::IOBuffer> output_io_buffer;
  size_t output_length;
  base::Closure intermediate_callback;

  // Setup expectations and record initial state.
  int num_callbacks = 0;
  byte_stream_output->RegisterCallback(
      base::Bind(CountCallbacks, &num_callbacks));
  EXPECT_CALL(*task_runner.get(), PostDelayedTask(_, _, base::TimeDelta()))
      .WillOnce(DoAll(SaveArg<1>(&intermediate_callback),
                      Return(true)));

  // Add data, and pass it across.
  EXPECT_TRUE(Write(byte_stream_input.get(), 4000));
  message_loop_.RunUntilIdle();

  // The task runner should have been hit, but the callback count
  // isn't changed until we actually run the callback.
  ::testing::Mock::VerifyAndClearExpectations(task_runner.get());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));
  EXPECT_EQ(0, num_callbacks);

  // If we change the callback now, the new one should be run
  // (simulates race with post task).
  int num_alt_callbacks = 0;
  byte_stream_output->RegisterCallback(
      base::Bind(CountCallbacks, &num_alt_callbacks));
  intermediate_callback.Run();
  EXPECT_EQ(0, num_callbacks);
  EXPECT_EQ(1, num_alt_callbacks);

  // Final cleanup.
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));
  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));

}

// Confirm that racing a change to a source callback with a post results
// in the new callback being called.
TEST_F(ByteStreamTest, ByteStream_SourceInterrupt) {
  scoped_refptr<MockTaskRunner> task_runner(new StrictMock<MockTaskRunner>());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));

  scoped_ptr<ByteStreamWriter> byte_stream_input;
  scoped_ptr<ByteStreamReader> byte_stream_output;
  CreateByteStream(
      task_runner, message_loop_.message_loop_proxy(),
      10000, &byte_stream_input, &byte_stream_output);

  scoped_refptr<net::IOBuffer> output_io_buffer;
  size_t output_length;
  base::Closure intermediate_callback;

  // Setup state for test and record initiali expectations
  int num_callbacks = 0;
  byte_stream_input->RegisterCallback(
      base::Bind(CountCallbacks, &num_callbacks));
  EXPECT_TRUE(Write(byte_stream_input.get(), 2000));
  EXPECT_TRUE(Write(byte_stream_input.get(), 2001));
  EXPECT_FALSE(Write(byte_stream_input.get(), 6000));
  message_loop_.RunUntilIdle();

  // Initial get should not trigger callback.
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));
  message_loop_.RunUntilIdle();

  // Setup expectations.
  EXPECT_CALL(*task_runner.get(), PostDelayedTask(_, _, base::TimeDelta()))
      .WillOnce(DoAll(SaveArg<1>(&intermediate_callback),
                      Return(true)));

  // Second get *should* trigger callback.
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  ::testing::Mock::VerifyAndClearExpectations(task_runner.get());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));

  // Which should do the right thing when it's run.
  int num_alt_callbacks = 0;
  byte_stream_input->RegisterCallback(
      base::Bind(CountCallbacks, &num_alt_callbacks));
  intermediate_callback.Run();
  EXPECT_EQ(0, num_callbacks);
  EXPECT_EQ(1, num_alt_callbacks);

  // Third get should also trigger callback.
  EXPECT_CALL(*task_runner.get(), PostDelayedTask(_, _, base::TimeDelta()))
      .WillOnce(DoAll(SaveArg<1>(&intermediate_callback),
                      Return(true)));
  EXPECT_EQ(ByteStreamReader::STREAM_HAS_DATA,
            byte_stream_output->Read(&output_io_buffer, &output_length));
  ::testing::Mock::VerifyAndClearExpectations(task_runner.get());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(ValidateIOBuffer(output_io_buffer, output_length));
  EXPECT_EQ(ByteStreamReader::STREAM_EMPTY,
            byte_stream_output->Read(&output_io_buffer, &output_length));
}

// Confirm that callback is called on zero data transfer but source
// complete.
TEST_F(ByteStreamTest, ByteStream_ZeroCallback) {
  scoped_refptr<MockTaskRunner> task_runner(new StrictMock<MockTaskRunner>());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));

  scoped_ptr<ByteStreamWriter> byte_stream_input;
  scoped_ptr<ByteStreamReader> byte_stream_output;
  CreateByteStream(
      message_loop_.message_loop_proxy(), task_runner,
      10000, &byte_stream_input, &byte_stream_output);

  base::Closure intermediate_callback;

  // Setup expectations and record initial state.
  int num_callbacks = 0;
  byte_stream_output->RegisterCallback(
      base::Bind(CountCallbacks, &num_callbacks));
  EXPECT_CALL(*task_runner.get(), PostDelayedTask(_, _, base::TimeDelta()))
      .WillOnce(DoAll(SaveArg<1>(&intermediate_callback),
                      Return(true)));

  // Immediately close the stream.
  byte_stream_input->Close(DOWNLOAD_INTERRUPT_REASON_NONE);
  ::testing::Mock::VerifyAndClearExpectations(task_runner.get());
  EXPECT_CALL(*task_runner.get(), RunsTasksOnCurrentThread())
      .WillRepeatedly(Return(true));
  intermediate_callback.Run();
  EXPECT_EQ(1, num_callbacks);
}

}  // namespace content
