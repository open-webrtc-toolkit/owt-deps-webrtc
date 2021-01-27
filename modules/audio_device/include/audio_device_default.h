/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_DEVICE_INCLUDE_AUDIO_DEVICE_DEFAULT_H_
#define MODULES_AUDIO_DEVICE_INCLUDE_AUDIO_DEVICE_DEFAULT_H_

#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_device/include/audio_device.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/task_queue/task_queue_factory.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/platform_thread.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/sleep.h"

namespace webrtc {
namespace webrtc_impl {

// AudioDeviceModuleDefault template adds default implementation for all
// AudioDeviceModule methods to the class, which inherits from
// AudioDeviceModuleDefault<T>.
template <typename T>
class AudioDeviceModuleDefault : public T {
 public:
  AudioDeviceModuleDefault() {
    task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
    audio_device_buffer.reset(new AudioDeviceBuffer(task_queue_factory.get()));
    playout_frames_in_10ms = 48000 / 100;
  }
  virtual ~AudioDeviceModuleDefault() {}

  int32_t RegisterAudioCallback(AudioTransport* audioCallback) override {
    return audio_device_buffer->RegisterAudioCallback(audioCallback);
  }
  int32_t Init() override { return 0; }
  int32_t InitSpeaker() override { return 0; }
  int32_t SetPlayoutDevice(uint16_t index) override { return 0; }
  int32_t SetPlayoutDevice(
      AudioDeviceModule::WindowsDeviceType device) override {
    return 0;
  }
  int32_t SetStereoPlayout(bool enable) override { return 0; }
  int32_t StopPlayout() override {
    {
      rtc::CritScope lock(&_critSect);
      playing = false;
    }
    if (play_thread.get()) {
      play_thread->Stop();
      play_thread.reset();
    }
    return 0;
  }
  int32_t InitMicrophone() override { return 0; }
  int32_t SetRecordingDevice(uint16_t index) override { return 0; }
  int32_t SetRecordingDevice(
      AudioDeviceModule::WindowsDeviceType device) override {
    return 0;
  }
  int32_t SetStereoRecording(bool enable) override { return 0; }
  int32_t StopRecording() override { return 0; }

  int32_t Terminate() override { return 0; }

