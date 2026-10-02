#include "music/audio.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>

namespace music {
void Engine::write_control(const Control& control) noexcept {
  switch (control.parameter) {
    case Parameter::volume: volume(control.value); break;
    case Parameter::filter: filter(control.value); break;
    case Parameter::delay: delay(control.value); break;
    case Parameter::mute: mute(control.value != 0); break;
    case Parameter::tempo: tempo(control.value); source_.synth_control(control); break;
    default: source_.synth_control(control); break;
  }
}
float Engine::read_control(Parameter parameter) const noexcept {
  switch (parameter) {
    case Parameter::volume: return volume();
    case Parameter::filter: return effects_.filter();
    case Parameter::delay: return effects_.delay();
    case Parameter::mute: return muted();
    default: return source_.synth_value(parameter);
  }
}
void Engine::render(float* out, std::size_t count, bool offline) noexcept {
  std::array<float, block_size> left{}, right{};
  std::array<float, block_size> dry_left{}, dry_right{};
  std::array<StereoFrame, block_size> recording{};
  float peak = 0;
  std::uint64_t invalid = 0;
  const auto chunk = source_.is_synth() ? std::size_t(64) : block_size;
  for (std::size_t offset = 0; offset < count;) {
    if (source_.is_synth()) score_.tick(source_.beat(), *this);
    effects_.begin_block();
    const auto n = std::min({chunk, count - offset, source_.frames_to_tick()});
    const bool ready = offline ? source_.read_offline(left.data(), right.data(), n)
                               : source_.read_buses(left.data(), right.data(), dry_left.data(), dry_right.data(), n);
    if (!ready) underruns_.fetch_add(1, std::memory_order_relaxed);
    const float target = muted_.load(std::memory_order_relaxed) ? 0 : volume_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i) {
      gain_ += (target - gain_) * (1.0f / 480.0f);
      auto clean = [&](float value) {
        if (!std::isfinite(value)) { ++invalid; return 0.0f; }
        return std::clamp(value, -4.0f, 4.0f);
      };
      const auto wet = effects_.process(clean(left[i]), clean(right[i]));
      auto bus_l = wet.left + clean(dry_left[i]);
      auto bus_r = wet.right + clean(dry_right[i]);
      if (source_.is_synth() && !offline) {
        bus_l = std::tanh(bus_l * 1.1f) * .85f;
        bus_r = std::tanh(bus_r * 1.1f) * .85f;
      }
      const auto l = std::clamp(bus_l * gain_, -1.0f, 1.0f);
      const auto r = std::clamp(bus_r * gain_, -1.0f, 1.0f);
      out[2 * (offset + i)] = l;
      out[2 * (offset + i) + 1] = r;
      recording[i] = {l, r};
      peak = std::max({peak, std::abs(l), std::abs(r)});
    }
    if (recorder_) recorder_->push(recording.data(), n);
    if (stream_) stream_->push(recording.data(), n);
    offset += n;
  }
  peak_.store(peak, std::memory_order_relaxed);
  invalid_samples_.fetch_add(invalid, std::memory_order_relaxed);
  frames_.fetch_add(count, std::memory_order_relaxed);
}

