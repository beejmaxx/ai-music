#pragma once

#include <iostream>
#include <mutex>
#include <string_view>

namespace music {
// Protocol writers share this lock across translation units. Assemble a whole
// line before calling so prompt-worker output cannot split host status or ACKs.
inline void log_line(std::ostream& output, std::string_view text) {
  static std::mutex mutex;
  const std::lock_guard<std::mutex> lock(mutex);
  output << text << '\n' << std::flush;
}

inline void log_line(std::string_view text) { log_line(std::cout, text); }
}  // namespace music
