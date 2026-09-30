#include "music/score.hpp"
#include <algorithm>
#include <cmath>

namespace music {
void ScorePlayer::advance(double beat, ScoreTarget& target) noexcept {
  for (auto& ramp : ramps_) {
    if (!ramp.active) continue;
    const auto fraction = std::clamp((beat - ramp.start) / (ramp.end - ramp.start), 0., 1.);
    auto control = ramp.to;
    // Cutoff sweeps are perceptually more even on a logarithmic scale.
    control.value = control.parameter == Parameter::filter
      ? float(ramp.from * std::pow(control.value / ramp.from, fraction))
      : float(ramp.from + (control.value - ramp.from) * fraction);
    if (fraction >= 1) { control.value = ramp.to.value; ramp.active = false; }
    target.write_control(control);
  }
}

void ScorePlayer::cancel_parameter(Parameter parameter) noexcept {
  if (parameter == Parameter::none) return;
  ramps_[static_cast<unsigned>(parameter)].active = false;
  const auto family = [](Parameter p) {
    if (p == Parameter::lead_midi) return Parameter::melody;
    if (p == Parameter::bassnotes || p == Parameter::bass_midi) return Parameter::bassline;
    return p;
  };
  for (std::size_t i = next_; i < active_.count; ++i)
    if (family(active_.events[i].control.parameter) == family(parameter))
      active_.events[i].control.parameter = Parameter::none;
}

void ScorePlayer::tick(double beat, ScoreTarget& target) noexcept {
  publication_.fetch_add(1);
  // Bound work even if a producer keeps submitting during this callback.
  for (unsigned received = 0; received < 32 && incoming_.read(&received_, 1); ++received) {
    if (received_.replace || received_.cancel) {
      active_.count = next_ = 0;
      for (auto& ramp : ramps_) ramp.active = false;
      revision_.fetch_add(1, std::memory_order_relaxed);
      const auto grid = 4. * received_.quantum;
      origin_ = std::ceil((beat - 1e-9) / grid) * grid;
      start_beat_.store(origin_, std::memory_order_relaxed);
      double end = origin_;
      for (std::size_t i = 0; i < received_.count; ++i)
        if (received_.events[i].bar >= 0)
          end = std::max(end, origin_ + 4 * (received_.events[i].bar + received_.events[i].duration));
      end_beat_.store(end, std::memory_order_relaxed);
    }
    for (std::size_t i = 0; i < received_.count; ++i) {
      const auto& event = received_.events[i];
      if (event.bar < 0) {
        cancel_parameter(event.control.parameter);
        target.write_control(event.control);
      } else {
        active_.events[active_.count++] = event;
      }
    }
  }
  while (next_ < active_.count && origin_ + 4 * active_.events[next_].bar <= beat + 1e-9) {
    const auto& event = active_.events[next_++];
    const auto parameter = event.control.parameter;
    if (parameter == Parameter::none) continue;
    const auto start = origin_ + 4 * event.bar;
    advance(start, target);
    auto& ramp = ramps_[static_cast<unsigned>(parameter)];
    ramp.active = false;
    if (event.duration > 0) {
      ramp = {event.control, target.read_control(parameter), start, start + 4 * event.duration, true};
    } else {
      target.write_control(event.control);
    }
  }
  advance(beat, target);
  unsigned remaining = 0;
  for (std::size_t i = next_; i < active_.count; ++i)
    remaining += active_.events[i].control.parameter != Parameter::none;
  for (const auto& ramp : ramps_) remaining += ramp.active;
  remaining_.store(remaining, std::memory_order_relaxed);
  publication_.fetch_add(1);
}
}  // namespace music
