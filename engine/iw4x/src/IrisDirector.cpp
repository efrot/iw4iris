// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Director.hpp"

#include "../ArenaLength.hpp"
#include "../FastFiles.hpp"
#include "../GSC/Script.hpp"
#include "Engine.hpp"
#include "Log.hpp"
#include "OfflineGuard.hpp"
#include "Runtime.hpp"
#include "Session.hpp"

#include <iris/director_sync.hpp>
#include <iris/templates.hpp>

#include <chrono>

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>

namespace IW4Iris::Director
{
  namespace
  {
    // Bridge state. Lock order: the session's mutex (when needed) before
    // this one, never the other way round.
    std::recursive_mutex bridgeMutex;

    bool requested = false;          // the editor started a film set
    std::string requestedMap;
    SetState state = SetState::None;
    std::string capabilities;
    std::string lastError;
    std::deque<std::string> logLines;

    // Desired scene vs. engine state: only acknowledged operations count
    // (iris::director::Reconciler, unit-tested against a simulated mod).
    iris::director::Reconciler reconciler;
    int protocolSeen = 0;
    std::uint64_t failuresLogged = 0;

    double Now()
    {
      static const auto start = std::chrono::steady_clock::now();
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    std::set<std::string> precachedModels, precachedAnims, precachedWeapons, precachedEffects;
    Catalog catalog;
    std::set<std::string> knownModels, knownAnims, knownWeapons, knownEffects;
    std::vector<std::string> needReload, unavailable;

    struct TagSample
    {
      iris::Transform transform;
      int time = 0;
    };
    std::map<std::pair<std::string, std::string>, std::pair<TagSample, TagSample>> tagSamples;
    std::set<std::pair<std::string, std::string>> tagRequests;

    struct FxEvent
    {
      std::string fx;
      iris::Vec3 origin, forward, up;
    };
    std::vector<FxEvent> fxQueue;

    // Prediction of the timeline time a server frame will be shown at.
    int clientTime = 0;
    double clientPlayhead = 0.0;
    double playRate = 0.0;
    int lastServerTime = 0;
    double lastPlayhead = 0.0;
    int lastClientTime = 0;
    bool wasPlaying = false;
    int framesWithoutGame = 0;
    int loadingFrames = 0;

    std::vector<MapInfo> mapCache;
    std::optional<SurfaceHit> viewTarget;

    void Note(const std::string& line)
    {
      logLines.push_back(line);
      while (logLines.size() > 200)
        logLines.pop_front();
      Log::Info("Director: {}", line);
    }

    bool Has(const std::set<std::string>& set, const std::string& name) { return !name.empty() && set.contains(name); }

    // ------------------------------------------------------------ assets

    template <typename F>
    void Enumerate(Game::XAssetType type, F&& each)
    {
      struct Ctx
      {
        Game::XAssetType type;
        std::vector<std::string> names;
      } ctx{type, {}};
      Game::DB_EnumXAssets(
        type,
        [](Game::XAssetHeader header, void* data) {
          auto* c = static_cast<Ctx*>(data);
          Game::XAsset asset{c->type, header};
          if (const char* name = Game::DB_GetXAssetName(&asset); name && *name)
            c->names.emplace_back(name);
        },
        &ctx, false);
      std::sort(ctx.names.begin(), ctx.names.end());
      ctx.names.erase(std::unique(ctx.names.begin(), ctx.names.end()), ctx.names.end());
      for (const auto& n : ctx.names)
        each(n);
    }

    void DiscoverAssets()
    {
      Catalog c;
      c.version = catalog.version + 1;
      Enumerate(Game::XAssetType::ASSET_TYPE_XMODEL, [&](const std::string& n) {
        c.modelNames.push_back(n);
        const auto e = iris::ClassifyModel(n);
        if (e.kind == iris::AssetKind::Model)
          c.props.push_back(e);
      });
      Enumerate(Game::XAssetType::ASSET_TYPE_XANIMPARTS, [&](const std::string& n) {
        c.animationNames.push_back(n);
        auto e = iris::ClassifyAnimation(n);
        // Multiplayer body / torso animations play on script models; the
        // others (view models, vehicles) are listed in professional mode.
        e.hidden = !(n.rfind("pb_", 0) == 0 || n.rfind("pt_", 0) == 0);
        c.animations.push_back(e);
      });
      Enumerate(Game::XAssetType::ASSET_TYPE_WEAPON, [&](const std::string& n) {
        if (n.size() < 4 || n.compare(n.size() - 3, 3, "_mp") != 0)
          return;
        c.weaponNames.push_back(n);
        c.weapons.push_back(iris::ClassifyWeapon(n));
      });
      Enumerate(Game::XAssetType::ASSET_TYPE_FX, [&](const std::string& n) {
        c.effectNames.push_back(n);
        c.effects.push_back(iris::ClassifyEffect(n));
      });
      c.characters = iris::BuildCharacters(c.modelNames);
      knownModels = {c.modelNames.begin(), c.modelNames.end()};
      knownAnims = {c.animationNames.begin(), c.animationNames.end()};
      knownWeapons = {c.weaponNames.begin(), c.weaponNames.end()};
      knownEffects = {c.effectNames.begin(), c.effectNames.end()};
      Note(std::format("asset database: {} models ({} characters), {} animations, {} weapons, {} effects",
                       c.modelNames.size(), c.characters.size(), c.animationNames.size(), c.weaponNames.size(),
                       c.effectNames.size()));
      catalog = std::move(c);
    }

    // The precache list: everything the scene uses that this map has, the
    // characters of the map and the animations directions need.
    std::vector<std::string> BuildManifest(const iris::Scene& scene)
    {
      precachedModels.clear();
      precachedAnims.clear();
      precachedWeapons.clear();
      precachedEffects.clear();
      for (const auto& m : scene.ReferencedModels())
        if (knownModels.contains(m))
          precachedModels.insert(m);
      for (const auto& ch : catalog.characters)
      {
        precachedModels.insert(ch.body);
        if (!ch.head.empty())
          precachedModels.insert(ch.head);
      }
      for (const auto& a : scene.ReferencedAnimations())
        if (knownAnims.contains(a))
          precachedAnims.insert(a);
      const auto roles = iris::FindAnimationRoles(catalog.animationNames);
      for (const auto& a : {roles.idle, roles.walk, roles.run, roles.crouch, roles.aim})
        if (!a.empty())
          precachedAnims.insert(a);
      for (const auto& w : scene.ReferencedWeapons())
        if (knownWeapons.contains(w))
          precachedWeapons.insert(w);
      for (const auto& f : scene.ReferencedEffects())
        if (knownEffects.contains(f))
          precachedEffects.insert(f);
      std::vector<std::string> out;
      for (const auto& m : precachedModels)
        out.push_back("model:" + m);
      for (const auto& a : precachedAnims)
        out.push_back("anim:" + a);
      for (const auto& w : precachedWeapons)
        out.push_back("weapon:" + w);
      for (const auto& f : precachedEffects)
        out.push_back("fx:" + f);
      return out;
    }

    // A new level (or none): the engine has none of our entities.
    void ResetObjects()
    {
      reconciler.BeginGeneration();
      tagSamples.clear();
      tagRequests.clear();
      fxQueue.clear();
    }

    // ------------------------------------------------------------ script helpers

    std::string ArgString(unsigned int i)
    {
      if (i >= Game::Scr_GetNumParam())
        return {};
      const char* s = Game::Scr_GetString(i);
      return s ? s : "";
    }

    void ReturnStrings(const std::vector<std::string>& values)
    {
      Game::Scr_MakeArray();
      for (const auto& v : values)
      {
        Game::Scr_AddString(v.c_str());
        Game::Scr_AddArray();
      }
    }

    void AddFloat(double v)
    {
      Game::Scr_AddFloat(static_cast<float>(v));
      Game::Scr_AddArray();
    }

    void AddString(const std::string& v)
    {
      Game::Scr_AddString(v.c_str());
      Game::Scr_AddArray();
    }

    // ------------------------------------------------------------ attach points

    iris::Transform Lerp(const iris::Transform& a, const iris::Transform& b, double t)
    {
      iris::Transform r;
      r.position = iris::Lerp(a.position, b.position, t);
      r.angles = iris::QuatToAngles(iris::Slerp(iris::AnglesToQuat(a.angles), iris::AnglesToQuat(b.angles), t));
      return r;
    }

    // ------------------------------------------------------------ scene sync

    bool IsWeapon(const std::string& name) { return name.size() > 3 && name.compare(name.size() - 3, 3, "_mp") == 0; }

    // The desired engine state at the playhead, limited to assets the level
    // precached (others are reported in the status, never sent).
    iris::director::Desired DesiredState(const Session& s)
    {
      iris::director::Desired d;
      const iris::Scene& scene = s.project.scene;
      std::set<std::string> actors;
      for (const auto& a : scene.actors)
      {
        if (!Has(precachedModels, a.bodyModel))
          continue;
        iris::director::DesiredActor da;
        da.id = a.id;
        da.body = a.bodyModel;
        da.head = Has(precachedModels, a.headModel) ? a.headModel : std::string();
        da.weapon = Has(precachedWeapons, a.weapon) ? a.weapon : std::string();
        da.weaponTag = a.weaponTag.empty() ? "tag_weapon_right" : a.weaponTag;
        da.camo = a.camo;
        for (const auto& at : a.attachments)
        {
          const bool weaponPart = IsWeapon(at.model);
          if (weaponPart ? Has(precachedWeapons, at.model) : Has(precachedModels, at.model))
            da.attachments.push_back({weaponPart ? "weapon" : "model", at.model, at.tag, at.camo});
        }
        const auto st = iris::EvaluateActor(a, s.playhead);
        if (Has(precachedAnims, st.anim))
        {
          da.anim = st.anim;
          da.animStart = st.animStart;
        }
        actors.insert(a.id);
        d.actors.push_back(std::move(da));
      }
      for (const auto& p : scene.props)
      {
        if (!Has(precachedModels, p.model))
          continue;
        iris::director::DesiredProp dp;
        dp.id = p.id;
        dp.model = p.model;
        if (!p.attachActor.empty() && actors.contains(p.attachActor))
        {
          dp.linkActor = p.attachActor;
          dp.linkTag = AttachTag(p.attachTag);
        }
        d.props.push_back(std::move(dp));
      }
      return d;
    }

    void SyncScene(Session& s)
    {
      const iris::Scene& scene = s.project.scene;
      if (s.playing && !wasPlaying)
        reconciler.RestartAnimations();  // clips play from their start
      reconciler.Reconcile(DesiredState(s), Now());

      // Bones the graphics follow (tag reports from the mod).
      tagRequests.clear();
      for (const auto& g : scene.graphics)
        if (g.tracking.mode == iris::TrackingMode::Actor && g.tracking.attach != "origin" && reconciler.Exists(g.tracking.actor))
          tagRequests.insert({g.tracking.actor, AttachTag(g.tracking.attach)});
      for (const auto& e : scene.effects)
        if (!e.attachActor.empty() && !e.attachTag.empty() && e.attachTag != "origin" && reconciler.Exists(e.attachActor))
          tagRequests.insert({e.attachActor, AttachTag(e.attachTag)});

      // New final failures go to the log once.
      const auto failures = reconciler.Failures();
      std::uint64_t finals = 0;
      for (const auto& f : failures)
        if (f.final)
          ++finals;
      if (finals > failuresLogged)
        for (const auto& f : failures)
          if (f.final)
          {
            lastError = f.object + " " + iris::director::ToString(f.facet) + ": " + f.message;
            Note("gave up after " + std::to_string(f.attempts) + " attempts: " + lastError);
          }
      failuresLogged = finals;
    }

    void UpdateAvailability(const iris::Scene& scene)
    {
      needReload.clear();
      unavailable.clear();
      auto check = [&](const std::string& name, const std::set<std::string>& known, const std::set<std::string>& cached) {
        if (name.empty())
          return;
        if (!known.contains(name))
        {
          if (std::find(unavailable.begin(), unavailable.end(), name) == unavailable.end())
            unavailable.push_back(name);
        }
        else if (!cached.contains(name))
        {
          if (std::find(needReload.begin(), needReload.end(), name) == needReload.end())
            needReload.push_back(name);
        }
      };
      for (const auto& m : scene.ReferencedModels())
        check(m, knownModels, precachedModels);
      for (const auto& a : scene.ReferencedAnimations())
        check(a, knownAnims, precachedAnims);
      for (const auto& w : scene.ReferencedWeapons())
        check(w, knownWeapons, precachedWeapons);
      for (const auto& f : scene.ReferencedEffects())
        check(f, knownEffects, precachedEffects);
    }
  }  // namespace

