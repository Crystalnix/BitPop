// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/media/dxva_video_decode_accelerator.h"

#if !defined(OS_WIN)
#error This file should only be built on Windows.
#endif   // !defined(OS_WIN)

#include <ks.h>
#include <codecapi.h>
#include <d3dx9tex.h>
#include <mfapi.h>
#include <mferror.h>
#include <wmcodecdsp.h>

#include "base/bind.h"
#include "base/callback.h"
#include "base/debug/trace_event.h"
#include "base/logging.h"
#include "base/memory/scoped_handle.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/process_util.h"
#include "base/shared_memory.h"
#include "media/video/video_decode_accelerator.h"
#include "ui/gfx/gl/gl_bindings.h"

// We only request 5 picture buffers from the client which are used to hold the
// decoded samples. These buffers are then reused when the client tells us that
// it is done with the buffer.
static const int kNumPictureBuffers = 5;

bool DXVAVideoDecodeAccelerator::pre_sandbox_init_done_ = false;
uint32 DXVAVideoDecodeAccelerator::dev_manager_reset_token_ = 0;
IDirect3DDeviceManager9* DXVAVideoDecodeAccelerator::device_manager_ = NULL;
IDirect3DDevice9Ex* DXVAVideoDecodeAccelerator::device_ = NULL;

#define RETURN_ON_FAILURE(result, log, ret)  \
  do {                                       \
    if (!(result)) {                         \
      DLOG(ERROR) << log;                    \
      return ret;                            \
    }                                        \
  } while (0)

#define RETURN_ON_HR_FAILURE(result, log, ret)                    \
  RETURN_ON_FAILURE(SUCCEEDED(result),                            \
                    log << ", HRESULT: 0x" << std::hex << result, \
                    ret);

#define RETURN_AND_NOTIFY_ON_FAILURE(result, log, error_code, ret)  \
  do {                                                              \
    if (!(result)) {                                                \
      DVLOG(1) << log;                                              \
      StopOnError(error_code);                                      \
      return ret;                                                   \
    }                                                               \
  } while (0)

#define RETURN_AND_NOTIFY_ON_HR_FAILURE(result, log, error_code, ret)  \
  RETURN_AND_NOTIFY_ON_FAILURE(SUCCEEDED(result),                      \
                               log << ", HRESULT: 0x" << std::hex << result, \
                               error_code, ret);

static IMFSample* CreateEmptySample() {
  base::win::ScopedComPtr<IMFSample> sample;
  HRESULT hr = MFCreateSample(sample.Receive());
  RETURN_ON_HR_FAILURE(hr, "MFCreateSample failed", NULL);
  return sample.Detach();
}

// Creates a Media Foundation sample with one buffer of length |buffer_length|
// on a |align|-byte boundary. Alignment must be a perfect power of 2 or 0.
static IMFSample* CreateEmptySampleWithBuffer(int buffer_length, int align) {
  CHECK_GT(buffer_length, 0);

  base::win::ScopedComPtr<IMFSample> sample;
  sample.Attach(CreateEmptySample());

  base::win::ScopedComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = E_FAIL;
  if (align == 0) {
    // Note that MFCreateMemoryBuffer is same as MFCreateAlignedMemoryBuffer
    // with the align argument being 0.
    hr = MFCreateMemoryBuffer(buffer_length, buffer.Receive());
  } else {
    hr = MFCreateAlignedMemoryBuffer(buffer_length,
                                     align - 1,
                                     buffer.Receive());
  }
  RETURN_ON_HR_FAILURE(hr, "Failed to create memory buffer for sample", NULL);

  hr = sample->AddBuffer(buffer);
  RETURN_ON_HR_FAILURE(hr, "Failed to add buffer to sample", NULL);

  return sample.Detach();
}

