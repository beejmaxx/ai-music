#include "music/audio.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

class ConstantSource final : public music::Source {
 public:
  bool stall = false;
  std::uint64_t reads = 0;
  bool read(float* left, float* right, std::size_t count) noexcept override {
    ++reads;
    // Deliberately violate the callback deadline in the overrun regression.
    if (stall) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::fill_n(left, count, .25f);
    std::fill_n(right, count, -.25f);
    return true;
  }
  void style(const std::string&) override {}
  void drums(bool) override {}
  void tempo(float) override {}
  void temperature(float) override {}
  const char* name() const override { return "callback test"; }
};

struct CallbackBuffer {
  std::array<float, 1024> samples;
  AudioBufferList data{};
  CallbackBuffer() {
    samples.fill(1);
    data.mNumberBuffers = 1;
    data.mBuffers[0] = {2, sizeof(samples), samples.data()};
  }
};

AudioTimeStamp timestamp(double sample) {
  AudioTimeStamp value{};
  value.mFlags = kAudioTimeStampSampleTimeValid;
  value.mSampleTime = sample;
  return value;
}

void exact_limit_and_silent_hardware() {
  ConstantSource source;
  const auto path = std::filesystem::temp_directory_path() /
      ("music-callback-" + std::to_string(getpid()) + "-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
  music::Recorder recorder(path);
  music::Engine engine(source, &recorder);
  music::AudioCallbackState callback(engine, true, 600);
  CallbackBuffer buffer;
  auto stamp = timestamp(0);
  AudioUnitRenderActionFlags flags = 0;
  check(callback.render(&stamp, 512, &buffer.data, &flags) == noErr, "First callback failed");
  check(engine.frames() == 512 && !callback.done(), "Callback stopped before its frame limit");
  check(std::all_of(buffer.samples.begin(), buffer.samples.end(), [](float value) { return value == 0; }),
        "Silent validation sent audio to the hardware buffer");
  check(flags & kAudioUnitRenderAction_OutputIsSilence, "Silent hardware output was not flagged");
  stamp.mSampleTime = 512;
  check(callback.render(&stamp, 512, &buffer.data) == noErr, "Final partial callback failed");
  check(engine.frames() == 600 && callback.done(), "Final callback exceeded or missed the exact frame limit");
  // CoreAudio's timestamp still advances by all requested frames, including
  // hardware silence after the requested recording has finished.
  stamp.mSampleTime = 1024;
  buffer.samples.fill(1);
  check(callback.render(&stamp, 512, &buffer.data) == noErr && engine.frames() == 600,
        "Callback generated extra audio after completion");
  check(std::all_of(buffer.samples.begin(), buffer.samples.end(), [](float value) { return value == 0; }),
        "Completed callback left stale audio in the hardware buffer");
  check(callback.metrics().timestamp_discontinuities == 0 && callback.metrics().callbacks == 3,
        "Partial recording changed the hardware timestamp sequence");
  recorder.finish();
  check(!recorder.failed() && recorder.written() == 600 && recorder.dropped() == 0,
        "Recording did not contain exactly the requested rendered frames");
  std::ifstream input(path, std::ios::binary);
  const std::vector<unsigned char> wav((std::istreambuf_iterator<char>(input)), {});
  check(wav.size() == 44 + 600 * 4, "Recording length did not match callback limit");
  check(std::any_of(wav.begin() + 44, wav.end(), [](unsigned char value) { return value != 0; }),
        "Silencing hardware also silenced the recording");
  std::filesystem::remove(path);
}

void audible_output_and_partial_tail() {
  ConstantSource source;
  music::Engine engine(source);
  music::AudioCallbackState callback(engine, false, 100);
  CallbackBuffer buffer;
  const auto stamp = timestamp(0);
  check(callback.render(&stamp, 512, &buffer.data) == noErr && callback.done(), "Audible callback failed");
  check(std::any_of(buffer.samples.begin(), buffer.samples.begin() + 200,
                    [](float value) { return value != 0; }), "Default output became silent");
  check(std::all_of(buffer.samples.begin() + 200, buffer.samples.end(),
                    [](float value) { return value == 0; }), "Partial callback left its tail uninitialized");
}

void warmup_activation_and_recording() {
  ConstantSource source;
  const auto path = std::filesystem::temp_directory_path() /
      ("music-warmup-" + std::to_string(getpid()) + "-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
  music::Recorder recorder(path);
  music::Engine engine(source, &recorder);
  music::AudioCallbackState callback(engine, false, 600, false);
  CallbackBuffer buffer;
  check(!callback.active() && !callback.done(), "Inactive output already started or completed");
  for (double sample : {0., 512.}) {
    const auto stamp = timestamp(sample);
    buffer.samples.fill(1);
    AudioUnitRenderActionFlags flags = 0;
    check(callback.render(&stamp, 512, &buffer.data, &flags) == noErr, "Warmup callback failed");
    check(std::all_of(buffer.samples.begin(), buffer.samples.end(), [](float value) { return value == 0; }) &&
          (flags & kAudioUnitRenderAction_OutputIsSilence), "Warmup did not emit flagged hardware silence");
    callback.note_device_overload();
  }
  check(source.reads == 0 && engine.frames() == 0 && recorder.written() == 0 && !callback.done(),
        "Warmup consumed source audio, recorded silence, or advanced the frame limit");
  callback.activate();
  callback.note_device_overload();
  check(!callback.active(), "Activation changed phase before callback entry");
  auto stamp = timestamp(1024);
  check(callback.render(&stamp, 512, &buffer.data) == noErr && callback.active() && !callback.done(),
        "First active callback did not take the requested transition");
  check(engine.frames() == 512 && source.reads != 0, "Active callback did not consume the source");
  callback.note_device_overload();
  callback.activate();  // Repeated requests cannot reset the recording or counters.
  stamp.mSampleTime = 1536;
  callback.render(&stamp, 512, &buffer.data);
  check(callback.done() && engine.frames() == 600, "Warmup changed the exact active frame limit");
  callback.note_device_overload();
  stamp.mSampleTime = 2048;
  callback.render(&stamp, 512, &buffer.data);
  const auto metrics = callback.metrics();
  check(metrics.active_started && metrics.callbacks == 5 && metrics.warmup.callbacks == 2 &&
        metrics.active.callbacks == 3 && metrics.timestamp_discontinuities == 0,
        "Activation reset or misplaced callback/timestamp counters");
  check(metrics.device_overloads == 5 && metrics.warmup.device_overloads == 3 &&
        metrics.active.device_overloads == 2, "Overload notifications used the wrong activation boundary");
  check(metrics.max_callback_ms == std::max(metrics.warmup.max_callback_ms, metrics.active.max_callback_ms),
        "Cumulative callback maximum did not preserve both phases");
  recorder.finish();
  check(recorder.written() == 600 && recorder.dropped() == 0 && !recorder.failed(),
        "Warmup leaked frames into the recording");
  check(std::filesystem::file_size(path) == 44 + 600 * 4, "Warmup changed recorded file duration");
  std::filesystem::remove(path);

  music::AudioCallbackState empty(engine, false, 0, false);
  empty.render(&stamp, 512, &buffer.data);
  check(!empty.done(), "Zero-frame inactive output completed before activation");
  empty.activate();
  empty.render(&stamp, 512, &buffer.data);
  check(empty.done() && engine.frames() == 600, "Zero-frame activation consumed source audio");
}

void phase_errors_and_overload_race() {
  ConstantSource source;
  music::Engine engine(source);
  music::AudioCallbackState callback(engine, false, UINT64_MAX, false);
  CallbackBuffer buffer;
  auto stamp = timestamp(0);
  callback.render(&stamp, 48, &buffer.data);
  stamp.mSampleTime = 50;  // Warmup gap and malformed output are still counted.
  buffer.data.mBuffers[0].mDataByteSize = sizeof(float);
  check(callback.render(&stamp, 48, &buffer.data) == kAudio_ParamError,
        "Warmup accepted malformed callback data");
  buffer.data.mBuffers[0].mDataByteSize = sizeof(buffer.samples);
  callback.render(nullptr, 48, &buffer.data);
  stamp.mSampleTime = 144;
  callback.render(&stamp, 48, &buffer.data);
  check(source.reads == 0, "Warmup error handling consumed source audio");

  // Exercise simultaneous notification/activation without depending on which
  // thread wins. Each event must appear exactly once in one of the two phases.
  constexpr std::uint64_t notifications = 10000;
  std::thread notifier([&] {
    for (std::uint64_t i = 0; i < notifications; ++i) callback.note_device_overload();
  });
  callback.activate();
  stamp.mSampleTime = 200;  // Gap from last warmup callback belongs to active.
  callback.render(&stamp, 48, &buffer.data);
  notifier.join();
  callback.note_device_overload();
  stamp.mFlags = 0;
  buffer.data.mBuffers[0].mDataByteSize = sizeof(float);
  check(callback.render(&stamp, 48, &buffer.data) == kAudio_ParamError,
        "Active phase accepted malformed callback data");
  source.stall = true;
  stamp = timestamp(296);
  buffer.data.mBuffers[0].mDataByteSize = sizeof(buffer.samples);
  callback.render(&stamp, 48, &buffer.data);
  const auto metrics = callback.metrics();
  check(metrics.callbacks == 7 && metrics.warmup.callbacks == 4 && metrics.active.callbacks == 3,
        "Phase callback counters were reset or misattributed");
  check(metrics.timestamp_discontinuities == 2 && metrics.warmup.timestamp_discontinuities == 1 &&
        metrics.active.timestamp_discontinuities == 1, "Activation concealed a transition timestamp gap");
  check(metrics.invalid_timestamps == 2 && metrics.warmup.invalid_timestamps == 1 &&
        metrics.active.invalid_timestamps == 1 && metrics.callback_errors == 2 &&
        metrics.warmup.callback_errors == 1 && metrics.active.callback_errors == 1,
        "Warmup/active malformed timestamps or buffers were not independently counted");
  check(metrics.active.callback_overruns >= 1 && metrics.active.max_callback_ms > 1 &&
        metrics.callback_overruns == metrics.warmup.callback_overruns + metrics.active.callback_overruns,
        "Active render deadline violations were lost across activation");
  check(metrics.device_overloads == notifications + 1 && metrics.active.device_overloads >= 1 &&
        metrics.device_overloads == metrics.warmup.device_overloads + metrics.active.device_overloads,
        "Concurrent activation lost, duplicated, or concealed device overloads");
}

void timestamps_errors_and_overruns() {
  ConstantSource source;
  music::Engine engine(source);
  music::AudioCallbackState callback(engine);
  CallbackBuffer buffer;
  for (double sample : {100., 164., 230.}) {
    const auto stamp = timestamp(sample);
    check(callback.render(&stamp, 64, &buffer.data) == noErr, "Timestamp callback failed");
  }
  auto invalid = timestamp(294);
  invalid.mFlags = 0;
  callback.render(&invalid, 64, &buffer.data);
  callback.render(nullptr, 64, &buffer.data);
  invalid = timestamp(std::numeric_limits<double>::quiet_NaN());
  callback.render(&invalid, 64, &buffer.data);
  auto stamp = timestamp(1000);
  callback.render(&stamp, 64, &buffer.data);
  auto metrics = callback.metrics();
  check(metrics.timestamp_discontinuities == 1 && metrics.invalid_timestamps == 3,
        "Timestamp jumps or missing sample times were counted incorrectly");
  const auto frames = engine.frames();
  stamp.mSampleTime += 64;
  buffer.data.mBuffers[0].mDataByteSize = sizeof(float);
  check(callback.render(&stamp, 64, &buffer.data) == kAudio_ParamError,
        "Undersized hardware buffer was accepted");
  check(callback.metrics().callback_errors == 1 && engine.frames() == frames,
        "Malformed callback was not counted or still consumed audio");

  ConstantSource stalled_source;
  stalled_source.stall = true;
  music::Engine stalled_engine(stalled_source);
  music::AudioCallbackState stalled(stalled_engine);
  CallbackBuffer stalled_buffer;
  stamp = timestamp(0);
  stalled.render(&stamp, 48, &stalled_buffer.data);  // One millisecond budget.
  check(stalled.metrics().callback_overruns == 1 && stalled.metrics().max_callback_ms > 1,
        "A callback that exceeded its hardware block duration was not detected");
}
}  // namespace

int main() {
  try {
    exact_limit_and_silent_hardware();
    audible_output_and_partial_tail();
    warmup_activation_and_recording();
    phase_errors_and_overload_race();
    timestamps_errors_and_overruns();
    std::cout << "PASS: native callback limits, warmup activation, silence, phase metrics, timestamps, errors, and overruns.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
