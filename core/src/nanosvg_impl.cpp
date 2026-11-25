// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The one translation unit holding nanosvg's implementation (see
// vendor/nanosvg_impl.hpp).
#if defined(_MSC_VER)
// C4702 (unreachable code) is a code-generation warning: MSVC reports it
// with the state at the end of the translation unit, so the push / pop in
// the wrapper does not reach it. This unit holds only nanosvg (e.g. its
// "return c.value;" after a switch that returns in every case).
#pragma warning(disable : 4702)
#endif
#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "vendor/nanosvg_impl.hpp"
