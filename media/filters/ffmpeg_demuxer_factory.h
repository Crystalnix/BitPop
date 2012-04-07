// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implements the DemuxerFactory interface using FFmpegDemuxer.

#ifndef MEDIA_FILTERS_FFMPEG_DEMUXER_FACTORY_H_
#define MEDIA_FILTERS_FFMPEG_DEMUXER_FACTORY_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "media/base/demuxer_factory.h"

class MessageLoop;

namespace media {

class MEDIA_EXPORT FFmpegDemuxerFactory : public DemuxerFactory {
 public:
  FFmpegDemuxerFactory(const scoped_refptr<DataSource>& data_source,
                       MessageLoop* loop);
  virtual ~FFmpegDemuxerFactory();

  // DemuxerFactory methods.
  virtual void Build(const std::string& url, const BuildCallback& cb) OVERRIDE;

 private:
  scoped_refptr<DataSource> data_source_;
  MessageLoop* loop_;  // Unowned.

  DISALLOW_IMPLICIT_CONSTRUCTORS(FFmpegDemuxerFactory);
};

}  // namespace media

#endif  // MEDIA_FILTERS_FFMPEG_DEMUXER_FACTORY_H_
