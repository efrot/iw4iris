// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The IW4x component that hosts IW4Iris. Registered by Components::Loader in
// the cinematic build (see engine/iw4x/patches/).
#pragma once

namespace Components
{
  class Cinematic : public Component
  {
  public:
    Cinematic();
    ~Cinematic() override;
  };
}
