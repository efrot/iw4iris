# IW4Iris Launcher (Windows only): Win32 + Direct3D 9 + Dear ImGui.
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")

add_executable(iw4iris_launcher WIN32
  src/Main.cpp
  src/Environment.cpp
  src/InstallPlan.cpp
  res/launcher.rc
  ${CMAKE_SOURCE_DIR}/core/src/diagnostics.cpp
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)
set_target_properties(iw4iris_launcher PROPERTIES OUTPUT_NAME "IW4Iris Launcher")
target_include_directories(iw4iris_launcher PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends ${CMAKE_SOURCE_DIR}/core/include)
target_include_directories(iw4iris_launcher SYSTEM PRIVATE "${IW4IRIS_JSON_INCLUDE}")
target_compile_definitions(iw4iris_launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  IMGUI_IMPL_WIN32_DISABLE_GAMEPAD IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(iw4iris_launcher PRIVATE d3d9 dwmapi shell32 ole32 advapi32)
if(MINGW)
  target_link_options(iw4iris_launcher PRIVATE -static -municode)
elseif(MSVC)
  set_property(TARGET iw4iris_launcher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # res/launcher.rc embeds launcher.manifest (DPI awareness, common
  # controls); the linker's own default manifest would be a duplicate
  # resource (CVT1100).
  target_link_options(iw4iris_launcher PRIVATE /MANIFEST:NO)
endif()
# Only the launcher's own sources get the strict warnings.
set_source_files_properties(src/Main.cpp src/Environment.cpp src/InstallPlan.cpp PROPERTIES COMPILE_OPTIONS
  "$<IF:$<CXX_COMPILER_ID:MSVC>,/W4;/permissive-;/utf-8,-Wall;-Wextra>")
