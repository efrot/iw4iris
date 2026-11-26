// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The editor session shared by the game's main thread (camera, demo
// control, dvars) and the render thread (editor UI, frame capture). All
// fields are guarded by `mutex`; work that has to touch the game is posted
// to the main thread with Post().
#pragma once

#include <iris/demo.hpp>
#include <iris/history.hpp>
#include <iris/moments.hpp>
#include <iris/players.hpp>
#include <iris/project.hpp>
#include <iris/rig.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace IW4Iris
{
  struct UserSettings
  {
    double uiScale = 1.0;
    int accent = 0;                  // theme accent preset
    bool onboarding = true;          // show the getting-started window
    bool showHome = true;            // the start screen when nothing is loaded
    double mouseSensitivity = 0.11;  // degrees per mouse count
    iris::FreeCameraSettings freeCamera;
    std::vector<std::string> demoFolders;
    std::vector<std::string> recentProjects;
    std::set<std::string> favoriteDemos;
    std::map<std::string, std::string> demoLabels;
    std::string ffmpegPath;
    bool showPathOverlay = true;
    bool fitGameView = true;         // with the editor shown, scale the game into the space between the panels
    bool showFrustums = true;
    bool autosave = true;
    int autosaveSeconds = 60;

    // Director Mode
    bool beginnerMode = true;        // only the essential controls; raw names hidden
    std::set<std::string> favoriteAssets;   // raw asset names
    std::vector<std::string> recentAssets;
    std::set<std::string> favoriteMaps;
    std::vector<std::string> recentMaps;
    std::vector<std::string> favoritePlayers;  // player names starred in the Players panel
    std::vector<std::string> fontFolders;   // extra folders with .ttf / .otf

    // Composition guides (editor overlay only, never recorded)
    bool guideThirds = false;
    bool guideCentre = false;
    bool guideSafeAreas = false;
    bool guideHorizon = false;
    int guideAspect = 0;             // 0 off, 1 2.39:1, 2 1.85:1, 3 9:16, 4 1:1, 5 4:5
  };

  struct EngineStatus
  {
    bool cgameReady = false;
    bool demoPlaying = false;
    std::string demoName;
    std::string mapName;
    int connectionState = 0;
    int clientServerTime = 0;
    int snapshotServerTime = 0;
    std::optional<int> demoFirstServerTime;
    std::optional<int> demoLastServerTime;
    double sceneTime = 0.0;          // seconds since the demo's first snapshot
    double demoDuration = 0.0;       // from the demo index, 0 when unknown
    int renderWidth = 0;
    int renderHeight = 0;
    double fps = 0.0;
    std::vector<std::pair<int, std::string>> players;
    std::optional<iris::CameraState> gameView;  // the view the game computed last frame
    bool seeking = false;
    // The shot camera had no picture this frame (e.g. its player is not in
    // the demo now) and the game's own view was shown: its name, else empty.
    std::string cameraWithoutPicture;
    std::uint64_t captureFramesWithoutPicture = 0;  // during the current / last recording
    std::string seekMessage;
    bool paused = false;
    std::vector<std::string> checkedDvars;   // the dvars the editor uses
    std::set<std::string> availableDvars;    // those this game has
  };

  enum class SeekState
  {
    Idle,
    Restarting,   // demo restarted for a backward seek, waiting for its first snapshot
    Forward,      // skipping forward towards the target
  };

  class Session
  {
  public:
    std::recursive_mutex mutex;

    // Project
    iris::Project project = iris::MakeDefaultProject("Untitled");
    std::filesystem::path projectFile;
    bool dirty = false;
    iris::History<iris::Project> history{300};
    std::string lastError;
    std::string notice;           // a message for the editor's notification (main thread -> UI)

    // Time
    double playhead = 0.0;        // cinematic seconds
    bool playing = false;
    double playbackRate = 1.0;    // multiplies the project's speed curve in preview
    bool scrubbing = false;       // the timeline's ruler is being dragged
    bool timelineFitRequested = false;  // the timeline view should show everything (new demo, new project)
    double demoLengthFitted = 0.0;      // demo length the timeline was fitted to (0: none yet)

    // Editor
    bool editorVisible = false;
    bool flying = false;          // free camera controlled by the mouse
    // The viewport shows the navigation (free) camera instead of the shot.
    // Flying turns it on; it never changes the project's cameras. Captures
    // and a hidden editor always show the shot.
    bool freeView = false;
    iris::FreeCameraController freeCamera;
    bool freeCameraPlaced = false;  // the free camera starts where the view is
    std::string selectedCamera;   // camera shown in the inspector
    std::set<std::size_t> selectedKeys;  // keys of the selected camera's path
    std::string selectedTrack;    // property track for the graph editor

    // Director Mode
    std::string selectedObject;   // actor / prop / effect / graphic id
    int gizmoMode = 0;            // 0 move, 1 rotate, 2 scale
    std::string pendingTemplate;  // scene template to build once the film set is ready

    // Demo
    std::optional<std::filesystem::path> loadedDemoPath;  // the original file the demo was copied from
    std::optional<iris::DemoIndex> demoIndex;
    std::uint64_t demoGeneration = 0;  // bumped for every demo started: late index scans of an older one are dropped

    // Players of the demo or film set (updated every frame on the main thread)
    iris::PlayerRoster players;
    std::string playersKey;       // the demo / map the roster belongs to
    std::string selectedPlayer;   // roster id

    // Moments of the demo (kills, deaths, trickshot candidates ...), from
    // what has been played; the analysis pass plays the whole demo fast.
    iris::MomentLog moments;
    struct MomentAnalysis
    {
      bool active = false;
      double returnTo = 0.0;      // playhead to go back to
      bool wasPaused = true;
      int startedFrames = 0;
    } analysis;
    std::vector<iris::DemoEntry> demoList;
    SeekState seekState = SeekState::Idle;
    double seekTargetScene = 0.0;
    int seekRestartTime = 0;
    bool paused = false;
    int pausedRealtime = 0;

    EngineStatus status;
    UserSettings settings;
    std::filesystem::path baseDirectory;  // <game>/iw4iris

    // Project edits. Commit() records an undo step after a change.
    void Commit(const std::string& label, bool merge = false);
    bool Undo();
    bool Redo();
    void ResetHistory();

    iris::CameraDef* SelectedCamera();
    // The dolly camera new keys go to: the selected camera when it is a
    // dolly, else the first dolly (created when there is none).
    iris::CameraDef& EditTargetDolly();

    // Work for the game's main thread.
    void Post(std::function<void()> work);
    std::vector<std::function<void()>> TakePosted();

    std::filesystem::path ProjectsDirectory() const { return baseDirectory / "projects"; }
    std::filesystem::path RecordingsDirectory() const { return baseDirectory / "recordings"; }
    std::filesystem::path ExportsDirectory() const { return baseDirectory / "exports"; }

  private:
    std::vector<std::function<void()>> posted_;
  };

  Session& GetSession();

  // Settings persistence (<game>/iw4iris/settings.json).
  void LoadSettings(Session& session);
  void SaveSettings(const Session& session);
}
