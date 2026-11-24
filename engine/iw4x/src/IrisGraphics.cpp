// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "Graphics.hpp"

#include "Director.hpp"
#include "Engine.hpp"
#include "Log.hpp"
#include "Session.hpp"

#include <iris/projection.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <set>

namespace IW4Iris::Graphics
{
  namespace
  {
    std::mutex gfxMutex;

    struct CacheEntry
    {
      std::string key;
      std::shared_ptr<const iris::GraphicGeometry> geometry;
    };
    std::map<std::string, CacheEntry> cache;
    std::map<std::string, std::shared_ptr<iris::Font>> fonts;
    std::map<std::string, iris::GraphicPose> lastPoses;

    struct DrawItem
    {
      std::string graphic;
      std::shared_ptr<const iris::GraphicGeometry> geometry;
      std::size_t element = 0;
      iris::Matrix4 world{};
      iris::MaterialSlots materials;
      bool useElementColor = false;
      iris::Vec3 elementColor;
      double opacity = 1.0;
      double emissionBoost = 0.0;
      iris::GraphicRenderMode mode = iris::GraphicRenderMode::Depth;
      bool doubleSided = false;
      double distance = 0.0;
    };

    struct Frame
    {
      int time = 0;
      iris::CameraState camera;
      iris::Vec3 lightDirection{0.35, -0.45, -0.82};
      iris::Vec3 lightColor{1.0, 0.97, 0.9};
      std::vector<DrawItem> items;
    };
    std::deque<Frame> frames;

    Status status;

    // Render-thread resources.
    IDirect3DQuery9* occlusionQuery = nullptr;
    bool queryPending = false;
    int probeCountdown = 0;
    DWORD probePixels = 0;
    std::map<std::string, IDirect3DTexture9*> textures;

    std::filesystem::path FromUtf8(const std::string& s)
    {
      return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
    }

    std::filesystem::path FontsFolder() { return GetSession().baseDirectory / "fonts"; }

    std::filesystem::path WindowsFonts()
    {
      wchar_t buffer[MAX_PATH]{};
      const UINT n = GetWindowsDirectoryW(buffer, MAX_PATH);
      return n ? std::filesystem::path(buffer) / "Fonts" : std::filesystem::path();
    }

    std::shared_ptr<iris::Font> ResolveFont(const std::string& font, const std::filesystem::path& projectDir, std::string* error)
    {
      std::vector<std::filesystem::path> candidates;
      if (font.empty())
        candidates.push_back(FontsFolder() / "Rubik-SemiBold.ttf");
      else
      {
        const auto p = FromUtf8(font);
        if (p.is_absolute())
          candidates.push_back(p);
        else
        {
          if (!projectDir.empty())
            candidates.push_back(projectDir / p);
          candidates.push_back(FontsFolder() / p);
          candidates.push_back(WindowsFonts() / p);
        }
      }
      std::error_code ec;
      for (const auto& c : candidates)
      {
        if (!std::filesystem::is_regular_file(c, ec))
          continue;
        const std::string key = c.string();
        if (const auto it = fonts.find(key); it != fonts.end())
          return it->second;
        auto loaded = iris::Font::Load(c, error);
        if (loaded)
        {
          fonts[key] = loaded;
          if (loaded->Info().RestrictedEmbedding())
            Log::Info("Graphics: font {} restricts embedding; it is used for rendering only", key);
        }
        return loaded;
      }
      if (error)
        *error = "The font \"" + font + "\" was not found.";
      return nullptr;
    }

    iris::Matrix4 ElementMatrix(const iris::GraphicElement& e, const iris::ElementPose& p)
    {
      const double s = std::max(p.scale, 0.0);
      const iris::Vec3 r0 = iris::RotateGraphicSpace({s, 0, 0}, p.rotationDeg);
      const iris::Vec3 r1 = iris::RotateGraphicSpace({0, s, 0}, p.rotationDeg);
      const iris::Vec3 r2 = iris::RotateGraphicSpace({0, 0, s * std::max(p.depthScale, 0.0)}, p.rotationDeg);
      const iris::Vec3 t = e.pivot + p.offset;
      auto f = [](double v) { return static_cast<float>(v); };
      return {f(r0.x), f(r0.y), f(r0.z), 0, f(r1.x), f(r1.y), f(r1.z), 0,
              f(r2.x), f(r2.y), f(r2.z), 0, f(t.x),  f(t.y),  f(t.z),  1};
    }

    D3DCOLORVALUE Color(const iris::Vec3& c, double a = 1.0)
    {
      return {static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z), static_cast<float>(a)};
    }

    D3DMATRIX ToD3D(const iris::Matrix4& m)
    {
      D3DMATRIX d;
      std::memcpy(&d, m.data(), sizeof d);
      return d;
    }

    iris::Matrix4 FromGfx(const Game::GfxMatrix& g, bool transpose)
    {
      iris::Matrix4 m{};
      for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
          m[static_cast<std::size_t>(r * 4 + c)] = transpose ? g.m[c][r] : g.m[r][c];
      return m;
    }

    double Det3(const iris::Matrix4& m)
    {
      return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
    }

    // The renderer's source state of the frame being drawn: the address
    // IW4x itself uses for it (Renderer::ListSamplers, 0x6CAF080).
    Game::GfxCmdBufSourceState* EngineSource()
    {
      return reinterpret_cast<Game::GfxCmdBufSourceState*>(0x6CAF080);
    }

    struct ViewSetup
    {
      iris::Matrix4 view{}, projection{};
      bool engine = false;
      bool reversed = false;
      std::string reason;
    };

    // Uses the engine's view and projection of the presented frame when its
    // camera is the one the frame was built with and the matrices map that
    // camera's view direction to the screen centre; otherwise our own.
    ViewSetup MakeView(const Frame& frame, double aspect)
    {
      ViewSetup v;
      const iris::ViewParams own = iris::MakeViewParams(frame.camera, aspect, Engine::GetFloat("r_znear").value_or(4.0f));
      v.view = iris::D3DViewMatrix(own);
      v.projection = iris::D3DProjectionMatrix(own, iris::DepthConvention::StandardInfinite);
      auto* src = EngineSource();
      const Game::GfxViewParms* vp = src ? src->viewParms3D : nullptr;
      if (!vp)
      {
        v.reason = "own matrices (the engine has no 3D view this frame)";
        return v;
      }
      const iris::Vec3 eo{vp->camera.origin[0], vp->camera.origin[1], vp->camera.origin[2]};
      if (iris::Distance(eo, frame.camera.position) > 1.0)
      {
        v.reason = "own matrices (the engine's camera is not this frame's camera)";
        return v;
      }
      const iris::Vec3 ahead = frame.camera.position + iris::AnglesToForward(frame.camera.angles) * 200.0;
      for (const bool transpose : {false, true})
      {
        const auto view = FromGfx(vp->viewMatrix, transpose);
        const auto proj = FromGfx(vp->projectionMatrix, transpose);
        double w = 0.0;
        const iris::Vec3 ndc = iris::TransformPoint(iris::Multiply(view, proj), ahead, &w);
        if (w > 0.0 && std::abs(ndc.x) < 0.02 && std::abs(ndc.y) < 0.02)
        {
          v.view = view;
          v.projection = proj;
          v.engine = true;
          // Depth direction: is a far point's depth smaller than a near one's?
          const iris::Vec3 closePoint = frame.camera.position + iris::AnglesToForward(frame.camera.angles) * 20.0;
          const double zn = iris::TransformPoint(iris::Multiply(view, proj), closePoint).z;
          const double zf = iris::TransformPoint(iris::Multiply(view, proj), ahead).z;
          v.reversed = zf < zn;
          v.reason = std::string("engine matrices") + (transpose ? " (transposed)" : "");
          return v;
        }
      }
      v.reason = "own matrices (the engine's matrices did not validate)";
      return v;
    }

