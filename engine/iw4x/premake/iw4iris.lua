-- IW4Iris — the editor libraries the cinematic client links: the
-- game-independent cinematic core and Dear ImGui (with its Win32 / DX9
-- backends and stb_image_write for the built-in PNG / JPEG writer).
--
-- Installed into deps/premake/ by tools/iw4x-overlay (with the sources in
-- deps/iw4iris/); paths are relative to deps/.
--
local targets = ...

local function git_revision (dir)
  local rev = os.outputof (string.format ('git -C "%s" rev-parse --short=12 HEAD', dir))
  return (rev and rev ~= "") and rev:gsub ("%s+", "") or "unknown"
end

local iw4x_revision = git_revision (path.join (_MAIN_SCRIPT_DIR))

targets:static_library ("iw4iris-imgui",
{
  directory = "iw4iris/imgui",
  language  = "C++",
  warnings  = "Off",

  files =
  {
    "imgui.cpp",
    "imgui_draw.cpp",
    "imgui_tables.cpp",
    "imgui_widgets.cpp",
    "backends/imgui_impl_dx9.cpp",
    "backends/imgui_impl_win32.cpp",
    "misc/cpp/imgui_stdlib.cpp"
  },

  includedirs = {"."},
  defines     = {"IMGUI_IMPL_WIN32_DISABLE_GAMEPAD", "IMGUI_DISABLE_OBSOLETE_FUNCTIONS"},

  export =
  {
    includedirs = {".", "backends"},
    defines     = {"IMGUI_IMPL_WIN32_DISABLE_GAMEPAD", "IMGUI_DISABLE_OBSOLETE_FUNCTIONS"}
  }
})

targets:static_library ("iw4iris-core",
{
  directory = "iw4iris",
  language  = "C++",

  files =
  {
    "core/src/*.cpp",
    "stb_impl.cpp"
  },

  includedirs = {"core/include", "stb", "../json/single_include"},
  defines     = {"_CRT_SECURE_NO_WARNINGS"},

  export =
  {
    includedirs = {"core/include", "stb", "../json/single_include"},
    defines     = {"IW4IRIS_IW4X_REVISION=\"" .. iw4x_revision .. "\""}
  }
})
