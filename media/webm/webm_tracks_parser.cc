// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/webm/webm_tracks_parser.h"

#include "base/logging.h"
#include "base/string_util.h"
#include "media/base/buffers.h"
#include "media/webm/webm_constants.h"
#include "media/webm/webm_content_encodings.h"

namespace media {

// Values for TrackType element.
static const int kWebMTrackTypeVideo = 1;
static const int kWebMTrackTypeAudio = 2;

WebMTracksParser::WebMTracksParser()
    : track_type_(-1),
      track_num_(-1),
      audio_track_num_(-1),
      video_track_num_(-1) {
}

WebMTracksParser::~WebMTracksParser() {}

const std::string& WebMTracksParser::video_encryption_key_id() const {
  if (!video_content_encodings_client_.get())
    return EmptyString();

  DCHECK(!video_content_encodings_client_->content_encodings().empty());
  return video_content_encodings_client_->content_encodings()[0]->
      encryption_key_id();
}

int WebMTracksParser::Parse(const uint8* buf, int size) {
  track_type_ =-1;
  track_num_ = -1;
  audio_track_num_ = -1;
  video_track_num_ = -1;

  WebMListParser parser(kWebMIdTracks, this);
  int result = parser.Parse(buf, size);

  if (result <= 0)
    return result;

  // For now we do all or nothing parsing.
  return parser.IsParsingComplete() ? result : 0;
}


WebMParserClient* WebMTracksParser::OnListStart(int id) {
  if (id == kWebMIdContentEncodings) {
    DCHECK(!track_content_encodings_client_.get());
    track_content_encodings_client_.reset(new WebMContentEncodingsClient);
    return track_content_encodings_client_->OnListStart(id);
  }

  if (id == kWebMIdTrackEntry) {
    track_type_ = -1;
    track_num_ = -1;
    return this;
  }

  return this;
}

bool WebMTracksParser::OnListEnd(int id) {
  if (id == kWebMIdContentEncodings) {
    DCHECK(track_content_encodings_client_.get());
    return track_content_encodings_client_->OnListEnd(id);
  }

  if (id == kWebMIdTrackEntry) {
    if (track_type_ == -1 || track_num_ == -1) {
      DVLOG(1) << "Missing TrackEntry data"
               << " TrackType " << track_type_
               << " TrackNum " << track_num_;
      return false;
    }

    if (track_type_ == kWebMTrackTypeVideo) {
      video_track_num_ = track_num_;
      if (track_content_encodings_client_.get()) {
        video_content_encodings_client_ =
            track_content_encodings_client_.Pass();
      }
    } else if (track_type_ == kWebMTrackTypeAudio) {
      audio_track_num_ = track_num_;
      if (track_content_encodings_client_.get()) {
        audio_content_encodings_client_ =
            track_content_encodings_client_.Pass();
      }
    } else {
      DVLOG(1) << "Unexpected TrackType " << track_type_;
      return false;
    }

    track_type_ = -1;
    track_num_ = -1;
    track_content_encodings_client_.reset();
    return true;
  }

  return true;
}

bool WebMTracksParser::OnUInt(int id, int64 val) {
  int64* dst = NULL;

  switch (id) {
    case kWebMIdTrackNumber:
      dst = &track_num_;
      break;
    case kWebMIdTrackType:
      dst = &track_type_;
      break;
    default:
      return true;
  }

  if (*dst != -1) {
    DVLOG(1) << "Multiple values for id " << std::hex << id << " specified";
    return false;
  }

  *dst = val;
  return true;
}

bool WebMTracksParser::OnFloat(int id, double val) {
  return true;
}

bool WebMTracksParser::OnBinary(int id, const uint8* data, int size) {
  return true;
}

bool WebMTracksParser::OnString(int id, const std::string& str) {
  if (id == kWebMIdCodecID && str != "A_VORBIS" && str != "V_VP8") {
    DVLOG(1) << "Unexpected CodecID " << str;
    return false;
  }

  return true;
}

}  // namespace media
