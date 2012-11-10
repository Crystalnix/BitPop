// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/sys_byteorder.h"
#include "media/base/decoder_buffer.h"
#include "media/base/decrypt_config.h"
#include "media/base/mock_filters.h"
#include "media/crypto/aes_decryptor.h"
#include "media/webm/webm_constants.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Gt;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::SaveArg;
using ::testing::StrNe;

namespace media {

// |encrypted_data| is encrypted from |plain_text| using |key|. |key_id| is
// used to distinguish |key|.
struct WebmEncryptedData {
  uint8 plain_text[32];
  int plain_text_size;
  uint8 key_id[32];
  int key_id_size;
  uint8 key[32];
  int key_size;
  uint8 encrypted_data[64];
  int encrypted_data_size;
};

static const char kClearKeySystem[] = "org.w3.clearkey";

// Frames 0 & 1 are encrypted with the same key. Frame 2 is encrypted with a
// different key. Frame 3 has the same HMAC key as frame 2, but frame 3 is
// unencrypted.
const WebmEncryptedData kWebmEncryptedFrames[] = {
  {
    // plaintext
    "Original data.", 14,
    // key_id
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
      0x10, 0x11, 0x12, 0x13
      }, 20,
    // key
    { 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
      0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23
      }, 16,
    // encrypted_data
    { 0x3c, 0x4e, 0xb8, 0xd9, 0x5c, 0x20, 0x48, 0x18,
      0x4f, 0x03, 0x74, 0xa1, 0x01, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0x99, 0xaa, 0xff,
      0xb7, 0x74, 0x02, 0x4e, 0x1c, 0x75, 0x3d, 0xee,
      0xcb, 0x64, 0xf7
      }, 35
  },
  {
    // plaintext
    "Changed Original data.", 22,
    // key_id
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
      0x10, 0x11, 0x12, 0x13
      }, 20,
    // key
    { 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
      0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23
      }, 16,
    // encrypted_data
    { 0xe8, 0x4c, 0x51, 0x33, 0x14, 0x0d, 0xc7, 0x17,
      0x32, 0x60, 0xc9, 0xd0, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0xec, 0x8e, 0x87,
      0x21, 0xd3, 0xb9, 0x1c, 0x61, 0xf6, 0x5a, 0x60,
      0xaa, 0x07, 0x0e, 0x96, 0xd0, 0x54, 0x5d, 0x35,
      0x9a, 0x4a, 0xd3
      }, 43
  },
  {
    // plaintext
    "Original data.", 14,
    // key_id
    { 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
      0x2c, 0x2d, 0x2e, 0x2f, 0x30
      }, 13,
    // key
    { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40
      }, 16,
    // encrypted_data
    { 0x46, 0x93, 0x8c, 0x93, 0x48, 0xf9, 0xeb, 0x30,
      0x74, 0x55, 0x6b, 0xf2, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x01, 0x48, 0x5e, 0x4a,
      0x41, 0x2a, 0x8b, 0xf4, 0xc6, 0x47, 0x54, 0x90,
      0x34, 0xf4, 0x8b
      }, 35
  },
  {
    // plaintext
    "Changed Original data.", 22,
    // key_id
    { 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
      0x2c, 0x2d, 0x2e, 0x2f, 0x30
      }, 13,
    // key
    { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40
      }, 16,
    // encrypted_data
    { 0xee, 0xd6, 0xf5, 0x64, 0x5f, 0xe0, 0x6a, 0xa2,
      0x9e, 0xd6, 0xce, 0x34, 0x00, 0x43, 0x68, 0x61,
      0x6e, 0x67, 0x65, 0x64, 0x20, 0x4f, 0x72, 0x69,
      0x67, 0x69, 0x6e, 0x61, 0x6c, 0x20, 0x64, 0x61,
      0x74, 0x61, 0x2e
      }, 35
  }
};

static const uint8 kWebmWrongSizedKey[] = { 0x20, 0x20 };

static const uint8 kSubsampleOriginalData[] = "Original subsample data.";
static const int kSubsampleOriginalDataSize = 24;

static const uint8 kSubsampleKeyId[] = { 0x00, 0x01, 0x02, 0x03 };

static const uint8 kSubsampleKey[] = {
  0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
  0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13
};

