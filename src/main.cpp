#include "music/audio.hpp"
#include "music/commands.hpp"
#include "music/radio.hpp"
#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <poll.h>
#include <sstream>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t interrupted = 0;
void on_signal(int) { interrupted = 1; }

struct Options {
  std::string source = "demo", style, model_dir = "models", record, watch, program = "trance";
  float duration = 0, volume = 0.25f, stats_every = 10;
  int stream_fd = -1;
  bool no_audio = false, no_input = false, offline = false, radio = false;
};

void usage() {
  std::cout << "ai-music — a continuous, programmable instrumental audio engine\n\n"
    "Usage: ai-music [options]\n"
    "  --source synth|demo|magenta  live synth, demo (default), or actual AI\n"
    "  --style TEXT            synth: house/trance; demo: ambient/pulse; AI: text\n"
    "  --radio                 automatic synth arrangement\n"
    "  --program trance|french-house  fixed tempo: 132 / 124 BPM\n"
    "  --model-dir PATH        downloaded model root (default: models)\n"
    "  --record PATH.wav       stream stereo PCM16 to disk; rotate hourly\n"
    "  --watch PATH            load controls/a new DJ score when you save it\n"
    "  --volume 0..1           initial gain (default: 0.25)\n"
    "  --duration SECONDS      stop after this duration; 0 runs indefinitely\n"
    "  --no-audio              consume at real-time speed without speakers\n"
    "  --offline               render AI to WAV at the model's own speed\n"
    "  --no-input              ignore stdin (for unattended sessions)\n"
    "  --stats-every SECONDS   status interval (default: 10)\n"
    "  --help                  show this help\n\n" << music::command_help();
}

Options options(int argc, char** argv) {
  Options result;
  for (int i = 1; i < argc; ++i) {
    const std::string name = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 == argc) throw std::runtime_error("Missing value for " + name);
      return argv[++i];
    };
    if (name == "--source") result.source = next();
    else if (name == "--style") result.style = next();
    else if (name == "--model-dir") result.model_dir = next();
    else if (name == "--record") result.record = next();
    else if (name == "--watch") result.watch = next();
    else if (name == "--duration") result.duration = music::number_in_range(next(), 0, 1e7f);
    else if (name == "--volume") result.volume = music::number_in_range(next(), 0, 1);
    else if (name == "--stats-every") result.stats_every = music::number_in_range(next(), 0.1f, 3600);
    else if (name == "--no-audio") result.no_audio = true;
    else if (name == "--offline") { result.offline = true; result.no_audio = true; }
    else if (name == "--no-input") result.no_input = true;
    else if (name == "--radio") result.radio = true;
    else if (name == "--program") result.program = next();
    else if (name == "--stream-fd") {
      const auto fd = music::number_in_range(next(), 3, 1024);
      if (int(fd) != fd) throw std::runtime_error("Stream descriptor must be an integer");
      result.stream_fd = int(fd);
    }
    else throw std::runtime_error("Unknown option: " + name);
  }
  if (result.source != "demo" && result.source != "synth" && result.source != "magenta") throw std::runtime_error("Source must be synth, demo, or magenta");
  if (result.style.empty()) result.style = result.source == "demo" ? "ambient" : result.source == "synth" ? "house" : "instrumental ambient electronic, warm synth pads, soft percussion";
  if (!result.record.empty() && std::filesystem::path(result.record).extension() != ".wav")
    throw std::runtime_error("Use a .wav recording path");
  if (result.style.size() > 2048) throw std::runtime_error("Style is too long (2048 characters maximum)");
  if (result.radio && result.source != "synth") throw std::runtime_error("Use --radio with --source synth");
  if (result.program != "trance" && result.program != "french-house") throw std::runtime_error("Program must be trance or french-house");
  if (result.offline && (result.source != "magenta" || result.record.empty() || result.duration <= 0))
    throw std::runtime_error("Use --offline with --source magenta, --record PATH.wav, and a positive --duration");
  return result;
}

void status(music::Source& source, const music::Engine& engine, const music::Recorder* recorder,
            const music::StreamOutput* stream, bool radio, bool custom) {
  const auto m = source.metrics();
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  std::cout << std::fixed << std::setprecision(2)
    << "[status] seconds=" << double(engine.frames()) / music::sample_rate
    << " frames=" << engine.frames() << " underruns=" << engine.underruns()
    << " invalid_samples=" << engine.invalid_samples()
    << " peak=" << engine.peak() << " volume=" << engine.volume()
    << " muted=" << engine.muted()
    << " generation_ms=" << m.generation_ms
    << " buffer_ms=" << double(m.buffered_frames) * 1000 / music::sample_rate
    << " prompt_status=" << m.prompt_status
    << " peak_rss_mb=" << double(usage.ru_maxrss) / (1024 * 1024)
    << " recording_dropped=" << (recorder ? recorder->dropped() : 0)
    << " stream_dropped=" << (stream ? stream->dropped() : 0);
  if (source.is_synth()) {
    const auto score = engine.score_status();
    std::cout << " bar=" << 1 + source.beat() / 4
      << " bpm=" << source.synth_value(music::Parameter::tempo)
      << " score=" << score.revision << " score_start_bar=" << 1 + score.start_beat / 4
      << " score_end_bar=" << 1 + score.end_beat / 4
      << " cues_remaining=" << score.remaining << " radio=" << radio << " custom=" << custom;
    for (const auto& [name, parameter] : {std::pair{"kick", music::Parameter::kick}, {"clap", music::Parameter::clap},
         {"hats", music::Parameter::hats}, {"bass", music::Parameter::bass}, {"lead", music::Parameter::lead}, {"pad", music::Parameter::pad}})
      std::cout << ' ' << name << '=' << source.synth_value(parameter);
  }
  std::cout << std::endl;
}

