cmake_minimum_required(VERSION 3.27)
project(ai_music VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
option(AI_MUSIC_MAGENTA "Build the Magenta RealTime 2 source (Apple Silicon)" OFF)

if(NOT APPLE)
  message(FATAL_ERROR "The first audio host uses macOS CoreAudio.")
endif()

add_library(music_core
  src/audio.cpp
  src/commands.cpp
  src/score.cpp
  src/radio.cpp
  src/demo_source.cpp
  src/synth_source.cpp
  src/recorder.cpp)
target_include_directories(music_core PUBLIC include)
target_compile_options(music_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(music_core PUBLIC "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio")

add_executable(ai-music src/main.cpp)
target_link_libraries(ai-music PRIVATE music_core)
target_compile_options(ai-music PRIVATE -Wall -Wextra -Wpedantic)

if(AI_MUSIC_MAGENTA)
  include(cmake/Magenta.cmake)
  target_sources(ai-music PRIVATE src/magenta_source.cpp)
  target_compile_definitions(ai-music PRIVATE AI_MUSIC_MAGENTA=1)
  target_link_libraries(ai-music PRIVATE magentart::core)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(music_tests tests/core_tests.cpp)
  target_link_libraries(music_tests PRIVATE music_core)
  add_test(NAME music_core COMMAND music_tests)
  add_executable(score_tests tests/score_tests.cpp)
  target_link_libraries(score_tests PRIVATE music_core)
  add_test(NAME music_score COMMAND score_tests)
endif()