  const char* ToString(SetState s)
  {
    switch (s)
    {
      case SetState::None: return "none";
      case SetState::Loading: return "loading";
      case SetState::Ready: return "ready";
      case SetState::Failed: return "failed";
    }
    return "none";
  }

  const char* AttachTag(const std::string& a)
  {
    if (a == "head") return "j_head";
    if (a == "neck") return "j_neck";
    if (a == "chest") return "j_spine4";
    if (a == "pelvis") return "j_mainroot";
    if (a == "rightHand") return "tag_weapon_right";
    if (a == "leftHand") return "tag_weapon_left";
    if (a == "weapon") return "tag_flash";
    if (a == "back") return "tag_stowed_back";
    return "origin";
  }

  // ---------------------------------------------------------------- builtins

  void RegisterBuiltins()
  {
    using Components::GSC::Script;

    // IW4IrisDirectorActive(): 1 only for a film set the editor started.
    Script::AddFunction("IW4IrisDirectorActive", [] {
      std::lock_guard _(bridgeMutex);
      Game::Scr_AddInt(requested && OfflineGuard::GetStatus().installed ? 1 : 0);
    });

    // IW4IrisManifest(): ["model:<name>", "anim:<name>", "weapon:<name>", "fx:<name>", ...]
    Script::AddFunction("IW4IrisManifest", [] {
      auto& s = GetSession();
      std::lock_guard sl(s.mutex);
      std::lock_guard _(bridgeMutex);
      DiscoverAssets();
      const auto manifest = BuildManifest(s.project.scene);
      Note(std::format("precache list: {} entries", manifest.size()));
      ReturnStrings(manifest);
    });

    // IW4IrisReady(<capabilities>): the mod is running.
    Script::AddFunction("IW4IrisReady", [] {
      std::lock_guard _(bridgeMutex);
      capabilities = ArgString(0);
      ResetObjects();  // a fresh level has no entities of ours
      // "protocol <n> ..."
      protocolSeen = 0;
      if (capabilities.rfind("protocol ", 0) == 0)
        protocolSeen = std::atoi(capabilities.c_str() + 9);
      if (protocolSeen != kProtocol)
      {
        state = SetState::Failed;
        lastError = "The Director Mod in the environment speaks protocol " + std::to_string(protocolSeen) +
                    ", this client protocol " + std::to_string(kProtocol) +
                    ". Repair the installation in the IW4Iris launcher (Setup > Repair / update).";
        Note(lastError);
        return;
      }
      state = SetState::Ready;
      Note("Director Mod ready: " + capabilities);
    });

    // IW4IrisNextRequest(): the next request as [op, id, args...], or [].
    Script::AddFunction("IW4IrisNextRequest", [] {
      std::lock_guard _(bridgeMutex);
      if (state != SetState::Ready)
      {
        Game::Scr_MakeArray();
        return;
      }
      const auto r = reconciler.NextRequest(Now());
      if (!r)
      {
        Game::Scr_MakeArray();
        return;
      }
      ReturnStrings(*r);
    });

    // IW4IrisRespond(<id>, <"ok" | "error">, <message>)
    Script::AddFunction("IW4IrisRespond", [] {
      std::lock_guard _(bridgeMutex);
      const std::string id = ArgString(0), status = ArgString(1), message = ArgString(2);
      const auto r = reconciler.OnResponse(id, status == "ok", message, Now());
      if (r == iris::director::Response::Stale)
        Log::Debug("Director: stale answer {} ignored", id);
      else if (r == iris::director::Response::Unknown)
        Log::Debug("Director: answer {} matches no pending request", id);
      else if (status != "ok")
        Note("request " + id + " failed: " + message);
    });

    // IW4IrisRegister(<object id>, <part>, <entity number>)
    Script::AddFunction("IW4IrisRegister", [] {
      // Entity numbers are informational (diagnostics); selection uses the
      // editor's own bounds.
      std::lock_guard _(bridgeMutex);
      if (Game::Scr_GetNumParam() >= 3)
        Log::Debug("Director: {} {} is entity {}", ArgString(0), ArgString(1), Game::Scr_GetInt(2));
    });

    // IW4IrisLog(<message>)
    Script::AddFunction("IW4IrisLog", [] {
      std::lock_guard _(bridgeMutex);
      Note("mod: " + ArgString(0));
    });

    // IW4IrisPoses(<server time>): [id, x, y, z, pitch, yaw, roll, visible, ...]
    Script::AddFunction("IW4IrisPoses", [] {
      auto& s = GetSession();
      std::lock_guard sl(s.mutex);
      std::lock_guard _(bridgeMutex);
      const int serverTime = Game::Scr_GetNumParam() > 0 ? Game::Scr_GetInt(0) : clientTime;
      lastServerTime = serverTime;
      // The client shows this frame when its time reaches serverTime.
      const double t = clientPlayhead + (serverTime - clientTime) / 1000.0 * playRate;
      Game::Scr_MakeArray();
      for (const auto& a : s.project.scene.actors)
      {
        if (!reconciler.Exists(a.id))
          continue;
        const auto st = iris::EvaluateActor(a, t);
        AddString(a.id);
        AddFloat(st.transform.position.x);
        AddFloat(st.transform.position.y);
        AddFloat(st.transform.position.z);
        AddFloat(st.transform.angles.pitch);
        AddFloat(st.transform.angles.yaw);
        AddFloat(st.transform.angles.roll);
        Game::Scr_AddInt(st.visible ? 1 : 0);
        Game::Scr_AddArray();
      }
      for (const auto& p : s.project.scene.props)
      {
        if (!reconciler.Exists(p.id))
          continue;
        const auto st = iris::EvaluateProp(p, t);
        AddString(p.id);
        AddFloat(st.transform.position.x);
        AddFloat(st.transform.position.y);
        AddFloat(st.transform.position.z);
        AddFloat(st.transform.angles.pitch);
        AddFloat(st.transform.angles.yaw);
        AddFloat(st.transform.angles.roll);
        Game::Scr_AddInt(st.visible ? 1 : 0);
        Game::Scr_AddArray();
      }
    });

    // IW4IrisEffects(): [fx, ox, oy, oz, fx, fy, fz, ux, uy, uz, ...]
    Script::AddFunction("IW4IrisEffects", [] {
      std::lock_guard _(bridgeMutex);
      Game::Scr_MakeArray();
      for (const auto& e : fxQueue)
      {
        if (!precachedEffects.contains(e.fx))
          continue;
        AddString(e.fx);
        for (const auto& v : {e.origin, e.forward, e.up})
        {
          AddFloat(v.x);
          AddFloat(v.y);
          AddFloat(v.z);
        }
      }
      fxQueue.clear();
    });

    // IW4IrisTagRequests(): [object id, tag, ...]
    Script::AddFunction("IW4IrisTagRequests", [] {
      std::lock_guard _(bridgeMutex);
      Game::Scr_MakeArray();
      for (const auto& [object, tag] : tagRequests)
      {
        AddString(object);
        AddString(tag);
      }
    });

    // IW4IrisReportTag(<object id>, <tag>, x, y, z, pitch, yaw, roll)
    Script::AddFunction("IW4IrisReportTag", [] {
      std::lock_guard _(bridgeMutex);
      if (Game::Scr_GetNumParam() < 8)
        return;
      TagSample sample;
      sample.transform.position = {Game::Scr_GetFloat(2), Game::Scr_GetFloat(3), Game::Scr_GetFloat(4)};
      sample.transform.angles = {Game::Scr_GetFloat(5), Game::Scr_GetFloat(6), Game::Scr_GetFloat(7)};
      sample.time = lastServerTime;
      auto& slot = tagSamples[{ArgString(0), ArgString(1)}];
      slot.first = slot.second.time ? slot.second : sample;
      slot.second = sample;
    });

    Log::Info("Director: {} GSC builtins registered (protocol {})", 11, kProtocol);
  }

