// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The asset library behind the character, animation, equipment, prop and
// effect browsers. The engine reports the names of the assets the loaded
// fast files actually contain; this module turns them into human-readable,
// categorised entries. Nothing here assumes an asset exists: unknown names
// land in "Other" with a readable label, and the raw name is always kept.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace iris
{
    enum class AssetKind
    {
        Body,
        Head,
        Model,      // props, vehicles, world objects
        Animation,
        Weapon,
        Effect,
    };
    std::string_view ToString(AssetKind kind);

    struct AssetEntry
    {
        std::string name;      // raw engine name
        std::string label;     // human-readable
        std::string category;  // browser group
        AssetKind kind = AssetKind::Model;
        std::vector<std::string> tags;  // extra search terms
        bool hidden = false;   // engine internals only shown in professional mode
    };

    // "mp_body_tf141_lmg" -> "Tf141 Lmg"-style readable text (prefixes and
    // underscores removed, words capitalised, known abbreviations expanded).
    std::string HumanizeAssetName(std::string_view name);

    AssetEntry ClassifyModel(std::string_view name);
    AssetEntry ClassifyAnimation(std::string_view name);
    AssetEntry ClassifyWeapon(std::string_view name);
    AssetEntry ClassifyEffect(std::string_view name);

    // Display name of a weapon's base ("cheytac" -> "Intervention"), or the
    // humanised name when it is not a known multiplayer weapon.
    std::string WeaponDisplayName(std::string_view weaponName);
    // The attachments encoded in a weapon name ("cheytac_acog_fmj_mp" -> ACOG, FMJ).
    std::vector<std::string> WeaponAttachments(std::string_view weaponName);

    // A body with its best matching head.
    struct CharacterEntry
    {
        std::string label;
        std::string category;  // faction
        std::string body;
        std::string head;      // empty when no head matched
        std::vector<std::string> tags;
    };

    // Pairs every body model with the head that shares the most name tokens.
    std::vector<CharacterEntry> BuildCharacters(const std::vector<std::string>& modelNames);

    // Case-insensitive search over label, category, raw name and tags; every
    // whitespace-separated term must match.
    bool MatchesSearch(const AssetEntry& entry, std::string_view query);
    bool MatchesSearch(const CharacterEntry& entry, std::string_view query);

    // Picks the first animation whose name contains every one of `tokens`
    // (in order of preference), for templates and directions.
    std::string FindAnimation(const std::vector<std::string>& animations, const std::vector<std::vector<std::string>>& preferences);

    // The categories in browser order.
    const std::vector<std::string>& CharacterCategories();
    const std::vector<std::string>& AnimationCategories();
    const std::vector<std::string>& PropCategories();
    const std::vector<std::string>& EffectCategories();
}  // namespace iris
