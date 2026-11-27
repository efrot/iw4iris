// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The bridge between the editor's scene (iris::Scene) and the IW4Iris
// Director Mod (mods/iw4iris_director, GSC) that owns the game entities of
// a film set: a map the editor loads as a local, offline session.
//
// The bridge registers GSC builtins with IW4x's own API
// (Components::GSC::Script::AddFunction). The mod calls them every server
// frame to fetch requests (spawn, change, delete, animate), the transforms
// for the current timeline time, effects and bone queries, and answers
// every request. Protocol: docs/DIRECTOR_PROTOCOL.md.
//
// Everything here runs on the game's main thread (the server's script VM
// runs there too) except the read-only accessors, which lock.
#pragma once

#include <iris/assets.hpp>
#include <iris/director_sync.hpp>
#include <iris/scene.hpp>

#include <optional>
#include <string>
#include <vector>

namespace IW4Iris
{
  class Session;
}

namespace IW4Iris::Director
{
  inline constexpr int kProtocol = 2;

  // Called from the component's constructor, before any script compiles.
  void RegisterBuiltins();

  enum class SetState
  {
    None,     // no film set
    Loading,  // the map is loading / the mod has not answered yet
    Ready,    // the mod answered; objects follow the timeline
    Failed,
  };
  const char* ToString(SetState state);

  struct MapInfo
  {
    std::string name;         // "mp_highrise"
    std::string label;        // localised, e.g. "Highrise"
    std::string description;
    bool installed = false;   // its fast file is present
  };

  struct Status
  {
    SetState state = SetState::None;
    std::string map;
    std::string modFolder;        // "" = the standard game files
    std::string capabilities;     // from the mod
    std::string lastError;
    std::size_t pendingRequests = 0;  // queued + sent, not answered
    std::size_t objects = 0;      // scene objects the mod confirmed
    std::uint64_t timedOut = 0;   // requests that got no answer in time
    std::uint64_t staleAnswers = 0;
    int protocol = 0;             // the mod's protocol (0: no answer yet)
    std::vector<std::string> failures;  // objects whose operations gave up
    int precachedModels = 0;
    int precachedAnimations = 0;
    int precachedWeapons = 0;
    int precachedEffects = 0;
    // Scene assets that are on this map but were not precached when the set
    // loaded: they appear after "Reload set".
    std::vector<std::string> needReload;
    // Scene assets this map does not have at all.
    std::vector<std::string> unavailable;
    std::vector<std::string> log;
  };

  // Loaded assets of the current map (from the asset database).
  struct Catalog
  {
    int version = 0;              // changes when the catalog is rebuilt
    std::vector<iris::CharacterEntry> characters;
    std::vector<iris::AssetEntry> animations;
    std::vector<iris::AssetEntry> weapons;
    std::vector<iris::AssetEntry> props;
    std::vector<iris::AssetEntry> effects;
    std::vector<std::string> modelNames, animationNames, weaponNames, effectNames;
  };

  // Main thread.
  // modFolder: "" for the standard game files, else "mods/<name>" (see
  // iris::FilmSetSettings::modFolder); applied right before the map loads.
  bool StartFilmSet(const std::string& map, const std::string& gametype, const std::string& modFolder,
                    std::string* error);
  // The IW4x mod folders of the installation ("mods/<name>" with a mod.ff or
  // an .iwd), for the film set's "Game files" choice.
  std::vector<std::string> InstalledModFolders();
  void ReloadFilmSet();
  void LeaveFilmSet();
  void MainFrame(Session& session);  // with the session locked
  void RestartAnimations();          // re-send the current clips (a recording begins)
  std::vector<MapInfo> ListMaps();
  void Rediscover();                 // rebuild the catalog from the loaded zones

  struct SurfaceHit
  {
    iris::Vec3 position;
    iris::Vec3 normal;
    double fraction = 1.0;
  };
  // World geometry trace (CONTENTS_SOLID) through the film set's server.
  std::optional<SurfaceHit> TraceSurface(const iris::Vec3& start, const iris::Vec3& end);
  void RefreshMaps();                // rebuilds the map list Maps() returns

  // Any thread.
  Status GetStatus();
  Catalog GetCatalog();
  int CatalogVersion();
  bool InFilmSet();                  // a film set was requested and is loading or ready
  // Engine state of a scene object (spawning, ready, failed ...).
  iris::director::ObjectStatus ObjectStatusOf(const std::string& id);
  bool Ready();
  // World transform of an actor's attach point ("origin", "head", "neck",
  // "chest", "pelvis", "rightHand", "leftHand", "weapon") at the client's
  // current render time, from the bone reports of the mod.
  std::optional<iris::Transform> AttachPoint(const std::string& actor, const std::string& attach);
  // The engine tag an attach point name refers to.
  const char* AttachTag(const std::string& attach);
  const std::vector<std::string>& AttachPoints();

  std::vector<MapInfo> Maps();       // cached by RefreshMaps
  // The surface the shot camera looks at (traced every frame in a film set).
  std::optional<SurfaceHit> ViewTarget();
}
