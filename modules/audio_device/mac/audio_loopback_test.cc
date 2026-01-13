/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 *  Audio Loopback Test for USB Audio Pulsating Bug
 *
 *  Captures audio from input device and plays it back to output device.
 *  This lets you HEAR if the pulsating bug is present.
 */

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "api/environment/environment_factory.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_device/mac/audio_device_mac.h"
#include "rtc_base/logging.h"

namespace webrtc {

// Audio transport that loops captured audio back to playback
class LoopbackAudioTransport : public AudioTransport {
 public:
  LoopbackAudioTransport() : recording_(false), playing_(false) {
    // Initialize loopback buffer
    loopback_buffer_.resize(48000 * 2 * 10);  // 10 seconds of stereo at 48kHz

    // Prebuffer size: 100ms of stereo audio at 48kHz to prevent underruns
    prebuffer_samples_ = 48000 * 2 * 100 / 1000;  // 9600 samples
    prebuffered_ = false;

    // Statistics
    record_callbacks_ = 0;
    playback_callbacks_ = 0;
    underrun_count_ = 0;
    last_log_time_ = std::chrono::steady_clock::now();
    last_record_callback_time_ = std::chrono::steady_clock::now();
    last_playback_callback_time_ = std::chrono::steady_clock::now();
    total_samples_recorded_ = 0;
    total_samples_played_ = 0;
  }

  int32_t RecordedDataIsAvailable(const void* audioSamples,
                                  size_t nSamples,
                                  size_t nBytesPerSample,
                                  size_t nChannels,
                                  uint32_t samplesPerSec,
                                  uint32_t totalDelayMS,
                                  int32_t clockDrift,
                                  uint32_t currentMicLevel,
                                  bool keyPressed,
                                  uint32_t& newMicLevel) override {
    newMicLevel = currentMicLevel;

    if (recording_ && audioSamples && nSamples > 0) {
      // Convert mono to stereo if needed and store in loopback buffer
      const int16_t* samples = static_cast<const int16_t*>(audioSamples);

      std::lock_guard<std::mutex> lock(buffer_mutex_);
      record_callbacks_++;
      total_samples_recorded_ += nSamples;

      // Track callback timing
      auto now = std::chrono::steady_clock::now();
      auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - last_record_callback_time_)
                     .count();
      if (gap > 50) {  // Log large gaps
        std::cout << "[WARNING] Large gap in record callbacks: " << gap << " ms"
                  << std::endl;
      }
      last_record_callback_time_ = now;

      // Detect silence in input audio (CHECK IF MIC IS CUTTING OUT)
      int64_t sum_abs = 0;
      for (size_t i = 0; i < nSamples; i++) {
        sum_abs += std::abs(samples[i]);
      }
      int32_t avg_level = sum_abs / nSamples;

      static int silence_count = 0;
      static int audio_count = 0;
      if (avg_level < 10) {  // Very quiet/silent
        silence_count++;
        if (silence_count == 1 || (silence_count % 50 == 0)) {
          std::cout << "[INPUT SILENCE #" << silence_count << "] "
                    << "Avg level: " << avg_level << ", samples: " << nSamples
                    << std::endl;
        }
      } else {
        if (silence_count > 0 && audio_count == 0) {
          std::cout << "[INPUT RESUMED] After " << silence_count
                    << " silent callbacks" << std::endl;
        }
        silence_count = 0;
        audio_count++;
      }

      for (size_t i = 0; i < nSamples; i++) {
        int16_t sample = samples[i];
        // Write to both left and right channels
        loopback_buffer_[write_pos_] = sample;
        write_pos_ = (write_pos_ + 1) % loopback_buffer_.size();
        loopback_buffer_[write_pos_] = sample;
        write_pos_ = (write_pos_ + 1) % loopback_buffer_.size();
      }

      size_t available = GetAvailableSamplesLocked();

      // Mark as prebuffered once we have enough data
      if (!prebuffered_ && available >= prebuffer_samples_) {
        prebuffered_ = true;
        std::cout << "\n[PREBUFFER] Complete! Buffer has " << available
                  << " samples (" << (available / 48000.0 / 2.0 * 1000.0)
                  << " ms)" << std::endl;
      }

      // Periodic logging every 2 seconds
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - last_log_time_)
                         .count();
      if (elapsed >= 2000) {
        std::cout << "[STATUS] Record: " << record_callbacks_ << " calls ("
                  << total_samples_recorded_ << " samples, " << nSamples
                  << " per call, " << nChannels << " ch, " << samplesPerSec
                  << " Hz), Playback: " << playback_callbacks_ << " calls ("
                  << total_samples_played_ << " samples), "
                  << "Buffer: " << available << " samples (" << std::fixed
                  << std::setprecision(1)
                  << (available / 48000.0 / 2.0 * 1000.0) << " ms), "
                  << "Underruns: " << underrun_count_ << std::endl;
        last_log_time_ = now;
      }
    }

