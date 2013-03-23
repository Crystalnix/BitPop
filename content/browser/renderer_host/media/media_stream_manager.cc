// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/media/media_stream_manager.h"

#include <list>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/threading/thread.h"
#include "content/browser/renderer_host/media/audio_input_device_manager.h"
#include "content/browser/renderer_host/media/media_stream_requester.h"
#include "content/browser/renderer_host/media/media_stream_ui_controller.h"
#include "content/browser/renderer_host/media/video_capture_manager.h"
#include "content/browser/renderer_host/media/web_contents_capture_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/media_observer.h"
#include "content/public/browser/media_request_state.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/media_stream_request.h"
#include "googleurl/src/gurl.h"

#if defined(OS_WIN)
#include "base/win/scoped_com_initializer.h"
#endif

namespace {
const char kExtensionScheme[] = "chrome-extension";
}  // namespace

namespace content {

// Creates a random label used to identify requests.
static std::string RandomLabel() {
  // An earlier PeerConnection spec,
  // http://dev.w3.org/2011/webrtc/editor/webrtc.html, specified the
  // MediaStream::label alphabet as containing 36 characters from
  // range: U+0021, U+0023 to U+0027, U+002A to U+002B, U+002D to U+002E,
  // U+0030 to U+0039, U+0041 to U+005A, U+005E to U+007E.
  // Here we use a safe subset.
  static const char kAlphabet[] = "0123456789"
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

  std::string label(36, ' ');
  for (size_t i = 0; i < label.size(); ++i) {
    int random_char = base::RandGenerator(sizeof(kAlphabet) - 1);
    label[i] = kAlphabet[random_char];
  }
  return label;
}

// Helper to verify if a media stream type is part of options or not.
static bool Requested(const StreamOptions& options,
                      MediaStreamType stream_type) {
  return (options.audio_type == stream_type ||
          options.video_type == stream_type);
}

// TODO(xians): Merge DeviceRequest with MediaStreamRequest.
class MediaStreamManager::DeviceRequest {
 public:
  DeviceRequest()
      : requester(NULL),
        type(MEDIA_GENERATE_STREAM),
        render_process_id(-1),
        render_view_id(-1),
        state_(NUM_MEDIA_TYPES, MEDIA_REQUEST_STATE_NOT_REQUESTED) {
  }

  DeviceRequest(MediaStreamRequester* requester,
                const StreamOptions& request_options,
                MediaStreamRequestType request_type,
                int render_process_id,
                int render_view_id,
                const GURL& request_security_origin)
      : requester(requester),
        options(request_options),
        type(request_type),
        render_process_id(render_process_id),
        render_view_id(render_view_id),
        security_origin(request_security_origin),
        state_(NUM_MEDIA_TYPES, MEDIA_REQUEST_STATE_NOT_REQUESTED) {
  }

  ~DeviceRequest() {}

  // Update the request state and notify observers.
  void SetState(MediaStreamType stream_type, MediaRequestState new_state) {
    state_[stream_type] = new_state;

    if (options.video_type != MEDIA_TAB_VIDEO_CAPTURE &&
        options.audio_type != MEDIA_TAB_AUDIO_CAPTURE) {
      return;
    }

    MediaObserver* media_observer =
        GetContentClient()->browser()->GetMediaObserver();
    if (media_observer == NULL)
      return;

    // If we appended a device_id scheme, we want to remove it when notifying
    // observers which may be in different modules since this scheme is only
    // used internally within the content module.
    std::string device_id =
        WebContentsCaptureUtil::StripWebContentsDeviceScheme(
            requested_device_id);

    media_observer->OnMediaRequestStateChanged(
        render_process_id, render_view_id,
        MediaStreamDevice(
            stream_type, device_id, device_id), new_state);
  }

  MediaRequestState state(MediaStreamType stream_type) const {
    return state_[stream_type];
  }

  MediaStreamRequester* requester;  // Can be NULL.
  StreamOptions options;
  MediaStreamRequestType type;
  int render_process_id;
  int render_view_id;
  GURL security_origin;
  std::string requested_device_id;
  StreamDeviceInfoArray devices;

  // Callback to the requester which audio/video devices have been selected.
  // It can be null if the requester has no interest to know the result.
  // Currently it is only used by |DEVICE_ACCESS| type.
  MediaRequestResponseCallback callback;

