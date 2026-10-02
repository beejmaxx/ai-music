cmake_minimum_required(VERSION 3.27)
project(ai_music_mlx_cpu LANGUAGES C CXX)

# An optional, separate MLX library for offline inference without a GPU.
get_filename_component(AI_MUSIC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(AI_MUSIC_MLX_SOURCE "${AI_MUSIC_ROOT}/build-ai/_deps/mlx-src" CACHE PATH "MLX v0.31.1 source checkout")
if(NOT EXISTS "${AI_MUSIC_MLX_SOURCE}/mlx/export.cpp")
  message(FATAL_ERROR "Set AI_MUSIC_MLX_SOURCE to the pinned MLX v0.31.1 source checkout")
endif()
file(READ "${AI_MUSIC_MLX_SOURCE}/mlx/version.h" mlx_version)
if(NOT mlx_version MATCHES "MLX_VERSION_MAJOR 0([^0-9]|$)" OR
   NOT mlx_version MATCHES "MLX_VERSION_MINOR 31([^0-9]|$)" OR
   NOT mlx_version MATCHES "MLX_VERSION_PATCH 1([^0-9]|$)")
  message(FATAL_ERROR "The CPU import patch and Magenta build require MLX v0.31.1")
endif()

set(patch "${AI_MUSIC_ROOT}/patches/mlx-cpu-import.patch")
execute_process(COMMAND git apply --reverse --check "${patch}"
  WORKING_DIRECTORY "${AI_MUSIC_MLX_SOURCE}" RESULT_VARIABLE patch_present
  OUTPUT_QUIET ERROR_QUIET)
if(NOT patch_present EQUAL 0)
  execute_process(COMMAND git apply --check "${patch}"
    WORKING_DIRECTORY "${AI_MUSIC_MLX_SOURCE}" COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND git apply "${patch}"
    WORKING_DIRECTORY "${AI_MUSIC_MLX_SOURCE}" COMMAND_ERROR_IS_FATAL ANY)
endif()

foreach(feature METAL CUDA TESTS EXAMPLES BENCHMARKS PYTHON_BINDINGS PYTHON_STUBS GGUF)
  set(MLX_BUILD_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(MLX_BUILD_CPU ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
add_subdirectory("${AI_MUSIC_MLX_SOURCE}" mlx)
target_compile_definitions(mlx PRIVATE AI_MUSIC_CPU_IMPORT=1)
target_sources(mlx PRIVATE "${CMAKE_CURRENT_LIST_DIR}/import_cpu.cpp")
set_target_properties(mlx PROPERTIES OUTPUT_NAME mlx_cpu)
# Avoid the exceptionally slow -O3 template optimization in this translation unit.
set_source_files_properties("${AI_MUSIC_MLX_SOURCE}/mlx/backend/cpu/binary.cpp"
  TARGET_DIRECTORY mlx PROPERTIES COMPILE_OPTIONS "-O1")

option(AI_MUSIC_CPU_IMPORT_TESTS "Build numerical import tests using private MLX primitives" OFF)
if(AI_MUSIC_CPU_IMPORT_TESTS)
  # The fixtures construct private primitive types, whose symbols are hidden
  # outside MLX. Keep that code in the library only when testing is requested.
  target_sources(mlx PRIVATE "${CMAKE_CURRENT_LIST_DIR}/import_tests.cpp")
  enable_testing()
  add_executable(mlx_cpu_import_tests test_main.cpp)
  target_compile_features(mlx_cpu_import_tests PRIVATE cxx_std_20)
  target_link_libraries(mlx_cpu_import_tests PRIVATE mlx)
  add_test(NAME mlx_cpu_import COMMAND mlx_cpu_import_tests "${CMAKE_CURRENT_BINARY_DIR}/import-fixtures")
endif()