    return 0;
  }

  int32_t RecordedDataIsAvailable(
      const void* audioSamples,
      size_t nSamples,
      size_t nBytesPerSample,
      size_t nChannels,
      uint32_t samplesPerSec,
      uint32_t totalDelayMS,
      int32_t clockDrift,
      uint32_t currentMicLevel,
      bool keyPressed,
      uint32_t& newMicLevel,
      std::optional<int64_t> estimatedCaptureTimeNs) override {
    return RecordedDataIsAvailable(
        audioSamples, nSamples, nBytesPerSample, nChannels, samplesPerSec,
        totalDelayMS, clockDrift, currentMicLevel, keyPressed, newMicLevel);
  }

  int32_t NeedMorePlayData(size_t nSamples,
                           size_t nBytesPerSample,
                           size_t nChannels,
                           uint32_t samplesPerSec,
                           void* audioSamples,
                           size_t& nSamplesOut,
                           int64_t* elapsed_time_ms,
                           int64_t* ntp_time_ms) override {
    nSamplesOut = nSamples;

    if (playing_ && audioSamples) {
      int16_t* samples = static_cast<int16_t*>(audioSamples);

      std::lock_guard<std::mutex> lock(buffer_mutex_);
      playback_callbacks_++;

      // Track callback timing
      auto now = std::chrono::steady_clock::now();
      auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - last_playback_callback_time_)
                     .count();
      if (gap > 50) {  // Log large gaps
        std::cout << "[WARNING] Large gap in playback callbacks: " << gap
                  << " ms" << std::endl;
      }
      last_playback_callback_time_ = now;

      // Check if we have enough data available (prevent underrun)
      size_t required_samples = nSamples * nChannels;
      size_t available = GetAvailableSamplesLocked();

      // Log first few playback requests to see parameters
      if (playback_callbacks_ <= 5) {
        std::cout << "[PLAYBACK #" << playback_callbacks_
                  << "] nSamples: " << nSamples << ", nChannels: " << nChannels
                  << ", required: " << required_samples
                  << ", available: " << available << std::endl;
      }

      // If not prebuffered or buffer running too low, output silence
      if (!prebuffered_ || available < required_samples) {
        memset(audioSamples, 0, nSamples * nBytesPerSample * nChannels);
        underrun_count_++;

        // Log underrun condition
        if (underrun_count_ == 1 || underrun_count_ % 100 == 0) {
          std::cout << "[UNDERRUN #" << underrun_count_ << "] "
                    << "Required: " << required_samples
                    << ", Available: " << available
                    << ", Prebuffered: " << (prebuffered_ ? "YES" : "NO")
                    << ", Record callbacks: " << record_callbacks_
                    << ", Playback callbacks: " << playback_callbacks_
                    << std::endl;
        }

        // If we've run out of data, reset prebuffering
        if (available < required_samples / 2) {
          if (prebuffered_) {
            std::cout << "[RESET] Buffer depleted, resetting prebuffer state"
                      << std::endl;
            prebuffered_ = false;
          }
        }
        return 0;
      }

      // Read from buffer
      for (size_t i = 0; i < required_samples; i++) {
        samples[i] = loopback_buffer_[read_pos_];
        read_pos_ = (read_pos_ + 1) % loopback_buffer_.size();
      }
      total_samples_played_ += required_samples;
    } else {
      // Fill with silence
      memset(audioSamples, 0, nSamples * nBytesPerSample * nChannels);
    }

    return 0;
  }

  void PullRenderData(int bits_per_sample,
                      int sample_rate,
                      size_t number_of_channels,
                      size_t number_of_frames,
                      void* audio_data,
                      int64_t* elapsed_time_ms,
                      int64_t* ntp_time_ms) override {}

  void StartRecording() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    recording_ = true;
  }

  void StopRecording() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    recording_ = false;
  }

  void StartPlaying() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    playing_ = true;
  }

  void StopPlaying() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    playing_ = false;
  }

  bool IsPrebuffered() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return prebuffered_;
  }

  size_t GetAvailableSamples() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return GetAvailableSamplesLocked();
  }

 private:
  size_t GetAvailableSamplesLocked() const {
    if (write_pos_ >= read_pos_) {
      return write_pos_ - read_pos_;
    } else {
      return loopback_buffer_.size() - read_pos_ + write_pos_;
    }
  }

  bool recording_;
  bool playing_;
  std::vector<int16_t> loopback_buffer_;
  size_t write_pos_ = 0;
  size_t read_pos_ = 0;
  mutable std::mutex buffer_mutex_;

  // Prebuffering to prevent initial underrun
  size_t prebuffer_samples_;
  bool prebuffered_;

  // Statistics and logging
  std::atomic<uint64_t> record_callbacks_;
  std::atomic<uint64_t> playback_callbacks_;
  std::atomic<uint64_t> underrun_count_;
  std::atomic<uint64_t> total_samples_recorded_;
  std::atomic<uint64_t> total_samples_played_;
  std::chrono::steady_clock::time_point last_log_time_;
  std::chrono::steady_clock::time_point last_record_callback_time_;
  std::chrono::steady_clock::time_point last_playback_callback_time_;
};

}  // namespace webrtc