 private:
  std::vector<MediaRequestState> state_;
};

MediaStreamManager::EnumerationCache::EnumerationCache()
    : valid(false) {
}

MediaStreamManager::EnumerationCache::~EnumerationCache() {
}

MediaStreamManager::MediaStreamManager(media::AudioManager* audio_manager)
    : ALLOW_THIS_IN_INITIALIZER_LIST(
          ui_controller_(new MediaStreamUIController(this))),
      audio_manager_(audio_manager),
      monitoring_started_(false),
      io_loop_(NULL) {
  DCHECK(audio_manager_);
  memset(active_enumeration_ref_count_, 0,
         sizeof(active_enumeration_ref_count_));

  // Some unit tests create the MSM in the IO thread and assumes the
  // initialization is done synchronously.
  if (BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    InitializeDeviceManagersOnIOThread();
  } else {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&MediaStreamManager::InitializeDeviceManagersOnIOThread,
                   base::Unretained(this)));
  }
}

MediaStreamManager::~MediaStreamManager() {
  DCHECK(requests_.empty());
  DCHECK(!device_thread_.get());
  DCHECK(!io_loop_);
}

VideoCaptureManager* MediaStreamManager::video_capture_manager() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(video_capture_manager_);
  return video_capture_manager_;
}

AudioInputDeviceManager* MediaStreamManager::audio_input_device_manager() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(audio_input_device_manager_);
  return audio_input_device_manager_;
}

std::string MediaStreamManager::MakeMediaAccessRequest(
    int render_process_id,
    int render_view_id,
    const StreamOptions& options,
    const GURL& security_origin,
    const MediaRequestResponseCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  // Create a new request based on options.
  DeviceRequest* request = new DeviceRequest(NULL,
                                             options,
                                             MEDIA_DEVICE_ACCESS,
                                             render_process_id,
                                             render_view_id,
                                             security_origin);
  const std::string& label = AddRequest(request);

  request->callback = callback;

  HandleRequest(label);

  return label;
}

std::string MediaStreamManager::GenerateStream(
    MediaStreamRequester* requester,
    int render_process_id,
    int render_view_id,
    const StreamOptions& options,
    const GURL& security_origin) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kUseFakeDeviceForMediaStream)) {
    UseFakeDevice();
  }

  // Create a new request based on options.
  DeviceRequest* request = new DeviceRequest(requester, options,
                                             MEDIA_GENERATE_STREAM,
                                             render_process_id,
                                             render_view_id,
                                             security_origin);
  const std::string& label = AddRequest(request);

  HandleRequest(label);

  return label;
}

std::string MediaStreamManager::GenerateStreamForDevice(
    MediaStreamRequester* requester, int render_process_id, int render_view_id,
    const StreamOptions& options, const std::string& device_id,
    const GURL& security_origin) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  int target_render_process_id = -1;
  int target_render_view_id = -1;

  // We will post the request to the target render view, not the source (i.e.
  // source is an extension, and target is the tab we want to capture).
  bool has_valid_device_id = WebContentsCaptureUtil::ExtractTabCaptureTarget(
      device_id, &target_render_process_id, &target_render_view_id);

  if (!has_valid_device_id ||
      !security_origin.SchemeIs(kExtensionScheme) ||
      (options.audio_type != MEDIA_TAB_AUDIO_CAPTURE &&
       options.audio_type != MEDIA_NO_SERVICE) ||
      (options.video_type != MEDIA_TAB_VIDEO_CAPTURE &&
       options.video_type != MEDIA_NO_SERVICE)) {
    LOG(ERROR) << "Invalid request or used tab capture outside extension API.";
    return std::string();
  }

  // Create a new request based on options.
  DeviceRequest* request = new DeviceRequest(requester, options,
                                             MEDIA_GENERATE_STREAM,
                                             target_render_process_id,
                                             target_render_view_id,
                                             security_origin);
  const std::string& label = AddRequest(request);
  request->requested_device_id = device_id;

  // Pass the request to UI to get user confirmation to use the capture device.
  // Note, GenerateStreamForDevice does not need device enumeration.
  PostRequestToUI(label);

  // TODO(miu): We should ask the device manager whether a device with id
  // |device_id| actually exists.  Note that no such MediaStreamProvider API for
  // this currently exists.  Also, we don't have a user-friendly device name for
  // the infobar UI.
  if (IsAudioMediaType(options.audio_type)) {
    // TODO(justinlin): Updating the state to requested and pending are no-ops
    // in terms of the media manager, but these are the state changes we want to
    // support in terms of extensions (which is registered as an observer).
    request->SetState(options.audio_type, MEDIA_REQUEST_STATE_REQUESTED);
    request->SetState(options.audio_type, MEDIA_REQUEST_STATE_PENDING_APPROVAL);
  }
  if (IsVideoMediaType(options.video_type)) {
    request->SetState(options.video_type, MEDIA_REQUEST_STATE_REQUESTED);
    request->SetState(options.video_type, MEDIA_REQUEST_STATE_PENDING_APPROVAL);
  }

  return label;
}

