// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// The entry points the IW4x sources call into IW4Iris (see
// engine/iw4x/patches/). Kept free of IW4Iris headers so the patched IW4x
// files only depend on this one small header.
#pragma once

namespace IW4Iris::Hooks
{
  // Called by Rumble::CG_SetRumbleReceiver (IW4x hooks the R_EndDobjScene
  // call in CG_DrawActiveFrame at 0x486DEC): the game has computed this
  // frame's view (cg_s::refdef) and the scene is not drawn yet. The
  // cinematic camera replaces the view here.
  void OnViewReady(int localClientNum);

  // Whether the editor has the mouse; RawMouse then neither moves the
  // player's view nor recenters / clips the cursor.
  bool BlocksGameMouse();
}
