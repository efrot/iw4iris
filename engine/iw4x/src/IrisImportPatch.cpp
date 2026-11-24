// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "ImportPatch.hpp"

#include <cstring>

namespace IW4Iris::ImportPatch
{
  int Replace(HMODULE module, const char* dll, const char* function, void* replacement, void** original)
  {
    if (!module || !dll || !function || !replacement)
      return 0;
    auto* base = reinterpret_cast<unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
      return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
      return 0;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size)
      return 0;

    // The address the function resolves to, for imports by ordinal and for
    // modules whose name table is missing.
    void* resolved = nullptr;
    if (const HMODULE target = GetModuleHandleA(dll))
      resolved = reinterpret_cast<void*>(GetProcAddress(target, function));

    int replaced = 0;
    for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc)
    {
      const char* name = reinterpret_cast<const char*>(base + desc->Name);
      if (_stricmp(name, dll) != 0)
        continue;
      auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
      const auto* lookup = desc->OriginalFirstThunk
                               ? reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk)
                               : nullptr;
      for (; thunk->u1.Function; ++thunk, lookup = lookup ? lookup + 1 : nullptr)
      {
        bool match = false;
        if (lookup && !IMAGE_SNAP_BY_ORDINAL(lookup->u1.Ordinal))
        {
          const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + lookup->u1.AddressOfData);
          match = std::strcmp(reinterpret_cast<const char*>(byName->Name), function) == 0;
        }
        if (!match && resolved)
          match = reinterpret_cast<void*>(thunk->u1.Function) == resolved;
        if (!match)
          continue;

        DWORD protection = 0;
        if (!VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), PAGE_READWRITE, &protection))
          continue;
        if (original && !*original)
          *original = reinterpret_cast<void*>(thunk->u1.Function);
        thunk->u1.Function = reinterpret_cast<decltype(thunk->u1.Function)>(replacement);
        VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), protection, &protection);
        ++replaced;
      }
    }
    return replaced;
  }
}
