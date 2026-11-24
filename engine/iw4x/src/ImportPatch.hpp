// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Import address table patching. Replaces the address a module calls for an
// imported function, which needs no knowledge of the game's code: the PE
// import directory says where each import lives.
#pragma once

#include <windows.h>

namespace IW4Iris::ImportPatch
{
  // Replaces every import of `function` from `dll` (case-insensitive, by
  // name or by ordinal resolving to the same address) in `module`. Stores the
  // previous address in `original` if given. Returns how many entries were
  // replaced.
  int Replace(HMODULE module, const char* dll, const char* function, void* replacement, void** original);
}
