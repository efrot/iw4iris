// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/diagnostics.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace iris
{
    std::string_view ToString(CapabilityState state)
    {
        switch (state)
        {
            case CapabilityState::Ready: return "Ready";
            case CapabilityState::Checking: return "Checking";
            case CapabilityState::Limited: return "Limited";
            case CapabilityState::NotAvailable: return "Not available";
            case CapabilityState::Failed: return "Failed";
        }
        return "Unknown";
    }

    namespace
    {
        int Severity(CapabilityState s)
        {
            switch (s)
            {
                case CapabilityState::Failed: return 3;
                case CapabilityState::Checking: return 2;
                case CapabilityState::Limited: return 1;
                default: return 0;
            }
        }

        char Fold(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char>(c - 'A' + 'a');
            return c == '\\' ? '/' : c;
        }

        // Replaces every occurrence of `needle` in `text`, comparing
        // case-insensitively and treating '\' and '/' alike.
        std::string ReplaceFolded(std::string text, std::string_view needle, std::string_view with, bool wholeWord)
        {
            if (needle.empty())
                return text;
            auto isWord = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                       c == '.';
            };
            std::string out;
            out.reserve(text.size());
            std::size_t i = 0;
            while (i < text.size())
            {
                bool match = i + needle.size() <= text.size();
                for (std::size_t k = 0; match && k < needle.size(); ++k)
                    match = Fold(text[i + k]) == Fold(needle[k]);
                if (match && wholeWord)
                {
                    const bool before = i > 0 && isWord(text[i - 1]);
                    const bool after = i + needle.size() < text.size() && isWord(text[i + needle.size()]);
                    match = !before && !after;
                }
                if (match)
                {
                    out += with;
                    i += needle.size();
                }
                else
                    out += text[i++];
            }
            return out;
        }
    }  // namespace

    CapabilityState DiagnosticsReport::Overall() const
    {
        CapabilityState worst = CapabilityState::Ready;
        for (const auto& c : capabilities)
            if (Severity(c.state) > Severity(worst))
                worst = c.state;
        return worst;
    }

    int DiagnosticsReport::Count(CapabilityState state) const
    {
        return static_cast<int>(std::count_if(capabilities.begin(), capabilities.end(),
                                               [&](const Capability& c) { return c.state == state; }));
    }

    std::string ReportText(const DiagnosticsReport& r)
    {
        std::string out = r.product + "\n" + r.build + "\n" + r.createdAt + "\n\nOverall: " +
                          std::string(ToString(r.Overall())) + "\n";
        std::string area;
        for (const auto& c : r.capabilities)
        {
            if (c.area != area)
            {
                area = c.area;
                out += "\n[" + area + "]\n";
            }
            out += "  " + c.name + ": " + std::string(ToString(c.state));
            if (!c.detail.empty())
                out += " - " + c.detail;
            out += "\n";
            if (!c.fix.empty())
                out += "      fix: " + c.fix + "\n";
        }
        if (!r.log.empty())
        {
            out += "\n[Log]\n";
            for (const auto& l : r.log)
                out += "  " + l + "\n";
        }
        return out;
    }

    nlohmann::json ReportJson(const DiagnosticsReport& r)
    {
        nlohmann::json caps = nlohmann::json::array();
        for (const auto& c : r.capabilities)
            caps.push_back({{"id", c.id},
                            {"name", c.name},
                            {"area", c.area},
                            {"state", std::string(ToString(c.state))},
                            {"detail", c.detail},
                            {"fix", c.fix}});
        return {{"format", "iw4iris-diagnostics"},
                {"version", 1},
                {"product", r.product},
                {"build", r.build},
                {"createdAt", r.createdAt},
                {"overall", std::string(ToString(r.Overall()))},
                {"capabilities", caps},
                {"log", r.log}};
    }

    std::string Redact(std::string_view text, const Redaction& redaction)
    {
        std::string out(text);
        // Longest homes first, so "C:/Users/Name/Documents" style entries
        // cannot leave a partial path behind.
        auto homes = redaction.homes;
        std::sort(homes.begin(), homes.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
        for (const auto& h : homes)
        {
            std::string_view home = h;
            while (!home.empty() && (home.back() == '/' || home.back() == '\\'))
                home.remove_suffix(1);
            if (home.size() >= 3)
                out = ReplaceFolded(out, home, "<home>", false);
        }
        for (const auto& u : redaction.userNames)
            if (u.size() >= 2)
                out = ReplaceFolded(out, u, "<user>", true);
        return out;
    }
}  // namespace iris
