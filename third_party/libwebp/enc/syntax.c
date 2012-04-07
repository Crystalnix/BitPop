// Copyright 2011 Google Inc.
//
// This code is licensed under the same terms as WebM:
//  Software License Agreement:  http://www.webmproject.org/license/software/
//  Additional IP Rights Grant:  http://www.webmproject.org/license/additional/
// -----------------------------------------------------------------------------
//
// Header syntax writing
//
// Author: Skal (pascal.massimino@gmail.com)

#include <assert.h>
#include <math.h>

#include "vp8enci.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define KSIGNATURE 0x9d012a
#define KHEADER_SIZE 10
#define KRIFF_SIZE 20
#define KSIZE_OFFSET (KRIFF_SIZE - 8)

#define MAX_PARTITION0_SIZE (1 << 19)   // max size of mode partition
#define MAX_PARTITION_SIZE  (1 << 24)   // max size for token partition

//------------------------------------------------------------------------------
// Writers for header's various pieces (in order of appearance)

// Main keyframe header

static void PutLE32(uint8_t* const data, uint32_t val) {
  data[0] = (val >>  0) & 0xff;
  data[1] = (val >>  8) & 0xff;
  data[2] = (val >> 16) & 0xff;
  data[3] = (val >> 24) & 0xff;
}

static int PutHeader(int profile, size_t size0, size_t total_size,
                     WebPPicture* const pic) {
  uint8_t buf[KHEADER_SIZE];
  uint8_t RIFF[KRIFF_SIZE] = {
    'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', ' '
  };
  uint32_t bits;

  if (size0 >= MAX_PARTITION0_SIZE) {  // partition #0 is too big to fit
    return WebPEncodingSetError(pic, VP8_ENC_ERROR_PARTITION0_OVERFLOW);
  }

  if (total_size > 0xfffffffeU - KRIFF_SIZE) {
    return WebPEncodingSetError(pic, VP8_ENC_ERROR_FILE_TOO_BIG);
  }

  PutLE32(RIFF + 4, (uint32_t)(total_size + KSIZE_OFFSET));
  PutLE32(RIFF + 16, (uint32_t)total_size);
  if (!pic->writer(RIFF, sizeof(RIFF), pic)) {
    return WebPEncodingSetError(pic, VP8_ENC_ERROR_BAD_WRITE);
  }

  bits = 0                         // keyframe (1b)
       | (profile << 1)            // profile (3b)
       | (1 << 4)                  // visible (1b)
       | ((uint32_t)size0 << 5);   // partition length (19b)
  buf[0] = bits & 0xff;
  buf[1] = (bits >> 8) & 0xff;
  buf[2] = (bits >> 16) & 0xff;
  // signature
  buf[3] = (KSIGNATURE >> 16) & 0xff;
  buf[4] = (KSIGNATURE >> 8) & 0xff;
  buf[5] = (KSIGNATURE >> 0) & 0xff;
  // dimensions
  buf[6] = pic->width & 0xff;
  buf[7] = pic->width >> 8;
  buf[8] = pic->height & 0xff;
  buf[9] = pic->height >> 8;

  return pic->writer(buf, sizeof(buf), pic);
}

// Segmentation header
static void PutSegmentHeader(VP8BitWriter* const bw,
                             const VP8Encoder* const enc) {
  const VP8SegmentHeader* const hdr = &enc->segment_hdr_;
  const VP8Proba* const proba = &enc->proba_;
  if (VP8PutBitUniform(bw, (hdr->num_segments_ > 1))) {
    // We always 'update' the quant and filter strength values
    const int update_data = 1;
    int s;
    VP8PutBitUniform(bw, hdr->update_map_);
    if (VP8PutBitUniform(bw, update_data)) {
      // we always use absolute values, not relative ones
      VP8PutBitUniform(bw, 1);   // (segment_feature_mode = 1. Paragraph 9.3.)
      for (s = 0; s < NUM_MB_SEGMENTS; ++s) {
        VP8PutSignedValue(bw, enc->dqm_[s].quant_, 7);
      }
      for (s = 0; s < NUM_MB_SEGMENTS; ++s) {
        VP8PutSignedValue(bw, enc->dqm_[s].fstrength_, 6);
      }
    }
    if (hdr->update_map_) {
      for (s = 0; s < 3; ++s) {
        if (VP8PutBitUniform(bw, (proba->segments_[s] != 255u))) {
          VP8PutValue(bw, proba->segments_[s], 8);
        }
      }
    }
  }
}

