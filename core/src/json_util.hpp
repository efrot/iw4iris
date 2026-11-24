// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// JSON helpers shared by the project and scene serialisers (internal).
#pragma once

#include <cmath>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "iris/curve.hpp"
#include "iris/math.hpp"

namespace iris::detail
{
    using json = nlohmann::json;

    inline json ToJson(const Vec3& v) { return json::array({v.x, v.y, v.z}); }
    inline json ToJson(const EulerAngles& a) { return json::array({a.pitch, a.yaw, a.roll}); }

    inline Vec3 Vec3From(const json& j, const Vec3& fallback = {})
    {
        if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number())
            return fallback;
        return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
    }

    inline EulerAngles AnglesFrom(const json& j, const EulerAngles& fallback = {})
    {
        const Vec3 v = Vec3From(j, {fallback.pitch, fallback.yaw, fallback.roll});
        return {v.x, v.y, v.z};
    }

    template <typename T>
    T Get(const json& j, std::string_view key, const T& fallback)
    {
        const auto it = j.find(key);
        if (it == j.end() || it->is_null())
            return fallback;
        try
        {
            return it->get<T>();
        }
        catch (const json::exception&)
        {
            return fallback;
        }
    }

    inline double GetNumber(const json& j, std::string_view key, double fallback)
    {
        const auto it = j.find(key);
        if (it == j.end() || !it->is_number())
            return fallback;
        const double v = it->get<double>();
        return std::isfinite(v) ? v : fallback;
    }

    inline json EaseJson(const Ease& e) { return {{"in", e.in}, {"out", e.out}}; }
    inline Ease EaseFrom(const json& j)
    {
        Ease e;
        if (j.is_object())
        {
            e.in = Clamp(GetNumber(j, "in", 0.0), 0.0, 1.0);
            e.out = Clamp(GetNumber(j, "out", 0.0), 0.0, 1.0);
        }
        return e;
    }

    inline json ScalarKeyJson(const ScalarKey& k)
    {
        json j{{"t", k.time},
               {"v", k.value},
               {"interp", ToString(k.interpolation)},
               {"tangent", ToString(k.tangentMode)}};
        if (k.ease.in != 0.0 || k.ease.out != 0.0)
            j["ease"] = EaseJson(k.ease);
        if (k.interpolation == Interpolation::Bezier || k.tangentMode == TangentMode::Free ||
            k.tangentMode == TangentMode::Aligned)
        {
            j["inHandle"] = json::array({k.inHandle.dt, k.inHandle.dv});
            j["outHandle"] = json::array({k.outHandle.dt, k.outHandle.dv});
        }
        return j;
    }

    inline ScalarKey ScalarKeyFrom(const json& j)
    {
        ScalarKey k;
        k.time = GetNumber(j, "t", 0.0);
        k.value = GetNumber(j, "v", 0.0);
        k.interpolation = InterpolationFromString(Get<std::string>(j, "interp", "smooth")).value_or(Interpolation::Smooth);
        k.tangentMode = TangentModeFromString(Get<std::string>(j, "tangent", "auto")).value_or(TangentMode::Auto);
        if (j.contains("ease"))
            k.ease = EaseFrom(j["ease"]);
        auto handle = [&](std::string_view key, Handle fallback) {
            const auto it = j.find(key);
            if (it != j.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number())
                return Handle{(*it)[0].get<double>(), (*it)[1].get<double>()};
            return fallback;
        };
        k.inHandle = handle("inHandle", k.inHandle);
        k.outHandle = handle("outHandle", k.outHandle);
        return k;
    }

    inline json CurveJson(const ScalarCurve& c)
    {
        json keys = json::array();
        for (const auto& k : c.Keys())
            keys.push_back(ScalarKeyJson(k));
        return {{"default", c.DefaultValue()}, {"keys", keys}};
    }

    inline ScalarCurve CurveFrom(const json& j, double defaultValue)
    {
        ScalarCurve c(defaultValue);
        if (!j.is_object())
            return c;
        c.SetDefaultValue(GetNumber(j, "default", defaultValue));
        std::vector<ScalarKey> keys;
        if (const auto it = j.find("keys"); it != j.end() && it->is_array())
            for (const auto& k : *it)
                if (k.is_object())
                    keys.push_back(ScalarKeyFrom(k));
        c.SetKeys(std::move(keys));
        return c;
    }
}  // namespace iris::detail