static const uint8 kSubsampleIv[] = {
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8 kSubsampleData[] = {
  0x4f, 0x72, 0x09, 0x16, 0x09, 0xe6, 0x79, 0xad,
  0x70, 0x73, 0x75, 0x62, 0x09, 0xbb, 0x83, 0x1d,
  0x4d, 0x08, 0xd7, 0x78, 0xa4, 0xa7, 0xf1, 0x2e
};

static const uint8 kPaddedSubsampleData[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x4f, 0x72, 0x09, 0x16, 0x09, 0xe6, 0x79, 0xad,
  0x70, 0x73, 0x75, 0x62, 0x09, 0xbb, 0x83, 0x1d,
  0x4d, 0x08, 0xd7, 0x78, 0xa4, 0xa7, 0xf1, 0x2e
};

// Encrypted with kSubsampleKey and kSubsampleIv but without subsamples.
static const uint8 kNoSubsampleData[] = {
  0x2f, 0x03, 0x09, 0xef, 0x71, 0xaf, 0x31, 0x16,
  0xfa, 0x9d, 0x18, 0x43, 0x1e, 0x96, 0x71, 0xb5,
  0xbf, 0xf5, 0x30, 0x53, 0x9a, 0x20, 0xdf, 0x95
};

static const SubsampleEntry kSubsampleEntries[] = {
  { 2, 7 },
  { 3, 11 },
  { 1, 0 }
};

// Returns a 16 byte CTR counter block. The CTR counter block format is a
// CTR IV appended with a CTR block counter. |iv| is a CTR IV. |iv_size| is
// the size of |iv| in bytes.
static std::string GenerateCounterBlock(const uint8* iv, int iv_size) {
  const int kDecryptionKeySize = 16;
  CHECK_GT(iv_size, 0);
  CHECK_LE(iv_size, kDecryptionKeySize);

  std::string counter_block(reinterpret_cast<const char*>(iv), iv_size);
  counter_block.append(kDecryptionKeySize - iv_size, 0);
  return counter_block;
}

// Creates a WebM encrypted buffer that the demuxer would pass to the
// decryptor. |data| is the payload of a WebM encrypted Block. |key_id| is
// initialization data from the WebM file. Every encrypted Block has
// an HMAC and a signal byte prepended to a frame. If the frame is encrypted
// then an IV is prepended to the Block. Current encrypted WebM request for
// comments specification is here
// http://wiki.webmproject.org/encryption/webm-encryption-rfc
static scoped_refptr<DecoderBuffer> CreateWebMEncryptedBuffer(
    const uint8* data, int data_size,
    const uint8* key_id, int key_id_size) {
  scoped_refptr<DecoderBuffer> encrypted_buffer = DecoderBuffer::CopyFrom(
      data + kWebMHmacSize, data_size - kWebMHmacSize);
  CHECK(encrypted_buffer);

  uint8 signal_byte = data[kWebMHmacSize];
  int data_offset = sizeof(signal_byte);

  // Setting the DecryptConfig object of the buffer while leaving the
  // initialization vector empty will tell the decryptor that the frame is
  // unencrypted but integrity should still be checked.
  std::string counter_block_str;

  if (signal_byte & kWebMFlagEncryptedFrame) {
    uint64 network_iv;
    memcpy(&network_iv, data + kWebMHmacSize + data_offset, sizeof(network_iv));
    const uint64 iv = base::NetToHost64(network_iv);
    counter_block_str =
        GenerateCounterBlock(reinterpret_cast<const uint8*>(&iv), sizeof(iv));
    data_offset += sizeof(iv);
  }

  encrypted_buffer->SetDecryptConfig(
      scoped_ptr<DecryptConfig>(new DecryptConfig(
          std::string(reinterpret_cast<const char*>(key_id), key_id_size),
          counter_block_str,
          std::string(reinterpret_cast<const char*>(data), kWebMHmacSize),
          data_offset,
          std::vector<SubsampleEntry>())));
  return encrypted_buffer;
}

static scoped_refptr<DecoderBuffer> CreateSubsampleEncryptedBuffer(
    const uint8* data, int data_size,
    const uint8* key_id, int key_id_size,
    const uint8* iv, int iv_size,
    int data_offset,
    const std::vector<SubsampleEntry>& subsample_entries) {
  scoped_refptr<DecoderBuffer> encrypted_buffer =
      DecoderBuffer::CopyFrom(data, data_size);
  CHECK(encrypted_buffer);
  encrypted_buffer->SetDecryptConfig(
      scoped_ptr<DecryptConfig>(new DecryptConfig(
          std::string(reinterpret_cast<const char*>(key_id), key_id_size),
          std::string(reinterpret_cast<const char*>(iv), iv_size),
          std::string(),
          data_offset,
          subsample_entries)));
  return encrypted_buffer;
}

class AesDecryptorTest : public testing::Test {
 public:
  AesDecryptorTest()
      : decryptor_(&client_),
        decrypt_cb_(base::Bind(&AesDecryptorTest::BufferDecrypted,
                               base::Unretained(this))),
        subsample_entries_(kSubsampleEntries,
                           kSubsampleEntries + arraysize(kSubsampleEntries)) {
  }

 protected:
  void GenerateKeyRequest(const uint8* key_id, int key_id_size) {
    EXPECT_CALL(client_, KeyMessageMock(kClearKeySystem, StrNe(std::string()),
                                        NotNull(), Gt(0), ""))
        .WillOnce(SaveArg<1>(&session_id_string_));
    decryptor_.GenerateKeyRequest(kClearKeySystem, key_id, key_id_size);
  }

  void AddKeyAndExpectToSucceed(const uint8* key_id, int key_id_size,
                                const uint8* key, int key_size) {
    EXPECT_CALL(client_, KeyAdded(kClearKeySystem, session_id_string_));
    decryptor_.AddKey(kClearKeySystem, key, key_size, key_id, key_id_size,
                      session_id_string_);
  }

  void AddKeyAndExpectToFail(const uint8* key_id, int key_id_size,
                             const uint8* key, int key_size) {
    EXPECT_CALL(client_, KeyError(kClearKeySystem, session_id_string_,
                                  Decryptor::kUnknownError, 0));
    decryptor_.AddKey(kClearKeySystem, key, key_size, key_id, key_id_size,
                      session_id_string_);
  }

  MOCK_METHOD2(BufferDecrypted, void(Decryptor::DecryptStatus,
                                     const scoped_refptr<DecoderBuffer>&));

  void DecryptAndExpectToSucceed(const scoped_refptr<DecoderBuffer>& encrypted,
                                 const uint8* plain_text, int plain_text_size) {
    scoped_refptr<DecoderBuffer> decrypted;
    EXPECT_CALL(*this, BufferDecrypted(AesDecryptor::kSuccess, NotNull()))
        .WillOnce(SaveArg<1>(&decrypted));

    decryptor_.Decrypt(encrypted, decrypt_cb_);
    ASSERT_TRUE(decrypted);
    ASSERT_EQ(plain_text_size, decrypted->GetDataSize());
    EXPECT_EQ(0, memcmp(plain_text, decrypted->GetData(), plain_text_size));
  }

  void DecryptAndExpectToFail(const scoped_refptr<DecoderBuffer>& encrypted) {
    EXPECT_CALL(*this, BufferDecrypted(AesDecryptor::kError, IsNull()));
    decryptor_.Decrypt(encrypted, decrypt_cb_);
  }

  MockDecryptorClient client_;
  AesDecryptor decryptor_;
  std::string session_id_string_;
  AesDecryptor::DecryptCB decrypt_cb_;
  std::vector<SubsampleEntry> subsample_entries_;
};

TEST_F(AesDecryptorTest, NormalWebMDecryption) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);
  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           frame.key, frame.key_size);
  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(frame.encrypted_data,
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToSucceed(encrypted_data,
                                                    frame.plain_text,
                                                    frame.plain_text_size));
}