void MediaStreamManager::CancelRequest(const std::string& label) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  DeviceRequests::iterator it = requests_.find(label);
  if (it != requests_.end()) {
    // The request isn't complete, notify the UI immediately.
    ui_controller_->CancelUIRequest(label);

    if (!RequestDone(*it->second)) {
      // TODO(xians): update the |state| to STATE_DONE to trigger a state
      // changed notification to UI before deleting the request?
      scoped_ptr<DeviceRequest> request(it->second);
      requests_.erase(it);
      for (int i = MEDIA_NO_SERVICE + 1; i < NUM_MEDIA_TYPES;
           ++i) {
        const MediaStreamType stream_type = static_cast<MediaStreamType>(i);
        if (request->state(stream_type) != MEDIA_REQUEST_STATE_OPENING) {
          continue;
        }
        for (StreamDeviceInfoArray::const_iterator device_it =
                 request->devices.begin();
             device_it != request->devices.end(); ++device_it) {
          if (device_it->device.type == stream_type) {
            GetDeviceManager(stream_type)->Close(device_it->session_id);
          }
        }
      }
    } else {
      StopGeneratedStream(label);
    }
  }
}

void MediaStreamManager::StopGeneratedStream(const std::string& label) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  // Find the request and close all open devices for the request.
  DeviceRequests::iterator it = requests_.find(label);
  if (it != requests_.end()) {
    if (it->second->type == MEDIA_ENUMERATE_DEVICES) {
      StopEnumerateDevices(label);
      return;
    }

    scoped_ptr<DeviceRequest> request(it->second);
    requests_.erase(it);
    for (StreamDeviceInfoArray::const_iterator device_it =
             request->devices.begin();
         device_it != request->devices.end(); ++device_it) {
      GetDeviceManager(device_it->device.type)->Close(device_it->session_id);
    }
    if (request->type == MEDIA_GENERATE_STREAM &&
        RequestDone(*request)) {
      // Notify observers that this device is being closed.
      for (int i = MEDIA_NO_SERVICE + 1; i != NUM_MEDIA_TYPES; ++i) {
        if (request->state(static_cast<MediaStreamType>(i)) !=
            MEDIA_REQUEST_STATE_NOT_REQUESTED) {
          request->SetState(static_cast<MediaStreamType>(i),
                            MEDIA_REQUEST_STATE_CLOSING);
        }
      }
      NotifyDevicesClosed(*request);
    }

    // If request isn't complete, notify the UI on the cancellation. And it
    // is also safe to call CancelUIRequest if the request has been done.
    ui_controller_->CancelUIRequest(label);
  }
}

std::string MediaStreamManager::EnumerateDevices(
    MediaStreamRequester* requester,
    int render_process_id,
    int render_view_id,
    MediaStreamType type,
    const GURL& security_origin) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(type == MEDIA_DEVICE_AUDIO_CAPTURE ||
         type == MEDIA_DEVICE_VIDEO_CAPTURE);

  // When the requester is NULL, the request is made by the UI to ensure MSM
  // starts monitoring devices.
  if (!requester) {
    if (!monitoring_started_)
      StartMonitoring();

    return std::string();
  }

  // Create a new request.
  StreamOptions options;
  EnumerationCache* cache = NULL;
  if (type == MEDIA_DEVICE_AUDIO_CAPTURE) {
    options.audio_type = type;
    cache = &audio_enumeration_cache_;
  } else if (type == MEDIA_DEVICE_VIDEO_CAPTURE) {
    options.video_type = type;
    cache = &video_enumeration_cache_;
  } else {
    NOTREACHED();
    return std::string();
  }

  DeviceRequest* request = new DeviceRequest(requester,
                                             options,
                                             MEDIA_ENUMERATE_DEVICES,
                                             render_process_id,
                                             render_view_id,
                                             security_origin);
  const std::string& label = AddRequest(request);

  if (cache->valid) {
    // Cached device list of this type exists. Just send it out.
    request->SetState(type, MEDIA_REQUEST_STATE_REQUESTED);

    // Need to post a task since the requester won't have label till
    // this function returns.
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&MediaStreamManager::SendCachedDeviceList,
                   base::Unretained(this), cache, label));
  } else {
    StartEnumeration(request);
  }

  return label;
}