// Filtering parameters header
static void PutFilterHeader(VP8BitWriter* const bw,
                            const VP8FilterHeader* const hdr) {
  const int use_lf_delta = (hdr->i4x4_lf_delta_ != 0);
  VP8PutBitUniform(bw, hdr->simple_);
  VP8PutValue(bw, hdr->level_, 6);
  VP8PutValue(bw, hdr->sharpness_, 3);
  if (VP8PutBitUniform(bw, use_lf_delta)) {
    // '0' is the default value for i4x4_lf_delta_ at frame #0.
    const int need_update = (hdr->i4x4_lf_delta_ != 0);
    if (VP8PutBitUniform(bw, need_update)) {
      // we don't use ref_lf_delta => emit four 0 bits
      VP8PutValue(bw, 0, 4);
      // we use mode_lf_delta for i4x4
      VP8PutSignedValue(bw, hdr->i4x4_lf_delta_, 6);
      VP8PutValue(bw, 0, 3);    // all others unused
    }
  }
}

// Nominal quantization parameters
static void PutQuant(VP8BitWriter* const bw,
                     const VP8Encoder* const enc) {
  VP8PutValue(bw, enc->base_quant_, 7);
  VP8PutSignedValue(bw, enc->dq_y1_dc_, 4);
  VP8PutSignedValue(bw, enc->dq_y2_dc_, 4);
  VP8PutSignedValue(bw, enc->dq_y2_ac_, 4);
  VP8PutSignedValue(bw, enc->dq_uv_dc_, 4);
  VP8PutSignedValue(bw, enc->dq_uv_ac_, 4);
}

// Partition sizes
static int EmitPartitionsSize(const VP8Encoder* const enc,
                              WebPPicture* const pic) {
  uint8_t buf[3 * (MAX_NUM_PARTITIONS - 1)];
  int p;
  for (p = 0; p < enc->num_parts_ - 1; ++p) {
    const size_t part_size = VP8BitWriterSize(enc->parts_ + p);
    if (part_size >= MAX_PARTITION_SIZE) {
      return WebPEncodingSetError(pic, VP8_ENC_ERROR_PARTITION_OVERFLOW);
    }
    buf[3 * p + 0] = (part_size >>  0) & 0xff;
    buf[3 * p + 1] = (part_size >>  8) & 0xff;
    buf[3 * p + 2] = (part_size >> 16) & 0xff;
  }
  return p ? pic->writer(buf, 3 * p, pic) : 1;
}

//------------------------------------------------------------------------------

#ifdef WEBP_EXPERIMENTAL_FEATURES

#define KTRAILER_SIZE 8

static void PutLE24(uint8_t* buf, size_t value) {
  buf[0] = (value >>  0) & 0xff;
  buf[1] = (value >>  8) & 0xff;
  buf[2] = (value >> 16) & 0xff;
}

static int WriteExtensions(VP8Encoder* const enc) {
  uint8_t buffer[KTRAILER_SIZE];
  VP8BitWriter* const bw = &enc->bw_;
  WebPPicture* const pic = enc->pic_;

  // Layer (bytes 0..3)
  PutLE24(buffer + 0, enc->layer_data_size_);
  buffer[3] = enc->pic_->colorspace & WEBP_CSP_UV_MASK;
  if (enc->layer_data_size_ > 0) {
    assert(enc->use_layer_);
    // append layer data to last partition
    if (!VP8BitWriterAppend(&enc->parts_[enc->num_parts_ - 1],
                            enc->layer_data_, enc->layer_data_size_)) {
      return WebPEncodingSetError(pic, VP8_ENC_ERROR_BITSTREAM_OUT_OF_MEMORY);
    }
  }
  // Alpha (bytes 4..6)
  PutLE24(buffer + 4, enc->alpha_data_size_);
  if (enc->alpha_data_size_ > 0) {
    assert(enc->has_alpha_);
    if (!VP8BitWriterAppend(bw, enc->alpha_data_, enc->alpha_data_size_)) {
      return WebPEncodingSetError(pic, VP8_ENC_ERROR_BITSTREAM_OUT_OF_MEMORY);
    }
  }

  buffer[KTRAILER_SIZE - 1] = 0x01;  // marker
  if (!VP8BitWriterAppend(bw, buffer, KTRAILER_SIZE)) {
    return WebPEncodingSetError(pic, VP8_ENC_ERROR_BITSTREAM_OUT_OF_MEMORY);
  }
  return 1;
}