TEST_F(AesDecryptorTest, UnencryptedFrameWebMDecryption) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[3];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);
  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           frame.key, frame.key_size);
  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(frame.encrypted_data,
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToSucceed(encrypted_data,
                                                    frame.plain_text,
                                                    frame.plain_text_size));
}

TEST_F(AesDecryptorTest, WrongKey) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);

  // Change the first byte of the key.
  std::vector<uint8> wrong_key(frame.key, frame.key + frame.key_size);
  wrong_key[0]++;

  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           &wrong_key[0], frame.key_size);
  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(frame.encrypted_data,
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToFail(encrypted_data));
}

TEST_F(AesDecryptorTest, NoKey) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);

  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(frame.encrypted_data, frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  EXPECT_CALL(*this, BufferDecrypted(AesDecryptor::kNoKey, IsNull()));
  decryptor_.Decrypt(encrypted_data, decrypt_cb_);
}

TEST_F(AesDecryptorTest, KeyReplacement) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);

  // Change the first byte of the key.
  std::vector<uint8> wrong_key(frame.key, frame.key + frame.key_size);
  wrong_key[0]++;

  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           &wrong_key[0], frame.key_size);
  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(frame.encrypted_data,
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToFail(encrypted_data));
  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           frame.key, frame.key_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToSucceed(encrypted_data,
                                                    frame.plain_text,
                                                    frame.plain_text_size));
}

