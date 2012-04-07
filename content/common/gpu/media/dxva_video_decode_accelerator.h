// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_MEDIA_DXVA_VIDEO_DECODE_ACCELERATOR_H_
#define CONTENT_COMMON_GPU_MEDIA_DXVA_VIDEO_DECODE_ACCELERATOR_H_

#include <d3d9.h>
#include <dxva2api.h>
#include <list>
#include <map>
#include <mfidl.h>
#include <vector>

#include "base/compiler_specific.h"
#include "base/threading/non_thread_safe.h"
#include "base/win/scoped_comptr.h"
#include "content/common/content_export.h"
#include "media/video/video_decode_accelerator.h"

interface IMFSample;
interface IDirect3DSurface9;

// Class to provide a DXVA 2.0 based accelerator using the Microsoft Media
// foundation APIs via the VideoDecodeAccelerator interface.
// This class lives on a single thread and DCHECKs that it is never accessed
// from any other.
class CONTENT_EXPORT DXVAVideoDecodeAccelerator
    : public media::VideoDecodeAccelerator,
      NON_EXPORTED_BASE(public base::NonThreadSafe) {
 public:
  enum State {
    kUninitialized,   // un-initialized.
    kNormal,          // normal playing state.
    kResetting,       // upon received Reset(), before ResetDone()
    kEosDrain,        // upon input EOS received.
    kStopped,         // upon output EOS received.
  };

  // Does not take ownership of |client| which must outlive |*this|.
  DXVAVideoDecodeAccelerator(
      media::VideoDecodeAccelerator::Client* client,
      base::ProcessHandle renderer_process);
  virtual ~DXVAVideoDecodeAccelerator();

  // media::VideoDecodeAccelerator implementation.
  virtual bool Initialize(Profile) OVERRIDE;
  virtual void Decode(const media::BitstreamBuffer& bitstream_buffer) OVERRIDE;
  virtual void AssignPictureBuffers(
      const std::vector<media::PictureBuffer>& buffers) OVERRIDE;
  virtual void ReusePictureBuffer(int32 picture_buffer_id) OVERRIDE;
  virtual void Flush() OVERRIDE;
  virtual void Reset() OVERRIDE;
  virtual void Destroy() OVERRIDE;

  // Initialization work needed before the process is sandboxed.
  // This includes:-
  // 1. Loads the dlls like mf/mfplat/d3d9, etc required for decoding.
  // 2. Setting up the device manager instance which is shared between all
  //    decoder instances.
  static void PreSandboxInitialization();

 private:
  // Creates and initializes an instance of the D3D device and the
  // corresponding device manager. The device manager instance is eventually
  // passed to the IMFTransform interface implemented by the h.264 decoder.
  static bool CreateD3DDevManager();

  // Creates, initializes and sets the media types for the h.264 decoder.
  bool InitDecoder();

  // Validates whether the h.264 decoder supports hardware video acceleration.
  bool CheckDecoderDxvaSupport();

  // Returns information about the input and output streams. This includes
  // alignment information, decoder support flags, minimum sample size, etc.
  bool GetStreamsInfoAndBufferReqs();

  // Registers the input and output media types on the h.264 decoder. This
  // includes the expected input and output formats.
  bool SetDecoderMediaTypes();

  // Registers the input media type for the h.264 decoder.
  bool SetDecoderInputMediaType();

  // Registers the output media type for the h.264 decoder.
  bool SetDecoderOutputMediaType(const GUID& subtype);

  // Passes a command message to the decoder. This includes commands like
  // start of stream, end of stream, flush, drain the decoder, etc.
  bool SendMFTMessage(MFT_MESSAGE_TYPE msg, int32 param);

  // The bulk of the decoding happens here. This function handles errors,
  // format changes and processes decoded output.
  void DoDecode();

  // Invoked when we have a valid decoded output sample. Retrieves the D3D
  // surface and maintains a copy of it which is passed eventually to the
  // client when we have a picture buffer to copy the surface contents to.
  bool ProcessOutputSample(IMFSample* sample);

  // Copies the output sample data to the picture buffer provided by the
  // client.
  bool CopyOutputSampleDataToPictureBuffer(IDirect3DSurface9* dest_surface,
                                           media::PictureBuffer picture_buffer,
                                           int32 input_buffer_id);

  // Processes pending output samples by copying them to available picture
  // slots.
  void ProcessPendingSamples();

  // Clears local state maintained by the decoder.
  void ClearState();

  // Helper function to notify the accelerator client about the error.
  void StopOnError(media::VideoDecodeAccelerator::Error error);

  // Transitions the decoder to the uninitialized state. The decoder will stop
  // accepting requests in this state.
  void Invalidate();

  // Helper function to read the bitmap from the D3D surface passed in.
  bool GetBitmapFromSurface(IDirect3DSurface9* surface,
                            scoped_array<char>* bits);

  // Notifies the client that the input buffer identifed by input_buffer_id has
  // been processed.
  void NotifyInputBufferRead(int input_buffer_id);

  // Notifies the client that initialize was completed.
  void NotifyInitializeDone();

  // Notifies the client that the decoder was flushed.
  void NotifyFlushDone();

  // Notifies the client that the decoder was reset.
  void NotifyResetDone();

  // Requests picture buffers from the client.
  void RequestPictureBuffers(int width, int height);

  // Notifies the client about the availability of a picture.
  void NotifyPictureReady(const media::Picture& picture);

  // To expose client callbacks from VideoDecodeAccelerator.
  media::VideoDecodeAccelerator::Client* client_;

  base::win::ScopedComPtr<IMFTransform> decoder_;

  // These interface pointers are initialized before the process is sandboxed.
  // They are not released when the GPU process exits. This is ok for now
  // because the GPU process does not exit normally on Windows. It is always
  // terminated. The device manager instance is shared among all decoder
  // instances. This is OK because there is internal locking performed by the
  // device manager.
  static IDirect3DDeviceManager9* device_manager_;
  static IDirect3DDevice9Ex* device_;

  // Current state of the decoder.
  State state_;

  MFT_INPUT_STREAM_INFO input_stream_info_;
  MFT_OUTPUT_STREAM_INFO output_stream_info_;

  // Contains information about a decoded sample.
  struct PendingSampleInfo {
    PendingSampleInfo(int32 buffer_id, IDirect3DSurface9* surface);
    ~PendingSampleInfo();

    int32 input_buffer_id;
    base::win::ScopedComPtr<IDirect3DSurface9> dest_surface;
  };

  typedef std::list<PendingSampleInfo> PendingOutputSamples;

  // List of decoded output samples.
  PendingOutputSamples pending_output_samples_;

  // Maintains information about a DXVA picture buffer, i.e. whether it is
  // available for rendering, the texture information, etc.
  struct DXVAPictureBuffer {
    explicit DXVAPictureBuffer(const media::PictureBuffer& buffer);

    bool available;
    media::PictureBuffer picture_buffer;
  };

  // This map maintains the picture buffers passed the client for decoding.
  // The key is the picture buffer id.
  typedef std::map<int32, DXVAPictureBuffer> OutputBuffers;
  OutputBuffers output_picture_buffers_;

  // Set to true if we requested picture slots from the client.
  bool pictures_requested_;

  // Contains the id of the last input buffer received from the client.
  int32 last_input_buffer_id_;

  // Handle to the renderer process.
  base::ProcessHandle renderer_process_;

  // Ideally the reset token would be a stack variable which is used while
  // creating the device manager. However it seems that the device manager
  // holds onto the token and attempts to access it if the underlying device
  // changes.
  // TODO(ananta): This needs to be verified.
  static uint32 dev_manager_reset_token_;

  // Counter which holds the number of input packets before a successful
  // decode.
  int inputs_before_decode_;

  // Set to true if all necessary initialization needed before the GPU process
  // is sandboxed is done.
  // This includes the following:
  // 1. All required decoder dlls were successfully loaded.
  // 2. The device manager initialization completed.
  static bool pre_sandbox_init_done_;
};

#endif  // CONTENT_COMMON_GPU_MEDIA_DXVA_VIDEO_DECODE_ACCELERATOR_H_