// Creates a Media Foundation sample with one buffer containing a copy of the
// given Annex B stream data.
// If duration and sample time are not known, provide 0.
// |min_size| specifies the minimum size of the buffer (might be required by
// the decoder for input). If no alignment is required, provide 0.
static IMFSample* CreateInputSample(const uint8* stream, int size,
                                    int min_size, int alignment) {
  CHECK(stream);
  CHECK_GT(size, 0);
  base::win::ScopedComPtr<IMFSample> sample;
  sample.Attach(CreateEmptySampleWithBuffer(std::max(min_size, size),
                                            alignment));
  RETURN_ON_FAILURE(sample, "Failed to create empty sample", NULL);

  base::win::ScopedComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = sample->GetBufferByIndex(0, buffer.Receive());
  RETURN_ON_HR_FAILURE(hr, "Failed to get buffer from sample", NULL);

  DWORD max_length = 0;
  DWORD current_length = 0;
  uint8* destination = NULL;
  hr = buffer->Lock(&destination, &max_length, &current_length);
  RETURN_ON_HR_FAILURE(hr, "Failed to lock buffer", NULL);

  CHECK_EQ(current_length, 0u);
  CHECK_GE(static_cast<int>(max_length), size);
  memcpy(destination, stream, size);

  hr = buffer->Unlock();
  RETURN_ON_HR_FAILURE(hr, "Failed to unlock buffer", NULL);

  hr = buffer->SetCurrentLength(size);
  RETURN_ON_HR_FAILURE(hr, "Failed to set buffer length", NULL);

  return sample.Detach();
}

static IMFSample* CreateSampleFromInputBuffer(
    const media::BitstreamBuffer& bitstream_buffer,
    base::ProcessHandle renderer_process,
    DWORD stream_size,
    DWORD alignment) {
  HANDLE shared_memory_handle = NULL;
  RETURN_ON_FAILURE(::DuplicateHandle(renderer_process,
                                      bitstream_buffer.handle(),
                                      base::GetCurrentProcessHandle(),
                                      &shared_memory_handle,
                                      0,
                                      FALSE,
                                      DUPLICATE_SAME_ACCESS),
                     "Duplicate handle failed", NULL);

  base::SharedMemory shm(shared_memory_handle, true);
  RETURN_ON_FAILURE(shm.Map(bitstream_buffer.size()),
                    "Failed in base::SharedMemory::Map", NULL);

  return CreateInputSample(reinterpret_cast<const uint8*>(shm.memory()),
                           bitstream_buffer.size(),
                           stream_size,
                           alignment);
}

DXVAVideoDecodeAccelerator::DXVAPictureBuffer::DXVAPictureBuffer(
    const media::PictureBuffer& buffer)
    : available(true),
      picture_buffer(buffer) {
}

DXVAVideoDecodeAccelerator::PendingSampleInfo::PendingSampleInfo(
    int32 buffer_id, IDirect3DSurface9* surface)
    : input_buffer_id(buffer_id),
      dest_surface(surface) {
}

DXVAVideoDecodeAccelerator::PendingSampleInfo::~PendingSampleInfo() {}

// static
void DXVAVideoDecodeAccelerator::PreSandboxInitialization() {
  // Should be called only once during program startup.
  DCHECK(!pre_sandbox_init_done_);

  static wchar_t* decoding_dlls[] = {
    L"d3d9.dll",
    L"d3dx9_43.dll",
    L"dxva2.dll",
    L"mf.dll",
    L"mfplat.dll",
    L"msmpeg2vdec.dll",
  };

  for (int i = 0; i < arraysize(decoding_dlls); ++i) {
    if (!::LoadLibrary(decoding_dlls[i])) {
      DLOG(ERROR) << "Failed to load decoder dll: " << decoding_dlls[i]
                  << ", Error: " << ::GetLastError();
      return;
    }
  }

  RETURN_ON_FAILURE(CreateD3DDevManager(),
                    "Failed to initialize D3D device and manager",);
  pre_sandbox_init_done_ = true;
}

// static
bool DXVAVideoDecodeAccelerator::CreateD3DDevManager() {
  base::win::ScopedComPtr<IDirect3D9Ex> d3d9;

  HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, d3d9.Receive());
  RETURN_ON_HR_FAILURE(hr, "Direct3DCreate9Ex failed", false);

  D3DPRESENT_PARAMETERS present_params = {0};
  present_params.BackBufferWidth = 1;
  present_params.BackBufferHeight = 1;
  present_params.BackBufferFormat = D3DFMT_UNKNOWN;
  present_params.BackBufferCount = 1;
  present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
  present_params.hDeviceWindow = ::GetShellWindow();
  present_params.Windowed = TRUE;
  present_params.Flags = D3DPRESENTFLAG_VIDEO;
  present_params.FullScreen_RefreshRateInHz = 0;
  present_params.PresentationInterval = 0;

  hr = d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT,
                            D3DDEVTYPE_HAL,
                            ::GetShellWindow(),
                            D3DCREATE_SOFTWARE_VERTEXPROCESSING |
                            D3DCREATE_MULTITHREADED |
                            D3DCREATE_FPU_PRESERVE,
                            &present_params,
                            NULL,
                            &device_);
  RETURN_ON_HR_FAILURE(hr, "Failed to create D3D device", false);

  hr = DXVA2CreateDirect3DDeviceManager9(&dev_manager_reset_token_,
                                         &device_manager_);
  RETURN_ON_HR_FAILURE(hr, "DXVA2CreateDirect3DDeviceManager9 failed", false);

  hr = device_manager_->ResetDevice(device_, dev_manager_reset_token_);
  RETURN_ON_HR_FAILURE(hr, "Failed to reset device", false);
  return true;
}

