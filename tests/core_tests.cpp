#include "music/audio.hpp"
#include "music/commands.hpp"
#include "music/playback_timing.hpp"
#include "music/ring.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template <typename F> void rejects(F fn) {
  bool caught = false;
  try { fn(); } catch (const std::exception&) { caught = true; }
  check(caught, "Expected rejection");
}

void ring_test() {
  music::Ring<int> ring(3);
  const int input[] = {1, 2, 3, 4};
  int output[4]{};
  check(ring.write(input, 4) == 3, "Queue must remain bounded");
  check(ring.read(output, 2) == 2 && output[0] == 1 && output[1] == 2, "FIFO order");
  check(ring.write(input + 3, 1) == 1, "Wraparound write");
  check(ring.read(output, 4) == 2 && output[0] == 3 && output[1] == 4, "Wraparound order");
  check(ring.read(output, 1) == 0, "Empty queue");

  music::Ring<std::uint64_t> concurrent(257);
  constexpr std::uint64_t limit = 250000;
  std::thread producer([&] {
    for (std::uint64_t i = 0; i < limit;) {
      i += concurrent.write(&i, 1);
      if ((i % 1024) == 0) std::this_thread::yield();
    }
  });
  bool ordered = true;
  for (std::uint64_t expected = 0; expected < limit;) {
    std::uint64_t item;
    if (concurrent.read(&item, 1)) { ordered &= item == expected; ++expected; }
    else std::this_thread::yield();
  }
  producer.join();
  check(ordered, "Concurrent queue lost or reordered samples");
}

void command_test() {
  auto commands = music::parse_commands("# comment\nstyle warm piano\nvolume .4\ndrums off\n");
  check(commands.size() == 3 && commands[0].text == "warm piano", "Style text survives parsing");
  music::validate_controls(commands, true, true);
  rejects([&] { music::validate_controls(commands, false, true); });
  for (const char* value : {"volume nan", "volume inf", "volume -.1", "volume 1.1", "volume .5 junk", "style", "drums maybe", "quit junk"})
    rejects([&] { music::parse_commands(value); });
  rejects([] { music::validate_controls(music::parse_commands("quit"), true, true); });
  rejects([] { music::validate_controls(music::parse_commands("tempo 120"), true); });
  music::validate_controls(music::parse_commands("style trance\nmix bass .6\nfilter 900\ndelay .3"), false, true, true);
  rejects([] { music::parse_commands("mix bass 2"); });
  rejects([] { music::parse_commands("mix guitar .5"); });
  rejects([] { music::parse_commands("filter nan"); });
  rejects([] { music::validate_controls(music::parse_commands("mix bass .6"), true); });
  rejects([] { music::validate_controls(music::parse_commands("style ambient"), false, true, true); });
}

class BadSource final : public music::Source {
 public:
  bool read(float* l, float* r, std::size_t n) noexcept override {
    for (std::size_t i = 0; i < n; ++i) { l[i] = std::numeric_limits<float>::quiet_NaN(); r[i] = 20; }
    return false;
  }
  void style(const std::string&) override {}
  void drums(bool) override {}
  void tempo(float) override {}
  void temperature(float) override {}
  const char* name() const override { return "test"; }
};

void engine_test() {
  BadSource bad;
  music::Engine engine(bad);
  std::array<float, 2048> samples{};
  engine.render(samples.data(), samples.size() / 2);
  check(engine.underruns() == 2 && engine.invalid_samples() == 1024, "Audio errors counted");
  for (float value : samples) check(std::isfinite(value) && std::abs(value) <= 1, "Output finite and bounded");
  auto demo = music::make_demo_source();
  music::Engine real(*demo);
  double energy = 0;
  for (int i = 0; i < 100; ++i) {
    real.render(samples.data(), samples.size() / 2);
    for (float value : samples) energy += value * value;
  }
  check(energy > 1 && real.underruns() == 0, "Demo makes continuous sound");
  real.mute(true);
  for (int i = 0; i < 30; ++i) real.render(samples.data(), samples.size() / 2);
  check(real.peak() < 1e-6f, "Mute fades to silence");
}

void playback_timing_test() {
  using namespace std::chrono_literals;
  using Clock = music::PlaybackTiming::Clock;
  music::PlaybackTiming timing;
  const auto start = Clock::time_point{};
  timing.record(start + 1ms, start + 10ms);
  timing.record(start + 13ms, start + 20ms);  // Ordinary wake jitter fits in the next block.
  timing.record(start + 30ms, start + 30ms);
  check(timing.misses() == 0 && timing.max_late_ms() == 0, "On-time blocks do not count as missed deadlines");
  timing.record(start + 65ms, start + 40ms);  // Consumer stalls with audio still in its ring.
  timing.record(start + 66ms, start + 50ms);  // Catch-up does not erase the stall.
  timing.record(start + 67ms, start + 60ms);
  timing.record(start + 71ms, start + 80ms);
  check(timing.misses() == 3 && timing.max_late_ms() == 25,
        "Missed playback deadlines stay visible after the consumer catches up");
}

