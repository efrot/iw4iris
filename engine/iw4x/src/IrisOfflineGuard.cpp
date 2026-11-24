// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
#include "OfflineGuard.hpp"
#include "ImportPatch.hpp"
#include "Log.hpp"

#include <atomic>
#include <mutex>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>

namespace IW4Iris::OfflineGuard
{
  namespace
  {
    std::atomic<bool> installed{false};
    std::atomic<bool> engineSendBlocked{false};
    std::atomic<int> winsockGuarded{0};
    std::atomic<int> httpGuarded{0};
    std::atomic<std::uint64_t> droppedPackets{0};
    std::atomic<std::uint64_t> blockedConnections{0};
    std::atomic<std::uint64_t> blockedHttp{0};
    std::atomic<bool> violation{false};
    std::mutex lastMutex;
    std::string lastBlocked;

    void NoteBlocked(std::string what)
    {
      std::lock_guard _(lastMutex);
      lastBlocked = std::move(what);
    }

    bool IsLoopback(const sockaddr* address, int length)
    {
      if (!address)
        return false;
      if (address->sa_family == AF_INET && length >= static_cast<int>(sizeof(sockaddr_in)))
      {
        const auto* in = reinterpret_cast<const sockaddr_in*>(address);
        return (ntohl(in->sin_addr.s_addr) >> 24) == 127;
      }
      if (address->sa_family == AF_INET6 && length >= static_cast<int>(sizeof(sockaddr_in6)))
      {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(address);
        static const unsigned char loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        if (std::memcmp(&in6->sin6_addr, loopback, 16) == 0)
          return true;
        // IPv4-mapped 127.0.0.0/8
        static const unsigned char mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        const auto* bytes = reinterpret_cast<const unsigned char*>(&in6->sin6_addr);
        return std::memcmp(bytes, mapped, 12) == 0 && bytes[12] == 127;
      }
      return false;
    }

    // ---- layer 1: the engine's UDP send path

    bool Sys_SendPacket_Blocked(Game::netsrc_t, size_t, const char*, Game::netadr_t adr)
    {
      ++droppedPackets;
      if (adr.type != Game::NA_LOOPBACK && adr.type != Game::NA_BOT)
        NoteBlocked("UDP packet to a network address");
      // Report success so the engine does not print a send error per packet;
      // nothing has left the machine.
      return true;
    }

    // ---- layer 2: Winsock

    using connect_t = int(WSAAPI*)(SOCKET, const sockaddr*, int);
    using sendto_t = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
    using WSAConnect_t = int(WSAAPI*)(SOCKET, const sockaddr*, int, LPWSABUF, LPWSABUF, LPQOS, LPQOS);
    using WSASendTo_t = int(WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr*, int, LPWSAOVERLAPPED,
                                     LPWSAOVERLAPPED_COMPLETION_ROUTINE);

    connect_t real_connect = nullptr;
    sendto_t real_sendto = nullptr;
    WSAConnect_t real_WSAConnect = nullptr;
    WSASendTo_t real_WSASendTo = nullptr;

    int Refuse(const char* what)
    {
      ++blockedConnections;
      NoteBlocked(what);
      WSASetLastError(WSAENETUNREACH);
      return SOCKET_ERROR;
    }

    int WSAAPI Guard_connect(SOCKET s, const sockaddr* name, int len)
    {
      if (!IsLoopback(name, len))
        return Refuse("connect() to a network address");
      return real_connect(s, name, len);
    }

    int WSAAPI Guard_sendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen)
    {
      // A null destination on a connected socket: the connection itself was
      // only allowed to loopback.
      if (to && !IsLoopback(to, tolen))
        return Refuse("sendto() to a network address");
      return real_sendto(s, buf, len, flags, to, tolen);
    }

    int WSAAPI Guard_WSAConnect(SOCKET s, const sockaddr* name, int len, LPWSABUF a, LPWSABUF b, LPQOS c, LPQOS d)
    {
      if (!IsLoopback(name, len))
        return Refuse("WSAConnect() to a network address");
      return real_WSAConnect(s, name, len, a, b, c, d);
    }

    int WSAAPI Guard_WSASendTo(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD sent, DWORD flags, const sockaddr* to,
                               int tolen, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE routine)
    {
      if (to && !IsLoopback(to, tolen))
        return Refuse("WSASendTo() to a network address");
      return real_WSASendTo(s, bufs, count, sent, flags, to, tolen, overlapped, routine);
    }

    // ---- layer 3: HTTP stacks

    HINTERNET WINAPI Guard_InternetOpenA(LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD)
    {
      ++blockedHttp;
      NoteBlocked("WinINet session");
      SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
      return nullptr;
    }

    HINTERNET WINAPI Guard_InternetOpenW(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD)
    {
      ++blockedHttp;
      NoteBlocked("WinINet session");
      SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
      return nullptr;
    }

    HRESULT WINAPI Guard_URLDownloadToFileA(LPUNKNOWN, LPCSTR, LPCSTR, DWORD, LPVOID)
    {
      ++blockedHttp;
      NoteBlocked("URL download");
      return E_ACCESSDENIED;
    }

