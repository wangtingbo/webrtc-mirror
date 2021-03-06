/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <numeric>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "webrtc/base/array_view.h"
#include "webrtc/base/random.h"
#include "webrtc/modules/audio_processing/audio_buffer.h"
#include "webrtc/modules/audio_processing/include/audio_processing.h"
#include "webrtc/modules/audio_processing/level_controller/level_controller.h"
#include "webrtc/modules/audio_processing/test/audio_buffer_tools.h"
#include "webrtc/modules/audio_processing/test/bitexactness_tools.h"
#include "webrtc/system_wrappers/include/clock.h"
#include "webrtc/test/testsupport/perf_test.h"

namespace webrtc {
namespace {

const size_t kNumFramesToProcess = 100;

struct SimulatorBuffers {
  SimulatorBuffers(int render_input_sample_rate_hz,
                   int capture_input_sample_rate_hz,
                   int render_output_sample_rate_hz,
                   int capture_output_sample_rate_hz,
                   size_t num_render_input_channels,
                   size_t num_capture_input_channels,
                   size_t num_render_output_channels,
                   size_t num_capture_output_channels) {
    Random rand_gen(42);
    CreateConfigAndBuffer(render_input_sample_rate_hz,
                          num_render_input_channels, &rand_gen,
                          &render_input_buffer, &render_input_config,
                          &render_input, &render_input_samples);

    CreateConfigAndBuffer(render_output_sample_rate_hz,
                          num_render_output_channels, &rand_gen,
                          &render_output_buffer, &render_output_config,
                          &render_output, &render_output_samples);

    CreateConfigAndBuffer(capture_input_sample_rate_hz,
                          num_capture_input_channels, &rand_gen,
                          &capture_input_buffer, &capture_input_config,
                          &capture_input, &capture_input_samples);

    CreateConfigAndBuffer(capture_output_sample_rate_hz,
                          num_capture_output_channels, &rand_gen,
                          &capture_output_buffer, &capture_output_config,
                          &capture_output, &capture_output_samples);

    UpdateInputBuffers();
  }

  void CreateConfigAndBuffer(int sample_rate_hz,
                             size_t num_channels,
                             Random* rand_gen,
                             std::unique_ptr<AudioBuffer>* buffer,
                             StreamConfig* config,
                             std::vector<float*>* buffer_data,
                             std::vector<float>* buffer_data_samples) {
    int samples_per_channel = rtc::CheckedDivExact(sample_rate_hz, 100);
    *config = StreamConfig(sample_rate_hz, num_channels, false);
    buffer->reset(new AudioBuffer(config->num_frames(), config->num_channels(),
                                  config->num_frames(), config->num_channels(),
                                  config->num_frames()));

    buffer_data_samples->resize(samples_per_channel * num_channels);
    for (auto& v : *buffer_data_samples) {
      v = rand_gen->Rand<float>();
    }

    buffer_data->resize(num_channels);
    for (size_t ch = 0; ch < num_channels; ++ch) {
      (*buffer_data)[ch] = &(*buffer_data_samples)[ch * samples_per_channel];
    }
  }

  void UpdateInputBuffers() {
    test::CopyVectorToAudioBuffer(capture_input_config, capture_input_samples,
                                  capture_input_buffer.get());
    test::CopyVectorToAudioBuffer(render_input_config, render_input_samples,
                                  render_input_buffer.get());
  }

  std::unique_ptr<AudioBuffer> render_input_buffer;
  std::unique_ptr<AudioBuffer> capture_input_buffer;
  std::unique_ptr<AudioBuffer> render_output_buffer;
  std::unique_ptr<AudioBuffer> capture_output_buffer;
  StreamConfig render_input_config;
  StreamConfig capture_input_config;
  StreamConfig render_output_config;
  StreamConfig capture_output_config;
  std::vector<float*> render_input;
  std::vector<float> render_input_samples;
  std::vector<float*> capture_input;
  std::vector<float> capture_input_samples;
  std::vector<float*> render_output;
  std::vector<float> render_output_samples;
  std::vector<float*> capture_output;
  std::vector<float> capture_output_samples;
};

class SubmodulePerformanceTimer {
 public:
  SubmodulePerformanceTimer() : clock_(webrtc::Clock::GetRealTimeClock()) {
    timestamps_us_.reserve(kNumFramesToProcess);
  }

  void StartTimer() {
    start_timestamp_us_ = rtc::Optional<int64_t>(clock_->TimeInMicroseconds());
  }
  void StopTimer() {
    RTC_DCHECK(start_timestamp_us_);
    timestamps_us_.push_back(clock_->TimeInMicroseconds() -
                             *start_timestamp_us_);
  }

  double GetDurationAverage() const {
    RTC_DCHECK(!timestamps_us_.empty());
    return static_cast<double>(std::accumulate(timestamps_us_.begin(),
                                               timestamps_us_.end(), 0)) /
           timestamps_us_.size();
  }