  // ---------------------------------------------------------------- main thread

  bool StartFilmSet(const std::string& map, const std::string& gametype, std::string* error)
  {
    if (map.empty() || map.find_first_of(" ;\"\n\r") != std::string::npos)
    {
      *error = "Choose a map.";
      return false;
    }
    if (!Components::FastFiles::Exists(map))
    {
      *error = "The map " + map + " is not installed.";
      return false;
    }
    const std::string gt = gametype.empty() || gametype.find_first_of(" ;\"\n\r") != std::string::npos ? "dm" : gametype;
    {
      std::lock_guard _(bridgeMutex);
      requested = true;
      requestedMap = map;
      state = SetState::Loading;
      capabilities.clear();
      lastError.clear();
      framesWithoutGame = 0;
      loadingFrames = 0;
      ResetObjects();
      Note("loading film set " + map + " (" + gt + ")");
    }
    // A local listen server under the offline guard: no match timer, no
    // score limit, no pre-match countdown.
    Engine::Execute(std::format("set g_gametype {0}; set scr_{0}_timelimit 0; set scr_{0}_scorelimit 0; "
                                "set scr_{0}_numlives 0; set scr_game_matchstarttime 0; set scr_game_playerwaittime 0; "
                                "map {1}",
                                gt, map));
    return true;
  }

