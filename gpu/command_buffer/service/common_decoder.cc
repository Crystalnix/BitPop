// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/common_decoder.h"
#include "gpu/command_buffer/service/cmd_buffer_engine.h"

namespace gpu {

CommonDecoder::Bucket::Bucket() : size_(0) {}

CommonDecoder::Bucket::~Bucket() {}

void* CommonDecoder::Bucket::GetData(size_t offset, size_t size) const {
  if (OffsetSizeValid(offset, size)) {
    return data_.get() + offset;
  }
  return NULL;
}

void CommonDecoder::Bucket::SetSize(size_t size) {
  if (size != size_) {
    data_.reset(size ? new int8[size] : NULL);
    size_ = size;
    memset(data_.get(), 0, size);
  }
}

bool CommonDecoder::Bucket::SetData(
    const void* src, size_t offset, size_t size) {
  if (OffsetSizeValid(offset, size)) {
    memcpy(data_.get() + offset, src, size);
    return true;
  }
  return false;
}

void CommonDecoder::Bucket::SetFromString(const char* str) {
  // Strings are passed NULL terminated to distinguish between empty string
  // and no string.
  if (!str) {
    SetSize(0);
  } else {
    size_t size = strlen(str) + 1;
    SetSize(size);
    SetData(str, 0, size);
  }
}

bool CommonDecoder::Bucket::GetAsString(std::string* str) {
  DCHECK(str);
  if (size_ == 0) {
    return false;
  }
  str->assign(GetDataAs<const char*>(0, size_ - 1), size_ - 1);
  return true;
}

CommonDecoder::CommonDecoder() : engine_(NULL) {}

CommonDecoder::~CommonDecoder() {}

void* CommonDecoder::GetAddressAndCheckSize(unsigned int shm_id,
                                            unsigned int offset,
                                            unsigned int size) {
  Buffer buffer = engine_->GetSharedMemoryBuffer(shm_id);
  if (!buffer.ptr)
    return NULL;
  unsigned int end = offset + size;
  if (end > buffer.size || end < offset) {
    return NULL;
  }
  return static_cast<int8*>(buffer.ptr) + offset;
}

bool CommonDecoder::PushAddress(uint32 offset) {
  if (call_stack_.size() < kMaxStackDepth) {
    CommandAddress return_address(engine_->GetGetOffset());
    if (engine_->SetGetOffset(offset)) {
      call_stack_.push(return_address);
      return true;
    }
  }
  return false;
}

const char* CommonDecoder::GetCommonCommandName(
    cmd::CommandId command_id) const {
  return cmd::GetCommandName(command_id);
}

CommonDecoder::Bucket* CommonDecoder::GetBucket(uint32 bucket_id) const {
  BucketMap::const_iterator iter(buckets_.find(bucket_id));
  return iter != buckets_.end() ? &(*iter->second) : NULL;
}

CommonDecoder::Bucket* CommonDecoder::CreateBucket(uint32 bucket_id) {
  Bucket* bucket = GetBucket(bucket_id);
  if (!bucket) {
    bucket = new Bucket();
    buckets_[bucket_id] = linked_ptr<Bucket>(bucket);
  }
  return bucket;
}

namespace {

// Returns the address of the first byte after a struct.
template <typename T>
const void* AddressAfterStruct(const T& pod) {
  return reinterpret_cast<const uint8*>(&pod) + sizeof(pod);
}

// Returns the address of the frst byte after the struct.
template <typename RETURN_TYPE, typename COMMAND_TYPE>
RETURN_TYPE GetImmediateDataAs(const COMMAND_TYPE& pod) {
  return static_cast<RETURN_TYPE>(const_cast<void*>(AddressAfterStruct(pod)));
}

// A struct to hold info about each command.
struct CommandInfo {
  int arg_flags;  // How to handle the arguments for this command
  int arg_count;  // How many arguments are expected for this command.
};

// A table of CommandInfo for all the commands.
const CommandInfo g_command_info[] = {
  #define COMMON_COMMAND_BUFFER_CMD_OP(name) {                           \
    cmd::name::kArgFlags,                                                \
    sizeof(cmd::name) / sizeof(CommandBufferEntry) - 1, },  /* NOLINT */ \

  COMMON_COMMAND_BUFFER_CMDS(COMMON_COMMAND_BUFFER_CMD_OP)

  #undef COMMON_COMMAND_BUFFER_CMD_OP
};

}  // anonymous namespace.

// Decode command with its arguments, and call the corresponding method.
// Note: args is a pointer to the command buffer. As such, it could be changed
// by a (malicious) client at any time, so if validation has to happen, it
// should operate on a copy of them.
error::Error CommonDecoder::DoCommonCommand(
    unsigned int command,
    unsigned int arg_count,
    const void* cmd_data) {
  if (command < arraysize(g_command_info)) {
    const CommandInfo& info = g_command_info[command];
    unsigned int info_arg_count = static_cast<unsigned int>(info.arg_count);
    if ((info.arg_flags == cmd::kFixed && arg_count == info_arg_count) ||
        (info.arg_flags == cmd::kAtLeastN && arg_count >= info_arg_count)) {
      uint32 immediate_data_size =
          (arg_count - info_arg_count) * sizeof(CommandBufferEntry);  // NOLINT
      switch (command) {
        #define COMMON_COMMAND_BUFFER_CMD_OP(name)                      \
          case cmd::name::kCmdId:                                       \
            return Handle ## name(                                      \
                immediate_data_size,                                    \
                *static_cast<const cmd::name*>(cmd_data));              \

        COMMON_COMMAND_BUFFER_CMDS(COMMON_COMMAND_BUFFER_CMD_OP)

        #undef COMMON_COMMAND_BUFFER_CMD_OP
      }
    } else {
      return error::kInvalidArguments;
    }
  }
  return DoCommonCommand(command, arg_count, cmd_data);
  return error::kUnknownCommand;
}