  double GetDurationStandardDeviation() const {
    RTC_DCHECK(!timestamps_us_.empty());
    double average_duration = GetDurationAverage();

    int64_t variance =
        std::accumulate(timestamps_us_.begin(), timestamps_us_.end(), 0,
                        [average_duration](const int64_t& a, const int64_t& b) {
                          return a + (b - average_duration);
                        });

    return sqrt(variance / timestamps_us_.size());
  }

 private:
  webrtc::Clock* clock_;
  rtc::Optional<int64_t> start_timestamp_us_;
  std::vector<int64_t> timestamps_us_;
};

std::string FormPerformanceMeasureString(
    const SubmodulePerformanceTimer& timer) {
  std::string s = std::to_string(timer.GetDurationAverage());
  s += ", ";
  s += std::to_string(timer.GetDurationStandardDeviation());
  return s;
}

void RunStandaloneSubmodule(int sample_rate_hz, size_t num_channels) {
  SimulatorBuffers buffers(sample_rate_hz, sample_rate_hz, sample_rate_hz,
                           sample_rate_hz, num_channels, num_channels,
                           num_channels, num_channels);
  SubmodulePerformanceTimer timer;

  LevelController level_controller;
  level_controller.Initialize(sample_rate_hz);

  for (size_t frame_no = 0; frame_no < kNumFramesToProcess; ++frame_no) {
    buffers.UpdateInputBuffers();

    timer.StartTimer();
    level_controller.Process(buffers.capture_input_buffer.get());
    timer.StopTimer();
  }
  webrtc::test::PrintResultMeanAndError(
      "level_controller_call_durations",
      "_" + std::to_string(sample_rate_hz) + "Hz_" +
          std::to_string(num_channels) + "_channels",
      "StandaloneLevelControl", FormPerformanceMeasureString(timer), "us",
      false);
}

void RunTogetherWithApm(std::string test_description,
                        int render_input_sample_rate_hz,
                        int render_output_sample_rate_hz,
                        int capture_input_sample_rate_hz,
                        int capture_output_sample_rate_hz,
                        size_t num_channels,
                        bool use_mobile_aec,
                        bool include_default_apm_processing) {
  SimulatorBuffers buffers(
      render_input_sample_rate_hz, capture_input_sample_rate_hz,
      render_output_sample_rate_hz, capture_output_sample_rate_hz, num_channels,
      num_channels, num_channels, num_channels);
  SubmodulePerformanceTimer render_timer;
  SubmodulePerformanceTimer capture_timer;
  SubmodulePerformanceTimer total_timer;

  webrtc::Config config;
  AudioProcessing::Config apm_config;
  if (include_default_apm_processing) {
    config.Set<DelayAgnostic>(new DelayAgnostic(true));
    config.Set<ExtendedFilter>(new ExtendedFilter(true));
  }
  apm_config.level_controller.enabled = true;

  std::unique_ptr<AudioProcessing> apm;
  apm.reset(AudioProcessing::Create(config));
  ASSERT_TRUE(apm.get());
  apm->ApplyConfig(apm_config);

  ASSERT_EQ(AudioProcessing::kNoError,
            apm->gain_control()->Enable(include_default_apm_processing));
  if (use_mobile_aec) {
    ASSERT_EQ(AudioProcessing::kNoError,
              apm->echo_cancellation()->Enable(false));
    ASSERT_EQ(AudioProcessing::kNoError, apm->echo_control_mobile()->Enable(
                                             include_default_apm_processing));
  } else {
    ASSERT_EQ(AudioProcessing::kNoError,
              apm->echo_cancellation()->Enable(include_default_apm_processing));
    ASSERT_EQ(AudioProcessing::kNoError,
              apm->echo_control_mobile()->Enable(false));
  }
  ASSERT_EQ(AudioProcessing::kNoError,
            apm->high_pass_filter()->Enable(include_default_apm_processing));
  ASSERT_EQ(AudioProcessing::kNoError,
            apm->noise_suppression()->Enable(include_default_apm_processing));
  ASSERT_EQ(AudioProcessing::kNoError,
            apm->voice_detection()->Enable(include_default_apm_processing));
  ASSERT_EQ(AudioProcessing::kNoError,
            apm->level_estimator()->Enable(include_default_apm_processing));

  StreamConfig render_input_config(render_input_sample_rate_hz, num_channels,
                                   false);
  StreamConfig render_output_config(render_output_sample_rate_hz, num_channels,
                                    false);
  StreamConfig capture_input_config(capture_input_sample_rate_hz, num_channels,
                                    false);
  StreamConfig capture_output_config(capture_output_sample_rate_hz,
                                     num_channels, false);

  for (size_t frame_no = 0; frame_no < kNumFramesToProcess; ++frame_no) {
    buffers.UpdateInputBuffers();

    total_timer.StartTimer();
    render_timer.StartTimer();
    ASSERT_EQ(AudioProcessing::kNoError,
              apm->ProcessReverseStream(
                  &buffers.render_input[0], render_input_config,
                  render_output_config, &buffers.render_output[0]));

    render_timer.StopTimer();

    capture_timer.StartTimer();
    ASSERT_EQ(AudioProcessing::kNoError, apm->set_stream_delay_ms(0));
    ASSERT_EQ(
        AudioProcessing::kNoError,
        apm->ProcessStream(&buffers.capture_input[0], capture_input_config,
                           capture_output_config, &buffers.capture_output[0]));

    capture_timer.StopTimer();
    total_timer.StopTimer();
  }

  webrtc::test::PrintResultMeanAndError(
      "level_controller_call_durations",
      "_" + std::to_string(render_input_sample_rate_hz) + "_" +
          std::to_string(render_output_sample_rate_hz) + "_" +
          std::to_string(capture_input_sample_rate_hz) + "_" +
          std::to_string(capture_output_sample_rate_hz) + "Hz_" +
          std::to_string(num_channels) + "_channels" + "_render",
      test_description, FormPerformanceMeasureString(render_timer), "us",
      false);
  webrtc::test::PrintResultMeanAndError(
      "level_controller_call_durations",
      "_" + std::to_string(render_input_sample_rate_hz) + "_" +
          std::to_string(render_output_sample_rate_hz) + "_" +
          std::to_string(capture_input_sample_rate_hz) + "_" +
          std::to_string(capture_output_sample_rate_hz) + "Hz_" +
          std::to_string(num_channels) + "_channels" + "_capture",
      test_description, FormPerformanceMeasureString(capture_timer), "us",
      false);
  webrtc::test::PrintResultMeanAndError(
      "level_controller_call_durations",
      "_" + std::to_string(render_input_sample_rate_hz) + "_" +
          std::to_string(render_output_sample_rate_hz) + "_" +
          std::to_string(capture_input_sample_rate_hz) + "_" +
          std::to_string(capture_output_sample_rate_hz) + "Hz_" +
          std::to_string(num_channels) + "_channels" + "_total",
      test_description, FormPerformanceMeasureString(total_timer), "us", false);
}

}  // namespace

TEST(LevelControllerPerformanceTest, StandaloneProcessing) {
  int sample_rates_to_test[] = {
      AudioProcessing::kSampleRate8kHz, AudioProcessing::kSampleRate16kHz,
      AudioProcessing::kSampleRate32kHz, AudioProcessing::kSampleRate48kHz};
  for (auto sample_rate : sample_rates_to_test) {
    for (size_t num_channels = 1; num_channels <= 2; ++num_channels) {
      RunStandaloneSubmodule(sample_rate, num_channels);
    }
  }
}

#if !defined(WEBRTC_ANDROID)
TEST(LevelControllerPerformanceTest, ProcessingViaApm) {
#else
TEST(LevelControllerPerformanceTest, DISABLED_ProcessingViaApm) {
#endif
  int sample_rates_to_test[] = {AudioProcessing::kSampleRate8kHz,
                                AudioProcessing::kSampleRate16kHz,
                                AudioProcessing::kSampleRate32kHz,
                                AudioProcessing::kSampleRate48kHz, 44100};
  for (auto capture_input_sample_rate_hz : sample_rates_to_test) {
    for (auto capture_output_sample_rate_hz : sample_rates_to_test) {
      for (size_t num_channels = 1; num_channels <= 2; ++num_channels) {
        RunTogetherWithApm("SimpleLevelControlViaApm", 48000, 48000,
                           capture_input_sample_rate_hz,
                           capture_output_sample_rate_hz, num_channels, false,
                           false);
      }
    }
  }
}

#if !defined(WEBRTC_ANDROID)
TEST(LevelControllerPerformanceTest, InteractionWithDefaultApm) {
#else
TEST(LevelControllerPerformanceTest, DISABLED_InteractionWithDefaultApm) {
#endif
  int sample_rates_to_test[] = {AudioProcessing::kSampleRate8kHz,
                                AudioProcessing::kSampleRate16kHz,
                                AudioProcessing::kSampleRate32kHz,
                                AudioProcessing::kSampleRate48kHz, 44100};
  for (auto capture_input_sample_rate_hz : sample_rates_to_test) {
    for (auto capture_output_sample_rate_hz : sample_rates_to_test) {
      for (size_t num_channels = 1; num_channels <= 2; ++num_channels) {
        RunTogetherWithApm("LevelControlAndDefaultDesktopApm", 48000, 48000,
                           capture_input_sample_rate_hz,
                           capture_output_sample_rate_hz, num_channels, false,
                           true);
        RunTogetherWithApm("LevelControlAndDefaultMobileApm", 48000, 48000,
                           capture_input_sample_rate_hz,
                           capture_output_sample_rate_hz, num_channels, true,
                           true);
      }
    }
  }
}

}  // namespace webrtc
