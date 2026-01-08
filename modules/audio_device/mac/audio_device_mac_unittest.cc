/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_device/mac/audio_device_mac.h"

#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "test/gtest.h"

namespace webrtc {
namespace {

class AudioDeviceMacTest : public ::testing::Test {};

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_BasicConversion) {
  // Test basic conversion from stereo Float32 to mono int16.
  const UInt32 num_frames = 4;
  const UInt32 num_channels = 2;
  const UInt32 channel_to_extract = 0;

  Float32 input[] = {0.5f, -0.5f,   // Frame 0: L=0.5, R=-0.5
                     0.0f, 0.0f,     // Frame 1: L=0.0, R=0.0
                     1.0f, -1.0f,    // Frame 2: L=1.0, R=-1.0
                     -0.25f, 0.25f}; // Frame 3: L=-0.25, R=0.25

  SInt16 output[num_frames];

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output, num_frames,
                                            num_channels, channel_to_extract);

  // Expected values extracting left channel (channel 0).
  EXPECT_EQ(output[0], static_cast<SInt16>(0.5f * 32767.0f));
  EXPECT_EQ(output[1], 0);
  EXPECT_EQ(output[2], 32767);  // 1.0f should map to max int16
  EXPECT_EQ(output[3], static_cast<SInt16>(-0.25f * 32767.0f));
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_ExtractRightChannel) {
  // Test extracting right channel from stereo input.
  const UInt32 num_frames = 3;
  const UInt32 num_channels = 2;
  const UInt32 channel_to_extract = 1;  // Right channel

  Float32 input[] = {0.1f, 0.2f,    // Frame 0: L=0.1, R=0.2
                     0.3f, 0.4f,    // Frame 1: L=0.3, R=0.4
                     0.5f, 0.6f};   // Frame 2: L=0.5, R=0.6

  SInt16 output[num_frames];

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output, num_frames,
                                            num_channels, channel_to_extract);

  // Expected values extracting right channel (channel 1).
  EXPECT_EQ(output[0], static_cast<SInt16>(0.2f * 32767.0f));
  EXPECT_EQ(output[1], static_cast<SInt16>(0.4f * 32767.0f));
  EXPECT_EQ(output[2], static_cast<SInt16>(0.6f * 32767.0f));
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_ClampingPositive) {
  // Test that values above 1.0 are clamped to 32767.
  const UInt32 num_frames = 3;
  const UInt32 num_channels = 1;
  const UInt32 channel_to_extract = 0;

  Float32 input[] = {1.5f, 2.0f, 10.0f};  // All above 1.0
  SInt16 output[num_frames];

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output, num_frames,
                                            num_channels, channel_to_extract);

  // All should be clamped to max int16 value.
  EXPECT_EQ(output[0], 32767);
  EXPECT_EQ(output[1], 32767);
  EXPECT_EQ(output[2], 32767);
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_ClampingNegative) {
  // Test that values below -1.0 are clamped to -32767.
  const UInt32 num_frames = 3;
  const UInt32 num_channels = 1;
  const UInt32 channel_to_extract = 0;

  Float32 input[] = {-1.5f, -2.0f, -10.0f};  // All below -1.0
  SInt16 output[num_frames];

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output, num_frames,
                                            num_channels, channel_to_extract);

  // All should be clamped to min representable value in our scaling.
  // -1.0f * 32767.0f = -32767
  EXPECT_EQ(output[0], -32767);
  EXPECT_EQ(output[1], -32767);
  EXPECT_EQ(output[2], -32767);
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_MultiChannel8) {
  // Test extraction from 8-channel input (e.g., Audient EVO8).
  const UInt32 num_frames = 2;
  const UInt32 num_channels = 8;
  const UInt32 channel_to_extract = 3;  // Extract 4th channel

  Float32 input[] = {
      0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f,  // Frame 0
      -0.1f, -0.2f, -0.3f, -0.4f, -0.5f, -0.6f, -0.7f, -0.8f  // Frame 1
  };

  SInt16 output[num_frames];

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output, num_frames,
                                            num_channels, channel_to_extract);

  // Should extract channel 3 from each frame.
  EXPECT_EQ(output[0], static_cast<SInt16>(0.4f * 32767.0f));
  EXPECT_EQ(output[1], static_cast<SInt16>(-0.4f * 32767.0f));
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_OutOfRangeChannel) {
  // Test that out-of-range channel falls back to channel 0.
  const UInt32 num_frames = 2;
  const UInt32 num_channels = 2;
  const UInt32 channel_to_extract = 5;  // Invalid for 2-channel input

  Float32 input[] = {0.1f, 0.9f,    // Frame 0
                     0.2f, 0.8f};   // Frame 1

  SInt16 output[num_frames];

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output, num_frames,
                                            num_channels, channel_to_extract);

  // Should fall back to channel 0.
  EXPECT_EQ(output[0], static_cast<SInt16>(0.1f * 32767.0f));
  EXPECT_EQ(output[1], static_cast<SInt16>(0.2f * 32767.0f));
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_ZeroFrames) {
  // Test edge case with zero frames (no-op).
  const UInt32 num_frames = 0;
  const UInt32 num_channels = 2;
  const UInt32 channel_to_extract = 0;

  Float32 input[] = {0.5f, -0.5f};
  SInt16 output[1] = {42};  // Sentinel value

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output, num_frames,
                                            num_channels, channel_to_extract);

  // Output should remain unchanged.
  EXPECT_EQ(output[0], 42);
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_BoundaryValues) {
  // Test exact boundary values: -1.0, 0.0, 1.0.
  const UInt32 num_frames = 3;
  const UInt32 num_channels = 1;
  const UInt32 channel_to_extract = 0;

  Float32 input[] = {-1.0f, 0.0f, 1.0f};
  SInt16 output[num_frames];

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output, num_frames,
                                            num_channels, channel_to_extract);

  EXPECT_EQ(output[0], -32767);
  EXPECT_EQ(output[1], 0);
  EXPECT_EQ(output[2], 32767);
}

}  // namespace
}  // namespace webrtc