DXVAVideoDecodeAccelerator::DXVAVideoDecodeAccelerator(
    media::VideoDecodeAccelerator::Client* client,
    base::ProcessHandle renderer_process)
    : client_(client),
      state_(kUninitialized),
      pictures_requested_(false),
      renderer_process_(renderer_process),
      last_input_buffer_id_(-1),
      inputs_before_decode_(0) {
}

DXVAVideoDecodeAccelerator::~DXVAVideoDecodeAccelerator() {
  client_ = NULL;
}

bool DXVAVideoDecodeAccelerator::Initialize(Profile) {
  DCHECK(CalledOnValidThread());

  RETURN_AND_NOTIFY_ON_FAILURE(pre_sandbox_init_done_,
      "PreSandbox initialization not completed", PLATFORM_FAILURE, false);

  RETURN_AND_NOTIFY_ON_FAILURE((state_ == kUninitialized),
      "Initialize: invalid state: " << state_, ILLEGAL_STATE, false);

  HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  RETURN_AND_NOTIFY_ON_HR_FAILURE(hr, "MFStartup failed.", PLATFORM_FAILURE,
      false);

  RETURN_AND_NOTIFY_ON_FAILURE(InitDecoder(),
      "Failed to initialize decoder", PLATFORM_FAILURE, false);

  RETURN_AND_NOTIFY_ON_FAILURE(GetStreamsInfoAndBufferReqs(),
      "Failed to get input/output stream info.", PLATFORM_FAILURE, false);

  RETURN_AND_NOTIFY_ON_FAILURE(
      SendMFTMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0),
      "Failed to start decoder", PLATFORM_FAILURE, false);

  state_ = kNormal;
  MessageLoop::current()->PostTask(FROM_HERE,
      base::Bind(&DXVAVideoDecodeAccelerator::NotifyInitializeDone, this));
  return true;
}

void DXVAVideoDecodeAccelerator::Decode(
    const media::BitstreamBuffer& bitstream_buffer) {
  DCHECK(CalledOnValidThread());

  RETURN_AND_NOTIFY_ON_FAILURE((state_ == kNormal || state_ == kStopped),
      "Invalid state: " << state_, ILLEGAL_STATE,);

  base::win::ScopedComPtr<IMFSample> sample;
  sample.Attach(CreateSampleFromInputBuffer(bitstream_buffer,
                                            renderer_process_,
                                            input_stream_info_.cbSize,
                                            input_stream_info_.cbAlignment));
  RETURN_AND_NOTIFY_ON_FAILURE(sample, "Failed to create input sample",
                               PLATFORM_FAILURE,);
  if (!inputs_before_decode_) {
    TRACE_EVENT_BEGIN_ETW("DXVAVideoDecodeAccelerator.Decoding", this, "");
  }
  inputs_before_decode_++;

  RETURN_AND_NOTIFY_ON_FAILURE(
      SendMFTMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0),
      "Failed to create input sample", PLATFORM_FAILURE,);

  HRESULT hr = decoder_->ProcessInput(0, sample, 0);
  RETURN_AND_NOTIFY_ON_HR_FAILURE(hr, "Failed to process input sample",
      PLATFORM_FAILURE,);

  RETURN_AND_NOTIFY_ON_FAILURE(
    SendMFTMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0),
    "Failed to send eos message to MFT", PLATFORM_FAILURE,);
  state_ = kEosDrain;

  last_input_buffer_id_ = bitstream_buffer.id();

  DoDecode();

  RETURN_AND_NOTIFY_ON_FAILURE((state_ == kStopped || state_ == kNormal),
      "Failed to process output. Unexpected decoder state: " << state_,
      ILLEGAL_STATE,);

  // The Microsoft Media foundation decoder internally buffers up to 30 frames
  // before returning a decoded frame. We need to inform the client that this
  // input buffer is processed as it may stop sending us further input.
  // Note: This may break clients which expect every input buffer to be
  // associated with a decoded output buffer.
  // TODO(ananta)
  // Do some more investigation into whether it is possible to get the MFT
  // decoder to emit an output packet for every input packet.
  // http://code.google.com/p/chromium/issues/detail?id=108121
  MessageLoop::current()->PostTask(FROM_HERE, base::Bind(
      &DXVAVideoDecodeAccelerator::NotifyInputBufferRead, this,
      bitstream_buffer.id()));
}

