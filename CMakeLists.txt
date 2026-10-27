cmake_minimum_required(VERSION 3.24)
project(RareStudio VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(OBS_SOURCE_DIR "C:/dev/obs-studio" CACHE PATH "OBS Studio source tree")
set(OBS_BUILD_DIR "${OBS_SOURCE_DIR}/build_x64" CACHE PATH "OBS build tree")
set(OBS_RUNDIR "${OBS_BUILD_DIR}/rundir/Release" CACHE PATH "Built OBS runtime (bin, data, obs-plugins)")

add_subdirectory(launcher)
add_subdirectory(app)
