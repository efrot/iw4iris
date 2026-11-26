// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Keeps the cinematic build offline. Layers, all installed before the game
// initialises its network:
//
//   1. Game::Sys_SendPacket (the engine's only UDP send path, which IW4x's
//      own Network component also goes through) drops every packet to a
//      network address. Packets to NA_LOOPBACK, NA_BOT and NA_BAD go on to
//      the original function, which delivers loopback in the process and
//      returns for bots / bad addresses: IW4x's Network::SendRaw sends to
//      the local server through Sys_SendPacket (the client's stats packets
//      while it connects), so dropping loopback left every film set on the
//      loading screen (seen in the game, 2026-11-26). The original is
//      reached through a trampoline holding its first instruction, which is
//      checked before use; without it every packet is dropped as before.
//   2. The Winsock imports of the game executable and of the client DLL
//      (connect, sendto, WSAConnect, WSASendTo) refuse every destination
//      that is not a loopback address, and listen() only works on sockets
//      bound to loopback (no TCP servers reachable from the network).
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
    // Loopback packets reach the engine (needed for film sets).
    bool loopbackForwarding = false;
    std::uint64_t forwardedLoopbackPackets = 0;
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
