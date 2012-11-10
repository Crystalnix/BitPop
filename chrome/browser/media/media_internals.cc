// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/media_internals.h"

#include "base/memory/scoped_ptr.h"
#include "base/string16.h"
#include "base/stringprintf.h"
#include "chrome/browser/media/media_internals_observer.h"
#include "chrome/browser/media/media_stream_capture_indicator.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_ui.h"
#include "media/base/media_log.h"
#include "media/base/media_log_event.h"

using content::BrowserThread;

MediaInternals* MediaInternals::GetInstance() {
  return Singleton<MediaInternals>::get();
}

MediaInternals::~MediaInternals() {}

void MediaInternals::OnDeleteAudioStream(void* host, int stream_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  std::string stream = base::StringPrintf("audio_streams.%p:%d",
                                          host, stream_id);
  DeleteItem(stream);
}

void MediaInternals::OnSetAudioStreamPlaying(
    void* host, int stream_id, bool playing) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  UpdateAudioStream(host, stream_id,
                    "playing", Value::CreateBooleanValue(playing));
}

void MediaInternals::OnSetAudioStreamStatus(
    void* host, int stream_id, const std::string& status) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  UpdateAudioStream(host, stream_id,
                    "status", Value::CreateStringValue(status));
}

void MediaInternals::OnSetAudioStreamVolume(
    void* host, int stream_id, double volume) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  UpdateAudioStream(host, stream_id,
                    "volume", Value::CreateDoubleValue(volume));
}

void MediaInternals::OnMediaEvent(
    int render_process_id, const media::MediaLogEvent& event) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Notify observers that |event| has occured.
  DictionaryValue dict;
  dict.SetInteger("renderer", render_process_id);
  dict.SetInteger("player", event.id);
  dict.SetString("type", media::MediaLog::EventTypeToString(event.type));
  dict.SetDouble("time", event.time.ToDoubleT());
  dict.Set("params", event.params.DeepCopy());
  SendUpdate("media.onMediaEvent", &dict);
}

void MediaInternals::OnCaptureDevicesOpened(
    int render_process_id,
    int render_view_id,
    const content::MediaStreamDevices& devices) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!media_stream_capture_indicator_.get())
    media_stream_capture_indicator_ = new MediaStreamCaptureIndicator();
  media_stream_capture_indicator_->CaptureDevicesOpened(render_process_id,
                                                        render_view_id,
                                                        devices);
}

void MediaInternals::OnCaptureDevicesClosed(
    int render_process_id,
    int render_view_id,
    const content::MediaStreamDevices& devices) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  media_stream_capture_indicator_->CaptureDevicesClosed(render_process_id,
                                                        render_view_id,
                                                        devices);
}

void MediaInternals::AddObserver(MediaInternalsObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  observers_.AddObserver(observer);
}

void MediaInternals::RemoveObserver(MediaInternalsObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  observers_.RemoveObserver(observer);
}

void MediaInternals::SendEverything() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  SendUpdate("media.onReceiveEverything", &data_);
}

MediaInternals::MediaInternals() {}

void MediaInternals::UpdateAudioStream(
    void* host, int stream_id, const std::string& property, Value* value) {
  std::string stream = base::StringPrintf("audio_streams.%p:%d",
                                          host, stream_id);
  UpdateItem("media.addAudioStream", stream, property, value);
}

void MediaInternals::DeleteItem(const std::string& item) {
  data_.Remove(item, NULL);
  scoped_ptr<Value> value(Value::CreateStringValue(item));
  SendUpdate("media.onItemDeleted", value.get());
}

void MediaInternals::UpdateItem(
    const std::string& update_fn, const std::string& id,
    const std::string& property, Value* value) {
  DictionaryValue* item_properties;
  if (!data_.GetDictionary(id, &item_properties)) {
    item_properties = new DictionaryValue();
    data_.Set(id, item_properties);
    item_properties->SetString("id", id);
  }
  item_properties->Set(property, value);
  SendUpdate(update_fn, item_properties);
}

void MediaInternals::SendUpdate(const std::string& function, Value* value) {
  // Only bother serializing the update to JSON if someone is watching.
  if (observers_.size()) {
    std::vector<const Value*> args;
    args.push_back(value);
    string16 update = content::WebUI::GetJavascriptCall(function, args);
    FOR_EACH_OBSERVER(MediaInternalsObserver, observers_, OnUpdate(update));
  }
}
