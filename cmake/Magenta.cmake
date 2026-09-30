# Pin the inference engine and its dependencies. Build only its reusable core;
# upstream's top-level build also assembles several GUI apps and npm projects.
enable_language(OBJCXX)
set(CMAKE_OBJCXX_STANDARD 20)
include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Compatibility for upstream dependencies")
set(ABSL_INTERNAL_AT_LEAST_CXX20 ON CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON)

FetchContent_Declare(magenta
  GIT_REPOSITORY https://github.com/magenta/magenta-realtime.git
  GIT_TAG 694a545e4ba0b88bf1150137b129582166d3e07f
  GIT_SUBMODULES ""
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(magenta)

# The pinned upstream encoder detaches a thread capturing model state. Own and
# join it before teardown, and close the pending-prompt/idle handoff race.
foreach(patch_name magenta-owned-prompt-worker magenta-metrics magenta-worker-qos magenta-stereo-read)
  set(patch "${CMAKE_CURRENT_LIST_DIR}/../patches/${patch_name}.patch")
  execute_process(COMMAND git apply --reverse --check "${patch}"
    WORKING_DIRECTORY "${magenta_SOURCE_DIR}" RESULT_VARIABLE patch_present
    OUTPUT_QUIET ERROR_QUIET)
  if(NOT patch_present EQUAL 0)
    execute_process(COMMAND git apply --check "${patch}"
      WORKING_DIRECTORY "${magenta_SOURCE_DIR}" RESULT_VARIABLE patch_valid)
    if(NOT patch_valid EQUAL 0)
      message(FATAL_ERROR "Magenta source does not match the pinned ${patch_name} patch")
    endif()
    execute_process(COMMAND git apply "${patch}"
      WORKING_DIRECTORY "${magenta_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
  endif()
endforeach()

set(AI_MUSIC_MLX_ROOT "" CACHE PATH "Prebuilt MLX prefix (python -m mlx --cmake-dir)")
if(AI_MUSIC_MLX_ROOT)
  find_package(MLX 0.31.1 EXACT CONFIG REQUIRED
    PATHS "${AI_MUSIC_MLX_ROOT}/share/cmake/MLX" NO_DEFAULT_PATH)
  # The wheel records the build machine's SDK paths; use the active SDK.
  set_target_properties(mlx PROPERTIES INTERFACE_LINK_LIBRARIES
    "-framework Metal;-framework Foundation;-framework QuartzCore;-framework Accelerate")
else()
  foreach(feature TESTS EXAMPLES BENCHMARKS PYTHON_BINDINGS PYTHON_STUBS GGUF CUDA)
    set(MLX_BUILD_${feature} OFF CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(mlx
    GIT_REPOSITORY https://github.com/ml-explore/mlx.git
    GIT_TAG v0.31.1 GIT_SHALLOW ON)
  FetchContent_MakeAvailable(mlx)
  # This CPU fallback is expensive to optimize; MRT2 runs on Metal.
  set_source_files_properties("${mlx_SOURCE_DIR}/mlx/backend/cpu/binary.cpp"
    TARGET_DIRECTORY mlx PROPERTIES COMPILE_OPTIONS "-O1")
endif()

set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sentencepiece
  GIT_REPOSITORY https://github.com/google/sentencepiece.git
  GIT_TAG v0.2.0 GIT_SHALLOW ON)
FetchContent_MakeAvailable(sentencepiece)

set(TFLITE_ENABLE_XNNPACK OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_GPU OFF CACHE BOOL "" FORCE)
set(TFLITE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tensorflow-lite
  GIT_REPOSITORY https://github.com/tensorflow/tensorflow.git
  GIT_TAG v2.21.0 GIT_SHALLOW ON
  SOURCE_SUBDIR tensorflow/lite)
# TFLite otherwise fetches a second, incompatible TensorFlow source tree.
set(FETCHCONTENT_SOURCE_DIR_TENSORFLOW "${FETCHCONTENT_BASE_DIR}/tensorflow-lite-src"
  CACHE PATH "TensorFlow source shared with TFLite" FORCE)
FetchContent_MakeAvailable(tensorflow-lite)
add_subdirectory("${magenta_SOURCE_DIR}/core" "${magenta_BINARY_DIR}/core" EXCLUDE_FROM_ALL)