void DXVAVideoDecodeAccelerator::AssignPictureBuffers(
    const std::vector<media::PictureBuffer>& buffers) {
  DCHECK(CalledOnValidThread());
  // Copy the picture buffers provided by the client to the available list,
  // and mark these buffers as available for use.
  for (size_t buffer_index = 0; buffer_index < buffers.size();
       ++buffer_index) {
    bool inserted = output_picture_buffers_.insert(std::make_pair(
        buffers[buffer_index].id(),
        DXVAPictureBuffer(buffers[buffer_index]))).second;
    DCHECK(inserted);
  }
  ProcessPendingSamples();
}

void DXVAVideoDecodeAccelerator::ReusePictureBuffer(
    int32 picture_buffer_id) {
  DCHECK(CalledOnValidThread());

  OutputBuffers::iterator it = output_picture_buffers_.find(picture_buffer_id);
  RETURN_AND_NOTIFY_ON_FAILURE(it != output_picture_buffers_.end(),
      "Invalid picture id: " << picture_buffer_id, INVALID_ARGUMENT,);

  it->second.available = true;
  ProcessPendingSamples();
}

void DXVAVideoDecodeAccelerator::Flush() {
  DCHECK(CalledOnValidThread());

  DVLOG(1) << "DXVAVideoDecodeAccelerator::Flush";

  RETURN_AND_NOTIFY_ON_FAILURE((state_ == kNormal || state_ == kStopped),
      "Unexpected decoder state: " << state_, ILLEGAL_STATE,);

  state_ = kEosDrain;

  RETURN_AND_NOTIFY_ON_FAILURE(SendMFTMessage(MFT_MESSAGE_COMMAND_DRAIN, 0),
      "Failed to send drain message", PLATFORM_FAILURE,);

  // As per MSDN docs after the client sends this message, it calls
  // IMFTransform::ProcessOutput in a loop, until ProcessOutput returns the
  // error code MF_E_TRANSFORM_NEED_MORE_INPUT. The DoDecode function sets
  // the state to kStopped when the decoder returns
  // MF_E_TRANSFORM_NEED_MORE_INPUT.
  // The MFT decoder can buffer upto 30 frames worth of input before returning
  // an output frame. This loop here attempts to retrieve as many output frames
  // as possible from the buffered set.
  while (state_ != kStopped) {
    DoDecode();
  }

  MessageLoop::current()->PostTask(FROM_HERE, base::Bind(
      &DXVAVideoDecodeAccelerator::NotifyFlushDone, this));

  state_ = kNormal;
}

void DXVAVideoDecodeAccelerator::Reset() {
  DCHECK(CalledOnValidThread());

  DVLOG(1) << "DXVAVideoDecodeAccelerator::Reset";

  RETURN_AND_NOTIFY_ON_FAILURE((state_ == kNormal || state_ == kStopped),
      "Reset: invalid state: " << state_, ILLEGAL_STATE,);

  state_ = kResetting;

  RETURN_AND_NOTIFY_ON_FAILURE(SendMFTMessage(MFT_MESSAGE_COMMAND_FLUSH, 0),
      "Reset: Failed to send message.", PLATFORM_FAILURE,);

  MessageLoop::current()->PostTask(FROM_HERE, base::Bind(
      &DXVAVideoDecodeAccelerator::NotifyResetDone, this));

  state_ = DXVAVideoDecodeAccelerator::kNormal;
}

void DXVAVideoDecodeAccelerator::Destroy() {
  DCHECK(CalledOnValidThread());
  Invalidate();
}