int run(const Options& opts) {
  std::unique_ptr<music::Source> source;
  if (opts.source == "demo") {
    source = music::make_demo_source();
    source->style(opts.style);
  } else if (opts.source == "synth") {
    source = music::make_synth_source();
    source->style(opts.style);
  } else {
#ifdef AI_MUSIC_MAGENTA
    source = music::make_magenta_source(opts.model_dir, opts.style, 160);
#else
    throw std::runtime_error("This build contains the procedural demo only. Rebuild with -DAI_MUSIC_MAGENTA=ON for AI generation.");
#endif
  }
  std::unique_ptr<music::Recorder> recorder;
  if (!opts.record.empty()) recorder = std::make_unique<music::Recorder>(opts.record);
  std::unique_ptr<music::StreamOutput> stream;
  if (opts.stream_fd >= 0) stream = std::make_unique<music::StreamOutput>(opts.stream_fd);
  music::Engine engine(*source, recorder.get(), stream.get());
  engine.volume(opts.volume);
  if (opts.radio) {
    const auto bpm = opts.program == "french-house" ? 124.f : 132.f;
    source->tempo(bpm); engine.tempo(bpm);
  }
  bool quit = false;
  bool radio_enabled = opts.radio, radio_waiting = false, custom_score = false;
  std::uint64_t radio_revision = 0;
  music::RadioDirector radio;
  radio.program(opts.program);
  auto next_chapter = [&]() {
    const auto score = radio.next();
    if (!engine.score(score)) throw std::runtime_error("Radio score queue is full");
    radio_waiting = true;
    custom_score = false;
    std::cout << "[radio] " << radio.chapter() << " — new melody, groove / breakdown / build / drop\n";
  };
  if (radio_enabled) next_chapter();
  auto commands = [&](const std::string& text, bool watched = false) {
    const auto parsed = music::parse_commands(text);
    music::validate_controls(parsed, source->is_ai(), watched, source->is_synth());
    if (source->is_synth()) {
      const auto score = music::compile_score(parsed, watched && !parsed.empty());
      if (score.count || score.replace || score.cancel) {
        if (!engine.score(score)) throw std::runtime_error("Score queue is full; try the edit again");
        if (score.replace) custom_score = true;
        if (score.replace || score.cancel)
          std::cout << "[score] " << (score.cancel ? "Cancel queued" : "New score queued")
            << "; controls=" << score.count << "; quantize=" << score.quantum << " bars\n";
      }
      if (score.cancel) { radio_enabled = false; std::cout << "[radio] Automatic program stopped; current groove continues.\n"; }
    }
    for (const auto& cmd : parsed) {
      if (cmd.action == music::Action::quit) quit = true;
      else if (cmd.action == music::Action::status) status(*source, engine, recorder.get(), stream.get(), radio_enabled, custom_score);
      else if (cmd.action == music::Action::help) std::cout << music::command_help() << std::flush;
      else if (cmd.action == music::Action::next || (cmd.action == music::Action::radio && cmd.number != 0)) {
        radio_enabled = true; next_chapter();
      }
      else if (!source->is_synth()) music::apply_control(cmd, *source, engine);
    }
  };

  std::filesystem::file_time_type last_write{};
  bool have_write = false, watch_error = false;
  auto reload = [&]() {
    if (opts.watch.empty()) return;
    try {
      const auto stamp = std::filesystem::last_write_time(opts.watch);
      if (have_write && stamp == last_write) return;
      have_write = true; last_write = stamp;
      if (std::filesystem::file_size(opts.watch) > 65536) throw std::runtime_error("Command file exceeds 64 KiB");
      std::ifstream input(opts.watch);
      if (!input) throw std::runtime_error("Cannot read command file");
      std::string text((std::istreambuf_iterator<char>(input)), {});
      commands(text, true);
      watch_error = false;
      std::cout << "[watch] Applied " << opts.watch << std::endl;
    } catch (const std::exception& e) {
      if (!watch_error) std::cerr << "[watch] " << e.what() << "; current settings retained.\n";
      watch_error = true;
    }
  };
  reload();
  source->start();

  std::unique_ptr<music::AudioOutput> audio;
  std::atomic<bool> clock_done{false};
  std::jthread clock_thread;
  if (!opts.no_audio) {
    audio = std::make_unique<music::AudioOutput>(engine);
    audio->start();
  } else {
    const auto limit = opts.duration > 0 ? std::uint64_t(double(opts.duration) * music::sample_rate) : UINT64_MAX;
    clock_thread = std::jthread([&engine, &clock_done, limit, offline = opts.offline](std::stop_token stop) {
      std::array<float, music::block_size * 2> samples{};
      const auto start = Clock::now();
      std::uint64_t frames = 0;
      while (!stop.stop_requested() && frames < limit) {
        const auto count = std::min<std::uint64_t>(music::block_size, limit - frames);
        engine.render(samples.data(), count, offline);
        frames += count;
        if (!offline)
          std::this_thread::sleep_until(start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(double(frames) / music::sample_rate)));
      }
      clock_done.store(true);
    });
  }

  std::cout << "Running: " << source->name() << " | 48 kHz stereo | "
    << (opts.offline ? "offline render" : opts.no_audio ? "silent real-time sink" : "default audio output") << '\n';
  if (!opts.record.empty()) std::cout << "Recording: " << opts.record << '\n';
  std::cout << "Type help for live controls; quit or Ctrl-C stops cleanly.\n" << std::flush;
  auto next_stats = Clock::now() + std::chrono::duration<float>(opts.stats_every);
  auto next_watch = Clock::now();
  const auto start = Clock::now();
  bool input_open = !opts.no_input;
  std::string pending_input;
  while (!quit && !interrupted && !clock_done.load()) {
    const auto now = Clock::now();
    if (!opts.no_audio && opts.duration > 0 && std::chrono::duration<double>(now - start).count() >= opts.duration) break;
    if (recorder && recorder->failed()) break;
    if (stream && stream->failed()) break;
    if (now >= next_stats) { status(*source, engine, recorder.get(), stream.get(), radio_enabled, custom_score); next_stats = now + std::chrono::duration<float>(opts.stats_every); }
    if (now >= next_watch) { reload(); next_watch = now + std::chrono::milliseconds(250); }
    if (radio_enabled) {
      const auto score = engine.score_status();
      if (score.revision != radio_revision) { radio_revision = score.revision; radio_waiting = false; }
      // Prepare the next chapter during the final bar, after all earlier cues.
      const auto next = custom_score ? std::max(score.start_beat + 128., score.end_beat + 32.)
                                     : score.start_beat + 256.;
      if (!radio_waiting && (!custom_score || score.remaining == 0) && source->beat() >= next - 4) next_chapter();
    }
    if (!input_open) { std::this_thread::sleep_for(std::chrono::milliseconds(25)); continue; }
    pollfd fd{STDIN_FILENO, POLLIN, 0};
    const auto polled = ::poll(&fd, 1, 25);
    if (polled <= 0) continue;
    if (!(fd.revents & (POLLIN | POLLHUP))) continue;
    std::array<char, 4096> buffer{};
    const auto n = ::read(STDIN_FILENO, buffer.data(), buffer.size());
    if (n <= 0) {
      input_open = false;
      if (!pending_input.empty()) {
        try { commands(pending_input); } catch (const std::exception& e) { std::cerr << "[command] " << e.what() << '\n'; }
      }
      if (opts.duration == 0 && opts.watch.empty()) quit = true;
      continue;
    }
    pending_input.append(buffer.data(), n);
    for (auto newline = pending_input.find('\n'); newline != std::string::npos; newline = pending_input.find('\n')) {
      const auto line = pending_input.substr(0, newline);
      pending_input.erase(0, newline + 1);
      try { commands(line); }
      catch (const std::exception& e) { std::cerr << "[command] " << e.what() << '\n'; }
    }
    if (pending_input.size() > 65536) { pending_input.clear(); std::cerr << "[command] Input line too long; discarded.\n"; }
  }
  if (audio) audio->stop();
  if (clock_thread.joinable()) { clock_thread.request_stop(); clock_thread.join(); }
  source->stop();
  if (recorder) recorder->finish();
  status(*source, engine, recorder.get(), stream.get(), radio_enabled, custom_score);
  if (recorder && recorder->failed()) throw std::runtime_error(recorder->error_after_finish());
  if (stream && stream->failed()) throw std::runtime_error("Live PCM pipe closed");
  if (engine.underruns() || engine.invalid_samples() || (recorder && recorder->dropped())) {
    std::cerr << "Stopped with audio gaps or recording errors; see the counters above.\n";
    return 2;
  }
  std::cout << "Stopped cleanly.\n";
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--help") { usage(); return 0; }
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::signal(SIGPIPE, SIG_IGN);
  try { return run(options(argc, argv)); }
  catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
