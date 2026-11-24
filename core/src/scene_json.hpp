// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// (De)serialisation of the scene section of a project (internal).
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "iris/scene.hpp"

namespace iris::detail
{
    nlohmann::json SceneToJson(const Scene& scene);
    Scene SceneFromJson(const nlohmann::json& j, std::vector<std::string>& warnings);
}  // namespace iris::detail