bool DXVAVideoDecodeAccelerator::InitDecoder() {
  // We cannot use CoCreateInstance to instantiate the decoder object as that
  // fails in the sandbox. We mimic the steps CoCreateInstance uses to
  // instantiate the object.
  HMODULE decoder_dll = ::GetModuleHandle(L"msmpeg2vdec.dll");
  RETURN_ON_FAILURE(decoder_dll,
                    "msmpeg2vdec.dll required for decoding is not loaded",
                    false);

  typedef HRESULT (WINAPI* GetClassObject)(const CLSID& clsid,
                                           const IID& iid,
                                           void** object);

  GetClassObject get_class_object = reinterpret_cast<GetClassObject>(
      GetProcAddress(decoder_dll, "DllGetClassObject"));
  RETURN_ON_FAILURE(get_class_object,
                    "Failed to get DllGetClassObject pointer", false);

  base::win::ScopedComPtr<IClassFactory> factory;
  HRESULT hr = get_class_object(__uuidof(CMSH264DecoderMFT),
                                __uuidof(IClassFactory),
                                reinterpret_cast<void**>(factory.Receive()));
  RETURN_ON_HR_FAILURE(hr, "DllGetClassObject for decoder failed", false);

  hr = factory->CreateInstance(NULL, __uuidof(IMFTransform),
                               reinterpret_cast<void**>(decoder_.Receive()));
  RETURN_ON_HR_FAILURE(hr, "Failed to create decoder instance", false);

  RETURN_ON_FAILURE(CheckDecoderDxvaSupport(),
                    "Failed to check decoder DXVA support", false);

  hr = decoder_->ProcessMessage(
            MFT_MESSAGE_SET_D3D_MANAGER,
            reinterpret_cast<ULONG_PTR>(device_manager_));
  RETURN_ON_HR_FAILURE(hr, "Failed to pass D3D manager to decoder", false);

  return SetDecoderMediaTypes();
}

bool DXVAVideoDecodeAccelerator::CheckDecoderDxvaSupport() {
  base::win::ScopedComPtr<IMFAttributes> attributes;
  HRESULT hr = decoder_->GetAttributes(attributes.Receive());
  RETURN_ON_HR_FAILURE(hr, "Failed to get decoder attributes", false);

  UINT32 dxva = 0;
  hr = attributes->GetUINT32(MF_SA_D3D_AWARE, &dxva);
  RETURN_ON_HR_FAILURE(hr, "Failed to check if decoder supports DXVA", false);

  hr = attributes->SetUINT32(CODECAPI_AVDecVideoAcceleration_H264, TRUE);
  RETURN_ON_HR_FAILURE(hr, "Failed to enable DXVA H/W decoding", false);
  return true;
}

bool DXVAVideoDecodeAccelerator::SetDecoderMediaTypes() {
  RETURN_ON_FAILURE(SetDecoderInputMediaType(),
                    "Failed to set decoder input media type", false);
  return SetDecoderOutputMediaType(MFVideoFormat_NV12);
}

bool DXVAVideoDecodeAccelerator::SetDecoderInputMediaType() {
  base::win::ScopedComPtr<IMFMediaType> media_type;
  HRESULT hr = MFCreateMediaType(media_type.Receive());
  RETURN_ON_HR_FAILURE(hr, "MFCreateMediaType failed", false);

  hr = media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  RETURN_ON_HR_FAILURE(hr, "Failed to set major input type", false);

  hr = media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  RETURN_ON_HR_FAILURE(hr, "Failed to set subtype", false);

  hr = decoder_->SetInputType(0, media_type, 0);  // No flags
  RETURN_ON_HR_FAILURE(hr, "Failed to set decoder input type", false);
  return true;
}

bool DXVAVideoDecodeAccelerator::SetDecoderOutputMediaType(
    const GUID& subtype) {
  base::win::ScopedComPtr<IMFMediaType> out_media_type;

  for (uint32 i = 0;
       SUCCEEDED(decoder_->GetOutputAvailableType(0, i,
                                                  out_media_type.Receive()));
       ++i) {
    GUID out_subtype = {0};
    HRESULT hr = out_media_type->GetGUID(MF_MT_SUBTYPE, &out_subtype);
    RETURN_ON_HR_FAILURE(hr, "Failed to get output major type", false);

    if (out_subtype == subtype) {
      hr = decoder_->SetOutputType(0, out_media_type, 0);  // No flags
      RETURN_ON_HR_FAILURE(hr, "Failed to set decoder output type", false);
      return true;
    }
    out_media_type.Release();
  }
  return false;
}

bool DXVAVideoDecodeAccelerator::SendMFTMessage(MFT_MESSAGE_TYPE msg,
                                                int32 param) {
  HRESULT hr = decoder_->ProcessMessage(msg, param);
  return SUCCEEDED(hr);
}