TEST_F(AesDecryptorTest, WrongSizedKey) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);
  AddKeyAndExpectToFail(frame.key_id, frame.key_id_size,
                        kWebmWrongSizedKey, arraysize(kWebmWrongSizedKey));
}

TEST_F(AesDecryptorTest, MultipleKeysAndFrames) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);
  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           frame.key, frame.key_size);
  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(frame.encrypted_data,
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToSucceed(encrypted_data,
                                                    frame.plain_text,
                                                    frame.plain_text_size));

  const WebmEncryptedData& frame2 = kWebmEncryptedFrames[2];
  GenerateKeyRequest(frame2.key_id, frame2.key_id_size);
  AddKeyAndExpectToSucceed(frame2.key_id, frame2.key_id_size,
                           frame2.key, frame2.key_size);

  const WebmEncryptedData& frame1 = kWebmEncryptedFrames[1];
  scoped_refptr<DecoderBuffer> encrypted_data1 =
      CreateWebMEncryptedBuffer(frame1.encrypted_data,
                                frame1.encrypted_data_size,
                                frame1.key_id, frame1.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToSucceed(encrypted_data1,
                                                    frame1.plain_text,
                                                    frame1.plain_text_size));

  scoped_refptr<DecoderBuffer> encrypted_data2 =
      CreateWebMEncryptedBuffer(frame2.encrypted_data,
                                frame2.encrypted_data_size,
                                frame2.key_id, frame2.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToSucceed(encrypted_data2,
                                                    frame2.plain_text,
                                                    frame2.plain_text_size));
}

TEST_F(AesDecryptorTest, HmacCheckFailure) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);
  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           frame.key, frame.key_size);

  // Change byte 0 to modify the HMAC. Bytes 0-11 of WebM encrypted data
  // contains the HMAC.
  std::vector<uint8> frame_with_bad_hmac(
      frame.encrypted_data, frame.encrypted_data + frame.encrypted_data_size);
  frame_with_bad_hmac[0]++;

  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(&frame_with_bad_hmac[0],
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToFail(encrypted_data));
}

TEST_F(AesDecryptorTest, IvCheckFailure) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);
  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           frame.key, frame.key_size);

  // Change byte 13 to modify the IV. Bytes 13-20 of WebM encrypted data
  // contains the IV.
  std::vector<uint8> frame_with_bad_iv(
      frame.encrypted_data, frame.encrypted_data + frame.encrypted_data_size);
  frame_with_bad_iv[kWebMHmacSize + 1]++;

  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(&frame_with_bad_iv[0],
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToFail(encrypted_data));
}

TEST_F(AesDecryptorTest, DataCheckFailure) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);
  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           frame.key, frame.key_size);

  // Change last byte to modify the data. Bytes 21+ of WebM encrypted data
  // contains the encrypted frame.
  std::vector<uint8> frame_with_bad_vp8_data(
      frame.encrypted_data, frame.encrypted_data + frame.encrypted_data_size);
  frame_with_bad_vp8_data[frame.encrypted_data_size - 1]++;

  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(&frame_with_bad_vp8_data[0],
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToFail(encrypted_data));
}

TEST_F(AesDecryptorTest, EncryptedAsUnencryptedFailure) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[0];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);
  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           frame.key, frame.key_size);

  // Change signal byte from an encrypted frame to an unencrypted frame. Byte
  // 12 of WebM encrypted data contains the signal byte.
  std::vector<uint8> frame_with_wrong_signal_byte(
      frame.encrypted_data, frame.encrypted_data + frame.encrypted_data_size);
  frame_with_wrong_signal_byte[kWebMHmacSize] = 0;

  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(&frame_with_wrong_signal_byte[0],
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToFail(encrypted_data));
}

