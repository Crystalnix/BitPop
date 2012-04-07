// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FILTERS_GPU_VIDEO_DECODER_H_
#define MEDIA_FILTERS_GPU_VIDEO_DECODER_H_

#include <deque>
#include <list>
#include <map>

#include "media/base/filters.h"
#include "media/base/pipeline_status.h"
#include "media/video/video_decode_accelerator.h"
#include "ui/gfx/size.h"

class MessageLoop;
template <class T> class scoped_refptr;
namespace base {
class MessageLoopProxy;
class SharedMemory;
}

namespace media {

// GPU-accelerated video decoder implementation.  Relies on
// AcceleratedVideoDecoderMsg_Decode and friends.
// All methods internally trampoline to the |message_loop| passed to the ctor.
class MEDIA_EXPORT GpuVideoDecoder
    : public VideoDecoder,
      public VideoDecodeAccelerator::Client {
 public:
  // Helper interface for specifying factories needed to instantiate a
  // GpuVideoDecoder.
  class MEDIA_EXPORT Factories : public base::RefCountedThreadSafe<Factories> {
   public:
    // Caller owns returned pointer.
    virtual VideoDecodeAccelerator* CreateVideoDecodeAccelerator(
        VideoDecodeAccelerator::Profile, VideoDecodeAccelerator::Client*) = 0;

    // Allocate & delete native textures.
    virtual bool CreateTextures(int32 count, const gfx::Size& size,
                                std::vector<uint32>* texture_ids) = 0;
    virtual void DeleteTexture(uint32 texture_id) = 0;

    // Allocate & return a shared memory segment.  Caller is responsible for
    // Close()ing the returned pointer.
    virtual base::SharedMemory* CreateSharedMemory(size_t size) = 0;

   protected:
    friend class base::RefCountedThreadSafe<Factories>;
    virtual ~Factories();
  };

  GpuVideoDecoder(MessageLoop* message_loop,
                  const scoped_refptr<Factories>& factories);
  virtual ~GpuVideoDecoder();

  // Filter implementation.
  virtual void Stop(const base::Closure& callback) OVERRIDE;
  virtual void Seek(base::TimeDelta time, const FilterStatusCB& cb) OVERRIDE;
  virtual void Pause(const base::Closure& callback) OVERRIDE;
  virtual void Flush(const base::Closure& callback) OVERRIDE;

  // VideoDecoder implementation.
  virtual void Initialize(DemuxerStream* demuxer_stream,
                          const PipelineStatusCB& callback,
                          const StatisticsCallback& stats_callback) OVERRIDE;
  virtual void Read(const ReadCB& callback) OVERRIDE;
  virtual const gfx::Size& natural_size() OVERRIDE;
  virtual bool HasAlpha() const OVERRIDE;
  virtual void PrepareForShutdownHack() OVERRIDE;

  // VideoDecodeAccelerator::Client implementation.
  virtual void NotifyInitializeDone() OVERRIDE;
  virtual void ProvidePictureBuffers(uint32 count,
                                     const gfx::Size& size) OVERRIDE;
  virtual void DismissPictureBuffer(int32 id) OVERRIDE;
  virtual void PictureReady(const media::Picture& picture) OVERRIDE;
  virtual void NotifyEndOfBitstreamBuffer(int32 id) OVERRIDE;
  virtual void NotifyFlushDone() OVERRIDE;
  virtual void NotifyResetDone() OVERRIDE;
  virtual void NotifyError(media::VideoDecodeAccelerator::Error error) OVERRIDE;

 private:
  enum State {
    kNormal,
    // Avoid the use of "flush" in these enums because the term is overloaded:
    // Filter::Flush() means drop pending data on the floor, but
    // VideoDecodeAccelerator::Flush() means drain pending data (Filter::Flush()
    // actually corresponds to VideoDecodeAccelerator::Reset(), confusingly
    // enough).
    kDrainingDecoder,
    kDecoderDrained,
  };

  // If no demuxer read is in flight and no bitstream buffers are in the
  // decoder, kick some off demuxing/decoding.
  void EnsureDemuxOrDecode();

  // Callback to pass to demuxer_stream_->Read() for receiving encoded bits.
  void RequestBufferDecode(const scoped_refptr<Buffer>& buffer);

  // Enqueue a frame for later delivery (or drop it on the floor if a
  // vda->Reset() is in progress) and trigger out-of-line delivery of the oldest
  // ready frame to the client if there is a pending read.  A NULL |frame|
  // merely triggers delivery, and requires the ready_video_frames_ queue not be
  // empty.
  void EnqueueFrameAndTriggerFrameDelivery(
      const scoped_refptr<VideoFrame>& frame);

  // Indicate the picturebuffer can be reused by the decoder.
  void ReusePictureBuffer(int64 picture_buffer_id);

  void RecordBufferTimeData(
      const BitstreamBuffer& bitstream_buffer, const Buffer& buffer);
  void GetBufferTimeData(
      int32 id, base::TimeDelta* timestamp, base::TimeDelta* duration);

  // A shared memory segment and its allocated size.
  struct SHMBuffer {
    SHMBuffer(base::SharedMemory* m, size_t s);
    ~SHMBuffer();
    base::SharedMemory* shm;
    size_t size;
  };

  // Request a shared-memory segment of at least |min_size| bytes.  Will
  // allocate as necessary.  Caller does not own returned pointer.
  SHMBuffer* GetSHM(size_t min_size);

  // Return a shared-memory segment to the available pool.
  void PutSHM(SHMBuffer* shm_buffer);

  StatisticsCallback statistics_callback_;

  // TODO(scherkus): I think this should be calculated by VideoRenderers based
  // on information provided by VideoDecoders (i.e., aspect ratio).
  gfx::Size natural_size_;

  // Frame duration specified in the video stream's configuration, or 0 if not
  // present.
  base::TimeDelta config_frame_duration_;

  // Pointer to the demuxer stream that will feed us compressed buffers.
  scoped_refptr<DemuxerStream> demuxer_stream_;

  // MessageLoop on which to fire callbacks and trampoline calls to this class
  // if they arrive on other loops.
  scoped_refptr<base::MessageLoopProxy> gvd_loop_proxy_;

  // Creation message loop (typically the render thread).  All calls to vda_
  // must be made on this loop (and beware this loop is paused during the
  // Pause/Flush/Stop dance PipelineImpl::Stop() goes through).
  scoped_refptr<base::MessageLoopProxy> render_loop_proxy_;

  scoped_refptr<Factories> factories_;

  // Populated during Initialize() (on success) and unchanged thereafter.
  scoped_refptr<VideoDecodeAccelerator> vda_;

  // Callbacks that are !is_null() only during their respective operation being
  // asynchronously executed.
  ReadCB pending_read_cb_;
  base::Closure pending_reset_cb_;

  State state_;

  // Is a demuxer read in flight?
  bool demuxer_read_in_progress_;

  // Shared-memory buffer pool.  Since allocating SHM segments requires a
  // round-trip to the browser process, we keep allocation out of the
  // steady-state of the decoder.
  std::vector<SHMBuffer*> available_shm_segments_;

  // Book-keeping variables.
  struct BufferPair {
    BufferPair(SHMBuffer* s, const scoped_refptr<Buffer>& b);
    ~BufferPair();
    SHMBuffer* shm_buffer;
    scoped_refptr<Buffer> buffer;
  };
  std::map<int32, BufferPair> bitstream_buffers_in_decoder_;
  std::map<int32, PictureBuffer> picture_buffers_in_decoder_;

  struct BufferTimeData {
    BufferTimeData(int32 bbid, base::TimeDelta ts, base::TimeDelta dur);
    ~BufferTimeData();
    int32 bitstream_buffer_id;
    base::TimeDelta timestamp;
    base::TimeDelta duration;
  };
  std::list<BufferTimeData> input_buffer_time_data_;

  // picture_buffer_id and the frame wrapping the corresponding Picture, for
  // frames that have been decoded but haven't been requested by a Read() yet.
  std::list<scoped_refptr<VideoFrame> > ready_video_frames_;
  int64 next_picture_buffer_id_;
  int64 next_bitstream_buffer_id_;

  // Indicates PrepareForShutdownHack()'s been called.  Makes further calls to
  // this class not require the render thread's loop to be processing.
  bool shutting_down_;

  DISALLOW_COPY_AND_ASSIGN(GpuVideoDecoder);
};

}  // namespace media

#endif  // MEDIA_FILTERS_GPU_VIDEO_DECODER_H_
