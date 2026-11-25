// IW4Iris - Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/search.hpp"

#include <algorithm>

namespace iris
{
    namespace
    {
        char Lower(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool WordStart(std::string_view text, std::size_t i)
        {
            if (i == 0)
                return true;
            const char p = text[i - 1], c = text[i];
            const bool pAlnum = (p >= 'a' && p <= 'z') || (p >= 'A' && p <= 'Z') || (p >= '0' && p <= '9');
            return !pAlnum || (p >= 'a' && p <= 'z' && c >= 'A' && c <= 'Z');
        }

        // One word of the query against the text: in-order subsequence,
        // greedy from each possible start, best score kept.
        std::optional<int> WordScore(std::string_view word, std::string_view text)
        {
            std::optional<int> best;
            for (std::size_t start = 0; start < text.size(); ++start)
            {
                if (Lower(text[start]) != Lower(word[0]))
                    continue;
                int score = 0;
                std::size_t ti = start, qi = 0, last = std::string_view::npos;
                while (qi < word.size() && ti < text.size())
                {
                    if (Lower(text[ti]) == Lower(word[qi]))
                    {
                        score += 1;
                        if (last != std::string_view::npos && ti == last + 1)
                            score += 6;  // consecutive
                        if (WordStart(text, ti))
                            score += 8;  // start of a word
                        last = ti;
                        ++qi;
                    }
                    ++ti;
                }
                if (qi < word.size())
                    break;  // later starts cannot match either
                score -= static_cast<int>(std::min<std::size_t>(start, 20));  // earlier is better
                if (!best || score > *best)
                    best = score;
            }
            return best;
        }
    }  // namespace

    std::optional<int> FuzzyScore(std::string_view query, std::string_view text)
    {
        int total = 0;
        bool any = false;
        std::size_t i = 0;
        while (i < query.size())
        {
            while (i < query.size() && (query[i] == ' ' || query[i] == '\t'))
                ++i;
            std::size_t j = i;
            while (j < query.size() && query[j] != ' ' && query[j] != '\t')
                ++j;
            if (j > i)
            {
                const auto score = WordScore(query.substr(i, j - i), text);
                if (!score)
                    return std::nullopt;
                total += *score;
                any = true;
            }
            i = j;
        }
        if (!any)
            return 0;
        // Shorter texts are a closer match for the same characters.
        return total * 4 - static_cast<int>(std::min<std::size_t>(text.size(), 60) / 4);
    }
}  // namespace iris