void MediaStreamManager::StopEnumerateDevices(const std::string& label) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  DeviceRequests::iterator it = requests_.find(label);
  if (it != requests_.end()) {
    DCHECK_EQ(it->second->type, MEDIA_ENUMERATE_DEVICES);
    // Delete the DeviceRequest.
    scoped_ptr<DeviceRequest> request(it->second);
    requests_.erase(it);
  }
}

std::string MediaStreamManager::OpenDevice(
    MediaStreamRequester* requester,
    int render_process_id,
    int render_view_id,
    const std::string& device_id,
    MediaStreamType type,
    const GURL& security_origin) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(type == MEDIA_DEVICE_AUDIO_CAPTURE ||
         type == MEDIA_DEVICE_VIDEO_CAPTURE);

  // Create a new request.
  StreamOptions options;
  if (IsAudioMediaType(type)) {
    options.audio_type = type;
  } else if (IsVideoMediaType(type)) {
    options.video_type = type;
  } else {
    NOTREACHED();
    return std::string();
  }

  DeviceRequest* request = new DeviceRequest(requester,
                                             options,
                                             MEDIA_OPEN_DEVICE,
                                             render_process_id,
                                             render_view_id,
                                             security_origin);
  request->requested_device_id = device_id;
  const std::string& label = AddRequest(request);
  StartEnumeration(request);

  return label;
}

void MediaStreamManager::NotifyUIDevicesOpened(
    int render_process_id,
    int render_view_id,
    const MediaStreamDevices& devices) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  ui_controller_->NotifyUIIndicatorDevicesOpened(render_process_id,
                                                 render_view_id,
                                                 devices);
}

void MediaStreamManager::NotifyUIDevicesClosed(
    int render_process_id,
    int render_view_id,
    const MediaStreamDevices& devices) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  ui_controller_->NotifyUIIndicatorDevicesClosed(render_process_id,
                                                 render_view_id,
                                                 devices);
}

void MediaStreamManager::SendCachedDeviceList(
    EnumerationCache* cache,
    const std::string& label) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (cache->valid) {
    DeviceRequests::iterator it = requests_.find(label);
    if (it != requests_.end()) {
      it->second->requester->DevicesEnumerated(label, cache->devices);
    }
  }
}

void MediaStreamManager::StartMonitoring() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!base::SystemMonitor::Get())
    return;

  if (!monitoring_started_) {
    monitoring_started_ = true;
    base::SystemMonitor::Get()->AddDevicesChangedObserver(this);

    // Enumerate both the audio and video devices to cache the device lists
    // and send them to media observer.
    ++active_enumeration_ref_count_[MEDIA_DEVICE_AUDIO_CAPTURE];
    audio_input_device_manager_->EnumerateDevices();
    ++active_enumeration_ref_count_[MEDIA_DEVICE_VIDEO_CAPTURE];
    video_capture_manager_->EnumerateDevices();
  }
}

void MediaStreamManager::StopMonitoring() {
  DCHECK_EQ(MessageLoop::current(), io_loop_);
  if (monitoring_started_) {
    base::SystemMonitor::Get()->RemoveDevicesChangedObserver(this);
    monitoring_started_ = false;
    ClearEnumerationCache(&audio_enumeration_cache_);
    ClearEnumerationCache(&video_enumeration_cache_);
  }
}

void MediaStreamManager::ClearEnumerationCache(EnumerationCache* cache) {
  DCHECK_EQ(MessageLoop::current(), io_loop_);
  cache->valid = false;
}

void MediaStreamManager::StartEnumeration(DeviceRequest* request) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Start monitoring the devices when doing the first enumeration.
  if (!monitoring_started_ && base::SystemMonitor::Get()) {
    StartMonitoring();

    if (IsAudioMediaType(request->options.audio_type)) {
      request->SetState(request->options.audio_type,
                        MEDIA_REQUEST_STATE_REQUESTED);
    }
    if (IsVideoMediaType(request->options.video_type)) {
      request->SetState(request->options.video_type,
                        MEDIA_REQUEST_STATE_REQUESTED);
    }
  } else {
    for (int i = MEDIA_NO_SERVICE + 1; i < NUM_MEDIA_TYPES; ++i) {
      const MediaStreamType stream_type = static_cast<MediaStreamType>(i);
      if (Requested(request->options, stream_type)) {
        request->SetState(stream_type, MEDIA_REQUEST_STATE_REQUESTED);
        DCHECK_GE(active_enumeration_ref_count_[stream_type], 0);
        if (active_enumeration_ref_count_[stream_type] == 0) {
          ++active_enumeration_ref_count_[stream_type];
          GetDeviceManager(stream_type)->EnumerateDevices();
        }
      }
    }
  }
}

