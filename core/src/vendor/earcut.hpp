// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Third-party: mapbox/earcut.hpp (ISC licence), polygon triangulation with
// holes, included with its own warnings silenced.
#pragma once
#if defined(__GNUC__)
#pragma GCC system_header
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <mapbox/earcut.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
