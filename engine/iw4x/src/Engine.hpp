// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The IW4 engine adapter: the only code in IW4Iris that reads or writes game
// state. It uses nothing but the bindings the pinned IW4x revision itself
// declares and uses (Game::* in src/Game, Components::* in src/Components);
// COMPATIBILITY.md lists each one with where IW4x uses it. Every function
// here runs on the game's main thread unless noted.
#pragma once

#include <iris/camera.hpp>
#include <iris/math.hpp>

#include <iris/players.hpp>

#include <optional>
#include <string>
#include <vector>

namespace IW4Iris::Engine
{
  // ---------------------------------------------------------------- state

  bool IsCgameReady();        // cgame initialised and the client active
  bool IsDemoPlaying();
  int ConnectionState();      // Game::connstate_t
  std::string DemoName();     // clientConnections->demoName (no extension)
  std::string MapName();      // clientActive_t::mapname

  int ClientServerTime();     // clientActive_t::serverTime (interpolated client time, ms)
  int SnapshotServerTime();   // the latest snapshot's server time
  int Realtime();             // clientStatic_t::realtime
  void SetRealtime(int value);

  // The view the game computed for this frame (cg_s::refdef), before any
  // override. Only meaningful from Hooks::OnViewReady.
  struct GameView
  {
    iris::Vec3 origin;
    iris::Mat3 axis;
    double tanHalfFovX = 1.0;
    double tanHalfFovY = 1.0;
    int width = 0;
    int height = 0;
  };
  std::optional<GameView> ReadView(int localClientNum);
  // Replaces the view origin, axis and field of view of this frame.
  void WriteView(int localClientNum, const iris::CameraState& camera);
  iris::CameraState ToCameraState(const GameView& view);

  int RenderWidth();
  int RenderHeight();

  // ---------------------------------------------------------------- entities

  struct PlayerInfo
  {
    int clientNum = -1;
    std::string name;
    iris::Vec3 origin;
    bool present = false;
  };
  std::vector<PlayerInfo> Players();
  // Every player the current snapshot lists or shows (demo or film set):
  // the client list gives slot, name and team; the player entities in the
  // snapshot give position, view angles (interpolated like the cgame does),
  // stance and life; the recording player comes from the predicted player
  // state (exact eyes). Main thread.
  std::vector<iris::PlayerObservation> ObservePlayers();
  std::optional<iris::Vec3> EntityOrigin(int entityNum);
  int LocalClientNum();          // the demo's / game's own client number
  iris::Vec3 LocalPlayerOrigin();

  // ---------------------------------------------------------------- dvars

  bool HasDvar(const char* name);
  std::optional<float> GetFloat(const char* name);
  std::optional<int> GetInt(const char* name);
  std::optional<bool> GetBool(const char* name);
  std::optional<std::string> GetString(const char* name);
  std::optional<iris::Vec3> GetVec3(const char* name);
  // Range of a float dvar, when it has one.
  std::optional<std::pair<float, float>> FloatDomain(const char* name);

  // Dvar overrides remember the original value the first time a dvar is
  // changed through them, so RestoreAll() puts the game back exactly as it
  // was ("reset to defaults"). They return false when the dvar is missing.
  bool OverrideFloat(const char* name, float value);
  bool OverrideInt(const char* name, int value);
  bool OverrideBool(const char* name, bool value);
  bool OverrideVec3(const char* name, const iris::Vec3& value);
  bool OverrideColor(const char* name, const iris::Vec3& rgb, float alpha = 1.0f);
  void Restore(const char* name);
  void RestoreAll();
  std::vector<std::string> OverriddenDvars();

  // ---------------------------------------------------------------- commands

  void Execute(const std::string& command);  // through the game's command buffer
  std::string BasePath();                    // fs_basepath
}