// Gets the minimum buffer sizes for input and output samples. The MFT will not
// allocate buffer for input nor output, so we have to do it ourselves and make
// sure they're the correct size. We only provide decoding if DXVA is enabled.
bool DXVAVideoDecodeAccelerator::GetStreamsInfoAndBufferReqs() {
  HRESULT hr = decoder_->GetInputStreamInfo(0, &input_stream_info_);
  RETURN_ON_HR_FAILURE(hr, "Failed to get input stream info", false);

  hr = decoder_->GetOutputStreamInfo(0, &output_stream_info_);
  RETURN_ON_HR_FAILURE(hr, "Failed to get decoder output stream info", false);

  DVLOG(1) << "Input stream info: ";
  DVLOG(1) << "Max latency: " << input_stream_info_.hnsMaxLatency;
  // There should be three flags, one for requiring a whole frame be in a
  // single sample, one for requiring there be one buffer only in a single
  // sample, and one that specifies a fixed sample size. (as in cbSize)
  CHECK_EQ(input_stream_info_.dwFlags, 0x7u);

  DVLOG(1) << "Min buffer size: " << input_stream_info_.cbSize;
  DVLOG(1) << "Max lookahead: " << input_stream_info_.cbMaxLookahead;
  DVLOG(1) << "Alignment: " << input_stream_info_.cbAlignment;

  DVLOG(1) << "Output stream info: ";
  // The flags here should be the same and mean the same thing, except when
  // DXVA is enabled, there is an extra 0x100 flag meaning decoder will
  // allocate its own sample.
  DVLOG(1) << "Flags: "
          << std::hex << std::showbase << output_stream_info_.dwFlags;
  CHECK_EQ(output_stream_info_.dwFlags, 0x107u);
  DVLOG(1) << "Min buffer size: " << output_stream_info_.cbSize;
  DVLOG(1) << "Alignment: " << output_stream_info_.cbAlignment;
  return true;
}

void DXVAVideoDecodeAccelerator::DoDecode() {
  // This function is also called from Flush in a loop which could result
  // in the state transitioning to kNormal due to decoded output.
  RETURN_AND_NOTIFY_ON_FAILURE((state_ == kNormal || state_ == kEosDrain),
      "DoDecode: not in normal/drain state", ILLEGAL_STATE,);

  MFT_OUTPUT_DATA_BUFFER output_data_buffer = {0};
  DWORD status = 0;

  HRESULT hr = decoder_->ProcessOutput(0,  // No flags
                                       1,  // # of out streams to pull from
                                       &output_data_buffer,
                                       &status);
  IMFCollection* events = output_data_buffer.pEvents;
  if (events != NULL) {
    VLOG(1) << "Got events from ProcessOuput, but discarding";
    events->Release();
  }
  if (FAILED(hr)) {
    // A stream change needs further ProcessInput calls to get back decoder
    // output which is why we need to set the state to stopped.
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
      if (!SetDecoderOutputMediaType(MFVideoFormat_NV12)) {
        // Decoder didn't let us set NV12 output format. Not sure as to why
        // this can happen. Give up in disgust.
        NOTREACHED() << "Failed to set decoder output media type to NV12";
        state_ = kStopped;
      } else {
        DVLOG(1) << "Received output format change from the decoder."
                    " Recursively invoking DoDecode";
        DoDecode();
      }
      return;
    } else if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
      // No more output from the decoder. Stop playback.
      state_ = kStopped;
      return;
    } else {
      NOTREACHED() << "Unhandled error in DoDecode()";
      return;
    }
  }
  TRACE_EVENT_END_ETW("DXVAVideoDecodeAccelerator.Decoding", this, "");

  TRACE_COUNTER1("DXVA Decoding", "TotalPacketsBeforeDecode",
                 inputs_before_decode_);

  inputs_before_decode_ = 0;

  RETURN_AND_NOTIFY_ON_FAILURE(ProcessOutputSample(output_data_buffer.pSample),
      "Failed to process output sample.", PLATFORM_FAILURE,);

  state_ = kNormal;
}