  void ReloadFilmSet()
  {
    {
      std::lock_guard _(bridgeMutex);
      if (!requested)
        return;
      state = SetState::Loading;
      loadingFrames = 0;
      ResetObjects();
      Note("reloading the film set");
    }
    Engine::Execute("map_restart");
  }

  void LeaveFilmSet()
  {
    bool wasRequested;
    {
      std::lock_guard _(bridgeMutex);
      wasRequested = requested;
      requested = false;
      state = SetState::None;
      ResetObjects();
    }
    if (wasRequested)
      Engine::Execute("disconnect");
  }

  void MainFrame(Session& s)
  {
    std::lock_guard _(bridgeMutex);
    if (!requested)
      return;
    const bool inGame = s.status.cgameReady && !s.status.demoPlaying;
    if (!inGame)
    {
      // Left the set (disconnect, demo, error): give up after a while.
      if (state == SetState::Ready && ++framesWithoutGame > 600)
      {
        Note("the film set was closed");
        requested = false;
        state = SetState::None;
        ResetObjects();
      }
      return;
    }
    framesWithoutGame = 0;
    if (state == SetState::Loading)
    {
      // In the game but the mod never answered: its scripts are missing or
      // failed to compile (the console has the script error).
      if (++loadingFrames > 600)
      {
        state = SetState::Failed;
        lastError = "The Director Mod did not start on this map. Repair the installation in the IW4Iris launcher "
                    "(Setup > Repair / update) and check the console for a script error.";
        Note(lastError);
      }
      return;
    }
    loadingFrames = 0;
    if (state != SetState::Ready)
      return;

    // Timeline prediction for the server's poses.
    const int now = Engine::ClientServerTime();
    if (now != lastClientTime)
    {
      const double measured = (s.playhead - lastPlayhead) / std::max((now - lastClientTime) / 1000.0, 1e-3);
      playRate = (s.playing || std::abs(measured) > 1e-9) ? std::clamp(measured, 0.0, 16.0) : 0.0;
      // Effects that became due since the last frame (forward playback only).
      const double delta = s.playhead - lastPlayhead;
      if (delta > 0.0 && delta < 0.5)
        for (const auto& ev : iris::EffectEventsBetween(s.project.scene, lastPlayhead, s.playhead))
        {
          iris::Transform tr = ev.effect->transform;
          if (!ev.effect->attachActor.empty())
          {
            if (const auto ap = AttachPoint(ev.effect->attachActor, ev.effect->attachTag))
            {
              tr.position = ap->position + tr.position;
              tr.angles = ap->angles;
            }
          }
          const iris::Mat3 axis = iris::AnglesToAxis(tr.angles);
          fxQueue.push_back({ev.effect->fx, tr.position, axis.col[0], axis.col[2]});
        }
      lastPlayhead = s.playhead;
      lastClientTime = now;
    }
    clientTime = now;
    clientPlayhead = s.playhead;

    SyncScene(s);
    wasPlaying = s.playing;

    // What the camera looks at: where new objects are placed.
    if (const auto cam = Runtime::LastAppliedCamera())
    {
      const iris::Vec3 from = cam->position;
      viewTarget = TraceSurface(from, from + iris::AnglesToForward(cam->angles) * 8192.0);
    }

    static int counter = 0;
    if (++counter % 30 == 0)
      UpdateAvailability(s.project.scene);
  }