std::string MediaStreamManager::AddRequest(DeviceRequest* request) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Create a label for this request and verify it is unique.
  std::string unique_label;
  do {
    unique_label = RandomLabel();
  } while (requests_.find(unique_label) != requests_.end());

  requests_.insert(std::make_pair(unique_label, request));

  return unique_label;
}

void MediaStreamManager::PostRequestToUI(const std::string& label) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DeviceRequest* request = requests_[label];
  // Get user confirmation to use capture devices.
  ui_controller_->MakeUIRequest(label,
                                request->render_process_id,
                                request->render_view_id,
                                request->options,
                                request->security_origin,
                                request->type,
                                request->requested_device_id);
}

void MediaStreamManager::HandleRequest(const std::string& label) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DeviceRequest* request = requests_[label];
  if ((IsAudioMediaType(request->options.audio_type) &&
       !audio_enumeration_cache_.valid) ||
      (IsVideoMediaType(request->options.video_type) &&
       !video_enumeration_cache_.valid)) {
    // Enumerate the devices if there is no valid device lists to be used.
    StartEnumeration(request);
    return;
  }

  // No need to do new device enumerations, post the request to UI
  // immediately.
  if (IsAudioMediaType(request->options.audio_type)) {
    request->SetState(request->options.audio_type,
                      MEDIA_REQUEST_STATE_PENDING_APPROVAL);
  }

  if (IsVideoMediaType(request->options.video_type)) {
    request->SetState(request->options.video_type,
                      MEDIA_REQUEST_STATE_PENDING_APPROVAL);
  }

  PostRequestToUI(label);
}

void MediaStreamManager::InitializeDeviceManagersOnIOThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (device_thread_.get())
    return;

  device_thread_.reset(new base::Thread("MediaStreamDeviceThread"));
#if defined(OS_WIN)
  device_thread_->init_com_with_mta(true);
#endif
  CHECK(device_thread_->Start());

  audio_input_device_manager_ = new AudioInputDeviceManager(audio_manager_);
  audio_input_device_manager_->Register(this,
                                        device_thread_->message_loop_proxy());

  video_capture_manager_ = new VideoCaptureManager();
  video_capture_manager_->Register(this, device_thread_->message_loop_proxy());

  // We want to be notified of IO message loop destruction to delete the thread
  // and the device managers.
  io_loop_ = MessageLoop::current();
  io_loop_->AddDestructionObserver(this);
}

void MediaStreamManager::Opened(MediaStreamType stream_type,
                                int capture_session_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Find the request containing this device and mark it as used.
  DeviceRequest* request = NULL;
  StreamDeviceInfoArray* devices = NULL;
  std::string label;
  for (DeviceRequests::iterator request_it = requests_.begin();
       request_it != requests_.end() && request == NULL; ++request_it) {
    devices = &(request_it->second->devices);
    for (StreamDeviceInfoArray::iterator device_it = devices->begin();
         device_it != devices->end(); ++device_it) {
      if (device_it->device.type == stream_type &&
          device_it->session_id == capture_session_id) {
        // We've found the request.
        device_it->in_use = true;
        label = request_it->first;
        request = request_it->second;
        break;
      }
    }
  }
  if (request == NULL) {
    // The request doesn't exist.
    return;
  }

  DCHECK_NE(request->state(stream_type), MEDIA_REQUEST_STATE_REQUESTED);

  // Check if all devices for this stream type are opened. Update the state if
  // they are.
  for (StreamDeviceInfoArray::iterator device_it = devices->begin();
       device_it != devices->end(); ++device_it) {
    if (device_it->device.type != stream_type) {
      continue;
    }
    if (device_it->in_use == false) {
      // Wait for more devices to be opened before we're done.
      return;
    }
  }

  request->SetState(stream_type, MEDIA_REQUEST_STATE_DONE);

  if (!RequestDone(*request)) {
    // This stream_type is done, but not the other type.
    return;
  }

  switch (request->type) {
    case MEDIA_OPEN_DEVICE:
      request->requester->DeviceOpened(label, devices->front());
      break;
    case MEDIA_GENERATE_STREAM: {
      // Partition the array of devices into audio vs video.
      StreamDeviceInfoArray audio_devices, video_devices;
      for (StreamDeviceInfoArray::const_iterator device_it = devices->begin();
           device_it != devices->end(); ++device_it) {
        if (IsAudioMediaType(device_it->device.type)) {
          audio_devices.push_back(*device_it);
        } else if (IsVideoMediaType(device_it->device.type)) {
          video_devices.push_back(*device_it);
        } else {
          NOTREACHED();
        }
      }

      request->requester->StreamGenerated(label, audio_devices, video_devices);
      NotifyDevicesOpened(*request);
      break;
    }
    default:
      NOTREACHED();
      break;
  }
}

