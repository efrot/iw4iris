// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Third-party: nanosvg (zlib licence) SVG parser.
//
// Without NANOSVG_IMPLEMENTATION this declares the API (logo.cpp); with it
// (only core/src/nanosvg_impl.cpp) it carries the implementation. The
// implementation lives in a translation unit of its own so MSVC cannot
// inline nanosvg into IW4Iris functions, where its warnings (C4702,
// unreachable code, reported at /W4 /WX) would count as ours. The library
// is compiled with its own warnings silenced; the project's warning flags
// apply to IW4Iris code only.
#pragma once
#if defined(__GNUC__)
#pragma GCC system_header
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stdio.h>  // nanosvg.h's declarations use FILE
#include <nanosvg.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