bool DXVAVideoDecodeAccelerator::ProcessOutputSample(IMFSample* sample) {
  RETURN_ON_FAILURE(sample, "Decode succeeded with NULL output sample", false);

  base::win::ScopedComPtr<IMFSample> output_sample;
  output_sample.Attach(sample);

  base::win::ScopedComPtr<IMFMediaBuffer> output_buffer;
  HRESULT hr = sample->GetBufferByIndex(0, output_buffer.Receive());
  RETURN_ON_HR_FAILURE(hr, "Failed to get buffer from output sample", false);

  base::win::ScopedComPtr<IDirect3DSurface9> surface;
  hr = MFGetService(output_buffer, MR_BUFFER_SERVICE,
                    IID_PPV_ARGS(surface.Receive()));
  RETURN_ON_HR_FAILURE(hr, "Failed to get D3D surface from output sample",
                       false);

  D3DSURFACE_DESC surface_desc;
  hr = surface->GetDesc(&surface_desc);
  RETURN_ON_HR_FAILURE(hr, "Failed to get surface description", false);

  TRACE_EVENT_BEGIN_ETW("DXVAVideoDecodeAccelerator.SurfaceCreation", this,
                        "");
  // TODO(ananta)
  // The code below may not be necessary once we have an ANGLE extension which
  // allows us to pass the Direct 3D surface directly for rendering.

  // The decoded bits in the source direct 3d surface are in the YUV
  // format. Angle does not support that. As a workaround we create an
  // offscreen surface in the RGB format and copy the source surface
  // to this surface.
  base::win::ScopedComPtr<IDirect3DSurface9> dest_surface;
  hr = device_->CreateOffscreenPlainSurface(surface_desc.Width,
                                            surface_desc.Height,
                                            D3DFMT_A8R8G8B8,
                                            D3DPOOL_DEFAULT,
                                            dest_surface.Receive(),
                                            NULL);
  RETURN_ON_HR_FAILURE(hr, "Failed to create offscreen surface", false);

  hr = D3DXLoadSurfaceFromSurface(dest_surface, NULL, NULL, surface, NULL,
                                  NULL, D3DX_DEFAULT, 0);
  RETURN_ON_HR_FAILURE(hr, "D3DXLoadSurfaceFromSurface failed", false);

  TRACE_EVENT_END_ETW("DXVAVideoDecodeAccelerator.SurfaceCreation", this, "");

  pending_output_samples_.push_back(
      PendingSampleInfo(last_input_buffer_id_, dest_surface));

  // If we have available picture buffers to copy the output data then use the
  // first one and then flag it as not being available for use.
  if (output_picture_buffers_.size()) {
    ProcessPendingSamples();
    return true;
  }
  if (pictures_requested_) {
    DVLOG(1) << "Waiting for picture slots from the client.";
    return true;
  }
  // Go ahead and request picture buffers.
  MessageLoop::current()->PostTask(FROM_HERE, base::Bind(
      &DXVAVideoDecodeAccelerator::RequestPictureBuffers,
      this, surface_desc.Width, surface_desc.Height));

  pictures_requested_ = true;
  return true;
}

bool DXVAVideoDecodeAccelerator::CopyOutputSampleDataToPictureBuffer(
    IDirect3DSurface9* dest_surface, media::PictureBuffer picture_buffer,
    int input_buffer_id) {
  DCHECK(dest_surface);

  D3DSURFACE_DESC surface_desc;
  HRESULT hr = dest_surface->GetDesc(&surface_desc);
  RETURN_ON_HR_FAILURE(hr, "Failed to get surface description", false);

  scoped_array<char> bits;
  RETURN_ON_FAILURE(GetBitmapFromSurface(dest_surface, &bits),
                    "Failed to get bitmap from surface for rendering", false);

  // This function currently executes in the context of IPC handlers in the
  // GPU process which ensures that there is always a OpenGL context.
  GLint current_texture = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_texture);

  glBindTexture(GL_TEXTURE_2D, picture_buffer.texture_id());
  glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, surface_desc.Width,
               surface_desc.Height, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE,
               reinterpret_cast<GLvoid*>(bits.get()));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  glBindTexture(GL_TEXTURE_2D, current_texture);

  media::Picture output_picture(picture_buffer.id(), input_buffer_id);
  MessageLoop::current()->PostTask(FROM_HERE, base::Bind(
      &DXVAVideoDecodeAccelerator::NotifyPictureReady, this, output_picture));
  return true;
}

void DXVAVideoDecodeAccelerator::ProcessPendingSamples() {
  if (pending_output_samples_.empty())
    return;

  OutputBuffers::iterator index;

  for (index = output_picture_buffers_.begin();
       index != output_picture_buffers_.end() &&
       !pending_output_samples_.empty();
       ++index) {
    if (index->second.available) {
      PendingSampleInfo sample_info = pending_output_samples_.front();

      CopyOutputSampleDataToPictureBuffer(sample_info.dest_surface,
                                          index->second.picture_buffer,
                                          sample_info.input_buffer_id);
      index->second.available = false;
      pending_output_samples_.pop_front();
    }
  }
}

