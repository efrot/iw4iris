# Motion graphics

3D text, logos, shapes, imported models and image planes placed in the
game world, animated on the timeline and recorded with the shot.

## Pipeline

```
GraphicDef (project) ──► BuildGraphicGeometry (core, cached by geometry key)
                           text:  stb_truetype outlines → shapes with holes → extrusion + bevel, one element per glyph (or word)
                           logo:  nanosvg paths / traced alpha silhouette → shapes → extrusion or flat, one element per part
                           shape, OBJ model, image plane
main thread, per frame ──► EvaluateGraphic(t, camera): tracking, parenting, procedural animations (seeded)
                           draw list tagged with the frame's scene time
render thread ──────────► Graphics::Draw before the capture reads the back buffer
                           (editor gizmos and outlines are drawn later: never recorded)
```

## Coordinates

Graphic space is the space meshes are built in: **x** right and **y** up as
the graphic is read, **z** out of its front face. A graphic's transform maps
it to the world with its front along the object's forward axis (+X at zero
angles), its right along the object's left axis (+Y; a viewer facing the
graphic sees it on their right) and its up along +Z. With zero angles a
graphic faces +X and reads correctly from a camera at +X looking back.
`iris::GraphicToWorld` and `iris::GraphicWorldMatrix` implement this; the
renderer, gizmos and picking all use them.

## Geometry

* **Triangulation:** mapbox/earcut with holes; every triangle wound
  counter-clockwise.
* **Nesting:** even-odd (SVG `evenodd`, traced images) or non-zero
  (fonts, SVG default) with orientation-relative holes.
* **Extrusion:** caps at ±depth/2, side walls, round bevels of 1–8
  segments (1 = chamfer) on both faces. The bevel is the largest that fits
  every shape (inset contours may not flip or cross); a reduction is
  reported. Normals are smoothed below 35° between edges.
* **Limits:** 400 000 triangles per graphic, 250 000 per OBJ, 8192 px
  images, 64 MB fonts.
* Tests check that glyphs (`O`, `B`, `R`, `8`, `%`, ...), logos with holes
  and primitives are closed, consistently outward-wound solids.

## Rendering

* Fixed-function Direct3D 9: Blinn–Phong with one directional key light
  (the Look panel's sun override drives it) and emission. Materials named
  metal, chrome or glass are approximations.
* **View:** the engine's own view and projection matrices of the frame
  (renderer source state, the address IW4x uses in `Renderer::ListSamplers`)
  when its camera equals the camera IW4Iris wrote for that frame and the
  matrices map the view direction to the screen centre; otherwise IW4Iris'
  own matrices. The panel shows which.
* **Depth:** used only if the bound depth buffer matches the back buffer
  (size, multisampling) and an occlusion query of a far-plane quad shows the
  buffer holds scene depth. Otherwise depth-tested graphics draw on top and
  the panel and inspector say why. Whether IW4's depth buffer is still
  bound when the frame is presented is exactly what this checks at run time.
* **Not applied to graphics:** the map's lighting, shadows, fog, depth of
  field, glow and film tweaks (graphics are drawn after post-processing).

## Tracking

| Mode | Behaviour |
| --- | --- |
| World | fixed transform or its keys |
| Actor | follows an attach point (origin, head, neck, chest, pelvis, hands, back, weapon muzzle) from the Director Mod's bone reports, interpolated to the client's render time; the transform is the offset |
| Billboard | world position, turned towards the camera (optionally upright) |
| Camera | locked in front of the shot camera at a distance |
| Parent | any graphic (e.g. an anchor) can be a parent |

## Determinism

A pose is a pure function of timeline time: keys, opacity curve, tracking
inputs and the seeded presets (fly in / out, rise, drop, scale, pop, spin,
typewriter, assemble / disassemble, extrusion growth, flicker, glitch,
float, rotate, pulse). Seeking back and forth gives identical poses.

## Status

Geometry, layout, tracking maths and animation are unit-tested. The
renderer is implemented and compiled but has not run in the game yet; the
depth and view checks report their result in the Motion Graphics panel.
