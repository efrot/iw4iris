// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Third-party: stb_truetype (public domain / MIT), static so it cannot clash with the copy IW4x links.
// Include from exactly one translation unit: it carries the implementation.
// The library is compiled with its own warnings silenced; the project's
// warning flags apply to IW4Iris code only.
#pragma once
#if defined(__GNUC__)
#pragma GCC system_header
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
