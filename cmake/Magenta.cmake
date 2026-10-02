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
set(magenta_patches)
foreach(patch_name magenta-owned-prompt-worker magenta-metrics magenta-worker-qos magenta-stereo-read magenta-live-performance magenta-stereo-write magenta-prompt-log magenta-producer-trace magenta-buffer-capacity)
  list(APPEND magenta_patches "${CMAKE_CURRENT_LIST_DIR}/../patches/${patch_name}.patch")
endforeach()
# Later patches extend earlier hunks. Reverse/apply sequentially on copies of
# only the five patched files; git apply --check does not stage earlier changes.
function(music_check_magenta_series reverse patches result)
  set(stage "${magenta_BINARY_DIR}/patch-check")
  foreach(file core/src/mlx_engine.cpp core/src/realtime_runner.cpp
      core/include/magentart/mlx_engine.h core/include/magentart/realtime_runner.h
      core/include/magentart/ring_buffer.h)
    get_filename_component(directory "${stage}/${file}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(COPY_FILE "${magenta_SOURCE_DIR}/${file}" "${stage}/${file}")
  endforeach()
  set(ordered_patches ${${patches}})
  if(reverse)
    list(REVERSE ordered_patches)
    set(reverse_argument --reverse)
  endif()
  foreach(patch IN LISTS ordered_patches)
    execute_process(COMMAND git "--git-dir=${magenta_SOURCE_DIR}/.git" "--work-tree=${stage}"
        apply ${reverse_argument} "${patch}"
      WORKING_DIRECTORY "${stage}" RESULT_VARIABLE failed OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT failed EQUAL 0)
      set(${result} FALSE PARENT_SCOPE)
      set(${result}_error "${error}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${result} TRUE PARENT_SCOPE)
endfunction()
# Find the installed prefix so appending a new patch also upgrades an existing
# dependency checkout. Validate the missing tail before changing any source.
set(magenta_known_patches ${magenta_patches})
set(magenta_missing_patches)
while(magenta_known_patches)
  music_check_magenta_series(TRUE magenta_known_patches patch_present)
  if(patch_present)
    break()
  endif()
  list(POP_BACK magenta_known_patches next_patch)
  list(PREPEND magenta_missing_patches "${next_patch}")
endwhile()
if(magenta_missing_patches)
  music_check_magenta_series(FALSE magenta_missing_patches patch_valid)
  if(NOT patch_valid)
    message(FATAL_ERROR "Magenta source does not match the pinned patch series: ${patch_valid_error}")
  endif()
  foreach(patch IN LISTS magenta_missing_patches)
    execute_process(COMMAND git apply "${patch}"
      WORKING_DIRECTORY "${magenta_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
  endforeach()
endif()

set(AI_MUSIC_MLX_ROOT "" CACHE PATH "Prebuilt MLX prefix (site-packages/mlx)")
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
target_include_directories(magentart_core PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../include")
