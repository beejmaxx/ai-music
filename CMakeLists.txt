cmake_minimum_required(VERSION 3.27)
project(ai_music VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
option(AI_MUSIC_MAGENTA "Build the Magenta RealTime 2 source (Apple Silicon)" OFF)
include(CTest)

if(NOT APPLE)
  message(FATAL_ERROR "The first audio host uses macOS CoreAudio.")
endif()

add_library(music_core
  src/audio.cpp
  src/commands.cpp
  src/score.cpp
  src/radio.cpp
  src/stream.cpp
  src/demo_source.cpp
  src/synth_source.cpp
  src/recorder.cpp)
target_include_directories(music_core PUBLIC include)
target_compile_options(music_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(music_core PUBLIC "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio")

add_executable(ai-music src/main.cpp)
target_link_libraries(ai-music PRIVATE music_core)
target_compile_options(ai-music PRIVATE -Wall -Wextra -Wpedantic)

# Short, deterministic sound-design auditions use the exact live signal path.
add_executable(music-render src/render.cpp)
target_link_libraries(music-render PRIVATE music_core)
target_compile_options(music-render PRIVATE -Wall -Wextra -Wpedantic)

# Instrument development without a sequencer or inference model.
add_library(music_voices src/bass_voice.cpp src/kick_voice.cpp)
target_include_directories(music_voices PUBLIC include)
target_compile_options(music_voices PRIVATE -Wall -Wextra -Wpedantic)
add_executable(music-sound src/sound.cpp)
target_link_libraries(music-sound PRIVATE music_voices music_core)
target_compile_options(music-sound PRIVATE -Wall -Wextra -Wpedantic)

add_executable(music-groove examples/kick-bass-study.cpp)
target_link_libraries(music-groove PRIVATE music_voices music_core)
target_compile_options(music-groove PRIVATE -Wall -Wextra -Wpedantic)

add_executable(music-track examples/first-track.cpp)
target_link_libraries(music-track PRIVATE music_voices music_core)
target_compile_options(music-track PRIVATE -Wall -Wextra -Wpedantic)

if(AI_MUSIC_MAGENTA)
  include(cmake/Magenta.cmake)
  if(BUILD_TESTING)
    add_executable(magenta_ring_tests tests/magenta_ring.cpp)
    target_include_directories(magenta_ring_tests PRIVATE "${magenta_SOURCE_DIR}/core/include")
    target_compile_options(magenta_ring_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME magenta_ring COMMAND magenta_ring_tests)
  endif()
  target_sources(ai-music PRIVATE src/magenta_source.cpp src/metal_device.mm)
  set_source_files_properties(src/metal_device.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_definitions(ai-music PRIVATE AI_MUSIC_MAGENTA=1)
  target_link_libraries(ai-music PRIVATE magentart::core)
  add_executable(music-benchmark src/benchmark.cpp src/metal_device.mm)
  target_link_libraries(music-benchmark PRIVATE magentart::core)
  target_compile_options(music-benchmark PRIVATE -Wall -Wextra -Wpedantic)

  set(AI_MUSIC_MLX_CPU_ROOT "" CACHE PATH "Optional MLX 0.31.1 CPU-only installation")
  if(AI_MUSIC_MLX_CPU_ROOT)
    if(NOT EXISTS "${AI_MUSIC_MLX_CPU_ROOT}/lib/libmlx_cpu.dylib")
      message(FATAL_ERROR "Build and install cmake/mlx-cpu before setting AI_MUSIC_MLX_CPU_ROOT")
    endif()
    file(READ "${AI_MUSIC_MLX_CPU_ROOT}/include/mlx/version.h" cpu_mlx_version)
    if(NOT cpu_mlx_version MATCHES "MLX_VERSION_MAJOR 0([^0-9]|$)" OR
       NOT cpu_mlx_version MATCHES "MLX_VERSION_MINOR 31([^0-9]|$)" OR
       NOT cpu_mlx_version MATCHES "MLX_VERSION_PATCH 1([^0-9]|$)")
      message(FATAL_ERROR "The CPU library must use the same MLX v0.31.1 ABI as Magenta")
    endif()
    add_library(ai_music_mlx_cpu SHARED IMPORTED)
    set_target_properties(ai_music_mlx_cpu PROPERTIES
      IMPORTED_LOCATION "${AI_MUSIC_MLX_CPU_ROOT}/lib/libmlx_cpu.dylib"
      INTERFACE_INCLUDE_DIRECTORIES "${AI_MUSIC_MLX_CPU_ROOT}/include"
      INTERFACE_LINK_LIBRARIES "-framework Accelerate")
    if(BUILD_TESTING)
      add_executable(mlx_constants_tests tests/mlx_constants.cpp)
      target_include_directories(mlx_constants_tests PRIVATE include)
      target_link_libraries(mlx_constants_tests PRIVATE ai_music_mlx_cpu)
      add_test(NAME mlx_constants COMMAND mlx_constants_tests)
    endif()
    add_executable(ai-music-cpu src/main.cpp src/magenta_source.cpp)
    target_compile_definitions(ai-music-cpu PRIVATE AI_MUSIC_MAGENTA=1 AI_MUSIC_CPU_ONLY=1)
    target_compile_options(ai-music-cpu PRIVATE -Wall -Wextra -Wpedantic)
    target_include_directories(ai-music-cpu PRIVATE "${magenta_SOURCE_DIR}/core/include")
    # The core archive is device-agnostic and ABI-compatible with the same MLX
    # version. Link it explicitly so its normal Metal MLX dependency is NOT
    # pulled into this separate process alongside the CPU allocator.
    add_dependencies(ai-music-cpu magentart_core)
    target_link_libraries(ai-music-cpu PRIVATE music_core "$<TARGET_FILE:magentart_core>"
      ai_music_mlx_cpu sentencepiece tensorflow-lite "-framework Foundation")
  endif()
endif()

if(BUILD_TESTING)
  add_executable(kick_voice_tests tests/kick_voice.cpp)
  target_link_libraries(kick_voice_tests PRIVATE music_voices)
  target_compile_options(kick_voice_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME kick_voice COMMAND kick_voice_tests)
  add_executable(bass_voice_tests tests/bass_voice.cpp)
  target_link_libraries(bass_voice_tests PRIVATE music_voices)
  target_compile_options(bass_voice_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME bass_voice COMMAND bass_voice_tests)
  add_executable(inference_trace_tests tests/inference_trace.cpp)
  target_include_directories(inference_trace_tests PRIVATE include)
  target_compile_options(inference_trace_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME inference_trace COMMAND inference_trace_tests)
  add_executable(audio_callback_tests tests/audio_callback.cpp)
  target_link_libraries(audio_callback_tests PRIVATE music_core)
  target_compile_options(audio_callback_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME audio_callback COMMAND audio_callback_tests)
  add_executable(music_log_tests tests/log.cpp)
  target_include_directories(music_log_tests PRIVATE include)
  target_compile_options(music_log_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME music_log COMMAND music_log_tests)
  add_executable(music_tests tests/core_tests.cpp)
  target_link_libraries(music_tests PRIVATE music_core)
  add_test(NAME music_core COMMAND music_tests)
  add_executable(score_tests tests/score_tests.cpp)
  target_link_libraries(score_tests PRIVATE music_core)
  add_test(NAME music_score COMMAND score_tests)
endif()