  std::vector<MapInfo> ListMaps()
  {
    std::vector<MapInfo> maps;
    Game::UI_UpdateArenas();
    const int count = *Game::arenaCount;
    for (int i = 0; i < count; ++i)
    {
      const auto& arena = Components::ArenaLength::NewArenas[i];
      MapInfo m;
      m.name = arena.mapName;
      if (m.name.empty())
        continue;
      auto localise = [](const char* key) -> std::string {
        if (!key || !*key)
          return {};
        const char* text = Game::SEH_StringEd_GetString(key);
        return text && *text ? text : "";
      };
      m.label = localise(arena.uiName);
      if (m.label.empty() || m.label == arena.uiName)
        m.label = iris::HumanizeAssetName(m.name);
      m.description = localise(arena.description);
      m.installed = Components::FastFiles::Exists(m.name);
      maps.push_back(std::move(m));
    }
    std::sort(maps.begin(), maps.end(), [](const MapInfo& a, const MapInfo& b) { return a.label < b.label; });
    return maps;
  }

  void RestartAnimations()
  {
    std::lock_guard _(bridgeMutex);
    reconciler.RestartAnimations();
  }

  void RefreshMaps()
  {
    auto maps = ListMaps();
    std::lock_guard _(bridgeMutex);
    mapCache = std::move(maps);
  }

