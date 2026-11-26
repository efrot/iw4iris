// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Engine.hpp"
#include "Log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

namespace IW4Iris::Engine
{
  namespace
  {
    // Every binding below is declared by IW4x (src/Game/*.cpp) and used by
    // IW4x components; see COMPATIBILITY.md for the references.

    Game::dvar_t* Find(const char* name)
    {
      return Game::Dvar_FindVar(name);
    }

    struct Saved
    {
      Game::dvar_type type{};
      Game::DvarValue value{};
      std::string string;  // for string dvars
    };

    std::mutex overridesMutex;
    std::map<std::string, Saved> overrides;

    void Remember(Game::dvar_t* dvar)
    {
      std::lock_guard _(overridesMutex);
      if (overrides.contains(dvar->name))
        return;
      Saved s;
      s.type = dvar->type;
      s.value = dvar->current;
      if (dvar->type == Game::DVAR_TYPE_STRING && dvar->current.string)
        s.string = dvar->current.string;
      overrides.emplace(dvar->name, s);
    }

    void Apply(Game::dvar_t* dvar, const Saved& s)
    {
      switch (s.type)
      {
      case Game::DVAR_TYPE_BOOL:
        Game::Dvar_SetBool(dvar, s.value.enabled);
        break;
      case Game::DVAR_TYPE_FLOAT:
        Game::Dvar_SetFloat(dvar, s.value.value);
        break;
      case Game::DVAR_TYPE_INT:
      case Game::DVAR_TYPE_ENUM:
        Game::Dvar_SetInt(dvar, s.value.integer);
        break;
      case Game::DVAR_TYPE_STRING:
        Game::Dvar_SetString(dvar, s.string.c_str());
        break;
      default:
        // Vectors and colours have no typed setter bound in IW4x: write
        // the value and mark the dvar modified, as the engine's own
        // setters do.
        dvar->current = s.value;
        dvar->latched = s.value;
        dvar->modified = true;
        break;
      }
    }
  }

  bool IsCgameReady()
  {
    return Game::CL_IsCgameInitialized() && Game::CL_GetLocalClientConnectionState(0) == Game::CA_ACTIVE;
  }

  bool IsDemoPlaying()
  {
    return Game::clientConnections->demoplaying != 0;
  }

  int ConnectionState()
  {
    return static_cast<int>(Game::CL_GetLocalClientConnectionState(0));
  }

  std::string DemoName()
  {
    std::string name(Game::clientConnections->demoName,
                     strnlen(Game::clientConnections->demoName, sizeof(Game::clientConnections->demoName)));
    if (name.ends_with(".dm_13"))
      name.resize(name.size() - 6);
    return name;
  }

  std::string MapName()
  {
    const auto* map = Game::clients[0].mapname;
    return {map, strnlen(map, sizeof(Game::clients[0].mapname))};
  }

  int ClientServerTime()
  {
    return Game::clients[0].serverTime;
  }

  int SnapshotServerTime()
  {
    return Game::clients[0].snap.serverTime;
  }

  int Realtime()
  {
    return Game::cls->realtime;
  }

  void SetRealtime(int value)
  {
    Game::cls->realtime = value;
  }

  std::optional<GameView> ReadView(int localClientNum)
  {
    if (localClientNum != 0)
      return std::nullopt;
    const auto& refdef = Game::cgArray[0].refdef;
    GameView v;
    v.origin = {refdef.view.org[0], refdef.view.org[1], refdef.view.org[2]};
    for (int i = 0; i < 3; ++i)
      v.axis.col[i] = {refdef.view.axis[i][0], refdef.view.axis[i][1], refdef.view.axis[i][2]};
    v.tanHalfFovX = refdef.view.tanHalfFovX;
    v.tanHalfFovY = refdef.view.tanHalfFovY;
    v.width = static_cast<int>(refdef.width);
    v.height = static_cast<int>(refdef.height);
    return v;
  }

  void WriteView(int localClientNum, const iris::CameraState& camera)
  {
    if (localClientNum != 0)
      return;
    auto& refdef = Game::cgArray[0].refdef;
    const auto axis = iris::AnglesToAxis(camera.angles);
    for (int i = 0; i < 3; ++i)
    {
      refdef.view.org[i] = static_cast<float>(camera.position[i]);
      for (int j = 0; j < 3; ++j)
        refdef.view.axis[i][j] = static_cast<float>(axis.col[i][j]);
    }
    const double tanX = std::tan(iris::Clamp(camera.horizontalFov, 1.0, 179.0) * 0.5 * iris::kDegToRad);
    const double aspect = refdef.width > 0 && refdef.height > 0
                              ? static_cast<double>(refdef.height) / static_cast<double>(refdef.width)
                              : 9.0 / 16.0;
    refdef.view.tanHalfFovX = static_cast<float>(tanX);
    refdef.view.tanHalfFovY = static_cast<float>(tanX * aspect);
  }