void synth_test() {
  auto source = music::make_synth_source();
  music::Engine engine(*source);
  std::array<float, music::block_size * 2> audio{};
  for (const auto* style : {"house", "trance"}) {
    source->style(style);
    double energy = 0;
    for (int block = 0; block < 200; ++block) {
      engine.render(audio.data(), music::block_size);
      for (auto value : audio) {
        check(std::isfinite(value) && std::abs(value) <= 1, "Synth finite and bounded");
        energy += value * value;
      }
    }
    check(energy > 1, "Both dance styles produce audio");
  }
  for (const auto* layer : {"kick", "clap", "hats", "bass", "lead", "pad"}) source->mix(layer, 0);
  for (int block = 0; block < 100; ++block) engine.render(audio.data(), music::block_size);
  check(engine.peak() < 1e-6f && engine.underruns() == 0, "Layer faders silence the synth without stopping its clock");

  music::Effects delay;
  delay.delay(1);
  delay.begin_block();
  delay.process(.5, 0);
  double echoes = 0;
  for (unsigned i = 0; i < music::sample_rate; ++i) {
    const auto value = delay.process(0, 0);
    echoes += value.left * value.left + value.right * value.right;
  }
  check(echoes > .1, "Delay retains and repeats an impulse");

  music::Effects filter;
  filter.filter(100);
  filter.begin_block();
  float last = 0;
  for (unsigned i = 0; i < music::sample_rate; ++i)
    last = filter.process(i % 2 ? 1 : -1, 0).left;
  check(std::abs(last) < .001, "Low-pass filter attenuates high frequencies");
}

void bus_routing_test() {
  // Musical effects must not wash out the kick/bass, but must affect the hook.
  for (const auto* layer : {"kick", "bass", "lead"}) {
    auto dry = music::make_synth_source(), filtered = music::make_synth_source();
    std::array<float, music::block_size> left{}, right{};
    for (auto* source : {dry.get(), filtered.get()}) {
      for (const auto* name : {"kick", "clap", "hats", "bass", "lead", "pad"}) source->mix(name, 0);
      source->mix(layer, .8f);
      // Settle faders before constructing either effects processor.
      for (int i = 0; i < 64; ++i) source->read(left.data(), right.data(), left.size());
    }
    music::Engine original(*dry), effect(*filtered);
    effect.filter(100);
    effect.delay(1);
    std::array<float, music::block_size * 2> a{}, b{};
    double difference = 0, original_energy = 0, filtered_energy = 0;
    for (int block = 0; block < 200; ++block) {
      original.render(a.data(), music::block_size);
      effect.render(b.data(), music::block_size);
      if (block < 20) continue;
      for (std::size_t i = 0; i < a.size(); ++i) {
        difference += std::abs(a[i] - b[i]);
        original_energy += a[i] * a[i];
        filtered_energy += b[i] * b[i];
      }
    }
    check(original_energy > 1, "Routing comparison has an audible solo instrument");
    if (std::string(layer) == "lead")
      check(filtered_energy < original_energy * .1, "The hook still passes through its musical filter");
    else check(difference < 1e-5, "Kick and bass bypass the musical filter and echo");
  }
}

void grit_ducking_test() {
  // Compare only the musical bus with/without a kick. A foreground sustained
  // hook must not nearly disappear at every downbeat, even with kick at unity.
  std::array<double, 2> energy{};
  for (unsigned drums = 0; drums < 2; ++drums) {
    auto source = music::make_synth_source();
    music::Engine controls(*source);
    const auto commands = music::parse_commands(
      "tempo 120\nvoice grit\nmix kick 1\nmix clap 0\nmix hats 0\nmix bass 0\nmix lead 1\nmix pad 0\n"
      "lead-midi 62 ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~");
    for (const auto& command : commands) music::apply_control(command, *source, controls);
    source->drums(drums);
    std::array<float, 128> left{}, right{}, dry_left{}, dry_right{};
    for (unsigned frame = 0; frame < music::sample_rate * 1.07; frame += left.size()) {
      source->read_buses(left.data(), right.data(), dry_left.data(), dry_right.data(), left.size());
      for (unsigned i = 0; i < left.size(); ++i) {
        const auto time = double(frame + i) / music::sample_rate;
        if (time > 1.005 && time < 1.06) energy[drums] += left[i] * left[i];
      }
    }
  }
  check(energy[0] > 1, "Sustained grit hook is audible");
  check(energy[1] > energy[0] * .7 && energy[1] < energy[0],
        "Grit hook keeps most of its energy through the kick attack");
}

std::uint32_t u32(const std::vector<unsigned char>& bytes, std::size_t at) {
  return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at+1]) << 8) |
    (std::uint32_t(bytes[at+2]) << 16) | (std::uint32_t(bytes[at+3]) << 24);
}

void recording_test() {
  const auto root = std::filesystem::temp_directory_path() / ("ai-music-test-" + std::to_string(getpid()));
  check(std::filesystem::create_directory(root), "Create isolated test directory");
  const auto path = root / "take.wav";
  {
    music::Recorder recorder(path, 1000);
    std::vector<music::StereoFrame> samples(1800, {0.25f, -0.25f});
    recorder.push(samples.data(), samples.size());
    recorder.finish();
    check(!recorder.failed() && recorder.written() == 1800 && recorder.dropped() == 0, "Recorder drains on shutdown");
  }
  for (const auto& [name, frames] : {std::pair{"take.wav", 1000}, {"take-2.wav", 800}}) {
    std::ifstream file(root / name, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    check(bytes.size() == std::size_t(44 + frames * 4), "Segment size correct");
    check(u32(bytes, 40) == std::uint32_t(frames * 4) && u32(bytes, 24) == 48000, "WAV header finalized");
    check(bytes[44] == 0 && bytes[45] == 32 && bytes[46] == 0 && bytes[47] == 224, "PCM channels preserved");
  }
  rejects([&] { music::Recorder recorder(path); });
  std::filesystem::remove_all(root);
}
}  // namespace

int main() {
  try {
    ring_test(); command_test(); engine_test(); playback_timing_test(); synth_test(); bus_routing_test(); grit_ducking_test(); recording_test();
    std::cout << "Passed: concurrent queue, controls, audio bounds, live synth/mixer/effects, streaming WAV.\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
