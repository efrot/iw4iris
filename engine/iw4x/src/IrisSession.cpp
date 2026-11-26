// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Session.hpp"
#include "Log.hpp"

#include <nlohmann/json.hpp>

namespace IW4Iris
{
  Session& GetSession()
  {
    static Session session;
    return session;
  }

  void Session::Commit(const std::string& label, bool merge)
  {
    std::lock_guard _(mutex);
    history.Commit(project, label, merge);
    dirty = true;
  }

  bool Session::Undo()
  {
    std::lock_guard _(mutex);
    if (const auto* state = history.Undo())
    {
      project = *state;
      selectedKeys.clear();
      dirty = true;
      return true;
    }
    return false;
  }

  bool Session::Redo()
  {
    std::lock_guard _(mutex);
    if (const auto* state = history.Redo())
    {
      project = *state;
      selectedKeys.clear();
      dirty = true;
      return true;
    }
    return false;
  }

  void Session::ResetHistory()
  {
    std::lock_guard _(mutex);
    history.Reset(project);
  }

  iris::CameraDef* Session::SelectedCamera()
  {
    if (auto* c = project.FindCamera(selectedCamera))
      return c;
    if (auto* c = project.ActiveCamera())
      return c;
    return project.cameras.empty() ? nullptr : &project.cameras.front();
  }

  iris::CameraDef& Session::EditTargetDolly()
  {
    if (auto* c = project.FindCamera(selectedCamera); c && c->mode == iris::CameraMode::Dolly)
      return *c;
    for (auto& c : project.cameras)
      if (c.mode == iris::CameraMode::Dolly)
        return c;
    iris::CameraDef dolly;
    dolly.id = iris::MakeUniqueId(project, "cam");
    dolly.name = "Dolly " + std::to_string(project.cameras.size() + 1);
    dolly.mode = iris::CameraMode::Dolly;
    project.cameras.push_back(dolly);
    return project.cameras.back();
  }

  void Session::Post(std::function<void()> work)
  {
    std::lock_guard _(mutex);
    posted_.push_back(std::move(work));
  }

  std::vector<std::function<void()>> Session::TakePosted()
  {
    std::lock_guard _(mutex);
    std::vector<std::function<void()>> out;
    out.swap(posted_);
    return out;
  }

  void LoadSettings(Session& s)
  {
    const auto file = s.baseDirectory / "settings.json";
    std::ifstream in(file, std::ios::binary);
    if (!in)
      return;
    try
    {
      const auto j = nlohmann::json::parse(in);
      auto& u = s.settings;
      u.uiScale = std::clamp(j.value("uiScale", u.uiScale), 0.6, 2.5);
      u.accent = j.value("accent", u.accent);
      u.onboarding = j.value("onboarding", u.onboarding);
      u.showHome = j.value("showHome", u.showHome);
      u.mouseSensitivity = std::clamp(j.value("mouseSensitivity", u.mouseSensitivity), 0.005, 2.0);
      u.freeCamera.speed = std::clamp(j.value("freeCamSpeed", u.freeCamera.speed), 1.0, 20000.0);
      u.freeCamera.acceleration = std::clamp(j.value("freeCamAcceleration", u.freeCamera.acceleration), 0.5, 100.0);
      u.freeCamera.lookSmoothing = std::clamp(j.value("freeCamSmoothing", u.freeCamera.lookSmoothing), 0.0, 0.95);
      u.freeCamera.rollSpeed = std::clamp(j.value("freeCamRollSpeed", u.freeCamera.rollSpeed), 1.0, 360.0);
      u.demoFolders = j.value("demoFolders", u.demoFolders);
      u.recentProjects = j.value("recentProjects", u.recentProjects);
      u.favoriteDemos = j.value("favoriteDemos", u.favoriteDemos);
      u.demoLabels = j.value("demoLabels", u.demoLabels);
      u.ffmpegPath = j.value("ffmpegPath", u.ffmpegPath);
      u.showPathOverlay = j.value("showPathOverlay", u.showPathOverlay);
      u.fitGameView = j.value("fitGameView", u.fitGameView);
      u.showFrustums = j.value("showFrustums", u.showFrustums);
      u.autosave = j.value("autosave", u.autosave);
      u.autosaveSeconds = std::clamp(j.value("autosaveSeconds", u.autosaveSeconds), 10, 3600);
      u.beginnerMode = j.value("beginnerMode", u.beginnerMode);
      u.favoriteAssets = j.value("favoriteAssets", u.favoriteAssets);
      u.recentAssets = j.value("recentAssets", u.recentAssets);
      u.favoriteMaps = j.value("favoriteMaps", u.favoriteMaps);
      u.recentMaps = j.value("recentMaps", u.recentMaps);
      u.fontFolders = j.value("fontFolders", u.fontFolders);
      u.guideThirds = j.value("guideThirds", u.guideThirds);
      u.guideCentre = j.value("guideCentre", u.guideCentre);
      u.guideSafeAreas = j.value("guideSafeAreas", u.guideSafeAreas);
      u.guideHorizon = j.value("guideHorizon", u.guideHorizon);
      u.guideAspect = std::clamp(j.value("guideAspect", u.guideAspect), 0, 5);
    }
    catch (const std::exception& e)
    {
      Log::Warn("Settings file {} could not be read ({}); using defaults", file.string(), e.what());
    }
  }

  void SaveSettings(const Session& s)
  {
    const auto& u = s.settings;
    const nlohmann::json j{{"uiScale", u.uiScale},
                           {"accent", u.accent},
                           {"onboarding", u.onboarding},
                           {"showHome", u.showHome},
                           {"mouseSensitivity", u.mouseSensitivity},
                           {"freeCamSpeed", u.freeCamera.speed},
                           {"freeCamAcceleration", u.freeCamera.acceleration},
                           {"freeCamSmoothing", u.freeCamera.lookSmoothing},
                           {"freeCamRollSpeed", u.freeCamera.rollSpeed},
                           {"demoFolders", u.demoFolders},
                           {"recentProjects", u.recentProjects},
                           {"favoriteDemos", u.favoriteDemos},
                           {"demoLabels", u.demoLabels},
                           {"ffmpegPath", u.ffmpegPath},
                           {"showPathOverlay", u.showPathOverlay},
                           {"fitGameView", u.fitGameView},
                           {"showFrustums", u.showFrustums},
                           {"autosave", u.autosave},
                           {"autosaveSeconds", u.autosaveSeconds},
                           {"beginnerMode", u.beginnerMode},
                           {"favoriteAssets", u.favoriteAssets},
                           {"recentAssets", u.recentAssets},
                           {"favoriteMaps", u.favoriteMaps},
                           {"recentMaps", u.recentMaps},
                           {"fontFolders", u.fontFolders},
                           {"guideThirds", u.guideThirds},
                           {"guideCentre", u.guideCentre},
                           {"guideSafeAreas", u.guideSafeAreas},
                           {"guideHorizon", u.guideHorizon},
                           {"guideAspect", u.guideAspect}};
    std::error_code ec;
    std::filesystem::create_directories(s.baseDirectory, ec);
    const auto file = s.baseDirectory / "settings.json";
    auto tmp = file;
    tmp += ".tmp";
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      out << j.dump(2);
      if (!out)
        return;
    }
    std::filesystem::rename(tmp, file, ec);
  }
}
