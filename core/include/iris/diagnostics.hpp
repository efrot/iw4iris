// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// What works right now, in plain terms: each capability (render clock,
// camera override, depth occlusion, Director Mod, capture ...) has a state
// and, when it is not ready, why and what to do. The editor and the
// launcher fill it from real checks; this file only holds the model, the
// report formats and the redaction used for support bundles.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace iris
{
    enum class CapabilityState
    {
        Ready,         // checked and working
        Checking,      // the check has not finished (e.g. waiting for a frame)
        Limited,       // works with a documented limitation
        NotAvailable,  // not present in this setup (not an error)
        Failed,        // checked and broken
    };
    std::string_view ToString(CapabilityState state);

    struct Capability
    {
        std::string id;        // "render-clock"
        std::string name;      // "Render clock"
        std::string area;      // "Engine", "Director", "Graphics", "Capture", "Installation" ...
        CapabilityState state = CapabilityState::Checking;
        std::string detail;    // what was found
        std::string fix;       // what to do when it is not ready (empty: nothing to do)
    };

    struct DiagnosticsReport
    {
        std::string product;   // "IW4Iris 0.3.0"
        std::string build;     // IW4x revision, compiler, ...
        std::string createdAt; // ISO 8601, UTC
        std::vector<Capability> capabilities;
        std::vector<std::string> log;  // recent log lines (already redacted)

        // The worst state: Failed > Checking > Limited > Ready; NotAvailable
        // never makes a report worse.
        CapabilityState Overall() const;
        int Count(CapabilityState state) const;
    };

    std::string ReportText(const DiagnosticsReport& report);
    nlohmann::json ReportJson(const DiagnosticsReport& report);

    // Personal information a support bundle must not carry: the user's home
    // folder (as written, with either slash, any letter case on Windows)
    // and the account name are replaced with placeholders.
    struct Redaction
    {
        std::vector<std::string> homes;     // e.g. "C:\\Users\\Name", "/home/name"
        std::vector<std::string> userNames; // e.g. "Name"
    };
    std::string Redact(std::string_view text, const Redaction& redaction);
}  // namespace iris