void MediaStreamManager::Closed(MediaStreamType stream_type,
                                int capture_session_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
}

void MediaStreamManager::DevicesEnumerated(
    MediaStreamType stream_type, const StreamDeviceInfoArray& devices) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Only cache the device list when the device list has been changed.
  bool need_update_clients = false;
  EnumerationCache* cache =
      stream_type == MEDIA_DEVICE_AUDIO_CAPTURE ?
      &audio_enumeration_cache_ : &video_enumeration_cache_;
  if (!cache->valid ||
      devices.size() != cache->devices.size() ||
      !std::equal(devices.begin(), devices.end(), cache->devices.begin(),
                  StreamDeviceInfo::IsEqual)) {
    cache->valid = true;
    cache->devices = devices;
    need_update_clients = true;
  }

  if (need_update_clients && monitoring_started_)
    NotifyDevicesChanged(stream_type, devices);

  // Publish the result for all requests waiting for device list(s).
  // Find the requests waiting for this device list, store their labels and
  // release the iterator before calling device settings. We might get a call
  // back from device_settings that will need to iterate through devices.
  std::list<std::string> label_list;
  for (DeviceRequests::iterator it = requests_.begin(); it != requests_.end();
       ++it) {
    if (it->second->state(stream_type) ==
        MEDIA_REQUEST_STATE_REQUESTED &&
        Requested(it->second->options, stream_type)) {
      if (it->second->type != MEDIA_ENUMERATE_DEVICES)
        it->second->SetState(stream_type, MEDIA_REQUEST_STATE_PENDING_APPROVAL);
      label_list.push_back(it->first);
    }
  }
  for (std::list<std::string>::iterator it = label_list.begin();
       it != label_list.end(); ++it) {
    DeviceRequest* request = requests_[*it];
    switch (request->type) {
      case MEDIA_ENUMERATE_DEVICES:
        if (need_update_clients && request->requester)
          request->requester->DevicesEnumerated(*it, devices);
        break;
      default:
        if (request->state(request->options.audio_type) ==
                MEDIA_REQUEST_STATE_REQUESTED ||
            request->state(request->options.video_type) ==
                MEDIA_REQUEST_STATE_REQUESTED) {
          // We are doing enumeration for other type of media, wait until it is
          // all done before posting the request to UI because UI needs
          // the device lists to handle the request.
          break;
        }

        // Post the request to UI for permission approval.
        PostRequestToUI(*it);
        break;
    }
  }
  label_list.clear();
  --active_enumeration_ref_count_[stream_type];
  DCHECK_GE(active_enumeration_ref_count_[stream_type], 0);
}

void MediaStreamManager::Error(MediaStreamType stream_type,
                               int capture_session_id,
                               MediaStreamProviderError error) {
  // Find the device for the error call.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  for (DeviceRequests::iterator it = requests_.begin(); it != requests_.end();
       ++it) {
    StreamDeviceInfoArray& devices = it->second->devices;

    // TODO(miu): BUG.  It's possible for the audio (or video) device array in
    // the "requester" to become out-of-sync with the order of devices we have
    // here.  See http://crbug.com/147650
    int audio_device_idx = -1;
    int video_device_idx = -1;
    for (StreamDeviceInfoArray::iterator device_it = devices.begin();
         device_it != devices.end(); ++device_it) {
      if (IsAudioMediaType(device_it->device.type)) {
        ++audio_device_idx;
      } else if (IsVideoMediaType(device_it->device.type)) {
        ++video_device_idx;
      } else {
        NOTREACHED();
        continue;
      }
      if (device_it->device.type != stream_type ||
          device_it->session_id != capture_session_id) {
        continue;
      }
      // We've found the failing device. Find the error case:
      // An error should only be reported to the MediaStreamManager if
      // the request has not been fulfilled yet.
      DCHECK(it->second->state(stream_type) != MEDIA_REQUEST_STATE_DONE);
      if (it->second->state(stream_type) != MEDIA_REQUEST_STATE_DONE) {
        // Request is not done, devices are not opened in this case.
        if (devices.size() <= 1) {
          scoped_ptr<DeviceRequest> request(it->second);
          // 1. Device not opened and no other devices for this request ->
          //    signal stream error and remove the request.
          if (request->requester)
            request->requester->StreamGenerationFailed(it->first);

          requests_.erase(it);
        } else {
          // 2. Not opened but other devices exists for this request -> remove
          //    device from list, but don't signal an error.
          devices.erase(device_it);  // NOTE: This invalidates device_it!
        }
      }
      return;
    }
  }
}