  std::vector<MapInfo> Maps()
  {
    std::lock_guard _(bridgeMutex);
    return mapCache;
  }

  std::optional<SurfaceHit> ViewTarget()
  {
    std::lock_guard _(bridgeMutex);
    return requested && state == SetState::Ready ? viewTarget : std::nullopt;
  }

  const std::vector<std::string>& AttachPoints()
  {
    static const std::vector<std::string> points{"origin", "head", "neck", "chest", "pelvis", "rightHand", "leftHand", "back", "weapon"};
    return points;
  }

  void Rediscover()
  {
    std::lock_guard _(bridgeMutex);
    if (Engine::IsCgameReady())
      DiscoverAssets();
  }

  std::optional<SurfaceHit> TraceSurface(const iris::Vec3& start, const iris::Vec3& end)
  {
    {
      std::lock_guard _(bridgeMutex);
      if (state != SetState::Ready)
        return std::nullopt;  // the trace needs the film set's server
    }
    Game::trace_t trace{};
    const float a[3] = {static_cast<float>(start.x), static_cast<float>(start.y), static_cast<float>(start.z)};
    const float b[3] = {static_cast<float>(end.x), static_cast<float>(end.y), static_cast<float>(end.z)};
    constexpr int kContentsSolid = 1;  // as in IW4x's Missile.cpp
    Game::G_TraceCapsule(&trace, a, b, Game::bounds_origin, static_cast<int>(Game::ENTITYNUM_NONE), kContentsSolid);
    if (trace.startsolid || trace.fraction >= 1.0f)
      return std::nullopt;
    SurfaceHit hit;
    hit.fraction = trace.fraction;
    hit.position = iris::Lerp(start, end, trace.fraction);
    hit.normal = iris::Normalize(iris::Vec3{trace.normal[0], trace.normal[1], trace.normal[2]});
    return hit;
  }