  int32_t ActiveAudioLayer(
      AudioDeviceModule::AudioLayer* audioLayer) const override {
    return 0;
  }
  bool Initialized() const override { return true; }
  int16_t PlayoutDevices() override { return 0; }
  int16_t RecordingDevices() override { return 0; }
  int32_t PlayoutDeviceName(uint16_t index,
                            char name[kAdmMaxDeviceNameSize],
                            char guid[kAdmMaxGuidSize]) override {
    return 0;
  }
  int32_t RecordingDeviceName(uint16_t index,
                              char name[kAdmMaxDeviceNameSize],
                              char guid[kAdmMaxGuidSize]) override {
    return 0;
  }
  int32_t PlayoutIsAvailable(bool* available) override {
    *available = true;
    return 0;
  }
  int32_t InitPlayout() override {
    rtc::CritScope lock(&_critSect);
    if (audio_device_buffer.get()) {
      audio_device_buffer->SetPlayoutSampleRate(48000);
      audio_device_buffer->SetPlayoutChannels(2);
    }
    return 0;
  }
  bool PlayoutIsInitialized() const override { return true; }
  int32_t RecordingIsAvailable(bool* available) override { return 0; }
  int32_t InitRecording() override { return 0; }
  bool RecordingIsInitialized() const override { return true; }
  int32_t StartPlayout() override {
    if (playing)
      return 0;

    playing = true;
    play_thread.reset(new rtc::PlatformThread(PlayThreadFunc,
	this, "fake_audio_play_thread", rtc::kRealtimePriority));
    play_thread->Start();
    return 0;
  }
  bool Playing() const override { return playing; }
  int32_t StartRecording() override { return 0; }
  bool Recording() const override { return true; }
  bool SpeakerIsInitialized() const override { return true; }
  bool MicrophoneIsInitialized() const override { return true; }
  int32_t SpeakerVolumeIsAvailable(bool* available) override { return 0; }
  int32_t SetSpeakerVolume(uint32_t volume) override { return 0; }
  int32_t SpeakerVolume(uint32_t* volume) const override { return 0; }
  int32_t MaxSpeakerVolume(uint32_t* maxVolume) const override { return 0; }
  int32_t MinSpeakerVolume(uint32_t* minVolume) const override { return 0; }
  int32_t MicrophoneVolumeIsAvailable(bool* available) override { return 0; }
  int32_t SetMicrophoneVolume(uint32_t volume) override { return 0; }
  int32_t MicrophoneVolume(uint32_t* volume) const override { return 0; }
  int32_t MaxMicrophoneVolume(uint32_t* maxVolume) const override { return 0; }
  int32_t MinMicrophoneVolume(uint32_t* minVolume) const override { return 0; }
  int32_t SpeakerMuteIsAvailable(bool* available) override { return 0; }
  int32_t SetSpeakerMute(bool enable) override { return 0; }
  int32_t SpeakerMute(bool* enabled) const override { return 0; }
  int32_t MicrophoneMuteIsAvailable(bool* available) override { return 0; }
  int32_t SetMicrophoneMute(bool enable) override { return 0; }
  int32_t MicrophoneMute(bool* enabled) const override { return 0; }
  int32_t StereoPlayoutIsAvailable(bool* available) const override {
    *available = true;
    return 0;
  }
  int32_t StereoPlayout(bool* enabled) const override { return 0; }
  int32_t StereoRecordingIsAvailable(bool* available) const override {
    *available = true;
    return 0;
  }
  int32_t StereoRecording(bool* enabled) const override { return 0; }
  int32_t PlayoutDelay(uint16_t* delayMS) const override {
    *delayMS = 0;
    return 0;
  }
  bool BuiltInAECIsAvailable() const override { return false; }
  int32_t EnableBuiltInAEC(bool enable) override { return -1; }
  bool BuiltInAGCIsAvailable() const override { return false; }
  int32_t EnableBuiltInAGC(bool enable) override { return -1; }
  bool BuiltInNSIsAvailable() const override { return false; }
  int32_t EnableBuiltInNS(bool enable) override { return -1; }

  int32_t GetPlayoutUnderrunCount() const override { return -1; }

  bool PlayThreadProcess() {
    if (!playing)
      return false;
    int64_t current_time = rtc::TimeMillis();

    _critSect.Enter();
    if (last_call_millis == 0 ||
	current_time - last_call_millis >= 10) {
      _critSect.Leave();
      audio_device_buffer->RequestPlayoutData(playout_frames_in_10ms);
      _critSect.Enter();
      last_call_millis = current_time;
    }
    _critSect.Leave();
    int64_t delta_time = rtc::TimeMillis() - current_time;
    if (delta_time < 10) {
      SleepMs(10 - delta_time);
    }
    return true;
  }

  static void PlayThreadFunc(void* pThis) {
    AudioDeviceModuleDefault* device = static_cast<AudioDeviceModuleDefault*>(pThis);
    while (device->PlayThreadProcess()) {
    }
  }
  std::unique_ptr<AudioDeviceBuffer> audio_device_buffer;
  std::unique_ptr<rtc::PlatformThread> play_thread;
  size_t playout_frames_in_10ms;
  rtc::CriticalSection _critSect;
  bool playing = false;
  int64_t last_call_millis = 0;
  std::unique_ptr<webrtc::TaskQueueFactory> task_queue_factory;

#if defined(WEBRTC_IOS)
  int GetPlayoutAudioParameters(AudioParameters* params) const override {
    return -1;
  }
  int GetRecordAudioParameters(AudioParameters* params) const override {
    return -1;
  }
#endif  // WEBRTC_IOS
};

}  // namespace webrtc_impl
}  // namespace webrtc

#endif  // MODULES_AUDIO_DEVICE_INCLUDE_AUDIO_DEVICE_DEFAULT_H_