void MediaStreamManager::DevicesAccepted(const std::string& label,
                                         const StreamDeviceInfoArray& devices) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!devices.empty());
  DeviceRequests::iterator request_it = requests_.find(label);
  if (request_it == requests_.end()) {
    return;
  }

  if (request_it->second->type == MEDIA_DEVICE_ACCESS) {
    scoped_ptr<DeviceRequest> request(request_it->second);
    if (!request->callback.is_null()) {
      // Map the devices to MediaStreamDevices.
      MediaStreamDevices selected_devices;
      for (StreamDeviceInfoArray::const_iterator it = devices.begin();
           it != devices.end(); ++it) {
        selected_devices.push_back(it->device);
      }

      request->callback.Run(label, selected_devices);
    }

    // Delete the request since it is done.
    requests_.erase(request_it);
    return;
  }

  // Process all newly-accepted devices for this request.
  DeviceRequest* request = request_it->second;
  bool found_audio = false, found_video = false;
  for (StreamDeviceInfoArray::const_iterator device_it = devices.begin();
       device_it != devices.end(); ++device_it) {
    StreamDeviceInfo device_info = *device_it;  // Make a copy.

    // TODO(justinlin): Nicer way to do this?
    // Re-append the device's id since we lost it when posting request to UI.
    if (device_info.device.type == content::MEDIA_TAB_VIDEO_CAPTURE ||
        device_info.device.type == content::MEDIA_TAB_AUDIO_CAPTURE)
      device_info.device.id = request->requested_device_id;

    // Set in_use to false to be able to track if this device has been
    // opened. in_use might be true if the device type can be used in more
    // than one session.
    DCHECK_EQ(request->state(device_it->device.type),
              MEDIA_REQUEST_STATE_PENDING_APPROVAL);
    device_info.in_use = false;

    device_info.session_id =
        GetDeviceManager(device_info.device.type)->Open(device_info);
    request->SetState(device_it->device.type, MEDIA_REQUEST_STATE_OPENING);
    request->devices.push_back(device_info);

    if (device_info.device.type == request->options.audio_type) {
      found_audio = true;
    } else if (device_info.device.type == request->options.video_type) {
      found_video = true;
    }
  }

  // Check whether we've received all stream types requested.
  if (!found_audio && IsAudioMediaType(request->options.audio_type))
    request->SetState(request->options.audio_type, MEDIA_REQUEST_STATE_ERROR);

  if (!found_video && IsVideoMediaType(request->options.video_type))
    request->SetState(request->options.video_type, MEDIA_REQUEST_STATE_ERROR);
}

void MediaStreamManager::SettingsError(const std::string& label) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  // Erase this request and report an error.
  DeviceRequests::iterator it = requests_.find(label);
  if (it == requests_.end())
    return;

  // Notify the users about the request result.
  scoped_ptr<DeviceRequest> request(it->second);
  if (request->requester)
    request->requester->StreamGenerationFailed(label);

  if (request->type == MEDIA_DEVICE_ACCESS &&
      !request->callback.is_null()) {
    request->callback.Run(label, MediaStreamDevices());
  }

  requests_.erase(it);
}

void MediaStreamManager::GetAvailableDevices(MediaStreamDevices* devices) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(audio_enumeration_cache_.valid || video_enumeration_cache_.valid);
  DCHECK(devices->empty());
  if (audio_enumeration_cache_.valid) {
    for (StreamDeviceInfoArray::const_iterator it =
             audio_enumeration_cache_.devices.begin();
         it != audio_enumeration_cache_.devices.end();
         ++it) {
      devices->push_back(it->device);
    }
  }

  if (video_enumeration_cache_.valid) {
    for (StreamDeviceInfoArray::const_iterator it =
             video_enumeration_cache_.devices.begin();
         it != video_enumeration_cache_.devices.end();
         ++it) {
      devices->push_back(it->device);
    }
  }
}

void MediaStreamManager::UseFakeDevice() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  video_capture_manager()->UseFakeDevice();
  audio_input_device_manager()->UseFakeDevice();
  ui_controller_->UseFakeUI();
}