  // ---------------------------------------------------------------- any thread

  Status GetStatus()
  {
    std::lock_guard _(bridgeMutex);
    Status st;
    st.state = state;
    st.map = requestedMap;
    st.capabilities = capabilities;
    st.lastError = lastError;
    st.pendingRequests = reconciler.Queued() + reconciler.InFlight();
    st.objects = reconciler.ExistingObjects().size();
    st.timedOut = reconciler.TimedOut();
    st.staleAnswers = reconciler.RejectedStale();
    st.protocol = protocolSeen;
    for (const auto& f : reconciler.Failures())
      if (f.final)
        st.failures.push_back(f.object + " (" + iris::director::ToString(f.facet) + "): " + f.message);
    st.precachedModels = static_cast<int>(precachedModels.size());
    st.precachedAnimations = static_cast<int>(precachedAnims.size());
    st.precachedWeapons = static_cast<int>(precachedWeapons.size());
    st.precachedEffects = static_cast<int>(precachedEffects.size());
    st.needReload = needReload;
    st.unavailable = unavailable;
    st.log.assign(logLines.begin(), logLines.end());
    return st;
  }

  Catalog GetCatalog()
  {
    std::lock_guard _(bridgeMutex);
    return catalog;
  }

  int CatalogVersion()
  {
    std::lock_guard _(bridgeMutex);
    return catalog.version;
  }