  iris::CameraState ToCameraState(const GameView& view)
  {
    iris::CameraState s;
    s.position = view.origin;
    s.angles = iris::AxisToAngles(view.axis);
    s.orientation = iris::AnglesToQuat(s.angles);
    s.horizontalFov = 2.0 * std::atan(view.tanHalfFovX) * iris::kRadToDeg;
    return s;
  }

  int RenderWidth()
  {
    return Components::Renderer::Width();
  }

  int RenderHeight()
  {
    return Components::Renderer::Height();
  }

  std::vector<PlayerInfo> Players()
  {
    std::vector<PlayerInfo> out;
    if (!IsCgameReady())
      return out;
    for (int i = 0; i < 18; ++i)
    {
      const auto& ent = Game::cg_entitiesArray[i].entity;
      PlayerInfo p;
      p.clientNum = i;
      p.present = ent.nextValid && ent.nextState.eType == Game::ET_PLAYER;
      p.origin = {ent.pose.origin[0], ent.pose.origin[1], ent.pose.origin[2]};
      char name[64]{};
      if (Game::CL_GetClientName(0, i, name, sizeof(name)))
        p.name = name;
      if (p.present || !p.name.empty())
        out.push_back(std::move(p));
    }
    return out;
  }

  std::vector<iris::PlayerObservation> ObservePlayers()
  {
    std::vector<iris::PlayerObservation> out;
    if (!IsCgameReady())
      return out;
    const auto& cg = Game::cgArray[0];
    const Game::snapshot_s* snap = cg.snap;
    if (!snap)
      return out;
    constexpr int kMaxClients = 18;  // IW4: snapshot_s::clients[18]
    std::array<iris::PlayerObservation, kMaxClients> obs{};
    std::array<bool, kMaxClients> used{};
    auto stanceOf = [](int eFlags) {
      return (eFlags & Game::EF_PRONE) ? iris::Stance::Prone
             : (eFlags & Game::EF_CROUCHING) ? iris::Stance::Crouch
                                               : iris::Stance::Stand;
    };

    // The client list: who is connected, their name and team.
    const int numClients = std::clamp(snap->numClients, 0, kMaxClients);
    for (int i = 0; i < numClients; ++i)
    {
      const auto& c = snap->clients[i];
      if (c.clientIndex < 0 || c.clientIndex >= kMaxClients)
        continue;
      auto& o = obs[static_cast<std::size_t>(c.clientIndex)];
      used[static_cast<std::size_t>(c.clientIndex)] = true;
      o.clientNum = c.clientIndex;
      o.name.assign(c.name, strnlen(c.name, sizeof(c.name)));
      o.team = static_cast<int>(c.team);
      o.inRoster = true;
    }

    // The player entities in this snapshot: where they are now.
    const int numEntities = std::clamp(snap->numEntities, 0, static_cast<int>(std::size(snap->entities)));
    const float f = std::clamp(cg.frameInterpolation, 0.0f, 1.0f);
    for (int i = 0; i < numEntities; ++i)
    {
      const auto& e = snap->entities[i];
      if (e.eType != Game::ET_PLAYER || e.clientNum < 0 || e.clientNum >= kMaxClients || e.number < 0 ||
          e.number >= static_cast<int>(Game::MAX_GENTITIES))
        continue;
      auto& o = obs[static_cast<std::size_t>(e.clientNum)];
      used[static_cast<std::size_t>(e.clientNum)] = true;
      o.clientNum = e.clientNum;
      o.visible = true;
      o.dead = (e.lerp.eFlags & Game::EF_DEAD) != 0;
      o.stance = stanceOf(e.lerp.eFlags);
      o.weapon = e.weapon;
      const auto& cent = Game::cg_entitiesArray[e.number].entity;
      if (cent.nextValid)
      {
        // Interpolated between the previous and the next snapshot with the
        // cgame's factor for this frame (computed here rather than read
        // from the entity's pose, which may still hold the previous frame
        // when the view is built), so cameras on players match the frame.
        const auto& pp = cent.prevState.pos.trBase;
        const auto& np = cent.nextState.lerp.pos.trBase;
        const float jump = std::abs(np[0] - pp[0]) + std::abs(np[1] - pp[1]) + std::abs(np[2] - pp[2]);
        const float k = jump > 512.0f ? 1.0f : f;  // a respawn / teleport: no sweep across the map
        o.origin = {pp[0] + (np[0] - pp[0]) * k, pp[1] + (np[1] - pp[1]) * k, pp[2] + (np[2] - pp[2]) * k};
        const auto lerpAngle = [f](float a, float b) {
          float d = std::fmod(b - a, 360.0f);
          if (d > 180.0f)
            d -= 360.0f;
          if (d < -180.0f)
            d += 360.0f;
          return a + d * f;
        };
        const auto& pa = cent.prevState.apos.trBase;
        const auto& na = cent.nextState.lerp.apos.trBase;
        o.angles = {lerpAngle(pa[0], na[0]), lerpAngle(pa[1], na[1]), 0.0};
      }
      else
      {
        o.origin = {e.lerp.pos.trBase[0], e.lerp.pos.trBase[1], e.lerp.pos.trBase[2]};
        o.angles = {e.lerp.apos.trBase[0], e.lerp.apos.trBase[1], 0.0};
      }
    }

    // The recording player: the predicted player state is their exact view.
    const int local = cg.clientNum;
    if (local >= 0 && local < kMaxClients)
    {
      auto& o = obs[static_cast<std::size_t>(local)];
      used[static_cast<std::size_t>(local)] = true;
      const auto& ps = cg.predictedPlayerState;
      o.clientNum = local;
      o.recording = true;
      o.visible = true;
      o.dead = (ps.eFlags & Game::EF_DEAD) != 0;
      o.stance = stanceOf(ps.eFlags);
      o.origin = {ps.origin[0], ps.origin[1], ps.origin[2]};
      o.angles = {ps.viewangles[0], ps.viewangles[1], 0.0};
      o.eyeHeight = ps.viewHeightCurrent;
    }

    for (int i = 0; i < kMaxClients; ++i)
    {
      if (!used[static_cast<std::size_t>(i)])
        continue;
      auto& o = obs[static_cast<std::size_t>(i)];
      if (o.name.empty())
      {
        char name[64]{};
        if (Game::CL_GetClientName(0, i, name, sizeof(name)))
          o.name = name;
      }
      out.push_back(std::move(o));
    }
    return out;
  }

