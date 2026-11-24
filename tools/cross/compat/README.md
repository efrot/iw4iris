# Cross-build compatibility shim

Used only by `tools/cross/build-iw4x.sh` when cross-compiling the IW4x client
on Linux with MinGW-w64 GCC 13 (the version Ubuntu 24.04 ships).

* `iw4iris_compat.h` — makes `std::ceilf` and friends visible for GCC < 14
  (libstdc++ added them in GCC 14). Needed by iw4-open-formats. It does not
  change program behaviour.

The IW4x sources also need mingw-w64 **13** headers and import libraries
(`CM_Get_Device_Interface_PropertyW`, `HREPORT`, ...). Ubuntu 24.04 ships
mingw-w64 11; see `BUILD.md` for installing the architecture-independent
mingw-w64 13 packages. Visual Studio builds need none of this.
