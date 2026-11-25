// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Fuzzy matching for the command palette and other quick searches: every
// query character must appear in order (case-insensitive); consecutive
// characters, word starts and an early match score higher.
#pragma once

#include <optional>
#include <string_view>

namespace iris
{
    // Score of `text` for `query`, or nothing when it does not match. An
    // empty query matches everything with score 0. Whitespace in the query
    // separates words that may match anywhere, in any order.
    std::optional<int> FuzzyScore(std::string_view query, std::string_view text);
}  // namespace iris