  std::optional<iris::Vec3> EntityOrigin(int entityNum)
  {
    if (!IsCgameReady() || entityNum < 0 || entityNum >= static_cast<int>(Game::MAX_GENTITIES))
      return std::nullopt;
    const auto& ent = Game::cg_entitiesArray[entityNum].entity;
    if (!ent.nextValid)
      return std::nullopt;
    return iris::Vec3{ent.pose.origin[0], ent.pose.origin[1], ent.pose.origin[2]};
  }

  int LocalClientNum()
  {
    return Game::cgArray[0].clientNum;
  }

  iris::Vec3 LocalPlayerOrigin()
  {
    const auto& ps = Game::cgArray[0].predictedPlayerState;
    return {ps.origin[0], ps.origin[1], ps.origin[2]};
  }

  bool HasDvar(const char* name)
  {
    return Find(name) != nullptr;
  }

  std::optional<float> GetFloat(const char* name)
  {
    const auto* d = Find(name);
    if (!d || d->type != Game::DVAR_TYPE_FLOAT)
      return std::nullopt;
    return d->current.value;
  }

  std::optional<int> GetInt(const char* name)
  {
    const auto* d = Find(name);
    if (!d || (d->type != Game::DVAR_TYPE_INT && d->type != Game::DVAR_TYPE_ENUM))
      return std::nullopt;
    return d->current.integer;
  }

  std::optional<bool> GetBool(const char* name)
  {
    const auto* d = Find(name);
    if (!d || d->type != Game::DVAR_TYPE_BOOL)
      return std::nullopt;
    return d->current.enabled;
  }

  std::optional<std::string> GetString(const char* name)
  {
    const auto* d = Find(name);
    if (!d || d->type != Game::DVAR_TYPE_STRING || !d->current.string)
      return std::nullopt;
    return std::string(d->current.string);
  }

  std::optional<iris::Vec3> GetVec3(const char* name)
  {
    const auto* d = Find(name);
    if (!d || (d->type != Game::DVAR_TYPE_FLOAT_3 && d->type != Game::DVAR_TYPE_FLOAT_3_COLOR))
      return std::nullopt;
    return iris::Vec3{d->current.vector[0], d->current.vector[1], d->current.vector[2]};
  }

