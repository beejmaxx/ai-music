#include "music/log.hpp"
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>

namespace {
// Yield between each character to exercise interleaving without relying on
// scheduling luck. The buffer itself remains safe even if the logger regresses.
class YieldingBuffer final : public std::streambuf {
 public:
  std::string text;
  unsigned flushes = 0;

 protected:
  std::streamsize xsputn(const char* data, std::streamsize count) override {
    for (std::streamsize i = 0; i < count; ++i) overflow(traits_type::to_int_type(data[i]));
    return count;
  }
  int_type overflow(int_type value) override {
    if (!traits_type::eq_int_type(value, traits_type::eof())) {
      {
        const std::lock_guard<std::mutex> lock(mutex_);
        text.push_back(traits_type::to_char_type(value));
      }
      std::this_thread::yield();
    }
    return traits_type::not_eof(value);
  }
  int sync() override {
    const std::lock_guard<std::mutex> lock(mutex_);
    ++flushes;
    return 0;
  }

 private:
  std::mutex mutex_;
};

std::string message(unsigned producer, unsigned index) {
  return "[protocol] producer=" + std::to_string(producer) + " message=" + std::to_string(index);
}
}  // namespace

int main() {
  try {
    YieldingBuffer buffer;
    std::array<std::thread, 4> producers;
    constexpr unsigned count = 100;
    for (unsigned producer = 0; producer < producers.size(); ++producer) {
      producers[producer] = std::thread([&buffer, producer] {
        // Separate stream objects avoid sharing formatting state; all writers
        // deliberately target the same character sink, as host/worker logs do.
        std::ostream output(&buffer);
        for (unsigned index = 0; index < count; ++index)
          music::log_line(output, message(producer, index));
      });
    }
    for (auto& producer : producers) producer.join();

    std::set<std::string> expected;
    for (unsigned producer = 0; producer < producers.size(); ++producer)
      for (unsigned index = 0; index < count; ++index)
        expected.insert(message(producer, index));
    std::istringstream input(buffer.text);
    for (std::string line; std::getline(input, line);)
      if (expected.erase(line) != 1) throw std::runtime_error("Protocol lines interleaved or repeated");
    if (!expected.empty() || buffer.flushes != count * producers.size())
      throw std::runtime_error("A protocol line was lost or not flushed");
    std::cout << "PASS: concurrent protocol lines remain complete and flushed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
