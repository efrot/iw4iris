// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/assets.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace iris
{
    std::string_view ToString(AssetKind kind)
    {
        switch (kind)
        {
            case AssetKind::Body: return "body";
            case AssetKind::Head: return "head";
            case AssetKind::Model: return "model";
            case AssetKind::Animation: return "animation";
            case AssetKind::Weapon: return "weapon";
            case AssetKind::Effect: return "effect";
        }
        return "model";
    }

    namespace
    {
        std::string Lower(std::string_view s)
        {
            std::string out(s);
            for (auto& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        bool Has(const std::string& s, std::string_view token) { return s.find(token) != std::string::npos; }
        bool StartsWith(const std::string& s, std::string_view p) { return s.rfind(p, 0) == 0; }

        std::vector<std::string> Tokens(std::string_view s)
        {
            std::vector<std::string> out;
            std::string cur;
            for (const char ch : s)
            {
                if (ch == '_' || ch == '/' || ch == '\\' || ch == '-' || ch == ' ' || ch == '.')
                {
                    if (!cur.empty())
                        out.push_back(Lower(cur));
                    cur.clear();
                }
                else
                    cur += ch;
            }
            if (!cur.empty())
                out.push_back(Lower(cur));
            return out;
        }

        // Readable forms of common tokens in IW4 asset names.
        const std::map<std::string, std::string>& Words()
        {
            static const std::map<std::string, std::string> w{
                {"tf141", "TF141"},        {"us", "US"},           {"army", "Army"},       {"seal", "SEAL"},
                {"udt", "UDT"},            {"opforce", "OpFor"},   {"lmg", "LMG"},         {"smg", "SMG"},
                {"ar", "Rifleman"},        {"assault", "Assault"}, {"shotgun", "Shotgun"}, {"sniper", "Sniper"},
                {"riot", "Riot"},          {"ghillie", "Ghillie"}, {"mp", ""},             {"body", ""},
                {"head", ""},              {"pb", ""},             {"pt", ""},             {"com", ""},
                {"fx", ""},                {"vehicle", ""},        {"crch", "Crouch"},     {"fwd", "Forward"},
                {"bk", "Back"},            {"lt", "Left"},         {"rt", "Right"},        {"ads", "Aim down sights"},
                {"hip", "Hip"},            {"alert", "Alert"},     {"pistol", "Pistol"},   {"rpg", "RPG"},
                {"heli", "Helicopter"},    {"ac130", "AC-130"},    {"uav", "UAV"},         {"ch", ""},
                {"bc", ""},                {"me", ""},             {"hu", ""},             {"mil", "Military"},
            };
            return w;
        }

        std::string Capitalise(std::string s)
        {
            if (!s.empty())
                s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
            return s;
        }

        std::string Join(const std::vector<std::string>& parts)
        {
            std::string out;
            for (const auto& p : parts)
            {
                if (p.empty())
                    continue;
                if (!out.empty())
                    out += ' ';
                out += p;
            }
            return out;
        }

        std::string FactionOf(const std::string& n)
        {
            if (Has(n, "ghillie"))
                return "Ghillie suits";
            if (Has(n, "tf141"))
                return "Task Force 141";
            if (Has(n, "seal") || Has(n, "udt"))
                return "Navy SEALs";
            if (Has(n, "us_army") || Has(n, "ranger"))
                return "Rangers";
            if (Has(n, "militia"))
                return "Militia";
            if (Has(n, "russian") || Has(n, "airborne") || Has(n, "spetsnaz"))
                return "Spetsnaz";
            if (Has(n, "opforce") || Has(n, "arab") || Has(n, "africa"))
                return "OpFor";
            return "";
        }

        struct WeaponName
        {
            const char* base;
            const char* label;
            const char* category;
        };

        // Multiplayer weapon bases of MW2 (2009) and their in-game names.
        const std::vector<WeaponName>& WeaponTable()
        {
            static const std::vector<WeaponName> t{
                {"cheytac", "Intervention", "Sniper rifles"},  {"barrett", "Barrett .50cal", "Sniper rifles"},
                {"wa2000", "WA2000", "Sniper rifles"},          {"m21", "M21 EBR", "Sniper rifles"},
                {"m4", "M4A1", "Assault rifles"},               {"famas", "FAMAS", "Assault rifles"},
                {"scar", "SCAR-H", "Assault rifles"},           {"tavor", "TAR-21", "Assault rifles"},
                {"fal", "FAL", "Assault rifles"},               {"m16", "M16A4", "Assault rifles"},
                {"masada", "ACR", "Assault rifles"},            {"fn2000", "F2000", "Assault rifles"},
                {"ak47", "AK-47", "Assault rifles"},            {"mp5k", "MP5K", "Submachine guns"},
                {"ump45", "UMP45", "Submachine guns"},          {"kriss", "Vector", "Submachine guns"},
                {"p90", "P90", "Submachine guns"},              {"uzi", "Mini-Uzi", "Submachine guns"},
                {"rpd", "RPD", "Light machine guns"},           {"sa80", "L86 LSW", "Light machine guns"},
                {"mg4", "MG4", "Light machine guns"},           {"m240", "M240", "Light machine guns"},
                {"aug", "AUG HBAR", "Light machine guns"},      {"pp2000", "PP2000", "Machine pistols"},
                {"tmp", "TMP", "Machine pistols"},              {"glock", "G18", "Machine pistols"},
                {"beretta393", "M93 Raffica", "Machine pistols"}, {"spas12", "SPAS-12", "Shotguns"},
                {"aa12", "AA-12", "Shotguns"},                  {"striker", "Striker", "Shotguns"},
                {"ranger", "Ranger", "Shotguns"},               {"m1014", "M1014", "Shotguns"},
                {"model1887", "Model 1887", "Shotguns"},        {"usp", "USP .45", "Handguns"},
                {"coltanaconda", ".44 Magnum", "Handguns"},     {"beretta", "M9", "Handguns"},
                {"deserteagle", "Desert Eagle", "Handguns"},    {"at4", "AT4-HS", "Launchers"},
                {"stinger", "Stinger", "Launchers"},            {"javelin", "Javelin", "Launchers"},
                {"rpg", "RPG-7", "Launchers"},                  {"m79", "Thumper", "Launchers"},
                {"riotshield", "Riot Shield", "Equipment"},
            };
            return t;
        }

        const std::map<std::string, std::string>& AttachmentNames()
        {
            static const std::map<std::string, std::string> a{
                {"acog", "ACOG"},         {"reflex", "Red Dot"},     {"eotech", "Holographic"},
                {"thermal", "Thermal"},   {"silencer", "Silencer"},  {"fmj", "FMJ"},
                {"heartbeat", "Heartbeat"}, {"grip", "Grip"},        {"gl", "Grenade launcher"},
                {"shotgun", "Shotgun"},   {"akimbo", "Akimbo"},      {"xmags", "Extended mags"},
                {"rof", "Rapid fire"},    {"tactical", "Tactical knife"},
            };
            return a;
        }
    }  // namespace

    std::string HumanizeAssetName(std::string_view name)
    {
        std::vector<std::string> parts;
        for (const auto& tok : Tokens(name))
        {
            const auto& w = Words();
            if (const auto it = w.find(tok); it != w.end())
                parts.push_back(it->second);
            else if (std::all_of(tok.begin(), tok.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
                parts.push_back(tok);
            else if (tok.size() == 1)
                parts.push_back(std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(tok[0])))));
            else
                parts.push_back(Capitalise(tok));
        }
        std::string out = Join(parts);
        return out.empty() ? std::string(name) : out;
    }

    AssetEntry ClassifyModel(std::string_view raw)
    {
        AssetEntry e;
        e.name = raw;
        e.label = HumanizeAssetName(raw);
        const std::string n = Lower(raw);
        if (StartsWith(n, "mp_body_") || StartsWith(n, "body_"))
        {
            e.kind = AssetKind::Body;
            e.category = FactionOf(n).empty() ? "Other characters" : FactionOf(n);
            if (Has(n, "sniper") || Has(n, "ghillie"))
                e.tags.push_back("sniper");
            return e;
        }
        if (StartsWith(n, "head_") || StartsWith(n, "mp_head_"))
        {
            e.kind = AssetKind::Head;
            e.category = FactionOf(n).empty() ? "Other heads" : FactionOf(n);
            return e;
        }
        e.kind = AssetKind::Model;
        if (StartsWith(n, "viewmodel_") || StartsWith(n, "viewhands") || n == "tag_origin" || n == "defaultactor" ||
            n == "defaultvehicle" || StartsWith(n, "collision") || Has(n, "_lod"))
        {
            e.category = "Engine";
            e.hidden = true;
            return e;
        }
        if (StartsWith(n, "weapon_") || StartsWith(n, "worldmodel_") || StartsWith(n, "projectile_"))
            e.category = "Weapons";
        else if (Has(n, "heli") || Has(n, "littlebird") || Has(n, "little_bird") || Has(n, "pavelow") || Has(n, "cobra") || Has(n, "blackhawk") ||
                 Has(n, "apache") || Has(n, "mi17") || Has(n, "mi24") || Has(n, "chopper"))
            e.category = "Helicopters";
        else if (Has(n, "ac130") || Has(n, "harrier") || Has(n, "mig") || Has(n, "f15") || Has(n, "plane") ||
                 Has(n, "jet") || Has(n, "uav") || Has(n, "predator") || Has(n, "c130"))
            e.category = "Aircraft";
        else if (StartsWith(n, "vehicle_") || Has(n, "humvee") || Has(n, "truck") || Has(n, "car_") || Has(n, "bmp") ||
                 Has(n, "tank") || Has(n, "stryker"))
            e.category = "Vehicles";
        else if (Has(n, "crate") || Has(n, "pallet") || Has(n, "box"))
            e.category = "Crates";
        else if (Has(n, "barrel") || Has(n, "drum"))
            e.category = "Barrels";
        else if (Has(n, "chair") || Has(n, "table") || Has(n, "desk") || Has(n, "bed") || Has(n, "couch") ||
                 Has(n, "shelf") || Has(n, "cabinet") || Has(n, "furniture") || Has(n, "bench"))
            e.category = "Furniture";
        else if (Has(n, "sign") || Has(n, "poster") || Has(n, "billboard"))
            e.category = "Signs";
        else if (Has(n, "debris") || Has(n, "rubble") || Has(n, "trash") || Has(n, "junk") || Has(n, "rock"))
            e.category = "Debris";
        else if (Has(n, "light") || Has(n, "lamp"))
            e.category = "Lights";
        else if (Has(n, "mil_") || Has(n, "sandbag") || Has(n, "ammo") || Has(n, "military") || Has(n, "bombsquad") ||
                 Has(n, "plasticcase") || Has(n, "laptop") || Has(n, "radio"))
            e.category = "Military equipment";
        else if (Has(n, "foliage") || Has(n, "tree") || Has(n, "bush") || Has(n, "grass") || Has(n, "plant"))
            e.category = "Foliage";
        else
            e.category = "Environment";
        return e;
    }

    AssetEntry ClassifyAnimation(std::string_view raw)
    {
        AssetEntry e;
        e.kind = AssetKind::Animation;
        e.name = raw;
        e.label = HumanizeAssetName(raw);
        const std::string n = Lower(raw);
        if (Has(n, "death") || Has(n, "_die"))
            e.category = "Deaths";
        else if (Has(n, "sprint"))
            e.category = "Sprinting";
        else if (Has(n, "prone"))
            e.category = "Prone";
        else if (Has(n, "crouch") || Has(n, "crch"))
            e.category = "Crouching";
        else if (Has(n, "jump"))
            e.category = "Jumping";
        else if (Has(n, "land"))
            e.category = "Landing";
        else if (Has(n, "fall"))
            e.category = "Falling";
        else if (Has(n, "run"))
            e.category = "Running";
        else if (Has(n, "walk"))
            e.category = "Walking";
        else if (Has(n, "turn"))
            e.category = "Turning";
        else if (Has(n, "reload"))
            e.category = "Reloading";
        else if (Has(n, "ads") || Has(n, "aim"))
            e.category = "Aiming";
        else if (Has(n, "pain") || Has(n, "flinch") || Has(n, "react"))
            e.category = "Reactions";
        else if (Has(n, "wave") || Has(n, "salute") || Has(n, "gesture") || Has(n, "point") || Has(n, "signal"))
            e.category = "Gestures";
        else if (Has(n, "stand") || Has(n, "idle") || Has(n, "alert"))
            e.category = "Standing";
        else
            e.category = "Other";
        return e;
    }

    std::string WeaponDisplayName(std::string_view raw)
    {
        const std::string n = Lower(raw);
        const auto toks = Tokens(n);
        if (toks.empty())
            return std::string(raw);
        for (const auto& w : WeaponTable())
            if (toks[0] == w.base)
                return w.label;
        return HumanizeAssetName(raw);
    }

    std::vector<std::string> WeaponAttachments(std::string_view raw)
    {
        std::vector<std::string> out;
        const auto toks = Tokens(raw);
        for (std::size_t i = 1; i < toks.size(); ++i)
            if (const auto it = AttachmentNames().find(toks[i]); it != AttachmentNames().end())
                out.push_back(it->second);
        return out;
    }

    AssetEntry ClassifyWeapon(std::string_view raw)
    {
        AssetEntry e;
        e.kind = AssetKind::Weapon;
        e.name = raw;
        const std::string n = Lower(raw);
        const auto toks = Tokens(n);
        e.category = "Other weapons";
        for (const auto& w : WeaponTable())
            if (!toks.empty() && toks[0] == w.base)
                e.category = w.category;
        std::string label = WeaponDisplayName(raw);
        const auto att = WeaponAttachments(raw);
        if (!att.empty())
        {
            label += " (";
            for (std::size_t i = 0; i < att.size(); ++i)
                label += (i ? ", " : "") + att[i];
            label += ")";
        }
        e.label = label;
        e.tags = att;
        if (!toks.empty())
            e.tags.push_back(toks[0]);
        // Killstreak / turret / vehicle weapons are not hand-held.
        if (Has(n, "turret") || Has(n, "_hind") || Has(n, "heli_") || Has(n, "ac130") || Has(n, "harrier") ||
            Has(n, "_remote") || StartsWith(n, "killstreak") || Has(n, "defaultweapon"))
            e.hidden = true;
        return e;
    }

    AssetEntry ClassifyEffect(std::string_view raw)
    {
        AssetEntry e;
        e.kind = AssetKind::Effect;
        e.name = raw;
        const std::string n = Lower(raw);
        // Readable from the file name part ("smoke/thin_black_smoke_m" -> "Thin Black Smoke M").
        const auto slash = raw.find_last_of("/\\");
        e.label = HumanizeAssetName(slash == std::string_view::npos ? raw : raw.substr(slash + 1));
        if (Has(n, "explosion") || Has(n, "exp_") || Has(n, "explode"))
            e.category = "Explosions";
        else if (Has(n, "fire") || Has(n, "flame") || Has(n, "burn"))
            e.category = "Fire";
        else if (Has(n, "smoke") || Has(n, "smk"))
            e.category = "Smoke";
        else if (Has(n, "dust") || Has(n, "sand"))
            e.category = "Dust";
        else if (Has(n, "spark") || Has(n, "elec"))
            e.category = "Sparks";
        else if (Has(n, "water") || Has(n, "rain") || Has(n, "snow") || Has(n, "weather"))
            e.category = "Weather";
        else if (Has(n, "muzzle") || Has(n, "flash") || Has(n, "shell") || Has(n, "tracer"))
            e.category = "Weapon effects";
        else if (Has(n, "impact") || Has(n, "blood") || Has(n, "flesh"))
            e.category = "Impacts";
        else if (Has(n, "light") || Has(n, "glow") || Has(n, "lamp"))
            e.category = "Lights";
        else
            e.category = "Other effects";
        return e;
    }

    std::vector<CharacterEntry> BuildCharacters(const std::vector<std::string>& modelNames)
    {
        std::vector<std::string> bodies, heads;
        for (const auto& m : modelNames)
        {
            const auto e = ClassifyModel(m);
            if (e.kind == AssetKind::Body)
                bodies.push_back(m);
            else if (e.kind == AssetKind::Head)
                heads.push_back(m);
        }
        std::sort(bodies.begin(), bodies.end());
        std::sort(heads.begin(), heads.end());
        static const std::set<std::string> generic{"mp", "body", "head", "a", "b", "c"};
        std::vector<CharacterEntry> out;
        for (const auto& body : bodies)
        {
            const auto bt = Tokens(body);
            std::string best;
            int bestScore = 0;
            const std::string faction = FactionOf(Lower(body));
            for (const auto& head : heads)
            {
                int score = 0;
                for (const auto& t : Tokens(head))
                    if (!generic.count(t) && std::find(bt.begin(), bt.end(), t) != bt.end())
                        score += 2;
                if (!faction.empty() && FactionOf(Lower(head)) == faction)
                    score += 1;
                if (score > bestScore)
                {
                    bestScore = score;
                    best = head;
                }
            }
            CharacterEntry c;
            const auto e = ClassifyModel(body);
            c.body = body;
            c.head = best;
            c.category = e.category;
            c.label = e.label;
            c.tags = e.tags;
            out.push_back(std::move(c));
        }
        return out;
    }

    namespace
    {
        bool MatchTerms(std::string_view query, const std::vector<std::string>& fields)
        {
            std::string hay;
            for (const auto& f : fields)
                hay += Lower(f) + ' ';
            for (const auto& term : Tokens(query))
                if (!Has(hay, term))
                    return false;
            return true;
        }
    }  // namespace

    bool MatchesSearch(const AssetEntry& e, std::string_view query)
    {
        std::vector<std::string> fields{e.name, e.label, e.category};
        fields.insert(fields.end(), e.tags.begin(), e.tags.end());
        return MatchTerms(query, fields);
    }

    bool MatchesSearch(const CharacterEntry& e, std::string_view query)
    {
        std::vector<std::string> fields{e.body, e.head, e.label, e.category};
        fields.insert(fields.end(), e.tags.begin(), e.tags.end());
        return MatchTerms(query, fields);
    }

    std::string FindAnimation(const std::vector<std::string>& anims, const std::vector<std::vector<std::string>>& prefs)
    {
        for (const auto& tokens : prefs)
            for (const auto& a : anims)
            {
                const std::string n = Lower(a);
                if (std::all_of(tokens.begin(), tokens.end(), [&](const std::string& t) { return Has(n, t); }))
                    return a;
            }
        return {};
    }

    const std::vector<std::string>& CharacterCategories()
    {
        static const std::vector<std::string> c{"Task Force 141", "Rangers", "Navy SEALs", "Spetsnaz",
                                                "Militia",        "OpFor",   "Ghillie suits", "Other characters"};
        return c;
    }

    const std::vector<std::string>& AnimationCategories()
    {
        static const std::vector<std::string> c{"Standing", "Walking", "Running",   "Sprinting", "Crouching",
                                                "Prone",    "Jumping", "Falling",   "Landing",   "Turning",
                                                "Aiming",   "Reloading", "Reactions", "Gestures", "Deaths", "Other"};
        return c;
    }

    const std::vector<std::string>& PropCategories()
    {
        static const std::vector<std::string> c{"Military equipment", "Crates",    "Barrels",    "Furniture",
                                                "Debris",             "Signs",     "Weapons",    "Vehicles",
                                                "Aircraft",           "Helicopters", "Lights",   "Foliage",
                                                "Environment",        "Engine"};
        return c;
    }

    const std::vector<std::string>& EffectCategories()
    {
        static const std::vector<std::string> c{"Smoke", "Fire",   "Explosions", "Dust",   "Sparks",
                                                "Weather", "Weapon effects", "Impacts", "Lights", "Other effects"};
        return c;
    }
}  // namespace iris