#endif    /* WEBP_EXPERIMENTAL_FEATURES */

//------------------------------------------------------------------------------

static size_t GeneratePartition0(VP8Encoder* const enc) {
  VP8BitWriter* const bw = &enc->bw_;
  const int mb_size = enc->mb_w_ * enc->mb_h_;
  uint64_t pos1, pos2, pos3;
#ifdef WEBP_EXPERIMENTAL_FEATURES
  const int need_extensions = enc->has_alpha_ || enc->use_layer_;
#endif

  pos1 = VP8BitWriterPos(bw);
  VP8BitWriterInit(bw, mb_size * 7 / 8);        // ~7 bits per macroblock
#ifdef WEBP_EXPERIMENTAL_FEATURES
  VP8PutBitUniform(bw, need_extensions);   // extensions
#else
  VP8PutBitUniform(bw, 0);   // colorspace
#endif
  VP8PutBitUniform(bw, 0);   // clamp type

  PutSegmentHeader(bw, enc);
  PutFilterHeader(bw, &enc->filter_hdr_);
  VP8PutValue(bw, enc->config_->partitions, 2);
  PutQuant(bw, enc);
  VP8PutBitUniform(bw, 0);   // no proba update
  VP8WriteProbas(bw, &enc->proba_);
  pos2 = VP8BitWriterPos(bw);
  VP8CodeIntraModes(enc);
  VP8BitWriterFinish(bw);

#ifdef WEBP_EXPERIMENTAL_FEATURES
  if (need_extensions && !WriteExtensions(enc)) {
    return 0;
  }
#endif

  pos3 = VP8BitWriterPos(bw);

  if (enc->pic_->stats) {
    enc->pic_->stats->header_bytes[0] = (int)((pos2 - pos1 + 7) >> 3);
    enc->pic_->stats->header_bytes[1] = (int)((pos3 - pos2 + 7) >> 3);
    enc->pic_->stats->alpha_data_size = (int)enc->alpha_data_size_;
    enc->pic_->stats->layer_data_size = (int)enc->layer_data_size_;
  }
  return !bw->error_;
}

int VP8EncWrite(VP8Encoder* const enc) {
  WebPPicture* const pic = enc->pic_;
  VP8BitWriter* const bw = &enc->bw_;
  int ok = 0;
  size_t coded_size, pad;
  int p;

  // Partition #0 with header and partition sizes
  ok = !!GeneratePartition0(enc);

  // Compute total size (for the RIFF header)
  coded_size = KHEADER_SIZE + VP8BitWriterSize(bw) + 3 * (enc->num_parts_ - 1);
  for (p = 0; p < enc->num_parts_; ++p) {
    coded_size += VP8BitWriterSize(enc->parts_ + p);
  }
  pad = coded_size & 1;
  coded_size += pad;

  // Emit headers and partition #0
  {
    const uint8_t* const part0 = VP8BitWriterBuf(bw);
    const size_t size0 = VP8BitWriterSize(bw);
    ok = ok && PutHeader(enc->profile_, size0, coded_size, pic)
            && pic->writer(part0, size0, pic)
            && EmitPartitionsSize(enc, pic);
    free((void*)part0);
  }

  // Token partitions
  for (p = 0; p < enc->num_parts_; ++p) {
    const uint8_t* const buf = VP8BitWriterBuf(enc->parts_ + p);
    const size_t size = VP8BitWriterSize(enc->parts_ + p);
    if (size)
      ok = ok && pic->writer(buf, size, pic);
    free((void*)buf);
  }

  // Padding byte
  if (ok && pad) {
    const uint8_t pad_byte[1] = { 0 };
    ok = pic->writer(pad_byte, 1, pic);
  }

  enc->coded_size_ = (int)coded_size + KRIFF_SIZE;
  return ok;
}

//------------------------------------------------------------------------------

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif
