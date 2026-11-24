// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Native world-space motion graphics: extruded text, logos, shapes, models
// and image planes drawn into the game's frame with Direct3D 9.
//
//   main thread   MainFrame: (re)builds geometry when a graphic's geometry
//                 key changes (iris::BuildGraphicGeometry, cached).
//                 OnView: evaluates every graphic for the frame's time and
//                 camera into a draw list tagged with the frame's scene time.
//   render thread Draw (before the capture reads the back buffer, so the
//                 graphics are part of recordings and editor overlays are
//                 not): picks the draw list of the frame being presented,
//                 sets up the view from the engine's own matrices when they
//                 match the frame's camera, and draws depth-tested against
//                 the scene's depth buffer when that buffer is verified to
//                 hold the scene.
//
// The graphics are drawn after the game's post-processing: they get their
// own directional key light, not the map's lighting, shadows, fog or depth
// of field (see FEATURE_MATRIX.md).
#pragma once

#include <iris/graphics.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

struct IDirect3DDevice9;

namespace IW4Iris
{
  class Session;
}

namespace IW4Iris::Graphics
{
  struct Status
  {
    bool depthAvailable = false;
    std::string depth;            // why depth testing is (not) available
    bool engineMatrices = false;
    std::string matrices;         // which view / projection is used and why
    bool reversedDepth = false;
    bool timeSynced = false;      // the draw list matched the presented frame's scene time
    int drawnGraphics = 0;
    int drawnTriangles = 0;
    std::map<std::string, std::string> errors;                 // graphic id -> why it has no geometry
    std::map<std::string, std::vector<std::string>> warnings;  // graphic id -> geometry warnings
  };

  void MainFrame(Session& session);  // main thread, session locked
  void OnView(Session& session, double t, const iris::CameraState& camera, int clientTime);
  void Draw(IDirect3DDevice9* device);
  void DeviceLost();

  Status GetStatus();
  // Geometry bounds (graphic space) and element count of a built graphic.
  std::optional<iris::Bounds3D> Bounds(const std::string& graphicId);
  std::size_t ElementCount(const std::string& graphicId);
  // Pose evaluated for the latest frame (for gizmos and selection).
  std::optional<iris::GraphicPose> LastPose(const std::string& graphicId);

  // Font files the font picker offers: the shipped fonts, imported ones and
  // the installed Windows fonts.
  std::vector<std::string> AvailableFonts();
  std::string DefaultFont();
}