  iris::director::ObjectStatus ObjectStatusOf(const std::string& id)
  {
    std::lock_guard _(bridgeMutex);
    return reconciler.Status(id);
  }

  bool InFilmSet()
  {
    std::lock_guard _(bridgeMutex);
    return requested;
  }

  bool Ready()
  {
    std::lock_guard _(bridgeMutex);
    return requested && state == SetState::Ready;
  }

  std::optional<iris::Transform> AttachPoint(const std::string& actor, const std::string& attach)
  {
    auto& s = GetSession();
    std::lock_guard sl(s.mutex);
    const iris::ActorDef* a = s.project.scene.FindActor(actor);
    if (!a)
      return std::nullopt;
    const std::string tag = AttachTag(attach);
    if (tag == "origin")
      return iris::EvaluateActor(*a, s.playhead).transform;
    std::lock_guard _(bridgeMutex);
    const auto it = tagSamples.find({actor, tag});
    if (it == tagSamples.end())
    {
      // No bone report yet: approximate from the actor's root.
      iris::Transform t = iris::EvaluateActor(*a, s.playhead).transform;
      const double height = attach == "head" ? 66 : attach == "neck" ? 60 : attach == "chest" ? 52 :
                            attach == "pelvis" ? 38 : 44;
      t.position.z += height;
      return t;
    }
    const auto& [prev, latest] = it->second;
    const int span = latest.time - prev.time;
    const double f = span > 0 ? std::clamp((Engine::ClientServerTime() - prev.time) / static_cast<double>(span), 0.0, 1.0) : 1.0;
    return Lerp(prev.transform, latest.transform, f);
  }
}
