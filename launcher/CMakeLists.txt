# Tiny launcher: the exe the desktop shortcut points at. It never changes between
# updates; it just starts the newest installed copy of RareStudioApp.exe.
add_executable(RareStudio WIN32 launcher.cpp launcher.rc)
set_target_properties(RareStudio PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_compile_definitions(RareStudio PRIVATE UNICODE _UNICODE)
target_link_libraries(RareStudio PRIVATE shell32 ole32)
