/*
 * IW4Iris — cross-build compatibility shim (build environment only).
 *
 * Ubuntu 24.04 ships mingw-w64 v11 with GCC 13, whose libstdc++ does not
 * declare the float overloads std::ceilf / std::floorf (added with GCC 14).
 * This header is force-included by tools/cross/build-iw4x.sh only; it does
 * not change behaviour, it only makes the C library functions visible in
 * namespace std. Not used for MSVC or newer MinGW toolchains.
 */
#pragma once
#ifdef __cplusplus
#include <cmath>
#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ < 14)
namespace std
{
  using ::ceilf;
  using ::floorf;
  using ::sqrtf;
  using ::roundf;
  using ::truncf;
}
#endif
#endif
