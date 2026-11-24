// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Third-party: stb_image (public domain / MIT), static, PNG / JPEG / BMP / TGA from memory.
// Include from exactly one translation unit: it carries the implementation.
// The library is compiled with its own warnings silenced; the project's
// warning flags apply to IW4Iris code only.
#pragma once
#if defined(__GNUC__)
#pragma GCC system_header
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
