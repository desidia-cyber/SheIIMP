// coop_steam.cpp -- [R10-STEAM] In-DLL Steam P2P & LAN UDP transport, matching
// dollsMP's steam_inproc.rs + session.rs architecture:
//
//   1. Never calls SteamAPI_ManualDispatch_Init() (coexists with UE5 OnlineSubsystemSteam).
//      Uses flat-C ISteamUtils::IsAPICallCompleted / GetAPICallResult for lobbies and
//      SteamAPI_RegisterCallback (CCallbackBase) for ISteamNetworkingMessages requests/failures.
//   2. Resolves Steamworks in Sparta\Engine\Binaries\ThirdParty\Steamworks\Steamv165\Win64
//      (and Steamv157 / Steamv151): if steam_api64.rne is present in that folder, loads and
//      uses steam_api64.rne for Steam P2P; if only steam_api64.dll is present (no .rne),
//      reuses the game's already-loaded steam_api64.dll or loads it from that folder.
//      Falls back to MS2Coop\steam_api64.dll, rejecting emulator DLLs.
//   3. Supports Steam Identity selection: Game AppID (Mortal Shell II) vs Spacewar (480),
//      with active_steam_app_id() mismatch guard and RestartWithAppId() relaunch.
//   4. Password-keyed public lobbies: 20,000-round SHA-256 of "ms2coop-v1|<password>" (32 hex)
//      stored under lobby keys "ms2"="1" and "k"=<key>.
//   5. LAN UDP fallback (ws2_32.dll loaded dynamically so dxgi.dll gains zero static imports).
//   6. Cleanly stoppable/restartable from the in-game F9 CO-OP menu without restarting the game.
//   7. Coexists with run-two.ps1: when IDLE, coop_net uses out_/peer_ files unchanged; when
//      CONNECTED, coop_net exchanges packets directly in-memory over Steam P2P / LAN UDP.

#include "coop_state.h"
#include "ms2_log.h"
#include "MinHook.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace coop {
namespace steam {

namespace {

constexpr uint32_t kSpacewarAppId = 480u;
constexpr uint32_t kDefaultGameAppId = 2584270u; // Mortal Shell II Steam AppID (verified live via GetAppID)

constexpr int kChData = 0;
constexpr int kChCtrl = 1;
constexpr int kSendUnreliableNoNagleRestart = 0 | 1 | 32;
constexpr int kIdentitySteamId = 16;
constexpr int kLobbyPublic = 2;
constexpr int kLobbyMax = 4;
constexpr int kCfgSendBufferSize = 9;
constexpr int kCfgSendRateMin = 10;
constexpr int kSendBufferBytes = 64 * 1024;
constexpr int kSendRateMinBps = 256 * 1024;
constexpr double kSilenceLimitSecs = 20.0;

// ---------------------------------------------------------------------------
// Minimal self-contained SHA-256 (for dollsMP-identical 20,000-round lobby key)
// ---------------------------------------------------------------------------
struct Sha256 {
    uint32_t s[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;

    static inline uint32_t RotR(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void Init() {
        s[0] = 0x6a09e667u; s[1] = 0xbb67ae85u; s[2] = 0x3c6ef372u; s[3] = 0xa54ff53au;
        s[4] = 0x510e527fu; s[5] = 0x9b05688cu; s[6] = 0x1f83d9abu; s[7] = 0x5be0cd19u;
        bitlen = 0;
        buflen = 0;
    }

    void Transform(const uint8_t block[64]) {
        static const uint32_t K[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4], f = s[5], g = s[6], h = s[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        s[0] += a; s[1] += b; s[2] += c; s[3] += d;
        s[4] += e; s[5] += f; s[6] += g; s[7] += h;
    }

    void Update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buf[buflen++] = data[i];
            if (buflen == 64) {
                Transform(buf);
                bitlen += 512;
                buflen = 0;
            }
        }
    }

    void Final(uint8_t out[32]) {
        bitlen += buflen * 8;
        buf[buflen++] = 0x80;
        if (buflen > 56) {
            while (buflen < 64) buf[buflen++] = 0;
            Transform(buf);
            buflen = 0;
        }
        while (buflen < 56) buf[buflen++] = 0;
        for (int i = 7; i >= 0; --i) buf[buflen++] = (uint8_t)(bitlen >> (i * 8));
        Transform(buf);
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = (uint8_t)(s[i] >> 24);
            out[i * 4 + 1] = (uint8_t)(s[i] >> 16);
            out[i * 4 + 2] = (uint8_t)(s[i] >> 8);
            out[i * 4 + 3] = (uint8_t)(s[i]);
        }
    }