    struct ProbeVertex
    {
      float x, y, z, rhw;
    };

    // Depth buffer check: draws a full-screen quad at the far end of the depth
    // range (colour writes off) with an occlusion query. If (nearly) every
    // pixel passes, the depth buffer holds no scene and occlusion would be
    // wrong.
    void Probe(IDirect3DDevice9* device, UINT width, UINT height, bool reversed)
    {
      if (!occlusionQuery)
      {
        if (FAILED(device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &occlusionQuery)))
        {
          occlusionQuery = nullptr;
          return;
        }
      }
      if (queryPending)
      {
        DWORD pixels = 0;
        if (occlusionQuery->GetData(&pixels, sizeof pixels, 0) == S_OK)
        {
          queryPending = false;
          probePixels = pixels;
          const double total = static_cast<double>(width) * height;
          std::lock_guard _(gfxMutex);
          if (pixels >= total * 0.995)
          {
            status.depthAvailable = false;
            status.depth = "unavailable: the depth buffer holds no scene when the frame is presented";
          }
          else
          {
            status.depthAvailable = true;
            status.depth = "verified (" + std::to_string(static_cast<int>(100.0 * (1.0 - pixels / total))) +
                           " % of the screen is covered by scene depth)";
          }
        }
        return;
      }
      if (--probeCountdown > 0)
        return;
      probeCountdown = 120;
      const float z = reversed ? 0.0001f : 0.9999f;
      const float w = static_cast<float>(width), h = static_cast<float>(height);
      const ProbeVertex quad[4] = {{-0.5f, -0.5f, z, 1}, {w - 0.5f, -0.5f, z, 1}, {-0.5f, h - 0.5f, z, 1}, {w - 0.5f, h - 0.5f, z, 1}};
      device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
      device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
      device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
      device->SetRenderState(D3DRS_ZFUNC, static_cast<DWORD>(reversed ? D3DCMP_GREATEREQUAL : D3DCMP_LESSEQUAL));
      device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
      device->SetFVF(D3DFVF_XYZRHW);
      occlusionQuery->Issue(D3DISSUE_BEGIN);
      device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(ProbeVertex));
      occlusionQuery->Issue(D3DISSUE_END);
      queryPending = true;
      device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    }

    IDirect3DTexture9* TextureFor(IDirect3DDevice9* device, const DrawItem& item)
    {
      const auto& img = item.geometry->texture;
      if (!img || !img->Valid())
        return nullptr;
      const std::string key = item.graphic + "#" + std::to_string(reinterpret_cast<std::uintptr_t>(item.geometry.get()));
      if (const auto it = textures.find(key); it != textures.end())
        return it->second;
      IDirect3DTexture9* tex = nullptr;
      if (FAILED(device->CreateTexture(static_cast<UINT>(img->width), static_cast<UINT>(img->height), 1, D3DUSAGE_DYNAMIC,
                                       D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr)))
        return nullptr;
      D3DLOCKED_RECT lr{};
      if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
      {
        for (int y = 0; y < img->height; ++y)
        {
          auto* row = static_cast<std::uint8_t*>(lr.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(lr.Pitch);
          for (int x = 0; x < img->width; ++x)
          {
            const std::uint8_t* p = &img->rgba[(static_cast<std::size_t>(y) * img->width + x) * 4];
            row[x * 4 + 0] = p[2];
            row[x * 4 + 1] = p[1];
            row[x * 4 + 2] = p[0];
            row[x * 4 + 3] = p[3];
          }
        }
        tex->UnlockRect(0);
      }
      textures[key] = tex;
      return tex;
    }

    void SetMaterial(IDirect3DDevice9* device, const iris::MaterialDef& m, const DrawItem& item, bool front)
    {
      const iris::Vec3 base = item.useElementColor && front ? item.elementColor : m.color;
      const double alpha = std::clamp(m.opacity * item.opacity, 0.0, 1.0);
      D3DMATERIAL9 mat{};
      mat.Diffuse = Color(base * (1.0 - 0.6 * m.metallic), alpha);
      mat.Ambient = Color(base * (1.0 - 0.6 * m.metallic), alpha);
      const iris::Vec3 spec = iris::Lerp(iris::Vec3{1, 1, 1}, base, m.metallic) * m.specular;
      mat.Specular = Color(spec, alpha);
      mat.Emissive = Color(m.emission * m.emissionStrength + base * item.emissionBoost, alpha);
      mat.Power = static_cast<float>(std::clamp(m.gloss, 1.0, 256.0));
      device->SetMaterial(&mat);
    }

    int DrawItemGeometry(IDirect3DDevice9* device, const DrawItem& item, bool index32)
    {
      const auto& e = item.geometry->elements[item.element];
      const auto& mesh = e.mesh;
      if (mesh.vertices.empty() || mesh.indices.empty() || (!index32 && mesh.vertices.size() > 0xFFFF))
        return 0;
      const D3DMATRIX w = ToD3D(item.world);
      device->SetTransform(D3DTS_WORLD, &w);
      int triangles = 0;
      std::vector<std::uint16_t> idx16;
      for (const auto& sm : mesh.submeshes)
      {
        SetMaterial(device, item.materials.For(sm.part), item, sm.part == iris::MeshPart::Front || sm.part == iris::MeshPart::Back);
        const void* indices = &mesh.indices[sm.firstIndex];
        D3DFORMAT format = D3DFMT_INDEX32;
        if (!index32)
        {
          idx16.assign(mesh.indices.begin() + sm.firstIndex, mesh.indices.begin() + sm.firstIndex + sm.indexCount);
          indices = idx16.data();
          format = D3DFMT_INDEX16;
        }
        device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(mesh.vertices.size()), sm.indexCount / 3,
                                       indices, format, mesh.vertices.data(), sizeof(iris::MeshVertex));
        triangles += static_cast<int>(sm.indexCount / 3);
      }
      return triangles;
    }
  }  // namespace

  // ---------------------------------------------------------------- main thread

  void MainFrame(Session& s)
  {
    const std::filesystem::path projectDir = s.projectFile.empty() ? s.ProjectsDirectory() : s.projectFile.parent_path();
    std::lock_guard _(gfxMutex);
    std::set<std::string> live;
    for (const auto& g : s.project.scene.graphics)
    {
      live.insert(g.id);
      const std::string key = iris::GeometryKey(g) + "|" + projectDir.string();
      auto& entry = cache[g.id];
      if (entry.key == key && entry.geometry)
        continue;
      auto geometry = std::make_shared<iris::GraphicGeometry>(iris::BuildGraphicGeometry(
        g, [&](const std::string& font, std::string* error) { return ResolveFont(font, projectDir, error); }, projectDir));
      if (!geometry->error.empty())
        status.errors[g.id] = geometry->error;
      else
        status.errors.erase(g.id);
      status.warnings[g.id] = geometry->warnings;
      entry.key = key;
      entry.geometry = std::move(geometry);
    }
    for (auto it = cache.begin(); it != cache.end();)
    {
      if (!live.contains(it->first))
      {
        status.errors.erase(it->first);
        status.warnings.erase(it->first);
        lastPoses.erase(it->first);
        it = cache.erase(it);
      }
      else
        ++it;
    }
  }

  void OnView(Session& s, double t, const iris::CameraState& camera, int clientTime)
  {
    Frame frame;
    frame.time = clientTime;
    frame.camera = camera;
    const auto& v = s.project.visuals;
    if (v.sunOverride)
    {
      frame.lightDirection = -iris::AnglesToForward(v.sunAngles);
      frame.lightColor = v.sunColor * std::clamp(v.sunIntensity, 0.0, 4.0);
    }
    iris::TrackingContext ctx;
    ctx.camera = camera;
    ctx.attachPoint = [](std::string_view actor, std::string_view attach) {
      return Director::AttachPoint(std::string(actor), std::string(attach));
    };
    const auto& graphics = s.project.scene.graphics;
    std::function<std::optional<iris::Transform>(std::string_view, int)> poseOf;
    poseOf = [&](std::string_view id, int depth) -> std::optional<iris::Transform> {
      if (depth > 8)
        return std::nullopt;
      const iris::GraphicDef* g = s.project.scene.FindGraphic(id);
      if (!g)
        return std::nullopt;
      iris::TrackingContext c = ctx;
      c.graphicPose = [&](std::string_view parent) { return poseOf(parent, depth + 1); };
      return iris::EvaluateGraphic(*g, t, 0, c).world;
    };
    ctx.graphicPose = [&](std::string_view id) { return poseOf(id, 0); };

    std::lock_guard _(gfxMutex);
    for (const auto& g : graphics)
    {
      const auto it = cache.find(g.id);
      if (it == cache.end() || !it->second.geometry || !it->second.geometry->error.empty())
        continue;
      const auto& geo = it->second.geometry;
      const auto pose = iris::EvaluateGraphic(g, t, geo->elements.size(), ctx);
      lastPoses[g.id] = pose;
      if (!pose.visible || g.kind == iris::GraphicKind::Anchor)
        continue;
      const iris::Matrix4 world =
        iris::GraphicWorldMatrix(pose.world.position, iris::AnglesToAxis(pose.world.angles), pose.world.scale);
      for (std::size_t i = 0; i < geo->elements.size(); ++i)
      {
        const auto& ep = pose.elements[i];
        const double opacity = pose.opacity * ep.opacity;
        if (opacity <= 0.001 || ep.scale <= 0.0)
          continue;
        DrawItem item;
        item.graphic = g.id;
        item.geometry = geo;
        item.element = i;
        item.world = iris::Multiply(ElementMatrix(geo->elements[i], ep), world);
        item.materials = g.materials;
        item.useElementColor = g.useLogoColors && geo->elements[i].hasColor;
        item.elementColor = geo->elements[i].color;
        item.opacity = opacity;
        item.emissionBoost = ep.emissionBoost;
        item.mode = g.renderMode;
        item.doubleSided = g.doubleSided || g.kind == iris::GraphicKind::Image ||
                           (g.kind == iris::GraphicKind::Logo && g.logoMode == iris::LogoMode::Flat) ||
                           (g.kind == iris::GraphicKind::Shape && g.shape == iris::ShapeKind::Plane);
        item.distance = iris::Distance(camera.position, pose.world.position);
        frame.items.push_back(std::move(item));
      }
    }
    frames.push_back(std::move(frame));
    while (frames.size() > 8)
      frames.pop_front();
  }

  // ---------------------------------------------------------------- render thread

  void Draw(IDirect3DDevice9* device)
  {
    Frame frame;
    {
      std::lock_guard _(gfxMutex);
      if (frames.empty())
        return;
      // The draw list of the frame being presented: the engine's scene time
      // of this frame says which one.
      const int engineTime = EngineSource()->sceneDef.time;
      const auto exact = std::find_if(frames.begin(), frames.end(), [&](const Frame& f) { return f.time == engineTime; });
      status.timeSynced = exact != frames.end();
      frame = exact != frames.end() ? *exact : frames.back();
    }
    if (frame.items.empty())
    {
      std::lock_guard _(gfxMutex);
      status.drawnGraphics = 0;
      status.drawnTriangles = 0;
      return;
    }

    IDirect3DSurface9* backBuffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer)
      return;
    D3DSURFACE_DESC bb{};
    backBuffer->GetDesc(&bb);

    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)))
    {
      backBuffer->Release();
      return;
    }
    IDirect3DSurface9* oldTarget = nullptr;
    IDirect3DSurface9* depth = nullptr;
    device->GetRenderTarget(0, &oldTarget);
    device->GetDepthStencilSurface(&depth);
    device->SetRenderTarget(0, backBuffer);

    // Depth: only a depth surface that matches the back buffer can be used.
    bool depthUsable = false;
    std::string depthWhy;
    if (depth)
    {
      D3DSURFACE_DESC dd{};
      depth->GetDesc(&dd);
      if (dd.Width >= bb.Width && dd.Height >= bb.Height && dd.MultiSampleType == bb.MultiSampleType)
        depthUsable = true;
      else
        depthWhy = "unavailable: the bound depth buffer does not match the back buffer";
    }
    else
      depthWhy = "unavailable: no depth buffer is bound when the frame is presented";
    device->SetDepthStencilSurface(depthUsable ? depth : nullptr);

    const double aspect = bb.Height ? static_cast<double>(bb.Width) / bb.Height : 16.0 / 9.0;
    const ViewSetup view = MakeView(frame, aspect);

    D3DVIEWPORT9 vp{0, 0, bb.Width, bb.Height, 0.0f, 1.0f};
    device->SetViewport(&vp);
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);

    bool depthVerified = false;
    if (depthUsable)
    {
      Probe(device, bb.Width, bb.Height, view.reversed);
      std::lock_guard _(gfxMutex);
      depthVerified = status.depthAvailable;
      if (!status.depthAvailable && status.depth.empty())
        status.depth = "checking";
    }

    const D3DMATRIX v = ToD3D(view.view), p = ToD3D(view.projection);
    device->SetTransform(D3DTS_VIEW, &v);
    device->SetTransform(D3DTS_PROJECTION, &p);
    // Our meshes wind counter-clockwise seen from outside; a view that flips
    // handedness shows front faces clockwise on screen.
    const DWORD cull = static_cast<DWORD>(Det3(iris::Multiply(view.view, view.projection)) < 0 ? D3DCULL_CW : D3DCULL_CCW);

    device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
    device->SetRenderState(D3DRS_LIGHTING, TRUE);
    device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    device->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
    device->SetRenderState(D3DRS_LOCALVIEWER, TRUE);
    device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(80, 82, 88));
    device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
    device->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
    device->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
    device->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    D3DLIGHT9 light{};
    light.Type = D3DLIGHT_DIRECTIONAL;
    const iris::Vec3 ld = iris::Normalize(frame.lightDirection);
    light.Direction = {static_cast<float>(ld.x), static_cast<float>(ld.y), static_cast<float>(ld.z)};
    light.Diffuse = Color(frame.lightColor);
    light.Specular = Color(frame.lightColor);
    device->SetLight(0, &light);
    device->LightEnable(0, TRUE);
    for (DWORD i = 1; i < 8; ++i)
      device->LightEnable(i, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTexture(0, nullptr);

    D3DCAPS9 caps{};
    device->GetDeviceCaps(&caps);
    const bool index32 = caps.MaxVertexIndex > 0xFFFF;

    // Opaque first, then translucent from far to near.
    std::vector<const DrawItem*> order;
    for (const auto& item : frame.items)
      order.push_back(&item);
    auto translucent = [](const DrawItem* i) { return i->opacity * i->materials.front.opacity < 0.999; };
    std::stable_sort(order.begin(), order.end(), [&](const DrawItem* a, const DrawItem* b) {
      const bool ta = translucent(a), tb = translucent(b);
      if (ta != tb)
        return !ta;
      return ta ? a->distance > b->distance : false;
    });

    const DWORD zLess = static_cast<DWORD>(view.reversed ? D3DCMP_GREATEREQUAL : D3DCMP_LESSEQUAL);
    const DWORD zHidden = static_cast<DWORD>(view.reversed ? D3DCMP_LESS : D3DCMP_GREATER);
    int drawn = 0, triangles = 0;
    std::set<std::string> graphicsDrawn;
    for (const DrawItem* item : order)
    {
      const bool depthTest = depthVerified && item->mode != iris::GraphicRenderMode::OnTop;
      const bool blend = translucent(item);
      device->SetRenderState(D3DRS_CULLMODE, item->doubleSided ? static_cast<DWORD>(D3DCULL_NONE) : cull);
      device->SetRenderState(D3DRS_ZENABLE, static_cast<DWORD>(depthTest ? D3DZB_TRUE : D3DZB_FALSE));
      device->SetRenderState(D3DRS_ZFUNC, zLess);
      device->SetRenderState(D3DRS_ZWRITEENABLE, depthTest && !blend ? TRUE : FALSE);
      device->SetRenderState(D3DRS_ALPHABLENDENABLE, blend ? TRUE : FALSE);
      device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
      device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
      IDirect3DTexture9* tex = TextureFor(device, *item);
      if (tex)
      {
        device->SetTexture(0, tex);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
      }
      triangles += DrawItemGeometry(device, *item, index32);
      if (item->mode == iris::GraphicRenderMode::XRay && depthTest)
      {
        // The hidden parts, faint, so the graphic can be placed behind walls.
        DrawItem ghost = *item;
        ghost.opacity *= 0.22;
        device->SetRenderState(D3DRS_ZFUNC, zHidden);
        device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        DrawItemGeometry(device, ghost, index32);
      }
      if (tex)
      {
        device->SetTexture(0, nullptr);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
      }
      ++drawn;
      graphicsDrawn.insert(item->graphic);
    }

    saved->Apply();
    saved->Release();
    device->SetRenderTarget(0, oldTarget ? oldTarget : backBuffer);
    device->SetDepthStencilSurface(depth);
    if (oldTarget)
      oldTarget->Release();
    if (depth)
      depth->Release();
    backBuffer->Release();

    std::lock_guard _(gfxMutex);
    status.engineMatrices = view.engine;
    status.matrices = view.reason;
    status.reversedDepth = view.reversed;
    if (!depthUsable)
    {
      status.depthAvailable = false;
      status.depth = depthWhy;
    }
    status.drawnGraphics = static_cast<int>(graphicsDrawn.size());
    status.drawnTriangles = triangles;
  }

  void DeviceLost()
  {
    if (occlusionQuery)
    {
      occlusionQuery->Release();
      occlusionQuery = nullptr;
    }
    queryPending = false;
    for (auto& [key, tex] : textures)
      if (tex)
        tex->Release();
    textures.clear();
  }

  // ---------------------------------------------------------------- queries

  Status GetStatus()
  {
    std::lock_guard _(gfxMutex);
    return status;
  }

  std::optional<iris::Bounds3D> Bounds(const std::string& id)
  {
    std::lock_guard _(gfxMutex);
    const auto it = cache.find(id);
    if (it == cache.end() || !it->second.geometry)
      return std::nullopt;
    return it->second.geometry->bounds;
  }

  std::size_t ElementCount(const std::string& id)
  {
    std::lock_guard _(gfxMutex);
    const auto it = cache.find(id);
    return it == cache.end() || !it->second.geometry ? 0 : it->second.geometry->elements.size();
  }

  std::optional<iris::GraphicPose> LastPose(const std::string& id)
  {
    std::lock_guard _(gfxMutex);
    const auto it = lastPoses.find(id);
    if (it == lastPoses.end())
      return std::nullopt;
    return it->second;
  }

  std::string DefaultFont() { return "Rubik-SemiBold.ttf"; }

  std::vector<std::string> AvailableFonts()
  {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& dir : {FontsFolder(), WindowsFonts()})
    {
      if (dir.empty())
        continue;
      for (std::filesystem::directory_iterator it(dir, ec), end; it != end; it.increment(ec))
      {
        auto ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".ttf" || ext == ".otf")
        {
          const auto u8 = it->path().filename().u8string();
          out.emplace_back(u8.begin(), u8.end());
        }
      }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
  }
}