TEST_F(AesDecryptorTest, UnencryptedAsEncryptedFailure) {
  const WebmEncryptedData& frame = kWebmEncryptedFrames[3];
  GenerateKeyRequest(frame.key_id, frame.key_id_size);
  AddKeyAndExpectToSucceed(frame.key_id, frame.key_id_size,
                           frame.key, frame.key_size);

  // Change signal byte from an unencrypted frame to an encrypted frame. Byte
  // 12 of WebM encrypted data contains the signal byte.
  std::vector<uint8> frame_with_wrong_signal_byte(
      frame.encrypted_data, frame.encrypted_data + frame.encrypted_data_size);
  frame_with_wrong_signal_byte[kWebMHmacSize] = kWebMFlagEncryptedFrame;

  scoped_refptr<DecoderBuffer> encrypted_data =
      CreateWebMEncryptedBuffer(&frame_with_wrong_signal_byte[0],
                                frame.encrypted_data_size,
                                frame.key_id, frame.key_id_size);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToFail(encrypted_data));
}

TEST_F(AesDecryptorTest, SubsampleDecryption) {
  GenerateKeyRequest(kSubsampleKeyId, arraysize(kSubsampleKeyId));
  AddKeyAndExpectToSucceed(kSubsampleKeyId, arraysize(kSubsampleKeyId),
                           kSubsampleKey, arraysize(kSubsampleKey));
  scoped_refptr<DecoderBuffer> encrypted_data = CreateSubsampleEncryptedBuffer(
      kSubsampleData, arraysize(kSubsampleData),
      kSubsampleKeyId, arraysize(kSubsampleKeyId),
      kSubsampleIv, arraysize(kSubsampleIv),
      0,
      subsample_entries_);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToSucceed(
      encrypted_data, kSubsampleOriginalData, kSubsampleOriginalDataSize));
}

// Ensures noninterference of data offset and subsample mechanisms. We never
// expect to encounter this in the wild, but since the DecryptConfig doesn't
// disallow such a configuration, it should be covered.
TEST_F(AesDecryptorTest, SubsampleDecryptionWithOffset) {
  GenerateKeyRequest(kSubsampleKeyId, arraysize(kSubsampleKeyId));
  AddKeyAndExpectToSucceed(kSubsampleKeyId, arraysize(kSubsampleKeyId),
                           kSubsampleKey, arraysize(kSubsampleKey));
  scoped_refptr<DecoderBuffer> encrypted_data = CreateSubsampleEncryptedBuffer(
      kPaddedSubsampleData, arraysize(kPaddedSubsampleData),
      kSubsampleKeyId, arraysize(kSubsampleKeyId),
      kSubsampleIv, arraysize(kSubsampleIv),
      arraysize(kPaddedSubsampleData) - arraysize(kSubsampleData),
      subsample_entries_);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToSucceed(
      encrypted_data, kSubsampleOriginalData, kSubsampleOriginalDataSize));
}

// No subsample or offset.
TEST_F(AesDecryptorTest, NormalDecryption) {
  GenerateKeyRequest(kSubsampleKeyId, arraysize(kSubsampleKeyId));
  AddKeyAndExpectToSucceed(kSubsampleKeyId, arraysize(kSubsampleKeyId),
                           kSubsampleKey, arraysize(kSubsampleKey));
  scoped_refptr<DecoderBuffer> encrypted_data = CreateSubsampleEncryptedBuffer(
      kNoSubsampleData, arraysize(kNoSubsampleData),
      kSubsampleKeyId, arraysize(kSubsampleKeyId),
      kSubsampleIv, arraysize(kSubsampleIv),
      0,
      std::vector<SubsampleEntry>());
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToSucceed(
      encrypted_data, kSubsampleOriginalData, kSubsampleOriginalDataSize));
}

TEST_F(AesDecryptorTest, IncorrectSubsampleSize) {
  GenerateKeyRequest(kSubsampleKeyId, arraysize(kSubsampleKeyId));
  AddKeyAndExpectToSucceed(kSubsampleKeyId, arraysize(kSubsampleKeyId),
                           kSubsampleKey, arraysize(kSubsampleKey));
  std::vector<SubsampleEntry> entries = subsample_entries_;
  entries[2].cypher_bytes += 1;

  scoped_refptr<DecoderBuffer> encrypted_data = CreateSubsampleEncryptedBuffer(
      kSubsampleData, arraysize(kSubsampleData),
      kSubsampleKeyId, arraysize(kSubsampleKeyId),
      kSubsampleIv, arraysize(kSubsampleIv),
      0,
      entries);
  ASSERT_NO_FATAL_FAILURE(DecryptAndExpectToFail(encrypted_data));
}

}  // namespace media
