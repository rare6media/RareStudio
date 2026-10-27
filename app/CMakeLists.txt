find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebEngineWidgets)

add_executable(RareStudioApp WIN32
  main.cpp
  ObsCore.h ObsCore.cpp
  MultiStream.h MultiStream.cpp
  PreviewWidget.h PreviewWidget.cpp
  MixerPanel.h MixerPanel.cpp
  AudioPresets.h AudioPresets.cpp
  PropertiesDialog.h PropertiesDialog.cpp
  TransformDialog.h TransformDialog.cpp
  EncoderPool.h EncoderPool.cpp
  Recorder.h Recorder.cpp
  HotkeyManager.h HotkeyManager.cpp
  FiltersDialog.h FiltersDialog.cpp
  RareTransitions.h RareTransitions.cpp
  TransitionManager.h TransitionManager.cpp
  TransitionsDialog.h TransitionsDialog.cpp
  SelfTest.h SelfTest.cpp
  VerticalClip.h VerticalClip.cpp
  Accounts.h Accounts.cpp
  Secrets.h Secrets.cpp
  ClipsPanel.h ClipsPanel.cpp
  SettingsDialog.h SettingsDialog.cpp
  resources.qrc
  WebDock.h WebDock.cpp
  Updater.h Updater.cpp
  MainWindow.h MainWindow.cpp
  app.rc
)

target_compile_definitions(RareStudioApp PRIVATE
  RARESTUDIO_VERSION="${PROJECT_VERSION}"
  UNICODE _UNICODE NOMINMAX)

target_include_directories(RareStudioApp PRIVATE
  "${OBS_SOURCE_DIR}/libobs"
  "${OBS_BUILD_DIR}/config")

target_link_libraries(RareStudioApp PRIVATE
  Qt6::Widgets Qt6::Network Qt6::WebEngineWidgets crypt32 psapi
  "${OBS_BUILD_DIR}/libobs/Release/obs.lib")

# Stage a runnable tree: <build>/stage = Qt + OBS runtime + our exe.
set(STAGE "${CMAKE_BINARY_DIR}/stage")

# FFmpeg, x264, curl, srt, zlib come from OBS's prebuilt dependency pack.
file(GLOB OBS_DEP_DIRS "${OBS_SOURCE_DIR}/.deps/obs-deps-*-x64/bin")
list(GET OBS_DEP_DIRS 0 OBS_DEPS_BIN)
file(GLOB OBS_DEP_DLLS "${OBS_DEPS_BIN}/*.dll")
list(FILTER OBS_DEP_DLLS EXCLUDE REGEX "lua51|datachannel") # scripting / webrtc: not built in
add_custom_command(TARGET RareStudioApp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE}" "${STAGE}/bin/64bit" "${STAGE}/data/rarestudio"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:RareStudioApp>" "${STAGE}/"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${OBS_RUNDIR}/bin/64bit" "${STAGE}"
  COMMAND ${CMAKE_COMMAND} -E copy ${OBS_DEP_DLLS} "${STAGE}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${OBS_RUNDIR}/obs-plugins/64bit" "${STAGE}/obs-plugins"
  # Plugins that belong to OBS's own Qt UI or other platforms; loading them fails or wastes memory.
  COMMAND ${CMAKE_COMMAND} -E rm -f "${STAGE}/obs-plugins/frontend-tools.dll"
          "${STAGE}/obs-plugins/coreaudio-encoder.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${OBS_RUNDIR}/data" "${STAGE}/data"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/assets/logo.png" "${STAGE}/data/rarestudio/logo.png"
  COMMAND ${CMAKE_COMMAND} -E echo "${PROJECT_VERSION}" > "${STAGE}/version.txt"
  COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe" --release --no-translations
          --no-system-d3d-compiler --no-opengl-sw "${STAGE}/RareStudioApp.exe"
  COMMENT "Staging RareStudio runtime")