    static void Hash(const uint8_t* data, size_t len, uint8_t out[32]) {
        Sha256 h; h.Init(); h.Update(data, len); h.Final(out);
    }
};

std::string LobbyKey(const std::string& password) {
    // Trim whitespace
    size_t a = 0, b = password.size();
    while (a < b && (unsigned char)password[a] <= ' ') ++a;
    while (b > a && (unsigned char)password[b - 1] <= ' ') --b;
    std::string seed = "ms2coop-v1|" + password.substr(a, b - a);
    uint8_t h[32];
    Sha256::Hash(reinterpret_cast<const uint8_t*>(seed.data()), seed.size(), h);
    for (int i = 0; i < 20000; ++i) Sha256::Hash(h, 32, h);
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 16; ++i) {
        out.push_back(kHex[h[i] >> 4]);
        out.push_back(kHex[h[i] & 15]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Steamworks flat-C ABI structs (identical to dollsMP steam_inproc.rs)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct SteamIdentity {
    int32_t kind;
    int32_t size;
    uint8_t data[128];

    static SteamIdentity FromId(uint64_t id) {
        SteamIdentity s{};
        s.kind = kIdentitySteamId;
        s.size = 8;
        std::memcpy(s.data, &id, 8);
        return s;
    }
    uint64_t SteamId() const {
        if (kind != kIdentitySteamId) return 0;
        uint64_t id = 0;
        std::memcpy(&id, data, 8);
        return id;
    }
};
#pragma pack(pop)

struct SteamNetMsg {
    const uint8_t* data;
    int32_t        size;
    uint32_t       conn;
    SteamIdentity  peer;
    int64_t        userData;
    int64_t        usec;
    int64_t        msgNum;
    int32_t        channel;
    int32_t        flags;
};

struct SteamCb;
struct SteamCbVTable {
    void (__cdecl *runA)(SteamCb*, const void*, uint8_t, uint64_t);
    void (__cdecl *runB)(SteamCb*, const void*, uint8_t, uint64_t);
    int32_t (__cdecl *getSize)(SteamCb*);
};
struct SteamCb {
    const SteamCbVTable* vtable;
    uint8_t              flags;
    uint8_t              pad[3];
    int32_t              callbackId;
    int32_t              paramSize;
    int32_t              pad2;
    std::mutex*          mu;
    std::vector<uint64_t>* q;
};

void __cdecl CbRunIdentity(SteamCb* self, const void* param, uint8_t, uint64_t) {
    if (!self || !param || !self->mu || !self->q) return;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(param);
    int32_t kind = 0;
    uint64_t id = 0;
    std::memcpy(&kind, b, 4);
    std::memcpy(&id, b + 8, 8);
    if (kind == kIdentitySteamId && id != 0) {
        std::lock_guard<std::mutex> lk(*self->mu);
        self->q->push_back(id);
    }
}
int32_t __cdecl CbGetSize(SteamCb* self) { return self ? self->paramSize : 0; }
const SteamCbVTable kCbVtable = {&CbRunIdentity, &CbRunIdentity, &CbGetSize};

struct SteamApi {
    bool weStarted = false;
    uint32_t appId = 0;
    void* user = nullptr;
    void* friends = nullptr;
    void* msgs = nullptr;
    void* mm = nullptr;
    void* utils = nullptr;

    void (__cdecl *runCallbacks)() = nullptr;
    void (__cdecl *registerCb)(void*, int32_t) = nullptr;
    void (__cdecl *unregisterCb)(void*) = nullptr;
    bool (__cdecl *isCallCompleted)(void*, uint64_t, bool*) = nullptr;
    bool (__cdecl *getCallResult)(void*, uint64_t, void*, int32_t, int32_t, bool*) = nullptr;
    uint64_t (__cdecl *getSteamId)(void*) = nullptr;
    const char* (__cdecl *persona)(void*) = nullptr;
    const char* (__cdecl *friendName)(void*, uint64_t) = nullptr;
    int32_t (__cdecl *sendMsg)(void*, const SteamIdentity*, const void*, uint32_t, int32_t, int32_t) = nullptr;
    int32_t (__cdecl *recvMsgs)(void*, int32_t, SteamNetMsg**, int32_t) = nullptr;
    bool (__cdecl *acceptSession)(void*, const SteamIdentity*) = nullptr;
    void (__cdecl *releaseMsg)(SteamNetMsg*) = nullptr;
    uint64_t (__cdecl *createLobby)(void*, int32_t, int32_t) = nullptr;
    bool (__cdecl *setLobbyData)(void*, uint64_t, const char*, const char*) = nullptr;
    uint64_t (__cdecl *requestLobbies)(void*) = nullptr;
    void (__cdecl *filterString)(void*, const char*, const char*, int32_t) = nullptr;
    void (__cdecl *filterDistance)(void*, int32_t) = nullptr;
    void (__cdecl *filterCount)(void*, int32_t) = nullptr;
    uint64_t (__cdecl *lobbyByIndex)(void*, int32_t) = nullptr;
    uint64_t (__cdecl *lobbyOwner)(void*, uint64_t) = nullptr;
    uint64_t (__cdecl *joinLobby)(void*, uint64_t) = nullptr;
    int32_t (__cdecl *lobbyMemberCount)(void*, uint64_t) = nullptr;
    uint64_t (__cdecl *lobbyMember)(void*, uint64_t, int32_t) = nullptr;
    void (__cdecl *leaveLobby)(void*, uint64_t) = nullptr;
};

// Dynamic Winsock2 loader (zero static ws2_32.dll import on dxgi.dll)
struct Ws2Api {
    HMODULE mod = nullptr;
    int (WSAAPI *startup)(WORD, LPWSADATA) = nullptr;
    SOCKET (WSAAPI *sock)(int, int, int) = nullptr;
    int (WSAAPI *closesock)(SOCKET) = nullptr;
    int (WSAAPI *ioctlsock)(SOCKET, long, u_long*) = nullptr;
    int (WSAAPI *bindFn)(SOCKET, const sockaddr*, int) = nullptr;
    int (WSAAPI *sendtoFn)(SOCKET, const char*, int, int, const sockaddr*, int) = nullptr;
    int (WSAAPI *recvfromFn)(SOCKET, char*, int, int, sockaddr*, int*) = nullptr;
    int (WSAAPI *inetPton)(INT, PCSTR, PVOID) = nullptr;
    u_short (WSAAPI *htonsFn)(u_short) = nullptr;

    bool Load() {
        if (mod) return true;
        mod = LoadLibraryW(L"ws2_32.dll");
        if (!mod) return false;
        startup    = (decltype(startup))GetProcAddress(mod, "WSAStartup");
        sock       = (decltype(sock))GetProcAddress(mod, "socket");
        closesock  = (decltype(closesock))GetProcAddress(mod, "closesocket");
        ioctlsock  = (decltype(ioctlsock))GetProcAddress(mod, "ioctlsocket");
        bindFn     = (decltype(bindFn))GetProcAddress(mod, "bind");
        sendtoFn   = (decltype(sendtoFn))GetProcAddress(mod, "sendto");
        recvfromFn = (decltype(recvfromFn))GetProcAddress(mod, "recvfrom");
        inetPton   = (decltype(inetPton))GetProcAddress(mod, "inet_pton");
        htonsFn    = (decltype(htonsFn))GetProcAddress(mod, "htons");
        if (!startup || !sock || !closesock || !ioctlsock || !bindFn || !sendtoFn || !recvfromFn || !inetPton || !htonsFn) {
            return false;
        }
        WSADATA wsa{};
        return startup(MAKEWORD(2, 2), &wsa) == 0;
    }
};

// ---------------------------------------------------------------------------
// Global session state
// ---------------------------------------------------------------------------
std::mutex      g_mu;
Config          g_cfg;
bool            g_cfgLoaded = false;
std::string     g_dllDir;
std::atomic<int> g_state{kStateIdle};
std::atomic<int> g_role{0}; // 0=host, 1=join
std::string     g_peerName;
std::string     g_statusMsg = "OFFLINE (SOLO)";
std::atomic<bool> g_stop{false};
std::thread     g_worker;
std::atomic<HMODULE> g_steamMod{nullptr};
std::atomic<bool>    g_steamWeStarted{false};

// Lock-guarded packet queues exchanged with coop_net.cpp
std::mutex              g_ioMu;
std::deque<std::string> g_txQueue;
std::string             g_rxLatest;
bool                    g_rxHasNew = false;

std::string WideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int cb = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(cb > 0 ? cb : 0, '\0');
    if (cb > 0) WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], cb, nullptr, nullptr);
    return s;
}

std::wstring ExeDirWide() {
    wchar_t w[MAX_PATH * 2] = L"";
    DWORD n = GetModuleFileNameW(nullptr, w, MAX_PATH * 2);
    if (!n) return L"";
    std::wstring ws(w, n);
    size_t sl = ws.find_last_of(L"\\/");
    if (sl != std::wstring::npos) ws.resize(sl);
    return ws;
}

std::string ExeDirNarrow() {
    return WideToUtf8(ExeDirWide());
}

bool FileExistsW(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void SetStatus(int st, const std::string& msg, const std::string& peer = "") {
    g_state.store(st, std::memory_order_release);
    std::lock_guard<std::mutex> lk(g_mu);
    g_statusMsg = msg;
    if (!peer.empty()) g_peerName = peer;
}

bool IsEmulatorDll(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > (8LL << 20)) { CloseHandle(h); return false; }
    std::string buf((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    ReadFile(h, &buf[0], (DWORD)buf.size(), &got, nullptr);
    CloseHandle(h);
    for (char& c : buf) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    static const char* kBad[] = {"tenoke", "goldberg", "codex", "steam_emu", "smartsteamemu"};
    for (const char* m : kBad) {
        if (buf.find(m) != std::string::npos) return true;
    }
    return false;
}

HMODULE ResolveSteamModule(bool* usedRne, std::string* err) {
    if (usedRne) *usedRne = false;

    std::wstring wd = ExeDirWide();
    if (wd.empty() && !g_dllDir.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, g_dllDir.c_str(), (int)g_dllDir.size(), nullptr, 0);
        if (n > 0) {
            wd.resize((size_t)n);
            MultiByteToWideChar(CP_UTF8, 0, g_dllDir.c_str(), (int)g_dllDir.size(), &wd[0], n);
        }
    }

    if (!wd.empty()) {
        const std::wstring steamDirs[] = {
            wd + L"\\..\\..\\..\\Engine\\Binaries\\ThirdParty\\Steamworks\\Steamv165\\Win64",
            wd + L"\\..\\..\\..\\Sparta\\Engine\\Binaries\\ThirdParty\\Steamworks\\Steamv165\\Win64",
            wd + L"\\..\\..\\..\\Engine\\Binaries\\ThirdParty\\Steamworks\\Steamv157\\Win64",
            wd + L"\\..\\..\\..\\Sparta\\Engine\\Binaries\\ThirdParty\\Steamworks\\Steamv157\\Win64",
            wd + L"\\..\\..\\..\\Engine\\Binaries\\ThirdParty\\Steamworks\\Steamv151\\Win64",
            wd + L"\\..\\..\\..\\Sparta\\Engine\\Binaries\\ThirdParty\\Steamworks\\Steamv151\\Win64",
        };
        for (const std::wstring& dir : steamDirs) {
            const std::wstring rnePath = dir + L"\\steam_api64.rne";
            const std::wstring dllPath = dir + L"\\steam_api64.dll";
            const bool hasRne = FileExistsW(rnePath);
            const bool hasDll = FileExistsW(dllPath);
            if (!hasRne && !hasDll) continue;

            if (hasRne) {
                // steam_api64.rne is present in this Steamworks folder: always use it
                // instead of steam_api64.dll.
                HMODULE m = GetModuleHandleW(L"steam_api64.rne");
                if (!m) m = LoadLibraryW(rnePath.c_str());
                if (!m) {
                    if (err) *err = "Failed to load steam_api64.rne (error " + std::to_string(GetLastError()) + ")";
                    ms2::Logf("steam: failed to load %s (err=%lu)", WideToUtf8(rnePath).c_str(), (unsigned long)GetLastError());
                    return nullptr;
                }
                if (usedRne) *usedRne = true;
                ms2::Logf("steam: using steam_api64.rne (%s)", WideToUtf8(rnePath).c_str());
                return m;
            }

            // Only steam_api64.dll is present (no steam_api64.rne in the same folder).
            // Reuse the game's already-loaded steam_api64.dll if present, otherwise load it.
            HMODULE m = GetModuleHandleW(L"steam_api64.dll");
            if (!m && !IsEmulatorDll(dllPath)) {
                m = LoadLibraryW(dllPath.c_str());
            }
            if (m) {
                ms2::Logf("steam: using steam_api64.dll (%s)", WideToUtf8(dllPath).c_str());
                return m;
            }
        }
    }

    // Fallback: already-loaded steam_api64.dll or local MS2Coop override
    HMODULE m = GetModuleHandleW(L"steam_api64.dll");
    if (!m && !wd.empty()) {
        const std::wstring cands[] = {
            wd + L"\\MS2Coop\\steam_api64.dll",
            wd + L"\\steam_api64.dll",
        };
        for (const std::wstring& c : cands) {
            if (FileExistsW(c) && !IsEmulatorDll(c)) {
                m = LoadLibraryW(c.c_str());
                if (m) {
                    ms2::Logf("steam: using fallback %s", WideToUtf8(c).c_str());
                    break;
                }
            }
        }
    }
    if (!m && err) {
        *err = "steam_api64.dll / steam_api64.rne not found (launch via Steam or check Steamworks Win64 folder)";
    }
    return m;
}

bool LoadSteamApi(int steamIdentity, SteamApi* out, std::string* err) {
    bool usedRne = false;
    HMODULE m = ResolveSteamModule(&usedRne, err);
    if (!m) {
        return false;
    }
    const char* modLabel = usedRne ? "steam_api64.rne" : "steam_api64.dll";
    g_steamMod.store(m, std::memory_order_release);

    auto findIface = [&](const char* const* names, int n) -> void* (__cdecl *)() {
        for (int i = 0; i < n; ++i) {
            if (void* p = (void*)GetProcAddress(m, names[i])) return (void* (__cdecl *)())p;
        }
        return nullptr;
    };

    static const char* kUserNames[] = {"SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021", "SteamAPI_SteamUser_v020"};
    static const char* kFrNames[]   = {"SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017", "SteamAPI_SteamFriends_v016"};
    static const char* kMsgNames[]  = {"SteamAPI_SteamNetworkingMessages_SteamAPI_v002"};
    static const char* kMmNames[]   = {"SteamAPI_SteamMatchmaking_v009"};
    static const char* kUtNames[]   = {"SteamAPI_SteamUtils_v011", "SteamAPI_SteamUtils_v010", "SteamAPI_SteamUtils_v009"};
    static const char* kNuNames[]   = {"SteamAPI_SteamNetworkingUtils_SteamAPI_v004", "SteamAPI_SteamNetworkingUtils_SteamAPI_v003"};

    auto userG = findIface(kUserNames, 4);
    auto frG   = findIface(kFrNames, 3);
    auto msgG  = findIface(kMsgNames, 1);
    auto mmG   = findIface(kMmNames, 1);
    auto utG   = findIface(kUtNames, 3);
    auto nuG   = findIface(kNuNames, 2);
    if (!userG || !frG || !msgG || !mmG || !utG) {
        if (err) *err = std::string(modLabel) + " missing required flat-C interface exports";
        return false;
    }

    const uint32_t wantApp = (steamIdentity == 1) ? kSpacewarAppId : kDefaultGameAppId;
    void* user = userG();
    bool weStarted = g_steamWeStarted.load(std::memory_order_acquire);
    if (!user) {
        char appBuf[32];
        std::snprintf(appBuf, sizeof(appBuf), "%u", wantApp);
        SetEnvironmentVariableA("SteamAppId", appBuf);
        SetEnvironmentVariableA("SteamGameId", appBuf);
        SetEnvironmentVariableA("SteamOverlayGameId", nullptr);
        if (auto initFlat = (int32_t (__cdecl *)(char*))GetProcAddress(m, "SteamAPI_InitFlat")) {
            char msg[512] = {};
            int32_t r = initFlat(msg);
            if (r != 0) {
                if (err) *err = std::string("SteamAPI_InitFlat failed: ") + msg;
                return false;
            }
        } else if (auto init = (bool (__cdecl *)())GetProcAddress(m, "SteamAPI_Init")) {
            if (!init()) {
                if (err) *err = "SteamAPI_Init failed (is Steam running and logged in?)";
                return false;
            }
        } else {
            if (err) *err = std::string(modLabel) + " missing SteamAPI_Init";
            return false;
        }
        weStarted = true;
        g_steamWeStarted.store(true, std::memory_order_release);
        user = userG();
    }

    void* friends = frG();
    void* msgs    = msgG();
    void* mm      = mmG();
    void* utils   = utG();
    if (!user || !friends || !msgs || !mm || !utils) {
        if (err) *err = "Steam interfaces are null";
        return false;
    }

    uint32_t liveApp = wantApp;
    if (auto getApp = (uint32_t (__cdecl *)(void*))GetProcAddress(m, "SteamAPI_ISteamUtils_GetAppID")) {
        uint32_t a = getApp(utils);
        if (a != 0) liveApp = a;
    }
    if (steamIdentity == 1 && liveApp != kSpacewarAppId) {
        if (err) *err = "Steam is locked to game AppID -- click 'Restart as Spacewar (480)' to switch";
        return false;
    }
    if (steamIdentity == 0 && liveApp == kSpacewarAppId) {
        if (err) *err = "Steam is locked to Spacewar (480) -- click 'Restart as Game' to switch";
        return false;
    }

    if (nuG) {
        if (void* nu = nuG()) {
            if (auto initR = (void (__cdecl *)(void*))GetProcAddress(m, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess"))
                initR(nu);
            if (auto setC = (bool (__cdecl *)(void*, int32_t, int32_t))GetProcAddress(m, "SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32")) {
                setC(nu, kCfgSendBufferSize, kSendBufferBytes);
                setC(nu, kCfgSendRateMin, kSendRateMinBps);
            }
        }
    }

    out->weStarted = weStarted;
    out->appId = liveApp;
    out->user = user; out->friends = friends; out->msgs = msgs; out->mm = mm; out->utils = utils;
#define LOAD_FN(field, sym) \
    out->field = (decltype(out->field))GetProcAddress(m, sym); \
    if (!out->field) { if (err) *err = std::string("Missing export ") + sym; return false; }

    LOAD_FN(runCallbacks,     "SteamAPI_RunCallbacks");
    LOAD_FN(registerCb,       "SteamAPI_RegisterCallback");
    LOAD_FN(unregisterCb,     "SteamAPI_UnregisterCallback");
    LOAD_FN(isCallCompleted,  "SteamAPI_ISteamUtils_IsAPICallCompleted");
    LOAD_FN(getCallResult,    "SteamAPI_ISteamUtils_GetAPICallResult");
    LOAD_FN(getSteamId,       "SteamAPI_ISteamUser_GetSteamID");
    LOAD_FN(persona,          "SteamAPI_ISteamFriends_GetPersonaName");
    LOAD_FN(friendName,       "SteamAPI_ISteamFriends_GetFriendPersonaName");
    LOAD_FN(sendMsg,          "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    LOAD_FN(recvMsgs,         "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    LOAD_FN(acceptSession,    "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    LOAD_FN(releaseMsg,       "SteamAPI_SteamNetworkingMessage_t_Release");
    LOAD_FN(createLobby,      "SteamAPI_ISteamMatchmaking_CreateLobby");
    LOAD_FN(setLobbyData,     "SteamAPI_ISteamMatchmaking_SetLobbyData");
    LOAD_FN(requestLobbies,   "SteamAPI_ISteamMatchmaking_RequestLobbyList");
    LOAD_FN(filterString,     "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter");
    LOAD_FN(filterDistance,   "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter");
    LOAD_FN(filterCount,      "SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter");
    LOAD_FN(lobbyByIndex,     "SteamAPI_ISteamMatchmaking_GetLobbyByIndex");
    LOAD_FN(lobbyOwner,       "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    LOAD_FN(joinLobby,        "SteamAPI_ISteamMatchmaking_JoinLobby");
    LOAD_FN(lobbyMemberCount, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    LOAD_FN(lobbyMember,      "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    LOAD_FN(leaveLobby,       "SteamAPI_ISteamMatchmaking_LeaveLobby");
#undef LOAD_FN
    return true;
}

bool WaitApiCall(const SteamApi& api, uint64_t call, int32_t cbId, void* outBuf, int32_t outSize,
                 int timeoutMs, std::string* err) {
    if (!call) { if (err) *err = "Invalid Steam API call handle"; return false; }
    const DWORD t0 = GetTickCount();
    while (!g_stop.load(std::memory_order_relaxed) && (int)(GetTickCount() - t0) < timeoutMs) {
        if (api.weStarted) api.runCallbacks();
        bool failed = false;
        if (api.isCallCompleted(api.utils, call, &failed)) {
            if (failed) { if (err) *err = "Steam API call failed"; return false; }
            bool resFailed = false;
            if (api.getCallResult(api.utils, call, outBuf, outSize, cbId, &resFailed) && !resFailed) {
                return true;
            }
            if (err) *err = "Steam GetAPICallResult failed";
            return false;
        }
        Sleep(15);
    }
    if (err) *err = g_stop.load() ? "Stopped" : "Steam API call timed out";
    return false;
}

void RunSteamThread(Config cfg, bool isHost) {
    SetStatus(kStateStarting, "Initializing Steam P2P...");
    SteamApi api;
    std::string err;
    if (!LoadSteamApi(cfg.steamIdentity, &api, &err)) {
        SetStatus(kStateError, err);
        ms2::Logf("steam: init failed: %s", err.c_str());
        return;
    }

    const std::string key = LobbyKey(cfg.password);
    const uint64_t me = api.getSteamId(api.user);

    std::mutex reqMu, failMu;
    std::vector<uint64_t> reqQ, failQ;
    SteamCb reqCb{&kCbVtable, 0, {0,0,0}, 1251, 136, 0, &reqMu, &reqQ};
    SteamCb failCb{&kCbVtable, 0, {0,0,0}, 1252, 136, 0, &failMu, &failQ};
    api.registerCb(&reqCb, 1251);
    api.registerCb(&failCb, 1252);

    uint64_t lobby = 0;
    uint64_t peerId = 0;

    if (isHost) {
        SetStatus(kStateWaiting, "Creating Steam lobby (code " + key.substr(0, 8) + ")...");
        uint64_t call = api.createLobby(api.mm, kLobbyPublic, kLobbyMax);
        uint8_t buf[32] = {};
        if (!WaitApiCall(api, call, 513, buf, sizeof(buf), 20000, &err)) {
            api.unregisterCb(&failCb); api.unregisterCb(&reqCb);
            SetStatus(kStateError, err);
            return;
        }
        int32_t res = 0;
        std::memcpy(&res, buf, 4);
        std::memcpy(&lobby, buf + 8, 8);
        if (res != 1 || !lobby) {
            api.unregisterCb(&failCb); api.unregisterCb(&reqCb);
            SetStatus(kStateError, "Steam could not create lobby");
            return;
        }
        api.setLobbyData(api.mm, lobby, "ms2", "1");
        api.setLobbyData(api.mm, lobby, "k", key.c_str());
        SetStatus(kStateWaiting, "Hosting on Steam (code " + key.substr(0, 8) + ") -- waiting for partner");
    } else {
        SetStatus(kStateWaiting, "Searching Steam lobby (code " + key.substr(0, 8) + ")...");
        while (!g_stop.load(std::memory_order_relaxed) && !lobby) {
            api.filterString(api.mm, "ms2", "1", 0);
            api.filterString(api.mm, "k", key.c_str(), 0);
            api.filterDistance(api.mm, 3); // Worldwide
            api.filterCount(api.mm, 10);
            uint64_t call = api.requestLobbies(api.mm);
            uint8_t buf[16] = {};
            if (WaitApiCall(api, call, 510, buf, sizeof(buf), 10000, &err)) {
                int32_t n = 0;
                std::memcpy(&n, buf, 4);
                for (int i = 0; i < n; ++i) {
                    uint64_t cand = api.lobbyByIndex(api.mm, i);
                    uint64_t owner = api.lobbyOwner(api.mm, cand);
                    if (cand && owner && owner != me) {
                        uint64_t jcall = api.joinLobby(api.mm, cand);
                        uint8_t jbuf[32] = {};
                        if (WaitApiCall(api, jcall, 504, jbuf, sizeof(jbuf), 10000, &err)) {
                            lobby = cand;
                            peerId = owner;
                            SteamIdentity id = SteamIdentity::FromId(peerId);
                            api.acceptSession(api.msgs, &id);
                            api.sendMsg(api.msgs, &id, "KA", 2, kSendUnreliableNoNagleRestart, kChCtrl);
                            const char* fn = api.friendName(api.friends, peerId);
                            SetStatus(kStateConnected, std::string("Steam P2P linked: ") + (fn ? fn : "Host"), fn ? fn : "Host");
                            break;
                        }
                    }
                }
            }
            if (!lobby) Sleep(1500);
        }
    }

    // Main Steam P2P pump loop
    DWORD lastBeat = GetTickCount();
    DWORD lastRx = GetTickCount();
    SteamNetMsg* msgs[32] = {};

    while (!g_stop.load(std::memory_order_relaxed)) {
        if (api.weStarted) api.runCallbacks();

        // Accept any incoming session requests from lobby members
        if (lobby) {
            int mc = api.lobbyMemberCount(api.mm, lobby);
            auto isMember = [&](uint64_t id) {
                for (int i = 0; i < mc; ++i) if (api.lobbyMember(api.mm, lobby, i) == id) return true;
                return false;
            };
            {
                std::lock_guard<std::mutex> lk(reqMu);
                for (uint64_t id : reqQ) {
                    if (id != me && isMember(id)) {
                        SteamIdentity si = SteamIdentity::FromId(id);
                        api.acceptSession(api.msgs, &si);
                        if (!peerId) {
                            peerId = id;
                            const char* fn = api.friendName(api.friends, peerId);
                            SetStatus(kStateConnected, std::string("Steam P2P linked: ") + (fn ? fn : "Partner"), fn ? fn : "Partner");
                        }
                    }
                }
                reqQ.clear();
            }
            {
                std::lock_guard<std::mutex> lk(failMu);
                for (uint64_t id : failQ) {
                    if (id == peerId) {
                        peerId = 0;
                        SetStatus(kStateWaiting, "Partner disconnected -- waiting...");
                    }
                }
                failQ.clear();
            }
        }

        // Send queued outbound packets
        if (peerId) {
            SteamIdentity to = SteamIdentity::FromId(peerId);
            std::deque<std::string> batch;
            {
                std::lock_guard<std::mutex> lk(g_ioMu);
                batch.swap(g_txQueue);
            }
            for (const std::string& pkt : batch) {
                api.sendMsg(api.msgs, &to, pkt.data(), (uint32_t)pkt.size(), kSendUnreliableNoNagleRestart, kChData);
            }
            const DWORD now = GetTickCount();
            if (now - lastBeat >= 1000u) {
                lastBeat = now;
                api.sendMsg(api.msgs, &to, "KA", 2, kSendUnreliableNoNagleRestart, kChCtrl);
            }
        }

        // Receive inbound packets
        for (int ch : {kChData, kChCtrl}) {
            int n = api.recvMsgs(api.msgs, ch, msgs, 32);
            for (int i = 0; i < n; ++i) {
                SteamNetMsg* m = msgs[i];
                if (!m) continue;
                uint64_t from = m->peer.SteamId();
                if (from && from != me) {
                    if (ch == kChCtrl && m->size == 3 && std::memcmp(m->data, "BYE", 3) == 0) {
                        if (from == peerId) {
                            peerId = 0;
                            SetStatus(kStateWaiting, "Partner left session");
                        }
                    } else {
                        if (!peerId) {
                            peerId = from;
                            const char* fn = api.friendName(api.friends, peerId);
                            SetStatus(kStateConnected, std::string("Steam P2P linked: ") + (fn ? fn : "Partner"), fn ? fn : "Partner");
                        }
                        lastRx = GetTickCount();
                        if (ch == kChData && m->size > 0) {
                            std::lock_guard<std::mutex> lk(g_ioMu);
                            g_rxLatest.assign(reinterpret_cast<const char*>(m->data), (size_t)m->size);
                            g_rxHasNew = true;
                        }
                    }
                }
                api.releaseMsg(m);
            }
        }

        if (peerId && (GetTickCount() - lastRx) > (DWORD)(kSilenceLimitSecs * 1000.0)) {
            peerId = 0;
            SetStatus(kStateWaiting, "Partner timed out -- waiting...");
        }
        Sleep(4);
    }

    if (peerId) {
        SteamIdentity to = SteamIdentity::FromId(peerId);
        api.sendMsg(api.msgs, &to, "BYE", 3, kSendUnreliableNoNagleRestart, kChCtrl);
    }
    if (lobby) api.leaveLobby(api.mm, lobby);
    api.unregisterCb(&failCb);
    api.unregisterCb(&reqCb);
    SetStatus(kStateIdle, "OFFLINE (SOLO)");
}

void RunLanThread(Config cfg, bool isHost) {
    Ws2Api ws;
    if (!ws.Load()) {
        SetStatus(kStateError, "Could not load ws2_32.dll for LAN UDP");
        return;
    }
    SOCKET s = ws.sock(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        SetStatus(kStateError, "Could not create UDP socket");
        return;
    }
    u_long nb = 1;
    ws.ioctlsock(s, FIONBIO, &nb);

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = ws.htonsFn(isHost ? cfg.port : 0);
    local.sin_addr.s_addr = INADDR_ANY;
    if (ws.bindFn(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        ws.closesock(s);
        SetStatus(kStateError, "Could not bind UDP port");
        return;
    }

    sockaddr_in peerAddr{};
    bool hasPeer = false;
    if (!isHost) {
        peerAddr.sin_family = AF_INET;
        peerAddr.sin_port = ws.htonsFn(cfg.port);
        if (ws.inetPton(AF_INET, cfg.server.c_str(), &peerAddr.sin_addr) != 1) {
            ws.closesock(s);
            SetStatus(kStateError, "Invalid LAN server IP: " + cfg.server);
            return;
        }
        hasPeer = true;
        ws.sendtoFn(s, "KA", 2, 0, reinterpret_cast<sockaddr*>(&peerAddr), sizeof(peerAddr));
        SetStatus(kStateWaiting, "Connecting to LAN " + cfg.server + ":" + std::to_string(cfg.port) + "...");
    } else {
        SetStatus(kStateWaiting, "Hosting LAN UDP on port " + std::to_string(cfg.port) + "...");
    }

    char buf[65536];
    DWORD lastBeat = GetTickCount();
    DWORD lastRx = GetTickCount();

    while (!g_stop.load(std::memory_order_relaxed)) {
        if (hasPeer) {
            std::deque<std::string> batch;
            {
                std::lock_guard<std::mutex> lk(g_ioMu);
                batch.swap(g_txQueue);
            }
            for (const std::string& pkt : batch) {
                ws.sendtoFn(s, pkt.data(), (int)pkt.size(), 0, reinterpret_cast<sockaddr*>(&peerAddr), sizeof(peerAddr));
            }
            DWORD now = GetTickCount();
            if (now - lastBeat >= 1000u) {
                lastBeat = now;
                ws.sendtoFn(s, "KA", 2, 0, reinterpret_cast<sockaddr*>(&peerAddr), sizeof(peerAddr));
            }
        }
        for (;;) {
            sockaddr_in from{};
            int flen = sizeof(from);
            int got = ws.recvfromFn(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &flen);
            if (got <= 0) break;
            if (!hasPeer) {
                peerAddr = from;
                hasPeer = true;
            }
            lastRx = GetTickCount();
            if (g_state.load() != kStateConnected) {
                SetStatus(kStateConnected, "LAN Connected", isHost ? "LAN Guest" : "LAN Host");
            }
            if (got == 3 && std::memcmp(buf, "BYE", 3) == 0) {
                if (isHost) hasPeer = false;
                SetStatus(kStateWaiting, "Partner disconnected");
            } else if (!(got == 2 && std::memcmp(buf, "KA", 2) == 0)) {
                std::lock_guard<std::mutex> lk(g_ioMu);
                g_rxLatest.assign(buf, (size_t)got);
                g_rxHasNew = true;
            }
        }
        if (hasPeer && g_state.load() == kStateConnected && (GetTickCount() - lastRx) > (DWORD)(kSilenceLimitSecs * 1000.0)) {
            SetStatus(kStateWaiting, "LAN partner timed out...");
            if (isHost) hasPeer = false;
        }
        Sleep(4);
    }

    if (hasPeer) ws.sendtoFn(s, "BYE", 3, 0, reinterpret_cast<sockaddr*>(&peerAddr), sizeof(peerAddr));
    ws.closesock(s);
    SetStatus(kStateIdle, "OFFLINE (SOLO)");
}

std::string IniPath() {
    std::string dir = g_dllDir.empty() ? ExeDirNarrow() : g_dllDir;
    return dir + "\\MS2Coop-Net.ini";
}

}  // namespace

void LoadConfig() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_cfgLoaded) return;
    g_cfgLoaded = true;
    g_dllDir = ExeDirNarrow();
    g_cfg = Config{};
    FILE* f = std::fopen(IniPath().c_str(), "rb");
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        std::string k(line), v(eq + 1);
        auto trim = [](std::string& s) {
            while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
            size_t i = 0; while (i < s.size() && (unsigned char)s[i] <= ' ') ++i;
            s.erase(0, i);
        };
        trim(k); trim(v);
        if (k == "mode") g_cfg.mode = (v == "join") ? 1 : 0;
        else if (k == "network") g_cfg.network = (v == "lan") ? 1 : 0;
        else if (k == "steam_identity") g_cfg.steamIdentity = (v == "spacewar") ? 1 : 0;
        else if (k == "port") { int p = std::atoi(v.c_str()); if (p > 0 && p < 65536) g_cfg.port = (uint16_t)p; }
        else if (k == "password") g_cfg.password = v.substr(0, 63);
        else if (k == "server" && !v.empty()) g_cfg.server = v.substr(0, 63);
    }
    std::fclose(f);
}

void SaveConfig(const Config& cfg) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_cfg = cfg;
    g_cfgLoaded = true;
    FILE* f = std::fopen(IniPath().c_str(), "wb");
    if (!f) return;
    std::fprintf(f,
                 "# MS2Coop connection settings (managed by the F9 CO-OP menu)\n"
                 "mode = %s\n"
                 "network = %s\n"
                 "steam_identity = %s\n"
                 "password = %s\n"
                 "server = %s\n"
                 "port = %u\n",
                 cfg.mode == 1 ? "join" : "host",
                 cfg.network == 1 ? "lan" : "steam",
                 cfg.steamIdentity == 1 ? "spacewar" : "game",
                 cfg.password.c_str(),
                 cfg.server.c_str(),
                 (unsigned)cfg.port);
    std::fclose(f);
}

Config GetConfig() {
    LoadConfig();
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cfg;
}

uint32_t ActiveSteamAppId() {
    HMODULE m = g_steamMod.load(std::memory_order_acquire);
    if (!m) m = GetModuleHandleW(L"steam_api64.rne");
    if (!m) m = GetModuleHandleW(L"steam_api64.dll");
    if (!m) return 0;
    auto getApp = (uint32_t (__cdecl *)(void*))GetProcAddress(m, "SteamAPI_ISteamUtils_GetAppID");
    static const char* kUt[] = {"SteamAPI_SteamUtils_v011", "SteamAPI_SteamUtils_v010", "SteamAPI_SteamUtils_v009"};
    if (getApp) {
        for (const char* n : kUt) {
            if (auto g = (void* (__cdecl *)())GetProcAddress(m, n)) {
                if (void* u = g()) {
                    uint32_t a = getApp(u);
                    if (a) return a;
                }
                break;
            }
        }
    }
    char env[32] = {};
    if (GetEnvironmentVariableA("SteamAppId", env, sizeof(env)) > 0) return (uint32_t)std::strtoul(env, nullptr, 10);
    return 0;
}

bool RestartWithAppId(uint32_t appId, std::string* err) {
    wchar_t exe[MAX_PATH * 2] = L"";
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH * 2)) {
        if (err) *err = "Could not query exe path";
        return false;
    }
    char appBuf[32];
    std::snprintf(appBuf, sizeof(appBuf), "%u", appId);
    SetEnvironmentVariableA("SteamAppId", appBuf);
    SetEnvironmentVariableA("SteamGameId", appBuf);
    SetEnvironmentVariableA("SteamOverlayGameId", nullptr);

    std::wstring cmd = std::wstring(L"\"") + exe + L"\" MortalShell2";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        if (err) *err = "CreateProcessW failed";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool StartSession(bool isHost, std::string* err) {
    StopSession();
    Config cfg = GetConfig();
    cfg.mode = isHost ? 0 : 1;
    SaveConfig(cfg);
    if (cfg.network == 0 && cfg.password.empty()) {
        if (err) *err = "Steam P2P requires a co-op password";
        SetStatus(kStateError, "Steam P2P requires a co-op password");
        return false;
    }
    g_role.store(isHost ? 0 : 1, std::memory_order_release);
    g_stop.store(false, std::memory_order_release);
    state().local.role = isHost ? "host" : "guest";
    state().local.me   = isHost ? "A" : "B";
    state().local.peer = isHost ? "B" : "A";
    g_worker = std::thread([cfg, isHost]() {
        if (cfg.network == 0) RunSteamThread(cfg, isHost);
        else RunLanThread(cfg, isHost);
    });
    return true;
}

void StopSession() {
    g_stop.store(true, std::memory_order_release);
    if (g_worker.joinable()) g_worker.join();
    {
        std::lock_guard<std::mutex> lk(g_ioMu);
        g_txQueue.clear();
        g_rxLatest.clear();
        g_rxHasNew = false;
    }
    SetStatus(kStateIdle, "OFFLINE (SOLO)");
}

bool IsActive() {
    const int st = g_state.load(std::memory_order_acquire);
    return st == kStateConnected;
}

void SendPacket(const std::string& text) {
    if (!IsActive() || text.empty()) return;
    std::lock_guard<std::mutex> lk(g_ioMu);
    if (g_txQueue.size() > 32) g_txQueue.pop_front();
    g_txQueue.push_back(text);
}

bool PopPeerPacket(std::string* out) {
    if (!IsActive() || !out) return false;
    std::lock_guard<std::mutex> lk(g_ioMu);
    if (!g_rxHasNew) return false;
    *out = g_rxLatest;
    g_rxHasNew = false;
    return true;
}

int State() { return g_state.load(std::memory_order_acquire); }
int Role()  { return g_role.load(std::memory_order_acquire); }

std::string StatusText() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_statusMsg;
}

std::string PeerName() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_peerName;
}

std::string Status() {
    // [R10-STEAMHEAP] The status line is written every loop.  It used to ask
    // steam_api64 (SteamUtils accessor + GetAppID) every time, even solo or on LAN;
    // the test installs' steam_api64 then died with a heap double free on the game
    // thread while a save loaded (13:41/13:44/13:46 dumps).  Ask Steam only while a
    // Steam-transport session is live; otherwise report the env app id.
    uint32_t app = 0;
    if (State() != kStateIdle && GetConfig().network == 0) {
        app = ActiveSteamAppId();
    } else {
        char env[32] = {};
        if (GetEnvironmentVariableA("SteamAppId", env, sizeof(env)) > 0) app = (uint32_t)std::strtoul(env, nullptr, 10);
    }
    return net::Fmt("steam=st:%d role:%d app:%u peer:%s guard:%d skipped:%llu\n",
                    State(), Role(), (unsigned)app,
                    PeerName().empty() ? "-" : PeerName().c_str(),
                    GuardActive() ? 1 : 0, (unsigned long long)GuardSkipped());
}

// [R10-STEAMGUARD] begin -- LAB ONLY, opt-in.
// The test installs' Steam emulator (steam_api64.dll) double-frees inside its own
// SteamAPI_RunCallbacks while the game's online thread dispatches callbacks
// (dumps 38852 / 70904: only that thread is in steam_api64, ~55 s after boot, also
// with the original R10 DLL and with the engine Steam subsystem disabled by -ini).
// coop\STEAMGUARD.txt turns SteamAPI_RunCallbacks into a no-op so a lab run can
// reach gameplay.  Off unless that file exists; STEAMGUARD_OFF.txt also forces it
// off.  Never ship STEAMGUARD.txt: with it, Steam callbacks (and the Steam
// transport) do not run.
namespace {
using RunCbFn = void (__cdecl*)();
RunCbFn g_origRunCb = nullptr;
std::atomic<bool> g_guardOn{false};
std::atomic<uint64_t> g_guardSkipped{0};
bool g_guardDone = false;
void __cdecl GuardRunCallbacks() { g_guardSkipped.fetch_add(1, std::memory_order_relaxed); }
bool CoopFileExists(const char* name) {
    const std::string p = state().local.dir + name;
    if (state().local.dir.empty()) return false;
    const int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    if (n <= 0) return false;
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &w[0], n);
    return GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES;
}
}  // namespace

bool GuardActive() { return g_guardOn.load(std::memory_order_acquire); }
uint64_t GuardSkipped() { return g_guardSkipped.load(std::memory_order_relaxed); }

void GuardTick() {
    if (g_guardDone || state().local.dir.empty()) return;
    if (!CoopFileExists("STEAMGUARD.txt") || CoopFileExists("STEAMGUARD_OFF.txt")) { g_guardDone = true; return; }
    HMODULE m = GetModuleHandleW(L"steam_api64.dll");
    if (!m) return;                                   // not loaded yet -- next tick
    g_guardDone = true;
    void* target = (void*)GetProcAddress(m, "SteamAPI_RunCallbacks");
    if (!target) { ms2::Log("steamguard: SteamAPI_RunCallbacks not exported -- guard off"); return; }
    if (MH_CreateHook(target, (LPVOID)&GuardRunCallbacks, (LPVOID*)&g_origRunCb) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        ms2::Log("steamguard: hook FAILED -- guard off");
        return;
    }
    g_guardOn.store(true, std::memory_order_release);
    ms2::Log("steamguard: LAB guard ON -- SteamAPI_RunCallbacks is a no-op (STEAMGUARD.txt)");
}
// [R10-STEAMGUARD] end

}  // namespace steam
}  // namespace coop
