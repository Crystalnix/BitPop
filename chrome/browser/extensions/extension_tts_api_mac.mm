// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/mac/cocoa_protocols.h"
#include "base/memory/scoped_nsobject.h"
#include "base/memory/singleton.h"
#include "base/sys_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/extensions/extension_tts_api_constants.h"
#include "chrome/browser/extensions/extension_tts_api_controller.h"
#include "chrome/browser/extensions/extension_tts_api_platform.h"

#import <Cocoa/Cocoa.h>

class ExtensionTtsPlatformImplMac;

@interface ChromeTtsDelegate : NSObject <NSSpeechSynthesizerDelegate> {
 @private
  ExtensionTtsPlatformImplMac* ttsImplMac_;  // weak.
}

- (id)initWithPlatformImplMac:(ExtensionTtsPlatformImplMac*)ttsImplMac;

@end

class ExtensionTtsPlatformImplMac : public ExtensionTtsPlatformImpl {
 public:
  virtual bool PlatformImplAvailable() {
    return true;
  }

  virtual bool Speak(
      int utterance_id,
      const std::string& utterance,
      const std::string& lang,
      const UtteranceContinuousParameters& params);

  virtual bool StopSpeaking();

  virtual bool IsSpeaking();

  virtual bool SendsEvent(TtsEventType event_type);

  // Called by ChromeTtsDelegate when we get a callback from the
  // native speech engine.
  void OnSpeechEvent(NSSpeechSynthesizer* sender,
                     TtsEventType event_type,
                     int char_index,
                     const std::string& error_message);

  // Get the single instance of this class.
  static ExtensionTtsPlatformImplMac* GetInstance();

 private:
  ExtensionTtsPlatformImplMac();
  virtual ~ExtensionTtsPlatformImplMac();

  scoped_nsobject<NSSpeechSynthesizer> speech_synthesizer_;
  scoped_nsobject<ChromeTtsDelegate> delegate_;
  int utterance_id_;
  std::string utterance_;
  bool sent_start_event_;

  friend struct DefaultSingletonTraits<ExtensionTtsPlatformImplMac>;

  DISALLOW_COPY_AND_ASSIGN(ExtensionTtsPlatformImplMac);
};

// static
ExtensionTtsPlatformImpl* ExtensionTtsPlatformImpl::GetInstance() {
  return ExtensionTtsPlatformImplMac::GetInstance();
}

bool ExtensionTtsPlatformImplMac::Speak(
    int utterance_id,
    const std::string& utterance,
    const std::string& lang,
    const UtteranceContinuousParameters& params) {
  // Deliberately construct a new speech synthesizer every time Speak is
  // called, otherwise there's no way to know whether calls to the delegate
  // apply to the current utterance or a previous utterance. In
  // experimentation, the overhead of constructing and destructing a
  // NSSpeechSynthesizer is minimal.
  speech_synthesizer_.reset([[NSSpeechSynthesizer alloc] init]);
  [speech_synthesizer_ setDelegate:delegate_];

  utterance_id_ = utterance_id;
  sent_start_event_ = false;

  // TODO: convert SSML to SAPI xml. http://crbug.com/88072
  utterance_ = utterance;

  // TODO: support languages other than the default: crbug.com/88059

  if (params.rate >= 0.0) {
    // The TTS api defines rate via words per minute. Let 200 be the default.
    [speech_synthesizer_
        setObject:[NSNumber numberWithInt:params.rate * 200]
        forProperty:NSSpeechRateProperty error:nil];
  }

  if (params.pitch >= 0.0) {
    // The TTS api allows an approximate range of 30 to 65 for speech pitch.
    [speech_synthesizer_
        setObject: [NSNumber numberWithInt:(params.pitch * 17 + 30)]
        forProperty:NSSpeechPitchBaseProperty error:nil];
  }

  if (params.volume >= 0.0) {
    [speech_synthesizer_
        setObject: [NSNumber numberWithFloat:params.volume]
        forProperty:NSSpeechVolumeProperty error:nil];
  }

  return [speech_synthesizer_ startSpeakingString:
             [NSString stringWithUTF8String: utterance.c_str()]];
}

bool ExtensionTtsPlatformImplMac::StopSpeaking() {
  if (speech_synthesizer_.get()) {
    [speech_synthesizer_ stopSpeaking];
    speech_synthesizer_.reset(nil);
  }
  return true;
}

bool ExtensionTtsPlatformImplMac::IsSpeaking() {
  // Note: this is OK even if speech_synthesizer_ is nil, it will return false.
  return [speech_synthesizer_ isSpeaking];
}

bool ExtensionTtsPlatformImplMac::SendsEvent(TtsEventType event_type) {
  return (event_type == TTS_EVENT_START ||
          event_type == TTS_EVENT_END ||
          event_type == TTS_EVENT_WORD ||
          event_type == TTS_EVENT_ERROR);
}

void ExtensionTtsPlatformImplMac::OnSpeechEvent(
    NSSpeechSynthesizer* sender,
    TtsEventType event_type,
    int char_index,
    const std::string& error_message) {
  // Don't send events from an utterance that's already completed.
  // This depends on the fact that we construct a new NSSpeechSynthesizer
  // each time we call Speak.
  if (sender != speech_synthesizer_.get())
    return;

  if (event_type == TTS_EVENT_END)
    char_index = utterance_.size();
  ExtensionTtsController* controller = ExtensionTtsController::GetInstance();
  if (event_type == TTS_EVENT_WORD && !sent_start_event_) {
    controller->OnTtsEvent(
        utterance_id_, TTS_EVENT_START, 0, "");
    sent_start_event_ = true;
  }
  controller->OnTtsEvent(
      utterance_id_, event_type, char_index, error_message);
}

ExtensionTtsPlatformImplMac::ExtensionTtsPlatformImplMac() {
  utterance_id_ = -1;
  sent_start_event_ = true;

  delegate_.reset([[ChromeTtsDelegate alloc] initWithPlatformImplMac:this]);
}

ExtensionTtsPlatformImplMac::~ExtensionTtsPlatformImplMac() {
}

// static
ExtensionTtsPlatformImplMac* ExtensionTtsPlatformImplMac::GetInstance() {
  return Singleton<ExtensionTtsPlatformImplMac>::get();
}

@implementation ChromeTtsDelegate

- (id)initWithPlatformImplMac:(ExtensionTtsPlatformImplMac*)ttsImplMac {
  if ((self = [super init])) {
    ttsImplMac_ = ttsImplMac;
  }
  return self;
}

- (void)speechSynthesizer:(NSSpeechSynthesizer*)sender
        didFinishSpeaking:(BOOL)finished_speaking {
  ttsImplMac_->OnSpeechEvent(sender, TTS_EVENT_END, 0, "");
}

- (void)speechSynthesizer:(NSSpeechSynthesizer*)sender
            willSpeakWord:(NSRange)character_range
                 ofString:(NSString*)string {
  ttsImplMac_->OnSpeechEvent(sender, TTS_EVENT_WORD,
      character_range.location, "");
}

- (void)speechSynthesizer:(NSSpeechSynthesizer*)sender
 didEncounterErrorAtIndex:(NSUInteger)character_index
                 ofString:(NSString*)string
                  message:(NSString*)message {
  std::string message_utf8 = base::SysNSStringToUTF8(message);
  ttsImplMac_->OnSpeechEvent(sender, TTS_EVENT_ERROR, character_index,
      message_utf8);
}

@end
