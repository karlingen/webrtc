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

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_BasicMixing) {
  // Test channel mixing from stereo Float32 to mono int16.
  const UInt32 num_frames = 4;
  const UInt32 num_channels = 2;

  Float32 input[] = {0.5f,   -0.5f,   // Frame 0: avg=(0.5-0.5)/2=0.0
                     0.0f,   0.0f,    // Frame 1: avg=(0.0+0.0)/2=0.0
                     1.0f,   -1.0f,   // Frame 2: avg=(1.0-1.0)/2=0.0
                     -0.25f, 0.25f};  // Frame 3: avg=(-0.25+0.25)/2=0.0

  std::vector<SInt16> output(num_frames);

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output.data(), num_frames,
                                            num_channels);

  // All frames should average to 0.
  EXPECT_EQ(output[0], 0);
  EXPECT_EQ(output[1], 0);
  EXPECT_EQ(output[2], 0);
  EXPECT_EQ(output[3], 0);
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_StereoMixing) {
  // Test averaging of stereo channels.
  const UInt32 num_frames = 3;
  const UInt32 num_channels = 2;

  Float32 input[] = {0.2f,  0.4f,    // Frame 0: avg=(0.2+0.4)/2=0.3
                     0.6f,  0.8f,    // Frame 1: avg=(0.6+0.8)/2=0.7
                     -0.2f, -0.6f};  // Frame 2: avg=(-0.2-0.6)/2=-0.4

  std::vector<SInt16> output(num_frames);

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output.data(), num_frames,
                                            num_channels);

  // Expected values after averaging channels.
  EXPECT_EQ(output[0], static_cast<SInt16>(0.3f * 32767.0f));
  EXPECT_EQ(output[1], static_cast<SInt16>(0.7f * 32767.0f));
  EXPECT_EQ(output[2], static_cast<SInt16>(-0.4f * 32767.0f));
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_ClampingPositive) {
  // Test that averaged values above 1.0 are clamped to 32767.
  const UInt32 num_frames = 3;
  const UInt32 num_channels = 2;

  Float32 input[] = {1.0f, 1.0f,   // Frame 0: avg=1.0 (at limit)
                     1.5f, 1.5f,   // Frame 1: avg=1.5 (above, clamped)
                     2.0f, 3.0f};  // Frame 2: avg=2.5 (above, clamped)
  std::vector<SInt16> output(num_frames);

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output.data(), num_frames,
                                            num_channels);

  // Frame 0 at limit, frames 1-2 clamped to max.
  EXPECT_EQ(output[0], 32767);
  EXPECT_EQ(output[1], 32767);
  EXPECT_EQ(output[2], 32767);
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_ClampingNegative) {
  // Test that averaged values below -1.0 are clamped to -32767.
  const UInt32 num_frames = 3;
  const UInt32 num_channels = 2;

  Float32 input[] = {-1.0f, -1.0f,   // Frame 0: avg=-1.0 (at limit)
                     -1.5f, -1.5f,   // Frame 1: avg=-1.5 (below, clamped)
                     -2.0f, -3.0f};  // Frame 2: avg=-2.5 (below, clamped)
  std::vector<SInt16> output(num_frames);

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output.data(), num_frames,
                                            num_channels);

  // All should be clamped to min representable value.
  EXPECT_EQ(output[0], -32767);
  EXPECT_EQ(output[1], -32767);
  EXPECT_EQ(output[2], -32767);
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_MultiChannel8) {
  // Test mixing of 8-channel input (e.g., Audient EVO8).
  const UInt32 num_frames = 2;
  const UInt32 num_channels = 8;

  // Frame 0: sum=3.6, avg=3.6/8=0.45
  // Frame 1: sum=-3.6, avg=-3.6/8=-0.45
  Float32 input[] = {
      0.1f,  0.2f,  0.3f,  0.4f,  0.5f,  0.6f,  0.7f,  0.8f,  // Frame 0
      -0.1f, -0.2f, -0.3f, -0.4f, -0.5f, -0.6f, -0.7f, -0.8f  // Frame 1
  };

  std::vector<SInt16> output(num_frames);

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output.data(), num_frames,
                                            num_channels);

  // Should average all 8 channels.
  EXPECT_EQ(output[0], static_cast<SInt16>(0.45f * 32767.0f));
  EXPECT_EQ(output[1], static_cast<SInt16>(-0.45f * 32767.0f));
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_SingleChannel) {
  // Test that single-channel input works (averaging of 1 channel = identity).
  const UInt32 num_frames = 3;
  const UInt32 num_channels = 1;

  Float32 input[] = {0.5f, -0.3f, 0.8f};
  std::vector<SInt16> output(num_frames);

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output.data(), num_frames,
                                            num_channels);

  // Single channel should pass through unchanged.
  EXPECT_EQ(output[0], static_cast<SInt16>(0.5f * 32767.0f));
  EXPECT_EQ(output[1], static_cast<SInt16>(-0.3f * 32767.0f));
  EXPECT_EQ(output[2], static_cast<SInt16>(0.8f * 32767.0f));
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_ZeroFrames) {
  // Test edge case with zero frames (no-op).
  const UInt32 num_frames = 0;
  const UInt32 num_channels = 2;

  Float32 input[] = {0.5f, -0.5f};
  SInt16 output[1] = {42};  // Sentinel value

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output, num_frames,
                                            num_channels);

  // Output should remain unchanged.
  EXPECT_EQ(output[0], 42);
}

TEST_F(AudioDeviceMacTest, ConvertFloat32ToInt16Mono_BoundaryValues) {
  // Test exact boundary values: -1.0, 0.0, 1.0.
  const UInt32 num_frames = 3;
  const UInt32 num_channels = 1;

  Float32 input[] = {-1.0f, 0.0f, 1.0f};
  std::vector<SInt16> output(num_frames);

  AudioDeviceMac::ConvertFloat32ToInt16Mono(input, output.data(), num_frames,
                                            num_channels);

  EXPECT_EQ(output[0], -32767);
  EXPECT_EQ(output[1], 0);
  EXPECT_EQ(output[2], 32767);
}

}  // namespace
}  // namespace webrtc