namespace {
void check(OSStatus code, const char* operation) {
  if (code != noErr) throw std::runtime_error(std::string(operation) + " failed (CoreAudio " + std::to_string(code) + ")");
}
constexpr AudioObjectPropertyAddress overload_address{
  kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}  // namespace

OSStatus AudioCallbackState::render(const AudioTimeStamp* timestamp, UInt32 count, AudioBufferList* data,
                                    AudioUnitRenderActionFlags* flags) noexcept {
  const auto started = std::chrono::steady_clock::now();
  if (!callback_active_ && activation_requested_.load(std::memory_order_acquire)) {
    phase_and_warmup_overloads_.fetch_or(active_bit, std::memory_order_acq_rel);
    callback_active_ = true;
  }
  auto& counters = callback_active_ ? active_ : warmup_;
  counters.callbacks.fetch_add(1, std::memory_order_relaxed);
  const auto finish = [&](OSStatus status) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (std::uint64_t(elapsed) > counters.max_callback_ns.load(std::memory_order_relaxed))
      counters.max_callback_ns.store(elapsed, std::memory_order_relaxed);
    if (count && double(elapsed) > double(count) * 1e9 / sample_rate)
      counters.callback_overruns.fetch_add(1, std::memory_order_relaxed);
    return status;
  };
  if (!timestamp || !(timestamp->mFlags & kAudioTimeStampSampleTimeValid) ||
      !std::isfinite(timestamp->mSampleTime)) {
    counters.invalid_timestamps.fetch_add(1, std::memory_order_relaxed);
    have_timestamp_ = false;
  } else {
    if (have_timestamp_ && std::abs(timestamp->mSampleTime - next_sample_time_) > .5)
      counters.timestamp_discontinuities.fetch_add(1, std::memory_order_relaxed);
    next_sample_time_ = timestamp->mSampleTime + count;
    have_timestamp_ = true;
  }
  if (!data || data->mNumberBuffers != 1 || !data->mBuffers[0].mData ||
      data->mBuffers[0].mNumberChannels != 2 ||
      data->mBuffers[0].mDataByteSize < std::size_t(count) * sizeof(float) * 2) {
    counters.callback_errors.fetch_add(1, std::memory_order_relaxed);
    return finish(kAudio_ParamError);
  }
  auto* output = static_cast<float*>(data->mBuffers[0].mData);
  const auto rendered = callback_active_ ? std::min<std::uint64_t>(count, frame_limit_ - rendered_frames_) : 0;
  if (rendered) engine_.render(output, rendered);
  rendered_frames_ += rendered;
  // Record/stream the rendered audio before silencing only the hardware buffer.
  const auto first_silent = silent_output_ ? 0 : rendered;
  std::fill(output + first_silent * 2, output + std::size_t(count) * 2, 0.f);
  if (flags && (silent_output_ || !rendered)) *flags |= kAudioUnitRenderAction_OutputIsSilence;

  if (callback_active_ && rendered_frames_ == frame_limit_) done_.store(true, std::memory_order_release);
  return finish(noErr);
}

void AudioCallbackState::note_device_overload() noexcept {
  auto phase = phase_and_warmup_overloads_.load(std::memory_order_acquire);
  while (!(phase & active_bit)) {
    if (phase_and_warmup_overloads_.compare_exchange_weak(phase, phase + 1,
          std::memory_order_acq_rel, std::memory_order_acquire)) return;
  }
  active_device_overloads_.fetch_add(1, std::memory_order_relaxed);
}

AudioPhaseMetrics AudioCallbackState::PhaseCounters::snapshot() const noexcept {
  AudioPhaseMetrics result;
  result.callbacks = callbacks.load(std::memory_order_relaxed);
  result.timestamp_discontinuities = timestamp_discontinuities.load(std::memory_order_relaxed);
  result.invalid_timestamps = invalid_timestamps.load(std::memory_order_relaxed);
  result.callback_overruns = callback_overruns.load(std::memory_order_relaxed);
  result.callback_errors = callback_errors.load(std::memory_order_relaxed);
  result.max_callback_ms = double(max_callback_ns.load(std::memory_order_relaxed)) / 1e6;
  return result;
}

AudioOutputMetrics AudioCallbackState::metrics() const noexcept {
  AudioOutputMetrics result;
  result.warmup = warmup_.snapshot();
  result.active = active_.snapshot();
  result.active.device_overloads = active_device_overloads_.load(std::memory_order_relaxed);
  const auto phase = phase_and_warmup_overloads_.load(std::memory_order_acquire);
  result.warmup.device_overloads = phase & ~active_bit;
  result.active_started = (phase & active_bit) != 0;
  result.callbacks = result.warmup.callbacks + result.active.callbacks;
  result.timestamp_discontinuities = result.warmup.timestamp_discontinuities + result.active.timestamp_discontinuities;
  result.invalid_timestamps = result.warmup.invalid_timestamps + result.active.invalid_timestamps;
  result.callback_overruns = result.warmup.callback_overruns + result.active.callback_overruns;
  result.callback_errors = result.warmup.callback_errors + result.active.callback_errors;
  result.device_overloads = result.warmup.device_overloads + result.active.device_overloads;
  result.max_callback_ms = std::max(result.warmup.max_callback_ms, result.active.max_callback_ms);
  return result;
}

