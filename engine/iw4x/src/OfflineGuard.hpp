// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Keeps the cinematic build offline. Layers, all installed before the game
// initialises its network:
//
//   1. Game::Sys_SendPacket (the engine's only UDP send path, which IW4x's
//      own Network component also goes through) drops every packet.
//      Loopback traffic of a local session never reaches it (the engine
//      delivers NA_LOOPBACK packets in NET_SendPacket).
//   2. The Winsock imports of the game executable and of the client DLL
//      (connect, sendto, WSAConnect, WSASendTo) refuse every destination
//      that is not a loopback address.
//   3. The WinINet / URLMon / WinHTTP imports of the client DLL fail, so the
//      news, updater and download code cannot reach the internet.
//   4. Every frame the client's connection is checked: connected to anything
//      but loopback (or a demo) and the session is disconnected and the
//      cinematic tools are disabled until the game restarts.
//
// If layer 1 cannot be installed, Install() returns false and the editor
// refuses to start (fail closed).
#pragma once

#include <cstdint>
#include <string>

namespace IW4Iris::OfflineGuard
{
  struct Status
  {
    bool installed = false;
    bool engineSendBlocked = false;
    int winsockImportsGuarded = 0;
    int httpImportsGuarded = 0;
    std::uint64_t droppedPackets = 0;
    std::uint64_t blockedConnections = 0;
    std::uint64_t blockedHttp = 0;
    bool sessionViolation = false;
    std::string lastBlocked;
  };

  bool Install();
  Status GetStatus();
  // Called once per frame on the main thread.
  void VerifySession();
  bool SessionAllowed();
}
