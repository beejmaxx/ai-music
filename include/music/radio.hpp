#pragma once
#include "music/score.hpp"
#include <random>
#include <string>

namespace music {
// A local, procedural station program. No model or network request is hidden here.
class RadioDirector {
 public:
  explicit RadioDirector(std::uint32_t seed = std::random_device{}()) : random_(seed) {}
  Score next();
  void program(const std::string& name);
  unsigned bars() const { return program_ == "funk-study" ? 16 : 64; }
  const std::string& chapter() const { return chapter_; }
 private:
  std::mt19937 random_;
  unsigned chapter_index_ = 0;
  std::string chapter_;
  std::string program_ = "trance";
  Score next_house();
  Score next_funk();
};
}  // namespace music