error::Error CommonDecoder::HandleNoop(
    uint32 immediate_data_size,
    const cmd::Noop& args) {
  return error::kNoError;
}

error::Error CommonDecoder::HandleSetToken(
    uint32 immediate_data_size,
    const cmd::SetToken& args) {
  engine_->set_token(args.token);
  return error::kNoError;
}

error::Error CommonDecoder::HandleJump(
    uint32 immediate_data_size,
    const cmd::Jump& args) {
  if (!engine_->SetGetOffset(args.offset)) {
    return error::kInvalidArguments;
  }
  return error::kNoError;
}

error::Error CommonDecoder::HandleJumpRelative(
    uint32 immediate_data_size,
    const cmd::JumpRelative& args) {
  if (!engine_->SetGetOffset(engine_->GetGetOffset() + args.offset)) {
    return error::kInvalidArguments;
  }
  return error::kNoError;
}

error::Error CommonDecoder::HandleCall(
    uint32 immediate_data_size,
    const cmd::Call& args) {
  if (!PushAddress(args.offset)) {
    return error::kInvalidArguments;
  }
  return error::kNoError;
}

error::Error CommonDecoder::HandleCallRelative(
    uint32 immediate_data_size,
    const cmd::CallRelative& args) {
  if (!PushAddress(engine_->GetGetOffset() + args.offset)) {
    return error::kInvalidArguments;
  }
  return error::kNoError;
}

error::Error CommonDecoder::HandleReturn(
    uint32 immediate_data_size,
    const cmd::Return& args) {
  if (call_stack_.empty()) {
    return error::kInvalidArguments;
  }
  CommandAddress return_address = call_stack_.top();
  call_stack_.pop();
  if (!engine_->SetGetOffset(return_address.offset)) {
    return error::kInvalidArguments;
  }
  return error::kNoError;
}

error::Error CommonDecoder::HandleSetBucketSize(
    uint32 immediate_data_size,
    const cmd::SetBucketSize& args) {
  uint32 bucket_id = args.bucket_id;
  uint32 size = args.size;

  Bucket* bucket = CreateBucket(bucket_id);
  bucket->SetSize(size);
  return error::kNoError;
}

error::Error CommonDecoder::HandleSetBucketData(
    uint32 immediate_data_size,
    const cmd::SetBucketData& args) {
  uint32 bucket_id = args.bucket_id;
  uint32 offset = args.offset;
  uint32 size = args.size;
  const void* data = GetSharedMemoryAs<const void*>(
      args.shared_memory_id, args.shared_memory_offset, size);
  if (!data) {
    return error::kInvalidArguments;
  }
  Bucket* bucket = GetBucket(bucket_id);
  if (!bucket) {
    return error::kInvalidArguments;
  }
  if (!bucket->SetData(data, offset, size)) {
    return error::kInvalidArguments;
  }

  return error::kNoError;
}

error::Error CommonDecoder::HandleSetBucketDataImmediate(
    uint32 immediate_data_size,
    const cmd::SetBucketDataImmediate& args) {
  const void* data = GetImmediateDataAs<const void*>(args);
  uint32 bucket_id = args.bucket_id;
  uint32 offset = args.offset;
  uint32 size = args.size;
  if (size > immediate_data_size) {
    return error::kInvalidArguments;
  }
  Bucket* bucket = GetBucket(bucket_id);
  if (!bucket) {
    return error::kInvalidArguments;
  }
  if (!bucket->SetData(data, offset, size)) {
    return error::kInvalidArguments;
  }
  return error::kNoError;
}

error::Error CommonDecoder::HandleGetBucketSize(
    uint32 immediate_data_size,
    const cmd::GetBucketSize& args) {
  uint32 bucket_id = args.bucket_id;
  uint32* data = GetSharedMemoryAs<uint32*>(
      args.shared_memory_id, args.shared_memory_offset, sizeof(*data));
  if (!data) {
    return error::kInvalidArguments;
  }
  // Check that the client initialized the result.
  if (*data != 0) {
    return error::kInvalidArguments;
  }
  Bucket* bucket = GetBucket(bucket_id);
  if (!bucket) {
    return error::kInvalidArguments;
  }
  *data = bucket->size();
  return error::kNoError;
}

error::Error CommonDecoder::HandleGetBucketData(
    uint32 immediate_data_size,
    const cmd::GetBucketData& args) {
  uint32 bucket_id = args.bucket_id;
  uint32 offset = args.offset;
  uint32 size = args.size;
  void* data = GetSharedMemoryAs<void*>(
      args.shared_memory_id, args.shared_memory_offset, size);
  if (!data) {
    return error::kInvalidArguments;
  }
  Bucket* bucket = GetBucket(bucket_id);
  if (!bucket) {
    return error::kInvalidArguments;
  }
  const void* src = bucket->GetData(offset, size);
  if (!src) {
      return error::kInvalidArguments;
  }
  memcpy(data, src, size);
  return error::kNoError;
}

}  // namespace gpu