AudioOutput::AudioOutput(Engine& engine, bool silent_output, std::uint64_t frame_limit, bool initially_active)
    : callback_state_(engine, silent_output, frame_limit, initially_active) {
  AudioComponentDescription desc{};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  const auto component = AudioComponentFindNext(nullptr, &desc);
  if (!component) throw std::runtime_error("No macOS audio output component");
  check(AudioComponentInstanceNew(component, &unit_), "Create audio output");
  try {
    AudioStreamBasicDescription format{};
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float) * 2;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    check(AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)), "Set stereo format");
    UInt32 max_frames = 4096;
    check(AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max_frames, sizeof(max_frames)), "Set callback capacity");
    AURenderCallbackStruct cb{callback, this};
    check(AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb)), "Set audio callback");
    check(AudioUnitInitialize(unit_), "Initialize audio output");
  } catch (...) { AudioComponentInstanceDispose(unit_); unit_ = nullptr; throw; }
  // Overload notifications are optional for ordinary playback. Certification
  // can require monitoring explicitly, without making unsupported devices fail.
  device_listener_ = AudioUnitAddPropertyListener(unit_, kAudioOutputUnitProperty_CurrentDevice,
                                                 device_changed, this) == noErr;
  UInt32 size = sizeof(device_);
  if (AudioUnitGetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global,
                           0, &device_, &size) == noErr && device_ != kAudioObjectUnknown &&
      AudioObjectHasProperty(device_, &overload_address)) {
    overload_listener_ = AudioObjectAddPropertyListener(device_, &overload_address, overload, this) == noErr;
  }
}
AudioOutput::~AudioOutput() {
  if (unit_) {
    stop();
    if (overload_listener_) AudioObjectRemovePropertyListener(device_, &overload_address, overload, this);
    if (device_listener_) AudioUnitRemovePropertyListenerWithUserData(
        unit_, kAudioOutputUnitProperty_CurrentDevice, device_changed, this);
    AudioUnitUninitialize(unit_);
    AudioComponentInstanceDispose(unit_);
  }
}
void AudioOutput::start() { check(AudioOutputUnitStart(unit_), "Start audio output"); }
void AudioOutput::stop() noexcept { if (unit_) AudioOutputUnitStop(unit_); }
AudioOutputMetrics AudioOutput::metrics() const noexcept {
  auto result = callback_state_.metrics();
  result.overload_monitoring = overload_listener_ && device_listener_ &&
                              !device_changed_.load(std::memory_order_relaxed);
  return result;
}
OSStatus AudioOutput::overload(AudioObjectID, UInt32 count, const AudioObjectPropertyAddress* addresses,
                                void* context) {
  for (UInt32 index = 0; index < count; ++index)
    if (addresses[index].mSelector == kAudioDeviceProcessorOverload)
      static_cast<AudioOutput*>(context)->callback_state_.note_device_overload();
  return noErr;
}
void AudioOutput::device_changed(void* context, AudioUnit, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement) {
  // Do not reconfigure listeners on an audio thread. A device switch invalidates
  // monitoring for this session; a subsequent validation can open the new device.
  static_cast<AudioOutput*>(context)->device_changed_.store(true, std::memory_order_relaxed);
}
OSStatus AudioOutput::callback(void* context, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* timestamp,
                               UInt32, UInt32 count, AudioBufferList* data) {
  return static_cast<AudioOutput*>(context)->callback_state_.render(timestamp, count, data, flags);
}
}  // namespace music
