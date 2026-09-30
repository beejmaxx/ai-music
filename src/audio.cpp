#include "music/audio.hpp"
#include <algorithm>
#include <array>
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
  std::array<StereoFrame, block_size> recording{};
  float peak = 0;
  std::uint64_t invalid = 0;
  const auto chunk = source_.is_synth() ? std::size_t(64) : block_size;
  for (std::size_t offset = 0; offset < count; offset += chunk) {
    if (source_.is_synth()) score_.tick(source_.beat(), *this);
    effects_.begin_block();
    const auto n = std::min(chunk, count - offset);
    const bool ready = offline ? source_.read_offline(left.data(), right.data(), n)
                               : source_.read(left.data(), right.data(), n);
    if (!ready) underruns_.fetch_add(1, std::memory_order_relaxed);
    const float target = muted_.load(std::memory_order_relaxed) ? 0 : volume_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i) {
      gain_ += (target - gain_) * (1.0f / 480.0f);
      auto clean = [&](float value) {
        if (!std::isfinite(value)) { ++invalid; return 0.0f; }
        return std::clamp(value, -4.0f, 4.0f);
      };
      const auto wet = effects_.process(clean(left[i]), clean(right[i]));
      const auto l = std::clamp(wet.left * gain_, -1.0f, 1.0f);
      const auto r = std::clamp(wet.right * gain_, -1.0f, 1.0f);
      out[2 * (offset + i)] = l;
      out[2 * (offset + i) + 1] = r;
      recording[i] = {l, r};
      peak = std::max({peak, std::abs(l), std::abs(r)});
    }
    if (recorder_) recorder_->push(recording.data(), n);
  }
  peak_.store(peak, std::memory_order_relaxed);
  invalid_samples_.fetch_add(invalid, std::memory_order_relaxed);
  frames_.fetch_add(count, std::memory_order_relaxed);
}

namespace {
void check(OSStatus code, const char* operation) {
  if (code != noErr) throw std::runtime_error(std::string(operation) + " failed (CoreAudio " + std::to_string(code) + ")");
}
}  // namespace

AudioOutput::AudioOutput(Engine& engine) : engine_(engine) {
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
}
AudioOutput::~AudioOutput() {
  if (unit_) { stop(); AudioUnitUninitialize(unit_); AudioComponentInstanceDispose(unit_); }
}
void AudioOutput::start() { check(AudioOutputUnitStart(unit_), "Start audio output"); }
void AudioOutput::stop() noexcept { if (unit_) AudioOutputUnitStop(unit_); }
OSStatus AudioOutput::callback(void* context, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                               UInt32, UInt32 count, AudioBufferList* data) {
  if (!data || data->mNumberBuffers != 1 || !data->mBuffers[0].mData ||
      data->mBuffers[0].mDataByteSize < count * sizeof(float) * 2) return kAudio_ParamError;
  static_cast<AudioOutput*>(context)->engine_.render(static_cast<float*>(data->mBuffers[0].mData), count);
  return noErr;
}
}  // namespace music