int main(int argc, char* argv[]) {
  std::cout << "\n=== Audio Loopback Test for USB Audio Bug ===" << std::endl;
  std::cout << "This test captures audio and plays it back." << std::endl;
  std::cout << "You will HEAR if the pulsating bug is present.\n" << std::endl;

  // Create audio device
  auto audio_device = std::make_unique<webrtc::AudioDeviceMac>();

  if (audio_device->Init() != webrtc::AudioDeviceGeneric::InitStatus::OK) {
    std::cerr << "Failed to initialize audio device!" << std::endl;
    return 1;
  }

  // Create and attach audio buffer with loopback transport
  webrtc::Environment env = webrtc::CreateEnvironment();
  auto audio_buffer = std::make_unique<webrtc::AudioDeviceBuffer>(env);
  webrtc::LoopbackAudioTransport transport;
  audio_buffer->RegisterAudioCallback(&transport);
  audio_device->AttachAudioBuffer(audio_buffer.get());

  // List devices
  int16_t num_rec_devices = audio_device->RecordingDevices();
  int16_t num_play_devices = audio_device->PlayoutDevices();

  std::cout << "Recording devices: " << num_rec_devices << std::endl;
  for (int16_t i = 0; i < num_rec_devices; i++) {
    char name[webrtc::kAdmMaxDeviceNameSize];
    char guid[webrtc::kAdmMaxGuidSize];
    if (audio_device->RecordingDeviceName(i, name, guid) == 0) {
      std::cout << "  [" << i << "] " << name << std::endl;
    }
  }

  std::cout << "\nPlayout devices: " << num_play_devices << std::endl;
  for (int16_t i = 0; i < num_play_devices; i++) {
    char name[webrtc::kAdmMaxDeviceNameSize];
    char guid[webrtc::kAdmMaxGuidSize];
    if (audio_device->PlayoutDeviceName(i, name, guid) == 0) {
      std::cout << "  [" << i << "] " << name << std::endl;
    }
  }

  // Select devices
  int rec_device = 0, play_device = 0;
  if (argc > 1) {
    rec_device = std::atoi(argv[1]);
  }
  if (argc > 2) {
    play_device = std::atoi(argv[2]);
  }

  std::cout << "\nUsing recording device: " << rec_device << std::endl;
  std::cout << "Using playout device: " << play_device << std::endl;

  if (audio_device->SetRecordingDevice(rec_device) != 0) {
    std::cerr << "Failed to set recording device!" << std::endl;
    return 1;
  }

  if (audio_device->SetPlayoutDevice(play_device) != 0) {
    std::cerr << "Failed to set playout device!" << std::endl;
    return 1;
  }

  // Initialize
  std::cout << "\nInitializing recording and playout..." << std::endl;
  if (audio_device->InitRecording() != 0) {
    std::cerr << "Failed to initialize recording!" << std::endl;
    return 1;
  }

  if (audio_device->InitPlayout() != 0) {
    std::cerr << "Failed to initialize playout!" << std::endl;
    return 1;
  }

  // Start
  std::cout << "\n=== Starting Audio Loopback ===" << std::endl;
  std::cout << "You should hear your microphone input through speakers."
            << std::endl;
  std::cout << "Listen for the on/off pulsating pattern." << std::endl;
  std::cout << "Press Ctrl+C to stop.\n" << std::endl;

  if (audio_device->StartRecording() != 0) {
    std::cerr << "Failed to start recording!" << std::endl;
    return 1;
  }
  transport.StartRecording();

  // Wait for prebuffering to complete (max 5 seconds)
  std::cout << "Prebuffering audio..." << std::flush;
  for (int i = 0; i < 50; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (transport.IsPrebuffered()) {
      std::cout << " ready!" << std::endl;
      break;
    }
    if (i % 5 == 0) {
      std::cout << "." << std::flush;
    }
  }

  if (!transport.IsPrebuffered()) {
    std::cout << " timeout (continuing anyway)" << std::endl;
  }

  if (audio_device->StartPlayout() != 0) {
    std::cerr << "Failed to start playout!" << std::endl;
    return 1;
  }
  transport.StartPlaying();

  std::cout << "Loopback running..." << std::endl;
  std::cout << "\nIF YOU HEAR PULSATING: Bug still present" << std::endl;
  std::cout << "IF AUDIO IS CONTINUOUS: Bug is fixed!\n" << std::endl;

  // Run for 60 seconds
  for (int i = 0; i < 60; i++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "." << std::flush;
  }
  std::cout << std::endl;

  // Stop
  std::cout << "\nStopping..." << std::endl;
  transport.StopRecording();
  transport.StopPlaying();
  audio_device->StopRecording();
  audio_device->StopPlayout();

  std::cout << "Test complete." << std::endl;
  return 0;
}
