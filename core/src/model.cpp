// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "iris/model.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace iris
{
    namespace
    {
        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
                s.remove_suffix(1);
            return s;
        }

        std::vector<std::string_view> Split(std::string_view s)
        {
            std::vector<std::string_view> out;
            std::size_t i = 0;
            while (i < s.size())
            {
                while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                    ++i;
                const std::size_t j = i;
                while (i < s.size() && s[i] != ' ' && s[i] != '\t')
                    ++i;
                if (i > j)
                    out.push_back(s.substr(j, i - j));
            }
            return out;
        }

        double ToDouble(std::string_view s)
        {
            return std::strtod(std::string(s).c_str(), nullptr);
        }

        long ToLong(std::string_view s, bool& ok)
        {
            long v = 0;
            const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
            ok = r.ec == std::errc() && v != 0;
            return v;
        }

        std::map<std::string, Vec3> ParseMtl(std::string_view text)
        {
            std::map<std::string, Vec3> colors;
            std::string current;
            std::istringstream in{std::string(text)};
            for (std::string line; std::getline(in, line);)
            {
                const auto t = Split(Trim(line));
                if (t.empty())
                    continue;
                if (t[0] == "newmtl" && t.size() >= 2)
                    current = std::string(t[1]);
                else if (t[0] == "Kd" && t.size() >= 4 && !current.empty())
                    colors[current] = {ToDouble(t[1]), ToDouble(t[2]), ToDouble(t[3])};
            }
            return colors;
        }

        std::string MtlLibName(std::string_view objText)
        {
            std::istringstream in{std::string(objText)};
            for (std::string line; std::getline(in, line);)
            {
                const auto t = Trim(line);
                if (t.rfind("mtllib", 0) == 0)
                    return std::string(Trim(t.substr(6)));
            }
            return {};
        }
    }  // namespace

    ModelImport ImportObj(std::string_view objText, const ModelImportSettings& st, std::string_view mtlText)
    {
        ModelImport result;
        std::vector<Vec3> positions;
        std::vector<Vec2> uvs;
        std::vector<Vec3> normals;
        const auto colors = ParseMtl(mtlText);

        struct Corner
        {
            long v = 0, vt = 0, vn = 0;
        };
        struct Face
        {
            std::vector<Corner> corners;
            std::size_t group = 0;
        };
        std::vector<Face> faces;
        std::vector<std::string> groupNames{"default"};
        std::size_t currentGroup = 0;
        std::size_t lineNo = 0, badFaces = 0;

        std::istringstream in{std::string(objText)};
        for (std::string raw; std::getline(in, raw);)
        {
            ++lineNo;
            const auto line = Trim(raw);
            if (line.empty() || line[0] == '#')
                continue;
            const auto t = Split(line);
            if (t[0] == "v" && t.size() >= 4)
                positions.push_back({ToDouble(t[1]), ToDouble(t[2]), ToDouble(t[3])});
            else if (t[0] == "vt" && t.size() >= 3)
                uvs.push_back({ToDouble(t[1]), ToDouble(t[2])});
            else if (t[0] == "vn" && t.size() >= 4)
                normals.push_back({ToDouble(t[1]), ToDouble(t[2]), ToDouble(t[3])});
            else if (t[0] == "usemtl" && t.size() >= 2)
            {
                const std::string name(t[1]);
                const auto it = std::find(groupNames.begin(), groupNames.end(), name);
                currentGroup = static_cast<std::size_t>(it - groupNames.begin());
                if (it == groupNames.end())
                    groupNames.push_back(name);
            }
            else if (t[0] == "f" && t.size() >= 4)
            {
                Face f;
                f.group = currentGroup;
                bool ok = true;
                for (std::size_t k = 1; k < t.size(); ++k)
                {
                    Corner c;
                    const auto tok = t[k];
                    const auto s1 = tok.find('/');
                    bool okv = false;
                    c.v = ToLong(tok.substr(0, s1), okv);
                    ok = ok && okv;
                    if (s1 != std::string_view::npos)
                    {
                        const auto rest = tok.substr(s1 + 1);
                        const auto s2 = rest.find('/');
                        bool okt = false, okn = false;
                        if (s2 != 0)
                            c.vt = ToLong(rest.substr(0, s2), okt);
                        if (s2 != std::string_view::npos)
                            c.vn = ToLong(rest.substr(s2 + 1), okn);
                    }
                    // Negative indices count back from the current end.
                    auto resolve = [](long idx, std::size_t size) -> long {
                        return idx < 0 ? static_cast<long>(size) + idx + 1 : idx;
                    };
                    c.v = resolve(c.v, positions.size());
                    c.vt = c.vt ? resolve(c.vt, uvs.size()) : 0;
                    c.vn = c.vn ? resolve(c.vn, normals.size()) : 0;
                    if (c.v < 1 || static_cast<std::size_t>(c.v) > positions.size())
                        ok = false;
                    if (c.vt < 0 || static_cast<std::size_t>(c.vt) > uvs.size())
                        c.vt = 0;
                    if (c.vn < 0 || static_cast<std::size_t>(c.vn) > normals.size())
                        c.vn = 0;
                    f.corners.push_back(c);
                }
                if (ok)
                    faces.push_back(std::move(f));
                else
                    ++badFaces;
            }
        }
        result.sourceFaces = faces.size();
        if (badFaces)
            result.warnings.push_back(std::to_string(badFaces) + " faces with invalid indices were skipped.");
        std::size_t triangles = 0;
        for (const auto& f : faces)
            triangles += f.corners.size() - 2;
        if (faces.empty())
        {
            result.error = "The file contains no faces.";
            return result;
        }
        if (triangles > st.maxTriangles)
        {
            result.error = "The model has " + std::to_string(triangles) + " triangles; the limit is " +
                           std::to_string(st.maxTriangles) + ". Decimate it in your 3D tool first.";
            return result;
        }

        auto convert = [&](const Vec3& p) -> Vec3 {
            const Vec3 q = st.upAxis == ModelUpAxis::Z ? Vec3{p.x, p.z, -p.y} : p;
            return q * st.scale;
        };

        // Smooth normals by position index for faces without normals.
        bool needNormals = false;
        for (const auto& f : faces)
            for (const auto& c : f.corners)
                if (!c.vn)
                    needNormals = true;
        std::vector<Vec3> computed;
        if (needNormals)
        {
            computed.assign(positions.size(), Vec3{});
            for (const auto& f : faces)
                for (std::size_t k = 1; k + 1 < f.corners.size(); ++k)
                {
                    const Vec3 a = convert(positions[static_cast<std::size_t>(f.corners[0].v - 1)]);
                    const Vec3 b = convert(positions[static_cast<std::size_t>(f.corners[k].v - 1)]);
                    const Vec3 c = convert(positions[static_cast<std::size_t>(f.corners[k + 1].v - 1)]);
                    const Vec3 n = Cross(b - a, c - a);
                    for (const auto idx : {f.corners[0].v, f.corners[k].v, f.corners[k + 1].v})
                        computed[static_cast<std::size_t>(idx - 1)] += n;
                }
            result.normalsComputed = true;
        }

        // Emit per group so each material is one index range.
        std::map<std::tuple<long, long, long>, std::uint32_t> cache;
        Mesh& mesh = result.mesh;
        for (std::size_t g = 0; g < groupNames.size(); ++g)
        {
            ModelGroup group;
            group.material = groupNames[g];
            if (const auto it = colors.find(group.material); it != colors.end())
                group.color = it->second;
            group.firstIndex = static_cast<std::uint32_t>(mesh.indices.size());
            for (const auto& f : faces)
            {
                if (f.group != g)
                    continue;
                std::vector<std::uint32_t> ids;
                for (const auto& c : f.corners)
                {
                    const auto key = std::make_tuple(c.v, c.vt, c.vn);
                    auto it = cache.find(key);
                    if (it == cache.end())
                    {
                        const Vec3 p = convert(positions[static_cast<std::size_t>(c.v - 1)]);
                        Vec3 n = c.vn ? convert(normals[static_cast<std::size_t>(c.vn - 1)])
                                      : computed[static_cast<std::size_t>(c.v - 1)];
                        n = Normalize(n);
                        if (Length(n) < 0.5)
                            n = {0, 0, 1};
                        const Vec2 uv = c.vt ? uvs[static_cast<std::size_t>(c.vt - 1)] : Vec2{};
                        MeshVertex v{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z),
                                     static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z),
                                     static_cast<float>(uv.x), static_cast<float>(1.0 - uv.y)};
                        it = cache.emplace(key, static_cast<std::uint32_t>(mesh.vertices.size())).first;
                        mesh.vertices.push_back(v);
                    }
                    ids.push_back(it->second);
                }
                for (std::size_t k = 1; k + 1 < ids.size(); ++k)
                    for (const auto i : {ids[0], ids[k], ids[k + 1]})
                        mesh.indices.push_back(i);
            }
            group.indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - group.firstIndex;
            if (group.indexCount)
            {
                result.groups.push_back(group);
                mesh.submeshes.push_back({MeshPart::Front, group.firstIndex, group.indexCount});
            }
        }
        if (mesh.vertices.size() > st.maxVertices)
        {
            result.mesh = {};
            result.groups.clear();
            result.error = "The model has too many vertices (" + std::to_string(mesh.vertices.size()) + ").";
            return result;
        }
        result.bounds = mesh.Bounds();
        if (st.centre && result.bounds.Valid())
        {
            mesh.Translate(-result.bounds.Center());
            result.bounds = mesh.Bounds();
        }
        return result;
    }

    ModelImport ImportObjFile(const std::filesystem::path& file, const ModelImportSettings& settings)
    {
        auto read = [](const std::filesystem::path& p) -> std::string {
            std::ifstream in(p, std::ios::binary);
            return in ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) : std::string();
        };
        const std::string obj = read(file);
        if (obj.empty())
        {
            ModelImport r;
            r.error = "Could not read the model file.";
            return r;
        }
        std::string mtl;
        if (const auto lib = MtlLibName(obj); !lib.empty())
        {
            mtl = read(file.parent_path() / lib);
            if (mtl.empty())
            {
                auto r = ImportObj(obj, settings);
                r.warnings.push_back("The material library " + lib + " was not found next to the model.");
                return r;
            }
        }
        return ImportObj(obj, settings, mtl);
    }
}  // namespace iris