void DXVAVideoDecodeAccelerator::ClearState() {
  last_input_buffer_id_ = -1;
  output_picture_buffers_.clear();
  pending_output_samples_.clear();
}

void DXVAVideoDecodeAccelerator::StopOnError(
  media::VideoDecodeAccelerator::Error error) {
  DCHECK(CalledOnValidThread());

  if (client_)
    client_->NotifyError(error);
  client_ = NULL;

  if (state_ != kUninitialized) {
    Invalidate();
  }
}

bool DXVAVideoDecodeAccelerator::GetBitmapFromSurface(
    IDirect3DSurface9* surface,
    scoped_array<char>* bits) {
  // Get the currently loaded bitmap from the DC.
  HDC hdc = NULL;
  HRESULT hr = surface->GetDC(&hdc);
  RETURN_ON_HR_FAILURE(hr, "Failed to get HDC from surface", false);

  HBITMAP bitmap =
      reinterpret_cast<HBITMAP>(GetCurrentObject(hdc, OBJ_BITMAP));
  if (!bitmap) {
    NOTREACHED() << "Failed to get bitmap from DC";
    surface->ReleaseDC(hdc);
    return false;
  }
  // TODO(ananta)
  // The code below may not be necessary once we have an ANGLE extension which
  // allows us to pass the Direct 3D surface directly for rendering.
  // The Device dependent bitmap is upside down for OpenGL. We convert the
  // bitmap to a DIB and render it on the texture instead.
  BITMAP bitmap_basic_info = {0};
  if (!GetObject(bitmap, sizeof(BITMAP), &bitmap_basic_info)) {
    NOTREACHED() << "Failed to read bitmap info";
    surface->ReleaseDC(hdc);
    return false;
  }
  BITMAPINFO bitmap_info = {0};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = bitmap_basic_info.bmWidth;
  bitmap_info.bmiHeader.biHeight = bitmap_basic_info.bmHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = bitmap_basic_info.bmBitsPixel;
  bitmap_info.bmiHeader.biCompression = BI_RGB;
  bitmap_info.bmiHeader.biSizeImage = 0;
  bitmap_info.bmiHeader.biClrUsed = 0;

  int ret = GetDIBits(hdc, bitmap, 0, 0, NULL, &bitmap_info, DIB_RGB_COLORS);
  if (!ret || bitmap_info.bmiHeader.biSizeImage <= 0) {
    NOTREACHED() << "Failed to read bitmap size";
    surface->ReleaseDC(hdc);
    return false;
  }

  bits->reset(new char[bitmap_info.bmiHeader.biSizeImage]);
  ret = GetDIBits(hdc, bitmap, 0, bitmap_basic_info.bmHeight, bits->get(),
                  &bitmap_info, DIB_RGB_COLORS);
  if (!ret) {
    NOTREACHED() << "Failed to retrieve bitmap bits.";
  }
  surface->ReleaseDC(hdc);
  return !!ret;
}

void DXVAVideoDecodeAccelerator::Invalidate() {
  if (state_ == kUninitialized)
    return;
  ClearState();
  decoder_.Release();
  MFShutdown();
  state_ = kUninitialized;
}

void DXVAVideoDecodeAccelerator::NotifyInitializeDone() {
  if (client_)
    client_->NotifyInitializeDone();
}

void DXVAVideoDecodeAccelerator::NotifyInputBufferRead(int input_buffer_id) {
  if (client_)
    client_->NotifyEndOfBitstreamBuffer(input_buffer_id);
}

void DXVAVideoDecodeAccelerator::NotifyFlushDone() {
  if (client_)
    client_->NotifyFlushDone();
}

void DXVAVideoDecodeAccelerator::NotifyResetDone() {
  if (client_)
    client_->NotifyResetDone();
}

void DXVAVideoDecodeAccelerator::RequestPictureBuffers(int width, int height) {
  // This task could execute after the decoder has been torn down.
  if (state_ != kUninitialized && client_) {
    client_->ProvidePictureBuffers(kNumPictureBuffers,
                                   gfx::Size(width, height));
  }
}

void DXVAVideoDecodeAccelerator::NotifyPictureReady(
    const media::Picture& picture) {
  // This task could execute after the decoder has been torn down.
  if (state_ != kUninitialized && client_)
    client_->PictureReady(picture);
}

