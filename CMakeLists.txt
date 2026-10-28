cmake_minimum_required(VERSION 3.24)
project(RareStudio VERSION 0.11.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Build identifier shown beside the version (splash, Settings > Updates, crash reports): short commit,
# with a "+" when the tree has uncommitted changes. Evaluated at configure time.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE RARESTUDIO_BUILD OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE RARESTUDIO_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT RARESTUDIO_BUILD)
  set(RARESTUDIO_BUILD "dev")
elseif(RARESTUDIO_DIRTY)
  set(RARESTUDIO_BUILD "${RARESTUDIO_BUILD}+")
endif()

set(OBS_SOURCE_DIR "C:/dev/obs-studio" CACHE PATH "OBS Studio source tree")
set(OBS_BUILD_DIR "${OBS_SOURCE_DIR}/build_x64" CACHE PATH "OBS build tree")
set(OBS_RUNDIR "${OBS_BUILD_DIR}/rundir/Release" CACHE PATH "Built OBS runtime (bin, data, obs-plugins)")

add_subdirectory(launcher)
add_subdirectory(app)