    HRESULT WINAPI Guard_URLDownloadToFileW(LPUNKNOWN, LPCWSTR, LPCWSTR, DWORD, LPVOID)
    {
      ++blockedHttp;
      NoteBlocked("URL download");
      return E_ACCESSDENIED;
    }

    void* WINAPI Guard_WinHttpOpen(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD)
    {
      ++blockedHttp;
      NoteBlocked("WinHTTP session");
      SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
      return nullptr;
    }

    HMODULE ThisModule()
    {
      HMODULE module = nullptr;
      GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(&ThisModule), &module);
      return module;
    }

    template <typename T>
    int GuardWinsock(HMODULE module, const char* function, T guard, T& real)
    {
      int count = 0;
      for (const char* dll : {"ws2_32.dll", "wsock32.dll"})
      {
        void* original = nullptr;
        count += ImportPatch::Replace(module, dll, function, reinterpret_cast<void*>(guard), &original);
        if (original && !real)
          real = reinterpret_cast<T>(original);
      }
      if (count > 0 && !real)
      {
        // Imported by ordinal through a forwarder: resolve the real function.
        if (const HMODULE ws2 = GetModuleHandleA("ws2_32.dll"))
          real = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(ws2, function)));
      }
      return count;
    }
  }

  bool Install()
  {
    if (installed)
      return true;

    // Layer 1
    Utils::Hook(Game::Sys_SendPacket, Sys_SendPacket_Blocked, HOOK_JUMP).install()->quick();
    engineSendBlocked = *reinterpret_cast<const std::uint8_t*>(Game::Sys_SendPacket) == 0xE9;
    if (!engineSendBlocked)
    {
      Log::Error("Offline guard: could not block the engine's packet sender; the cinematic tools stay disabled");
      return false;
    }

    // Layers 2 and 3, on the game executable and on this DLL.
    for (const HMODULE module : {GetModuleHandleA(nullptr), ThisModule()})
    {
      if (!module)
        continue;
      winsockGuarded += GuardWinsock(module, "connect", &Guard_connect, real_connect);
      winsockGuarded += GuardWinsock(module, "sendto", &Guard_sendto, real_sendto);
      winsockGuarded += GuardWinsock(module, "WSAConnect", &Guard_WSAConnect, real_WSAConnect);
      winsockGuarded += GuardWinsock(module, "WSASendTo", &Guard_WSASendTo, real_WSASendTo);

      httpGuarded += ImportPatch::Replace(module, "wininet.dll", "InternetOpenA", reinterpret_cast<void*>(&Guard_InternetOpenA), nullptr);
      httpGuarded += ImportPatch::Replace(module, "wininet.dll", "InternetOpenW", reinterpret_cast<void*>(&Guard_InternetOpenW), nullptr);
      httpGuarded += ImportPatch::Replace(module, "urlmon.dll", "URLDownloadToFileA", reinterpret_cast<void*>(&Guard_URLDownloadToFileA), nullptr);
      httpGuarded += ImportPatch::Replace(module, "urlmon.dll", "URLDownloadToFileW", reinterpret_cast<void*>(&Guard_URLDownloadToFileW), nullptr);
      httpGuarded += ImportPatch::Replace(module, "winhttp.dll", "WinHttpOpen", reinterpret_cast<void*>(&Guard_WinHttpOpen), nullptr);
    }

    // A guard that replaced an import must have the real function to call
    // for loopback traffic; otherwise fail closed.
    if ((winsockGuarded > 0) && (!real_connect && !real_sendto && !real_WSAConnect && !real_WSASendTo))
    {
      Log::Error("Offline guard: Winsock imports were guarded but the originals could not be resolved");
      return false;
    }

    installed = true;
    Log::Info("Offline guard installed: engine sends blocked, {} Winsock and {} HTTP imports guarded",
              winsockGuarded.load(), httpGuarded.load());
    return true;
  }

  Status GetStatus()
  {
    Status s;
    s.installed = installed;
    s.engineSendBlocked = engineSendBlocked;
    s.winsockImportsGuarded = winsockGuarded;
    s.httpImportsGuarded = httpGuarded;
    s.droppedPackets = droppedPackets;
    s.blockedConnections = blockedConnections;
    s.blockedHttp = blockedHttp;
    s.sessionViolation = violation;
    std::lock_guard _(lastMutex);
    s.lastBlocked = lastBlocked;
    return s;
  }

  void VerifySession()
  {
    if (violation)
      return;
    const auto state = Game::CL_GetLocalClientConnectionState(0);
    if (state < Game::CA_CONNECTING || Game::clientConnections->demoplaying)
      return;
    const auto type = Game::clientConnections->serverAddress.type;
    if (type == Game::NA_LOOPBACK || type == Game::NA_BOT)
      return;
    violation = true;
    NoteBlocked("connection to a network server");
    Log::Error("Offline guard: the client tried to connect to a network server; disconnecting and disabling the "
               "cinematic tools until the game restarts");
    Components::Command::Execute("disconnect", false);
  }

  bool SessionAllowed()
  {
    return installed && !violation;
  }
}