  std::optional<std::pair<float, float>> FloatDomain(const char* name)
  {
    const auto* d = Find(name);
    if (!d || d->type != Game::DVAR_TYPE_FLOAT)
      return std::nullopt;
    return std::make_pair(d->domain.value.min, d->domain.value.max);
  }

  bool OverrideFloat(const char* name, float value)
  {
    auto* d = Find(name);
    if (!d || d->type != Game::DVAR_TYPE_FLOAT)
      return false;
    Remember(d);
    if (d->current.value != value)
      Game::Dvar_SetFloat(d, value);
    return true;
  }

  bool OverrideInt(const char* name, int value)
  {
    auto* d = Find(name);
    if (!d || (d->type != Game::DVAR_TYPE_INT && d->type != Game::DVAR_TYPE_ENUM))
      return false;
    Remember(d);
    if (d->current.integer != value)
      Game::Dvar_SetInt(d, value);
    return true;
  }

  bool OverrideBool(const char* name, bool value)
  {
    auto* d = Find(name);
    if (!d || d->type != Game::DVAR_TYPE_BOOL)
      return false;
    Remember(d);
    if (d->current.enabled != value)
      Game::Dvar_SetBool(d, value);
    return true;
  }

  bool OverrideVec3(const char* name, const iris::Vec3& v)
  {
    auto* d = Find(name);
    if (!d || (d->type != Game::DVAR_TYPE_FLOAT_3 && d->type != Game::DVAR_TYPE_FLOAT_3_COLOR))
      return false;
    Remember(d);
    Game::DvarValue value = d->current;
    for (int i = 0; i < 3; ++i)
      value.vector[i] = static_cast<float>(v[i]);
    if (std::memcmp(&value, &d->current, sizeof(value)) != 0)
    {
      d->current = value;
      d->latched = value;
      d->modified = true;
    }
    return true;
  }

  bool OverrideColor(const char* name, const iris::Vec3& rgb, float alpha)
  {
    auto* d = Find(name);
    if (!d)
      return false;
    if (d->type == Game::DVAR_TYPE_FLOAT_4 || d->type == Game::DVAR_TYPE_FLOAT_3_COLOR || d->type == Game::DVAR_TYPE_FLOAT_3)
    {
      Remember(d);
      Game::DvarValue value = d->current;
      for (int i = 0; i < 3; ++i)
        value.vector[i] = static_cast<float>(rgb[i]);
      if (d->type == Game::DVAR_TYPE_FLOAT_4)
        value.vector[3] = alpha;
      if (std::memcmp(&value, &d->current, sizeof(value)) != 0)
      {
        d->current = value;
        d->latched = value;
        d->modified = true;
      }
      return true;
    }
    if (d->type == Game::DVAR_TYPE_COLOR)
    {
      Remember(d);
      Game::DvarValue value = d->current;
      for (int i = 0; i < 3; ++i)
        value.color[i] = static_cast<unsigned char>(iris::Clamp(rgb[i], 0.0, 1.0) * 255.0 + 0.5);
      value.color[3] = static_cast<unsigned char>(iris::Clamp(static_cast<double>(alpha), 0.0, 1.0) * 255.0 + 0.5);
      if (std::memcmp(&value, &d->current, sizeof(value)) != 0)
      {
        d->current = value;
        d->latched = value;
        d->modified = true;
      }
      return true;
    }
    return false;
  }

  void Restore(const char* name)
  {
    Saved s;
    {
      std::lock_guard _(overridesMutex);
      const auto it = overrides.find(name);
      if (it == overrides.end())
        return;
      s = it->second;
      overrides.erase(it);
    }
    if (auto* d = Find(name))
      Apply(d, s);
  }

  void RestoreAll()
  {
    std::map<std::string, Saved> all;
    {
      std::lock_guard _(overridesMutex);
      all.swap(overrides);
    }
    for (const auto& [name, s] : all)
      if (auto* d = Find(name.c_str()))
        Apply(d, s);
    if (!all.empty())
      Log::Info("Restored {} dvar(s) to their original values", all.size());
  }

  std::vector<std::string> OverriddenDvars()
  {
    std::lock_guard _(overridesMutex);
    std::vector<std::string> out;
    for (const auto& [name, _s] : overrides)
      out.push_back(name);
    return out;
  }

  void Execute(const std::string& command)
  {
    Components::Command::Execute(command, false);
  }

  std::string BasePath()
  {
    if (!Game::fs_basepath || !*Game::fs_basepath || !(*Game::fs_basepath)->current.string)
      return {};
    return (*Game::fs_basepath)->current.string;
  }
}