void MediaStreamManager::WillDestroyCurrentMessageLoop() {
  DCHECK_EQ(MessageLoop::current(), io_loop_);
  DCHECK(requests_.empty());
  if (device_thread_.get()) {
    StopMonitoring();

    video_capture_manager_->Unregister();
    audio_input_device_manager_->Unregister();
    device_thread_.reset();
  }

  audio_input_device_manager_ = NULL;
  video_capture_manager_ = NULL;
  io_loop_ = NULL;
  ui_controller_.reset();
}

void MediaStreamManager::NotifyDevicesOpened(const DeviceRequest& request) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  MediaStreamDevices opened_devices;
  DevicesFromRequest(request, &opened_devices);
  if (opened_devices.empty())
    return;

  NotifyUIDevicesOpened(request.render_process_id,
                        request.render_view_id,
                        opened_devices);
}

void MediaStreamManager::NotifyDevicesClosed(const DeviceRequest& request) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  MediaStreamDevices closed_devices;
  DevicesFromRequest(request, &closed_devices);
  if (closed_devices.empty())
    return;

  NotifyUIDevicesClosed(request.render_process_id,
                        request.render_view_id,
                        closed_devices);
}

void MediaStreamManager::DevicesFromRequest(
    const DeviceRequest& request, MediaStreamDevices* devices) {
  for (StreamDeviceInfoArray::const_iterator it = request.devices.begin();
       it != request.devices.end(); ++it) {
    devices->push_back(it->device);
  }
}

void MediaStreamManager::NotifyDevicesChanged(
    MediaStreamType stream_type,
    const StreamDeviceInfoArray& devices) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  MediaObserver* media_observer =
      GetContentClient()->browser()->GetMediaObserver();
  if (media_observer == NULL)
    return;

  // Map the devices to MediaStreamDevices.
  MediaStreamDevices new_devices;
  for (StreamDeviceInfoArray::const_iterator it = devices.begin();
       it != devices.end(); ++it) {
    new_devices.push_back(it->device);
  }

  if (IsAudioMediaType(stream_type)) {
    media_observer->OnAudioCaptureDevicesChanged(new_devices);
  } else if (IsVideoMediaType(stream_type)) {
    media_observer->OnVideoCaptureDevicesChanged(new_devices);
  } else {
    NOTREACHED();
  }
}

bool MediaStreamManager::RequestDone(const DeviceRequest& request) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  const bool requested_audio = IsAudioMediaType(request.options.audio_type);
  const bool requested_video = IsVideoMediaType(request.options.video_type);

  const bool audio_done =
      !requested_audio ||
      request.state(request.options.audio_type) ==
      MEDIA_REQUEST_STATE_DONE ||
      request.state(request.options.audio_type) ==
      MEDIA_REQUEST_STATE_ERROR;
  if (!audio_done)
    return false;

  const bool video_done =
      !requested_video ||
      request.state(request.options.video_type) ==
      MEDIA_REQUEST_STATE_DONE ||
      request.state(request.options.video_type) ==
      MEDIA_REQUEST_STATE_ERROR;
  if (!video_done)
    return false;

  for (StreamDeviceInfoArray::const_iterator it = request.devices.begin();
       it != request.devices.end(); ++it) {
    if (it->in_use == false)
      return false;
  }

  return true;
}

MediaStreamProvider* MediaStreamManager::GetDeviceManager(
    MediaStreamType stream_type) {
  if (IsVideoMediaType(stream_type)) {
    return video_capture_manager();
  } else if (IsAudioMediaType(stream_type)) {
    return audio_input_device_manager();
  }
  NOTREACHED();
  return NULL;
}

void MediaStreamManager::OnDevicesChanged(
    base::SystemMonitor::DeviceType device_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // NOTE: This method is only called in response to physical audio/video device
  // changes (from the operating system).

  MediaStreamType stream_type;
  if (device_type == base::SystemMonitor::DEVTYPE_AUDIO_CAPTURE) {
    stream_type = MEDIA_DEVICE_AUDIO_CAPTURE;
  } else if (device_type == base::SystemMonitor::DEVTYPE_VIDEO_CAPTURE) {
    stream_type = MEDIA_DEVICE_VIDEO_CAPTURE;
  } else {
    return;  // Uninteresting device change.
  }

  // Always do enumeration even though some enumeration is in progress,
  // because those enumeration commands could be sent before these devices
  // change.
  ++active_enumeration_ref_count_[stream_type];
  GetDeviceManager(stream_type)->EnumerateDevices();
}

}  // namespace content
